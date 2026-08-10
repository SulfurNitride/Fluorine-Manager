#ifndef SLROPERATIONCONTEXT_H
#define SLROPERATIONCONTEXT_H

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

struct SlrCancellationState
{
  std::atomic_bool requested{false};
  std::function<bool()> externallyRequested;
};

enum class SlrDurableStage
{
  PrepareInstallRoot,
  RuntimeSwap,
  XrandrCommit,
  BuildIdCommit,
  BackupCleanup,
  UiPersistence,
};

class SlrCancellationToken
{
public:
  SlrCancellationToken() = default;

  bool isCancellationRequested() const noexcept
  {
    if (!m_State) {
      return false;
    }
    if (m_State->requested.load(std::memory_order_acquire)) {
      return true;
    }
    try {
      return m_State->externallyRequested && m_State->externallyRequested();
    } catch (...) {
      return true;
    }
  }

private:
  explicit SlrCancellationToken(std::shared_ptr<SlrCancellationState> state)
      : m_State(std::move(state))
  {
  }

  std::shared_ptr<SlrCancellationState> m_State;

  friend class SlrCancellationSource;
  friend class SlrOperationTracker;
};

class SlrCancellationSource
{
public:
  SlrCancellationSource() : m_State(std::make_shared<SlrCancellationState>()) {}

  explicit SlrCancellationSource(std::function<bool()> externallyRequested)
      : SlrCancellationSource()
  {
    m_State->externallyRequested = std::move(externallyRequested);
  }

  SlrCancellationToken token() const noexcept { return SlrCancellationToken(m_State); }

  void cancel() const noexcept
  {
    if (m_State) {
      m_State->requested.store(true, std::memory_order_release);
    }
  }

  bool isCancellationRequested() const noexcept
  {
    return token().isCancellationRequested();
  }

private:
  std::shared_ptr<SlrCancellationState> m_State;
};

// Process-level admission and lifetime accounting for SLR network/install
// work. Suppression closes admission atomically with operation acquisition.
// Admitted operations remain counted until their RAII lease is destroyed.
class SlrOperationTracker
{
public:
  class Operation
  {
  public:
    Operation() = default;
    Operation(const Operation&) = delete;
    Operation& operator=(const Operation&) = delete;

    Operation(Operation&& other) noexcept
        : m_Owner(std::exchange(other.m_Owner, nullptr)),
          m_Cancellation(std::move(other.m_Cancellation))
    {
    }

    Operation& operator=(Operation&& other) noexcept
    {
      if (this != &other) {
        release();
        m_Owner = std::exchange(other.m_Owner, nullptr);
        m_Cancellation = std::move(other.m_Cancellation);
      }
      return *this;
    }

    ~Operation() { release(); }

    bool isCancellationRequested() const noexcept
    {
      return m_Owner == nullptr ||
             m_Owner->m_AdmissionSuppressed.load(std::memory_order_acquire) ||
             m_Cancellation.isCancellationRequested();
    }

    // Every durable commit checks fail-stop/caller cancellation at a named
    // production stage. A step that already started remains represented by
    // this operation's active lease until it completes.
    template <typename Mutation>
    bool runDurableStepIfAllowed(SlrDurableStage, Mutation&& mutation)
    {
      if (m_Owner == nullptr) {
        return false;
      }

      std::lock_guard durableLock(m_Owner->m_DurableStepMutex);
      if (m_Owner->m_AdmissionSuppressed.load(std::memory_order_relaxed) ||
          m_Cancellation.isCancellationRequested()) {
        return false;
      }

      std::invoke(std::forward<Mutation>(mutation));
      return true;
    }

  private:
    Operation(SlrOperationTracker& owner, SlrCancellationToken cancellation)
        : m_Owner(&owner), m_Cancellation(std::move(cancellation))
    {
    }

    void release() noexcept
    {
      if (m_Owner == nullptr) {
        return;
      }

      try {
        std::lock_guard lock(m_Owner->m_AdmissionMutex);
        --m_Owner->m_ActiveOperations;
      } catch (...) {
        // Losing lifetime accounting is unsafe. Keep fail-stop closed and the
        // drain conservatively false if the platform mutex itself fails.
        m_Owner->m_AdmissionSuppressed.store(true, std::memory_order_release);
      }
      m_Owner = nullptr;
    }

    SlrOperationTracker* m_Owner = nullptr;
    SlrCancellationToken m_Cancellation;

    friend class SlrOperationTracker;
  };

  std::optional<Operation> tryBegin(SlrCancellationToken cancellation = {}) noexcept
  {
    try {
      std::lock_guard lock(m_AdmissionMutex);
      if (m_AdmissionSuppressed.load(std::memory_order_relaxed) ||
          cancellation.isCancellationRequested()) {
        return std::nullopt;
      }

      ++m_ActiveOperations;
      return Operation(*this, std::move(cancellation));
    } catch (...) {
      m_AdmissionSuppressed.store(true, std::memory_order_release);
      return std::nullopt;
    }
  }

  void suppressAndCancel() noexcept
  {
    // Publish cancellation before taking the short admission lock. Durable
    // steps use a distinct mutex, so large filesystem cleanup cannot stall
    // fail-stop phase one.
    m_AdmissionSuppressed.store(true, std::memory_order_release);
    try {
      std::lock_guard lock(m_AdmissionMutex);
    } catch (...) {
    }
  }

  bool admissionSuppressed() const noexcept
  {
    return m_AdmissionSuppressed.load(std::memory_order_acquire);
  }

  bool drained() const noexcept
  {
    try {
      std::lock_guard lock(m_AdmissionMutex);
      return m_ActiveOperations == 0;
    } catch (...) {
      return false;
    }
  }

  std::size_t activeOperations() const noexcept
  {
    try {
      std::lock_guard lock(m_AdmissionMutex);
      return m_ActiveOperations;
    } catch (...) {
      return 1;
    }
  }

private:
  mutable std::mutex m_AdmissionMutex;
  mutable std::mutex m_DurableStepMutex;
  std::atomic_bool m_AdmissionSuppressed{false};
  std::size_t m_ActiveOperations = 0;
};

#endif // SLROPERATIONCONTEXT_H
