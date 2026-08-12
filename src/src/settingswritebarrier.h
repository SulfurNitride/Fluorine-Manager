#pragma once

#include <atomic>
#include <cstddef>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace fail_stop
{
// QSettings synchronizes from its destructor. Terminal fail-stop deliberately
// transfers such a backend to process lifetime so ordinary owner destruction
// cannot retry a write before the process reaches _Exit.
template <typename Backend>
Backend* retainBackendForProcessLifetime(Backend*& backend) noexcept
{
  return std::exchange(backend, nullptr);
}
}  // namespace fail_stop

class SettingsWriteBarrier
{
public:
  enum class Concurrency
  {
    Concurrent,
    Serialized,
  };

  class MutationLease
  {
  public:
    MutationLease() = default;
    MutationLease(const MutationLease&) = delete;
    MutationLease& operator=(const MutationLease&) = delete;

    MutationLease(MutationLease&& other) noexcept
        : m_Barrier(std::exchange(other.m_Barrier, nullptr)),
          m_SerializationLock(std::move(other.m_SerializationLock))
    {}

    MutationLease& operator=(MutationLease&&) = delete;

    ~MutationLease() { release(); }

    explicit operator bool() const noexcept { return m_Barrier != nullptr; }

  private:
    MutationLease(const SettingsWriteBarrier* barrier,
                  std::unique_lock<std::recursive_mutex> serializationLock) noexcept
        : m_Barrier(barrier),
          m_SerializationLock(std::move(serializationLock))
    {}

    void release() noexcept
    {
      if (m_Barrier != nullptr) {
        if (m_SerializationLock.owns_lock()) {
          try {
            m_SerializationLock.unlock();
          } catch (...) {
            // Preserve the recursive-depth marker rather than allowing a
            // nested mutation to be mistaken for new top-level work.
            return;
          }
        }
        finishCurrentThreadMutation(m_Barrier);
        m_Barrier = nullptr;
      }
    }

    const SettingsWriteBarrier* m_Barrier{nullptr};
    std::unique_lock<std::recursive_mutex> m_SerializationLock;
    friend class SettingsWriteBarrier;
  };

  explicit SettingsWriteBarrier(
      Concurrency concurrency = Concurrency::Concurrent) noexcept
      : m_SerializeMutations(concurrency == Concurrency::Serialized)
  {}

  MutationLease enterIfAllowed() const
  {
    // A mutation admitted before suppression remains one transaction even if
    // it enters another helper guarded by this same barrier. New top-level
    // work is rejected, while recursive work is counted until the outermost
    // admitted transaction has completely returned.
    const bool recursive = s_CurrentMutationDepth.contains(this);
    if (!recursive && m_Suppressed.load(std::memory_order_acquire)) {
      return {};
    }

    {
      const std::lock_guard admissionLock(m_AdmissionMutex);
      if (!recursive && m_Suppressed.load(std::memory_order_acquire)) {
        return {};
      }
      ++s_CurrentMutationDepth[this];
    }

    try {
      std::unique_lock<std::recursive_mutex> serializationLock;
      if (m_SerializeMutations) {
        // Admission happens before this potentially blocking lock. A mutation
        // already admitted when suppression closes remains one complete
        // transaction rather than being rejected halfway through nested work.
        serializationLock =
            std::unique_lock<std::recursive_mutex>(m_MutationMutex);
      }
      return MutationLease(this, std::move(serializationLock));
    } catch (...) {
      finishCurrentThreadMutation(this);
      throw;
    }
  }

  template <typename Mutation>
  bool runIfAllowed(Mutation&& mutation) const
  {
    auto lease = enterIfAllowed();
    if (!lease) {
      return false;
    }

    std::forward<Mutation>(mutation)();
    return true;
  }

  void suppress() noexcept
  {
    // Never wait for a mutation to finish here. A UI-thread fail-stop can be
    // reached while an admitted worker is synchronously waiting for that same
    // UI thread. Close top-level admission and let already-admitted
    // transactions return normally before the process-level _Exit.
    try {
      // Synchronize the admission decision without holding a mutex while a
      // mutation executes.
      const std::lock_guard lock(m_AdmissionMutex);
      m_Suppressed.store(true, std::memory_order_release);
    } catch (...) {
      // Admission still fails closed if mutex acquisition itself fails.
      m_Suppressed.store(true, std::memory_order_release);
    }
  }

  bool suppressed() const noexcept
  {
    return m_Suppressed.load(std::memory_order_acquire);
  }

  // Used before entering modal configuration flows that can end in terminal
  // rollback. Those flows must not suspend an admitted mutation under a nested
  // event loop and then close that mutation's admission gate.
  static bool currentThreadHasMutation() noexcept
  {
    return !s_CurrentMutationDepth.empty();
  }

private:
  inline static thread_local
      std::unordered_map<const SettingsWriteBarrier*, std::size_t>
          s_CurrentMutationDepth;

  static void finishCurrentThreadMutation(
      const SettingsWriteBarrier* barrier) noexcept
  {
    auto it = s_CurrentMutationDepth.find(barrier);
    if (it != s_CurrentMutationDepth.end() && --it->second == 0) {
      s_CurrentMutationDepth.erase(it);
    }
  }

  mutable std::mutex m_AdmissionMutex;
  mutable std::recursive_mutex m_MutationMutex;
  std::atomic_bool m_Suppressed{false};
  const bool m_SerializeMutations;
};
