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
            // The mutation cannot be proven retired if its serialization
            // lock did not release. Preserve both depth and active count so
            // fail-stop remains closed rather than authorizing teardown.
            return;
          }
        }
        finishCurrentThreadMutation(m_Barrier);
        m_Barrier->finishMutation();
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
      const std::lock_guard stateLock(m_StateMutex);
      if (!recursive && m_Suppressed.load(std::memory_order_acquire)) {
        return {};
      }
      ++s_CurrentMutationDepth[this];
      ++m_ActiveMutations;
    }

    try {
      std::unique_lock<std::recursive_mutex> serializationLock;
      if (m_SerializeMutations) {
        // Admission and active counting happen before this potentially
        // blocking lock. suppress() therefore remains nonblocking and drain
        // accounts for mutations already waiting to enter the serialized
        // persistence sink.
        serializationLock =
            std::unique_lock<std::recursive_mutex>(m_MutationMutex);
      }
      return MutationLease(this, std::move(serializationLock));
    } catch (...) {
      finishCurrentThreadMutation(this);
      finishMutation();
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
    // Never wait here. A UI-thread fail-stop can be reached while an admitted
    // worker mutation is synchronously waiting for that same UI thread. The
    // caller closes admission now and defers teardown until
    // suppressionDrained() reports that admitted work has returned.
    try {
      // Synchronize the admission decision with the active-count transition.
      // This mutex is held only for bookkeeping, never while a mutation runs.
      const std::lock_guard lock(m_StateMutex);
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

  bool suppressionDrained() const noexcept
  {
    try {
      const std::lock_guard lock(m_StateMutex);
      return m_ActiveMutations == 0;
    } catch (...) {
      // A state-query failure cannot safely prove quiescence.
      return false;
    }
  }

  std::size_t activeMutationCount() const noexcept
  {
    try {
      const std::lock_guard lock(m_StateMutex);
      return m_ActiveMutations;
    } catch (...) {
      // A conservative nonzero sentinel prevents callers from interpreting a
      // failed state query as quiescence.
      return static_cast<std::size_t>(-1);
    }
  }

  // Used before entering modal configuration flows that can end in terminal
  // rollback. Those flows must not be nested inside any admitted mutation;
  // after their event loops return, fail-stop can require an ordinary full
  // active-count drain before destructively cancelling shared resources.
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

  void finishMutation() const noexcept
  {
    try {
      const std::lock_guard lock(m_StateMutex);
      if (m_ActiveMutations > 0) {
        --m_ActiveMutations;
      }
    } catch (...) {
      // Keep reporting non-drained rather than authorizing teardown when the
      // mutex cannot confirm that the active mutation was retired.
    }
  }

  mutable std::mutex m_StateMutex;
  mutable std::recursive_mutex m_MutationMutex;
  mutable std::size_t m_ActiveMutations{0};
  std::atomic_bool m_Suppressed{false};
  const bool m_SerializeMutations;
};
