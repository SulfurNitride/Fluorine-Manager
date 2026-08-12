#ifndef PROCESSLAUNCHCONTEXT_H
#define PROCESSLAUNCHCONTEXT_H

#include <QHash>
#include <QString>

#include <atomic>
#include <exception>
#include <optional>
#include <mutex>
#include <type_traits>
#include <utility>

namespace process_coordination
{
// Re-check the predicate after every wake-up.  In particular, finishing one
// directory-refresh generation may synchronously start the next generation
// before releasing waiters for the first.
template <typename IsBusy, typename WaitOnce>
bool waitUntilIdle(IsBusy isBusy, WaitOnce waitOnce)
{
  while (isBusy()) {
    if (!waitOnce()) {
      return false;
    }
  }

  return true;
}
}  // namespace process_coordination

// Tracks ownership of launch-scoped resources.  A token remains registered
// until its one post-run cleanup claims it (or launch preparation is
// abandoned).  This deliberately is not a "latest launch" slot: native tools
// may overlap and may finish in either order.
class ProcessLaunchContextTracker
{
public:
  class ConfigurationLease
  {
  public:
    ConfigurationLease() = default;
    ConfigurationLease(const ConfigurationLease&) = delete;
    ConfigurationLease& operator=(const ConfigurationLease&) = delete;

    ConfigurationLease(ConfigurationLease&& other) noexcept
        : m_Tracker(std::exchange(other.m_Tracker, nullptr))
    {}

    ConfigurationLease& operator=(ConfigurationLease&& other) noexcept
    {
      if (this != &other) {
        release();
        m_Tracker = std::exchange(other.m_Tracker, nullptr);
      }
      return *this;
    }

    ~ConfigurationLease() { release(); }

    explicit operator bool() const noexcept { return m_Tracker != nullptr; }

  private:
    explicit ConfigurationLease(ProcessLaunchContextTracker* tracker) noexcept
        : m_Tracker(tracker)
    {}

    void release() noexcept
    {
      if (m_Tracker != nullptr) {
        m_Tracker->releaseConfigurationLease();
        m_Tracker = nullptr;
      }
    }

    ProcessLaunchContextTracker* m_Tracker{nullptr};
    friend class ProcessLaunchContextTracker;
  };

  struct Launch
  {
    QString profileName;
    bool ownsVfs = false;
    bool vfsReservationActive = false;
    bool completionClaimed = false;
  };

  struct ActiveLaunches
  {
    int total = 0;
    int vfs = 0;
    int native = 0;

    bool empty() const { return total == 0; }
  };

  bool reserve(const QString& token, const QString& profileName, bool ownsVfs)
  {
    if (m_ReservationsSuppressed.load(std::memory_order_acquire)) {
      return false;
    }

    std::lock_guard lock(m_Mutex);
    if (m_ReservationsSuppressed.load(std::memory_order_acquire) ||
        m_ConfigurationLeaseActive ||
        token.isEmpty() || m_Launches.contains(token)) {
      return false;
    }

    if (ownsVfs) {
      for (auto it = m_Launches.cbegin(); it != m_Launches.cend(); ++it) {
        if (it->vfsReservationActive) {
          return false;
        }
      }
    }

    m_Launches.insert(token, Launch{profileName, ownsVfs, ownsVfs, false});
    return true;
  }

  ConfigurationLease tryAcquireConfigurationLease()
  {
    std::lock_guard lock(m_Mutex);
    if (m_ReservationsSuppressed.load(std::memory_order_acquire) ||
        m_ConfigurationLeaseActive) {
      return {};
    }

    // Existing launches keep their normal lifetime and cleanup ownership, but
    // reserve() rejects every new launch until the Settings transaction ends.
    // This avoids imposing a broad, user-visible "no Settings while playing"
    // restriction merely to make the rare rollback-failure path simpler.
    m_ConfigurationLeaseActive = true;
    return ConfigurationLease(this);
  }

  void suppressNewReservations() noexcept
  {
    // A reservation already inside the short critical section may finish and
    // remains tracked through mandatory cleanup. Every later admission fails
    // closed without making fail-stop risk a throwing mutex acquisition.
    m_ReservationsSuppressed.store(true, std::memory_order_release);
  }

  // Keep the launch visible to shutdown checks while temporarily handing its
  // not-yet-mounted VFS reservation to a synchronous nested launch.
  bool handoffVfsReservation(const QString& token)
  {
    std::lock_guard lock(m_Mutex);
    const auto it = m_Launches.find(token);
    if (it == m_Launches.end() || !it->ownsVfs ||
        !it->vfsReservationActive || it->completionClaimed) {
      return false;
    }

    it->vfsReservationActive = false;
    return true;
  }

  bool reclaimVfsReservation(const QString& token)
  {
    std::lock_guard lock(m_Mutex);
    const auto launch = m_Launches.find(token);
    if (launch == m_Launches.end() || !launch->ownsVfs ||
        launch->vfsReservationActive || launch->completionClaimed) {
      return false;
    }

    for (auto it = m_Launches.cbegin(); it != m_Launches.cend(); ++it) {
      if (it->vfsReservationActive) {
        return false;
      }
    }

    launch->vfsReservationActive = true;
    return true;
  }

  void abandon(const QString& token)
  {
    std::lock_guard lock(m_Mutex);
    m_Launches.remove(token);
  }

  std::optional<Launch> claimCompletion(const QString& token,
                                        const QString& profileName,
                                        bool ownsVfs)
  {
    std::lock_guard lock(m_Mutex);
    const auto it = m_Launches.find(token);
    if (it == m_Launches.end() || it->completionClaimed ||
        it->profileName != profileName || it->ownsVfs != ownsVfs) {
      return std::nullopt;
    }

    it->completionClaimed = true;
    return *it;
  }

  // A mandatory cleanup attempt may fail after it has claimed the launch.
  // Only that exact token/profile/resource tuple may resume the claimed work;
  // ordinary duplicate completion claims remain rejected.
  std::optional<Launch> resumeCompletion(const QString& token,
                                         const QString& profileName,
                                         bool ownsVfs)
  {
    std::lock_guard lock(m_Mutex);
    const auto it = m_Launches.find(token);
    if (it == m_Launches.end() || !it->completionClaimed ||
        it->profileName != profileName || it->ownsVfs != ownsVfs) {
      return std::nullopt;
    }

    return *it;
  }

  struct CleanupAccess
  {
    enum class State
    {
      Rejected,
      Failed,
      Accepted,
    };

    State state{State::Rejected};
    bool vfsReservationActive{false};
    std::exception_ptr failure;
  };

  // Allocation-free access used by noexcept mandatory-cleanup orchestration.
  // It avoids copying QString-bearing Launch state after completionClaimed has
  // changed, so an allocation failure cannot terminate between claim and retry.
  CleanupAccess accessCompletionForCleanup(const QString& token,
                                           const QString& profileName,
                                           bool ownsVfs, bool retry) noexcept
  {
    try {
      std::lock_guard lock(m_Mutex);
      const auto it = m_Launches.find(token);
      if (it == m_Launches.end() || it->profileName != profileName ||
          it->ownsVfs != ownsVfs || it->completionClaimed != retry) {
        return {};
      }

      if (!retry) {
        it->completionClaimed = true;
      }
      return {CleanupAccess::State::Accepted, it->vfsReservationActive, {}};
    } catch (...) {
      return {CleanupAccess::State::Failed, false, std::current_exception()};
    }
  }

  void finishCompletion(const QString& token)
  {
    std::lock_guard lock(m_Mutex);
    m_Launches.remove(token);
  }

  // afterRun has released the physical VFS resource, so a finished-run
  // callback may launch its successor. The old context remains registered
  // until afterRun itself returns, keeping restart/exit safely denied.
  bool releaseCompletedVfsReservation(const QString& token)
  {
    std::lock_guard lock(m_Mutex);
    const auto it = m_Launches.find(token);
    if (it == m_Launches.end() || !it->ownsVfs ||
        !it->vfsReservationActive || !it->completionClaimed) {
      return false;
    }

    it->vfsReservationActive = false;
    return true;
  }

  bool contains(const QString& token) const
  {
    std::lock_guard lock(m_Mutex);
    return m_Launches.contains(token);
  }

  int activeCount() const
  {
    return activeLaunches().total;
  }

  ActiveLaunches activeLaunches() const
  {
    std::lock_guard lock(m_Mutex);
    ActiveLaunches result;
    result.total = m_Launches.size();
    for (auto it = m_Launches.cbegin(); it != m_Launches.cend(); ++it) {
      if (it->ownsVfs) {
        ++result.vfs;
      } else {
        ++result.native;
      }
    }
    return result;
  }

private:
  void releaseConfigurationLease() noexcept
  {
    try {
      std::lock_guard lock(m_Mutex);
      m_ConfigurationLeaseActive = false;
    } catch (...) {
      // A mutex failure cannot be recovered safely. Keep launch admission
      // closed rather than starting an application during uncertain Settings
      // ownership.
      m_ReservationsSuppressed.store(true, std::memory_order_release);
    }
  }

  mutable std::mutex m_Mutex;
  QHash<QString, Launch> m_Launches;
  bool m_ConfigurationLeaseActive{false};
  std::atomic_bool m_ReservationsSuppressed{false};
};

namespace launch_cleanup
{
enum class AttemptKind
{
  Initial,
  Retry,
};

enum class AttemptState
{
  Rejected,
  RetryRequired,
  Complete,
};

struct AttemptResult
{
  AttemptState state{AttemptState::Rejected};
  std::exception_ptr failure;
  bool vfsCleanupPerformed{false};
};

// Claims (or resumes) one exact launch and releases its physical VFS
// reservation only after the supplied mandatory cleanup has returned. Any
// exception leaves both the tracker entry and VFS reservation intact so a
// caller can safely retry instead of certifying incomplete cleanup.
template <typename Cleanup>
AttemptResult attemptMandatoryCleanup(ProcessLaunchContextTracker& tracker,
                                      const QString& token,
                                      const QString& profileName, bool ownsVfs,
                                      AttemptKind kind, Cleanup&& cleanup,
                                      bool additionalCleanupRequired = false) noexcept
{
  const auto access = tracker.accessCompletionForCleanup(
      token, profileName, ownsVfs, kind == AttemptKind::Retry);
  if (access.state == ProcessLaunchContextTracker::CleanupAccess::State::Rejected) {
    return {};
  }
  if (access.state == ProcessLaunchContextTracker::CleanupAccess::State::Failed) {
    return {AttemptState::RetryRequired, access.failure};
  }

  if ((ownsVfs && access.vfsReservationActive) ||
      additionalCleanupRequired) {
    try {
      if constexpr (std::is_invocable_v<Cleanup, bool>) {
        std::forward<Cleanup>(cleanup)(ownsVfs &&
                                       access.vfsReservationActive);
      } else {
        std::forward<Cleanup>(cleanup)();
      }
    } catch (...) {
      return {AttemptState::RetryRequired, std::current_exception()};
    }

    if (ownsVfs && access.vfsReservationActive) {
      try {
        if (!tracker.releaseCompletedVfsReservation(token)) {
          return {AttemptState::RetryRequired, {}};
        }
      } catch (...) {
        return {AttemptState::RetryRequired, std::current_exception()};
      }
    }
  }

  return {AttemptState::Complete, {},
          ownsVfs && access.vfsReservationActive};
}

struct FinalizationResult
{
  bool trackerFinished{false};
  bool completionInvoked{false};
  std::exception_ptr postFailure;
  std::exception_ptr trackerFailure;
  std::exception_ptr completionFailure;
};

// Once mandatory cleanup succeeds, optional profile/plugin work may fail but
// must not strand ownership or escape a queued Qt callback. The public cleanup
// completion is withheld unless tracker release itself succeeds.
template <typename PostCleanup, typename CleanupComplete>
FinalizationResult finalizeCompletedCleanup(
    ProcessLaunchContextTracker* tracker, const QString& token,
    PostCleanup&& postCleanup, CleanupComplete&& cleanupComplete) noexcept
{
  FinalizationResult result;
  try {
    postCleanup();
  } catch (...) {
    result.postFailure = std::current_exception();
  }

  if (tracker != nullptr) {
    try {
      tracker->finishCompletion(token);
      result.trackerFinished = true;
    } catch (...) {
      result.trackerFailure = std::current_exception();
      return result;
    }
  } else {
    result.trackerFinished = true;
  }

  try {
    result.completionInvoked = true;
    cleanupComplete();
  } catch (...) {
    result.completionFailure = std::current_exception();
  }
  return result;
}

// Two-stage state machine used by OrganizerCore: finalization is a no-op until
// mandatory cleanup reaches Complete, which mechanically withholds public
// cleanup completion across RetryRequired attempts.
class CleanupStage
{
public:
  explicit CleanupStage(AttemptResult attempt) : m_Attempt(std::move(attempt)) {}

  const AttemptResult& attempt() const noexcept { return m_Attempt; }

  template <typename PostCleanup, typename CleanupComplete>
  FinalizationResult finalize(ProcessLaunchContextTracker* tracker,
                              const QString& token, PostCleanup&& postCleanup,
                              CleanupComplete&& cleanupComplete) const noexcept
  {
    if (m_Attempt.state != AttemptState::Complete) {
      return {};
    }
    return finalizeCompletedCleanup(
        tracker, token, std::forward<PostCleanup>(postCleanup),
        std::forward<CleanupComplete>(cleanupComplete));
  }

private:
  AttemptResult m_Attempt;
};

template <typename Cleanup>
CleanupStage beginMandatoryCleanup(ProcessLaunchContextTracker& tracker,
                                   const QString& token,
                                   const QString& profileName, bool ownsVfs,
                                   AttemptKind kind, Cleanup&& cleanup) noexcept
{
  return CleanupStage(attemptMandatoryCleanup(
      tracker, token, profileName, ownsVfs, kind,
      std::forward<Cleanup>(cleanup)));
}
}  // namespace launch_cleanup

// Reserves one launch context for preparation and abandons it automatically
// unless the caller explicitly retains the successfully prepared session.
class ScopedProcessLaunchReservation
{
public:
  ScopedProcessLaunchReservation(ProcessLaunchContextTracker& tracker,
                                 const QString& token,
                                 const QString& profileName, bool ownsVfs)
      : m_Tracker(tracker.reserve(token, profileName, ownsVfs) ? &tracker
                                                               : nullptr),
        m_Token(token)
  {}

  ~ScopedProcessLaunchReservation()
  {
    if (m_Tracker != nullptr) {
      m_Tracker->abandon(m_Token);
    }
  }

  ScopedProcessLaunchReservation(const ScopedProcessLaunchReservation&) = delete;
  ScopedProcessLaunchReservation&
  operator=(const ScopedProcessLaunchReservation&) = delete;

  explicit operator bool() const { return m_Tracker != nullptr; }

  void retain() { m_Tracker = nullptr; }

private:
  ProcessLaunchContextTracker* m_Tracker;
  QString m_Token;
};

struct ProcessShutdownPolicy
{
  static bool allowsCoreDestruction(
      const ProcessLaunchContextTracker::ActiveLaunches& active)
  {
    return active.empty();
  }

  static bool checkDownloadPrompts(bool force) { return !force; }

  static bool showActiveLaunchFeedback(bool silentActiveLaunch)
  {
    return !silentActiveLaunch;
  }
};

#endif  // PROCESSLAUNCHCONTEXT_H
