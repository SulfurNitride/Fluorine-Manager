#ifndef APPLICATIONCOMPLETION_H
#define APPLICATIONCOMPLETION_H

#include <QString>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include <sys/types.h>

// Shared, compact state for the split startApplication()/waitForApplication()
// API. The process lifetime has exactly one observer; public waits consume its
// cached result instead of attaching to a PID that may since have been reused.
class ApplicationCompletion {
public:
  enum class Result {
    Running,
    Completed,
    Error,
    Cancelled,
    ForceUnlocked,
  };

  enum class Control {
    Continue,
    Cancel,
    ForceUnlock,
  };

  struct Snapshot {
    pid_t displayPid = 0;
    QString displayName;
    Result result = Result::Running;
    std::uint32_t exitCode = static_cast<std::uint32_t>(-1);
    bool cleanupFinished = false;
    bool refreshRequested = false;
    bool refreshFinished = false;
    bool refreshFailed = false;
    std::uint32_t observationFailures = 0;
  };

  ApplicationCompletion(pid_t rootPid, bool ownsVfs, QString profileName)
      : m_RootPid(rootPid), m_OwnsVfs(ownsVfs),
        m_ProfileName(std::move(profileName)), m_DisplayPid(rootPid) {}

  pid_t rootPid() const { return m_RootPid.load(std::memory_order_acquire); }
  void bindRootPid(pid_t pid) noexcept {
    pid_t expected = 0;
    m_RootPid.compare_exchange_strong(expected, pid, std::memory_order_release,
                                      std::memory_order_relaxed);
  }
  bool ownsVfs() const { return m_OwnsVfs; }
  const QString &profileName() const { return m_ProfileName; }

  static bool requiresLaunchCleanup(Result result, bool ownsVfs) {
    return result == Result::Completed ||
           (ownsVfs &&
            (result == Result::Cancelled || result == Result::ForceUnlocked));
  }

  void updateProcess(pid_t pid, const QString &name) {
    {
      std::lock_guard lock(m_Mutex);
      m_DisplayPid = pid;
      m_DisplayName = name;
    }
    m_Changed.notify_all();
  }

  void requestControl(Control control) {
    {
      std::lock_guard lock(m_Mutex);
      if (control == Control::ForceUnlock || m_Control == Control::Continue) {
        m_Control = control;
      }
    }
    m_Changed.notify_all();
  }

  Control requestedControl() const {
    std::lock_guard lock(m_Mutex);
    return m_Control;
  }

  void requestRefresh() {
    {
      std::lock_guard lock(m_Mutex);
      m_RefreshRequested = true;
    }
    m_Changed.notify_all();
  }

  // Called on the organizer thread immediately before afterRun(). This lets
  // an already-waiting caller fold its refresh into the normal post-run path.
  bool claimRefreshForCleanup() {
    std::lock_guard lock(m_Mutex);
    if (!m_CleanupFinished && m_RefreshRequested && !m_RefreshClaimed) {
      m_RefreshClaimed = true;
      return true;
    }
    return false;
  }

  // A caller may first wait after the no-refresh cleanup has completed. It
  // claims the same refresh exactly once through the refresh-only core path.
  bool claimRefreshAfterCleanup() {
    std::lock_guard lock(m_Mutex);
    if (m_CleanupFinished && m_Result != Result::Running &&
        m_Result != Result::Error && m_RefreshRequested && !m_RefreshClaimed) {
      m_RefreshClaimed = true;
      return true;
    }
    return false;
  }

  void finishRefresh() {
    {
      std::lock_guard lock(m_Mutex);
      if (m_RefreshFinished) {
        return;
      }
      m_RefreshFinished = true;
    }
    m_Changed.notify_all();
  }

  void failRefresh() {
    {
      std::lock_guard lock(m_Mutex);
      if (m_RefreshFinished) {
        return;
      }
      m_RefreshFailed = true;
      m_RefreshFinished = true;
    }
    m_Changed.notify_all();
  }

  void finishLifetime(Result result, std::uint32_t exitCode) {
    {
      std::lock_guard lock(m_Mutex);
      m_Result = result;
      m_ExitCode = exitCode;
    }
    m_Changed.notify_all();
  }

  void noteObservationFailure() {
    {
      std::lock_guard lock(m_Mutex);
      ++m_ObservationFailures;
    }
    m_Changed.notify_all();
  }

  void finishCleanup() {
    std::vector<std::function<void()>> callbacks;
    {
      std::lock_guard lock(m_Mutex);
      if (m_CleanupFinished) {
        return;
      }
      m_CleanupFinished = true;
      m_CleanupFinishedAt = std::chrono::steady_clock::now();
      callbacks.swap(m_CleanupCallbacks);
    }
    m_Changed.notify_all();
    for (auto &callback : callbacks) {
      try {
        callback();
      } catch (...) {
        // Retention callbacks are bookkeeping only and must never interfere
        // with launch-owned cleanup completion.
      }
    }
  }

  void onCleanupFinished(std::function<void()> callback) {
    bool invokeNow = false;
    {
      std::lock_guard lock(m_Mutex);
      if (m_CleanupFinished) {
        invokeNow = true;
      } else {
        m_CleanupCallbacks.push_back(callback);
      }
    }
    if (invokeNow) {
      try {
        callback();
      } catch (...) {
      }
    }
  }

  bool canRetire() const {
    std::lock_guard lock(m_Mutex);
    return m_CleanupFinished;
  }

  bool retentionExpired(std::chrono::steady_clock::duration grace) const {
    std::lock_guard lock(m_Mutex);
    return m_CleanupFinishedAt.has_value() &&
           std::chrono::steady_clock::now() - *m_CleanupFinishedAt >= grace;
  }

  Snapshot snapshot() const {
    std::lock_guard lock(m_Mutex);
    return {m_DisplayPid > 0 ? m_DisplayPid : rootPid(),
            m_DisplayName,
            m_Result,
            m_ExitCode,
            m_CleanupFinished,
            m_RefreshRequested,
            m_RefreshFinished,
            m_RefreshFailed,
            m_ObservationFailures};
  }

  void waitForChange(std::chrono::milliseconds duration) const {
    std::unique_lock lock(m_Mutex);
    m_Changed.wait_for(lock, duration);
  }

private:
  std::atomic<pid_t> m_RootPid;
  const bool m_OwnsVfs;
  const QString m_ProfileName;

  mutable std::mutex m_Mutex;
  mutable std::condition_variable m_Changed;
  pid_t m_DisplayPid = 0;
  QString m_DisplayName;
  Result m_Result = Result::Running;
  Control m_Control = Control::Continue;
  std::uint32_t m_ExitCode = static_cast<std::uint32_t>(-1);
  bool m_CleanupFinished = false;
  bool m_RefreshRequested = false;
  bool m_RefreshClaimed = false;
  bool m_RefreshFinished = false;
  bool m_RefreshFailed = false;
  std::uint32_t m_ObservationFailures = 0;
  std::optional<std::chrono::steady_clock::time_point> m_CleanupFinishedAt;
  std::vector<std::function<void()>> m_CleanupCallbacks;
};

#endif // APPLICATIONCOMPLETION_H
