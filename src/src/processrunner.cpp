#include "processrunner.h"
#include "applicationcompletion.h"
#include "asynctask.h"
#include "processlifetime.h"
#include "rootprocesscompletion.h"
#include "env.h"
#include "envmodule.h"
#include "instancemanager.h"
#include "iuserinterface.h"
#include "launchlifecycle.h"
#include "organizercore.h"
#include "vfsbackend.h"
#include "wineruntimeconfig.h"

#include <iplugingame.h>
#include <log.h>
#include <report.h>
#include <uibase/scopeguard.h>
#include <uibase/utility.h>

#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QProcess>
#include <QSettings>
#include <QThread>
#include <QUuid>

#include <algorithm>
#include <chrono>
#include <exception>
#include <thread>

using namespace MOBase;

void adjustForVirtualized(const IPluginGame* game, spawn::SpawnParameters& sp,
                          const Settings& settings)
{
  const QString modsPath = settings.paths().mods();

  // Check if this is a request with either an executable or a working
  // directory under our mods folder; if so, start the process in a
  // virtualized "environment" with the appropriate paths fixed:
  // (i.e. mods/FNIS/path/exe => game/data/path/exe)
  QString cwdPath         = sp.currentDirectory.absolutePath();
  QString trailedModsPath = modsPath;
  if (!trailedModsPath.endsWith('/')) {
    trailedModsPath = trailedModsPath + '/';
  }
  bool virtualizedCwd = cwdPath.startsWith(trailedModsPath, Qt::CaseInsensitive);
  QString binPath     = sp.binary.absoluteFilePath();
  bool virtualizedBin = binPath.startsWith(trailedModsPath, Qt::CaseInsensitive);
  if (!virtualizedCwd && !virtualizedBin) {
    return;
  }

  if (virtualizedCwd) {
    int cwdOffset       = cwdPath.indexOf('/', trailedModsPath.length());
    QString adjustedCwd = cwdPath.mid(cwdOffset, -1);
    cwdPath             = game->dataDirectory().absolutePath();
    if (cwdOffset >= 0)
      cwdPath += adjustedCwd;
  }

  if (virtualizedBin) {
    int binOffset       = binPath.indexOf('/', trailedModsPath.length());
    QString adjustedBin = binPath.mid(binOffset, -1);
    binPath             = game->dataDirectory().absolutePath();
    if (binOffset >= 0)
      binPath += adjustedBin;
  }

  // FUSE is already mounted on Linux — resolve paths directly without
  // launching through MO2-core (which would fail in the Proton prefix).
  //
  // Root Builder deploys Root/ contents to the game directory root,
  // stripping the "Root/" prefix.  Fix paths that were remapped to
  // <dataDir>/Root/... so they point to <gameDir>/... instead.
  // Also handle direct mods/.../Root/ paths (not just dataDir/Root/).
  const QString gameDir = game->gameDirectory().absolutePath();
  const QString dataDir = game->dataDirectory().absolutePath();

  auto normalizeRootPath = [&](QString& path) {
    const QString rootWithSlash = dataDir + QStringLiteral("/Root/");
    const QString rootExact     = dataDir + QStringLiteral("/Root");
    if (path.startsWith(rootWithSlash, Qt::CaseInsensitive)) {
      const QString after = path.mid(rootWithSlash.length());
      path = after.isEmpty() ? gameDir : gameDir + QStringLiteral("/") + after;
      return true;
    }
    if (path.compare(rootExact, Qt::CaseInsensitive) == 0) {
      path = gameDir;
      return true;
    }
    return false;
  };

  bool binNormalized = normalizeRootPath(binPath);
  bool cwdNormalized = normalizeRootPath(cwdPath);

  if (binNormalized) {
    log::info("Root Builder: rewrote binary -> '{}'", binPath);
  }
  if (cwdNormalized) {
    log::info("Root Builder: rewrote start-in -> '{}'", cwdPath);
  }

  // If neither was caught by the dataDir/Root/ check, the path might still be
  // the original mods/.../Root/ path (not yet remapped). This happens when
  // the first remapping above produced something that didn't match the
  // dataDir/Root pattern.
  if (!binNormalized && binPath.startsWith(trailedModsPath, Qt::CaseInsensitive)) {
    int rootIdx =
        binPath.indexOf("/Root/", trailedModsPath.length(), Qt::CaseInsensitive);
    if (rootIdx < 0)
      rootIdx =
          binPath.indexOf("/Root", trailedModsPath.length(), Qt::CaseInsensitive);
    if (rootIdx >= 0) {
      int afterRootStart = rootIdx + 5;  // skip "/Root"
      if (afterRootStart < binPath.length() && binPath[afterRootStart] == '/')
        ++afterRootStart;
      const QString afterRoot = binPath.mid(afterRootStart);
      const QString modRoot   = binPath.left(rootIdx + 5);

      binPath = afterRoot.isEmpty() ? gameDir
                                    : gameDir + QStringLiteral("/") + afterRoot;
      log::info("Root Builder: rewrote binary (mod path) -> '{}'", binPath);

      if (!cwdNormalized && cwdPath.startsWith(modRoot, Qt::CaseInsensitive)) {
        int cwdAfterStart = modRoot.length();
        if (cwdAfterStart < cwdPath.length() && cwdPath[cwdAfterStart] == '/')
          ++cwdAfterStart;
        const QString cwdAfter = cwdPath.mid(cwdAfterStart);
        cwdPath = cwdAfter.isEmpty() ? gameDir
                                     : gameDir + QStringLiteral("/") + cwdAfter;
        log::info("Root Builder: rewrote start-in (mod path) -> '{}'", cwdPath);
      }
    }
  }

  sp.binary = QFileInfo(binPath);
  sp.currentDirectory.setPath(cwdPath);
}

enum class Interest
{
  None = 0,
  Weak,
  Strong
};

QString toString(Interest i)
{
  switch (i) {
  case Interest::Weak:
    return "weak";

  case Interest::Strong:
    return "strong";

  case Interest::None:
  default:
    return "no";
  }
}

pid_t handleToPid(HANDLE h)
{
  return static_cast<pid_t>(reinterpret_cast<intptr_t>(h));
}

void logQueuedProcessFailure(const char* operation, pid_t pid,
                             std::exception_ptr failure) noexcept
{
  try {
    if (failure) {
      std::rethrow_exception(failure);
    }
    log::error("process runner: queued {} failed for pid {}", operation, pid);
  } catch (const std::exception& e) {
    try {
      log::error("process runner: queued {} failed for pid {}: {}", operation,
                 pid, e.what());
    } catch (...) {
    }
  } catch (...) {
    try {
      log::error("process runner: queued {} failed for pid {}", operation, pid);
    } catch (...) {
    }
  }
}

void finishApplicationCleanupNoThrow(
    const std::shared_ptr<ApplicationCompletion>& completion,
    const char* operation) noexcept
{
  try {
    completion->finishCleanup();
  } catch (...) {
    logQueuedProcessFailure(operation, completion->rootPid(),
                            std::current_exception());
  }
}

void finishApplicationRefreshNoThrow(
    const std::shared_ptr<ApplicationCompletion> &completion,
    const char *operation) noexcept {
  try {
    completion->finishRefresh();
  } catch (...) {
    logQueuedProcessFailure(operation, completion->rootPid(),
                            std::current_exception());
  }
}

void failApplicationRefreshNoThrow(
    const std::shared_ptr<ApplicationCompletion> &completion,
    const char *operation) noexcept {
  try {
    completion->failRefresh();
  } catch (...) {
    logQueuedProcessFailure(operation, completion->rootPid(),
                            std::current_exception());
  }
}

void failApplicationCompletionNoThrow(
    const std::shared_ptr<ApplicationCompletion>& completion,
    bool finishCleanup, const char* operation) noexcept
{
  try {
    completion->finishLifetime(ApplicationCompletion::Result::Error,
                               static_cast<std::uint32_t>(-1));
  } catch (...) {
    logQueuedProcessFailure(operation, completion->rootPid(),
                            std::current_exception());
  }
  if (finishCleanup) {
    finishApplicationCleanupNoThrow(completion, operation);
  }
}


ProcessRunner::Results waitForProcess(HANDLE initialProcess, LPDWORD exitCode,
                                      UILocker::Session* ls,
                                      const QStringList& expected,
                                      bool killTreeOnUnlock,
                                      const QString& launchToken,
                                      bool expectDetachedCompanion,
                                      std::uint64_t rootStartTime,
                                      const std::shared_ptr<
                                          process_lifetime::RootProcessCompletion>&
                                          rootCompletion)
{
  process_lifetime::Callbacks callbacks;
  if (ls != nullptr) {
    callbacks.updateProcess = [ls](pid_t pid, const QString& name) {
      ls->setInfo(static_cast<DWORD>(pid), name);
    };
    callbacks.control = []() {
      switch (UILocker::Session::result()) {
      case UILocker::Cancelled:
        return process_lifetime::Control::Cancel;
      case UILocker::ForceUnlocked:
        return process_lifetime::Control::ForceUnlock;
      case UILocker::StillLocked:
        return process_lifetime::Control::Continue;
      case UILocker::NoResult:
      default:
        return process_lifetime::Control::Error;
      }
    };
  }

  const auto result = process_lifetime::waitForPid(
      handleToPid(initialProcess), exitCode, callbacks, expected, killTreeOnUnlock,
      launchToken, expectDetachedCompanion, rootStartTime, rootCompletion);
  switch (result) {
  case process_lifetime::Result::Completed:
    return ProcessRunner::Completed;
  case process_lifetime::Result::Cancelled:
    return ProcessRunner::Cancelled;
  case process_lifetime::Result::ForceUnlocked:
    return ProcessRunner::ForceUnlocked;
  case process_lifetime::Result::Error:
  default:
    return ProcessRunner::Error;
  }
}

ProcessRunner::Results waitForProcesses(const std::vector<HANDLE>& initialProcesses,
                                        UILocker::Session* ls,
                                        const QStringList& expected,
                                        bool killTreeOnUnlock,
                                        const QString& launchToken,
                                        bool expectDetachedCompanion,
                                        std::uint64_t rootStartTime = 0)
{
  if (initialProcesses.empty()) {
    return ProcessRunner::Completed;
  }

  for (HANDLE h : initialProcesses) {
    DWORD ignored = 0;
    const auto r =
        waitForProcess(h, &ignored, ls, expected, killTreeOnUnlock,
                       launchToken, expectDetachedCompanion, rootStartTime, {});
    if (r != ProcessRunner::Completed) {
      return r;
    }
  }

  return ProcessRunner::Completed;
}

OrganizerCore::AfterRunResult invokeAfterRun(
    OrganizerCore& core, const QFileInfo& binary, DWORD exitCode, pid_t rootPid,
    bool unmountVfs, const QString& launchToken, const QString& profileName,
    bool triggerRefresh, spawn::SaveDeploymentReceipt saveDeployment,
    std::function<void()> refreshComplete = {},
    std::function<void(bool refreshScheduled)> cleanupComplete = {})
{
  const auto started = std::chrono::steady_clock::now();
  try {
    log::info("process runner: afterRun begin for root pid {} (exit code {})",
              rootPid, exitCode);
  } catch (...) {
  }
  const auto result = core.afterRun(
      binary, exitCode, unmountVfs, launchToken, profileName, triggerRefresh,
      std::move(saveDeployment), std::move(refreshComplete),
      std::move(cleanupComplete));
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - started)
                           .count();
  try {
    log::info("process runner: afterRun end for root pid {} ({} ms)", rootPid,
              elapsed);
  } catch (...) {
  }
  return result;
}

ProcessRunner::Results applicationResult(ApplicationCompletion::Result result)
{
  switch (result) {
  case ApplicationCompletion::Result::Completed:
    return ProcessRunner::Completed;
  case ApplicationCompletion::Result::Cancelled:
    return ProcessRunner::Cancelled;
  case ApplicationCompletion::Result::ForceUnlocked:
    return ProcessRunner::ForceUnlocked;
  case ApplicationCompletion::Result::Running:
  case ApplicationCompletion::Result::Error:
  default:
    return ProcessRunner::Error;
  }
}

ApplicationCompletion::Result
applicationResult(process_lifetime::Result result)
{
  switch (result) {
  case process_lifetime::Result::Completed:
    return ApplicationCompletion::Result::Completed;
  case process_lifetime::Result::Cancelled:
    return ApplicationCompletion::Result::Cancelled;
  case process_lifetime::Result::ForceUnlocked:
    return ApplicationCompletion::Result::ForceUnlocked;
  case process_lifetime::Result::Error:
  default:
    return ApplicationCompletion::Result::Error;
  }
}

std::function<void()> makeApplicationRefreshCompletion(
    OrganizerCore &core,
    const std::shared_ptr<ApplicationCompletion> &completion) {
  auto destroyedConnection = std::make_shared<QMetaObject::Connection>();
  *destroyedConnection =
      QObject::connect(&core, &QObject::destroyed, [completion]() noexcept {
        failApplicationRefreshNoThrow(completion,
                                      "destroyed-core refresh failure");
      });

  return [completion, destroyedConnection]() noexcept {
    try {
      QObject::disconnect(*destroyedConnection);
    } catch (...) {
      logQueuedProcessFailure("refresh disconnect", completion->rootPid(),
                              std::current_exception());
    }
    finishApplicationRefreshNoThrow(completion, "refresh completion");
  };
}

void scheduleLateApplicationRefresh(
    QPointer<OrganizerCore> core,
    const std::shared_ptr<ApplicationCompletion> &completion) noexcept {
  try {
    if (!completion->claimRefreshAfterCleanup()) {
      return;
    }

    if (!core) {
      failApplicationRefreshNoThrow(completion, "late refresh without core");
      return;
    }

    const bool queued = QMetaObject::invokeMethod(
        core,
        [core, completion]() noexcept {
          try {
            if (!core) {
              failApplicationRefreshNoThrow(completion,
                                            "late refresh without core");
              return;
            }

            if (!core->refreshAfterRun(
                    completion->profileName(),
                    makeApplicationRefreshCompletion(*core, completion))) {
              failApplicationRefreshNoThrow(completion,
                                            "unscheduled late refresh");
            }
          } catch (...) {
            logQueuedProcessFailure("late post-run refresh",
                                    completion->rootPid(),
                                    std::current_exception());
            failApplicationRefreshNoThrow(completion, "failed late refresh");
          }
        },
        Qt::QueuedConnection);
    if (!queued) {
      failApplicationRefreshNoThrow(completion, "unqueued late refresh");
    }
  } catch (...) {
    logQueuedProcessFailure("late refresh scheduling", completion->rootPid(),
                            std::current_exception());
    failApplicationRefreshNoThrow(completion, "failed late refresh scheduling");
  }
}

struct PreparedProcessObserver
{
  enum class Mode
  {
    AsyncRefresh,
    Application,
  };

  QPointer<OrganizerCore> core;
  QFileInfo binary;
  QStringList expectedExecutables;
  QString lifetimeToken;
  QString profileName;
  bool ownsVfs{false};
  spawn::SaveDeploymentReceipt saveDeployment;
  bool expectCompanion{false};
  std::shared_ptr<ApplicationCompletion> completion;
  process_lifetime::Callbacks callbacks;

  // Bound after spawn and before the release-store in executor.activate().
  pid_t pid{-1};
  std::uint64_t rootStartTime{process_lifetime::UnknownProcessStartTime};
  std::shared_ptr<process_lifetime::RootProcessCompletion> rootCompletion;
  Mode mode{Mode::AsyncRefresh};
  DWORD preservedExitCode{static_cast<DWORD>(-1)};
  bool triggerRefresh{false};
  std::atomic<bool> stopRequested{false};
};

void runPreparedAsyncObserver(
    const std::shared_ptr<PreparedProcessObserver>& observer,
    std::stop_token stop)
{
  constexpr auto invalidExitCode = static_cast<std::uint32_t>(-1);
  std::uint32_t observedExitCode = invalidExitCode;
  std::uint32_t lastExitCode = invalidExitCode;
  auto retryDelay = std::chrono::milliseconds(100);
  process_lifetime::Result result = process_lifetime::Result::Error;

  while (observer->core) {
    if (stop.stop_requested()) {
      return;
    }
    observedExitCode = invalidExitCode;
    result = process_lifetime::waitForPid(
        observer->pid, &observedExitCode, observer->callbacks,
        observer->expectedExecutables,
        /*killTreeOnUnlock=*/false, observer->lifetimeToken,
        observer->expectCompanion, observer->rootStartTime,
        observer->rootCompletion);
    if (observedExitCode != invalidExitCode) {
      lastExitCode = observedExitCode;
    }
    if (result != process_lifetime::Result::Error ||
        observer->lifetimeToken.isEmpty()) {
      break;
    }

    log::warn("process runner: asynchronous managed lifetime observation failed "
              "for pid {}; retrying in {} ms without releasing launch ownership",
              observer->pid, retryDelay.count());
    auto remaining = retryDelay;
    while (remaining > std::chrono::milliseconds::zero() && observer->core &&
           !stop.stop_requested()) {
      const auto slice = std::min(remaining, std::chrono::milliseconds(50));
      QThread::msleep(static_cast<unsigned long>(slice.count()));
      remaining -= slice;
    }
    retryDelay = std::min(retryDelay * 2, std::chrono::milliseconds(2000));
  }

  if (!observer->core || stop.stop_requested()) {
    return;
  }
  if (lastExitCode != invalidExitCode) {
    observedExitCode = lastExitCode;
  }
  if (result != process_lifetime::Result::Completed) {
    log::warn("process runner: asynchronous lifetime tracking failed for pid {} "
              "(result {})",
              observer->pid, static_cast<int>(result));
    observedExitCode = 1;
  } else if (observer->preservedExitCode != static_cast<DWORD>(-1)) {
    observedExitCode = observer->preservedExitCode;
  }

  if (result != process_lifetime::Result::Completed) {
    if (!observer->ownsVfs && !observer->saveDeployment.needsRollback()) {
      QMetaObject::invokeMethod(
          observer->core,
          [observer]() noexcept {
            try {
              if (observer->core) {
                observer->core->abandonProcessLaunch(observer->lifetimeToken);
              }
            } catch (...) {
              logQueuedProcessFailure("launch abandonment", observer->pid,
                                      std::current_exception());
            }
          },
          Qt::QueuedConnection);
    }
    return;
  }

  const bool queued = QMetaObject::invokeMethod(
      observer->core,
      [observer, observedExitCode]() noexcept {
        try {
          if (observer->core) {
            const auto result = invokeAfterRun(
                *observer->core, observer->binary, observedExitCode,
                observer->pid, observer->ownsVfs, observer->lifetimeToken,
                observer->profileName, observer->triggerRefresh,
                observer->saveDeployment);
            if (result.state == OrganizerCore::AfterRunState::Rejected) {
              log::error(
                  "process runner: queued afterRun was rejected for pid {}",
                  observer->pid);
            } else if (result.state ==
                       OrganizerCore::AfterRunState::RefreshFailed) {
              log::error("process runner: queued afterRun could not start its "
                         "refresh for pid {}",
                         observer->pid);
            }
          }
        } catch (...) {
          logQueuedProcessFailure("afterRun", observer->pid,
                                  std::current_exception());
        }
      },
      Qt::QueuedConnection);
  if (!queued) {
    log::error("process runner: unable to queue afterRun for pid {}; launch "
               "ownership remains retained",
               observer->pid);
  }
}

void runPreparedApplicationObserver(
    const std::shared_ptr<PreparedProcessObserver>& observer,
    std::stop_token stop)
{
  const auto& completion = observer->completion;
  constexpr auto invalidExitCode = static_cast<std::uint32_t>(-1);
  std::uint32_t exitCode = invalidExitCode;
  std::uint32_t preservedExitCode = invalidExitCode;
  auto retryDelay = std::chrono::milliseconds(100);
  process_lifetime::Result lifetimeResult = process_lifetime::Result::Error;
  for (;;) {
    exitCode = invalidExitCode;
    lifetimeResult = process_lifetime::waitForPid(
        completion->rootPid(), &exitCode, observer->callbacks,
        observer->expectedExecutables, observer->ownsVfs,
        observer->lifetimeToken, observer->expectCompanion,
        observer->rootStartTime, observer->rootCompletion);
    if (exitCode != invalidExitCode) {
      preservedExitCode = exitCode;
    }
    if (stop.stop_requested() ||
        lifetimeResult != process_lifetime::Result::Error ||
        observer->lifetimeToken.isEmpty() || !observer->core) {
      break;
    }

    completion->noteObservationFailure();
    log::warn("process runner: managed lifetime observation failed for pid {}; "
              "retrying in {} ms without releasing launch ownership",
              completion->rootPid(), retryDelay.count());

    const auto controlAtFailure = completion->requestedControl();
    auto remaining = retryDelay;
    while (remaining > std::chrono::milliseconds::zero() && observer->core &&
           !stop.stop_requested()) {
      if (completion->requestedControl() != controlAtFailure) {
        break;
      }
      const auto slice = std::min(remaining, std::chrono::milliseconds(50));
      QThread::msleep(static_cast<unsigned long>(slice.count()));
      remaining -= slice;
    }
    retryDelay = std::min(retryDelay * 2, std::chrono::milliseconds(2000));
  }
  if (exitCode == invalidExitCode && preservedExitCode != invalidExitCode) {
    exitCode = preservedExitCode;
  }
  if (stop.stop_requested()) {
    completion->finishLifetime(ApplicationCompletion::Result::Error,
                               static_cast<std::uint32_t>(-1));
    if (!observer->core || observer->lifetimeToken.isEmpty()) {
      finishApplicationCleanupNoThrow(completion,
                                      "stopped raw-pid observer cleanup");
    }
    return;
  }

  const auto result = applicationResult(lifetimeResult);
  completion->finishLifetime(result, exitCode);
  const bool needsLaunchCleanup =
      ApplicationCompletion::requiresLaunchCleanup(result, observer->ownsVfs) ||
      observer->saveDeployment.needsRollback();

  if (!observer->core) {
    completion->finishLifetime(ApplicationCompletion::Result::Error,
                               static_cast<std::uint32_t>(-1));
    completion->finishCleanup();
    return;
  }

  if (!needsLaunchCleanup) {
    if (!observer->ownsVfs) {
      const bool queued = QMetaObject::invokeMethod(
          observer->core,
          [observer]() noexcept {
            try {
              if (observer->core) {
                observer->core->abandonProcessLaunch(observer->lifetimeToken);
              }
              observer->completion->finishCleanup();
            } catch (...) {
              logQueuedProcessFailure("application cleanup",
                                      observer->completion->rootPid(),
                                      std::current_exception());
            }
          },
          Qt::QueuedConnection);
      if (!queued) {
        if (observer->lifetimeToken.isEmpty()) {
          finishApplicationCleanupNoThrow(completion,
                                          "raw-pid application cleanup");
        } else {
          logQueuedProcessFailure("unqueued application abandonment",
                                  completion->rootPid(), {});
        }
      }
    } else {
      log::error(
          "process runner: VFS application cleanup was not proven for pid "
          "{}; retaining cleanupFinished=false",
          completion->rootPid());
    }
    return;
  }

  const bool queued = QMetaObject::invokeMethod(
      observer->core,
      [observer, exitCode]() noexcept {
        try {
          const auto &completion = observer->completion;
          if (!observer->core) {
            failApplicationCompletionNoThrow(completion,
                                             /*finishCleanup=*/true,
                                             "cleanup without core");
            return;
          }

          const bool triggerRefresh = completion->claimRefreshForCleanup();
          const auto result = invokeAfterRun(
              *observer->core, observer->binary, exitCode, observer->pid,
              observer->ownsVfs, observer->lifetimeToken,
              completion->profileName(), triggerRefresh,
              observer->saveDeployment,
              triggerRefresh ? makeApplicationRefreshCompletion(*observer->core,
                                                                completion)
                             : std::function<void()>{},
              [observer, completion,
               triggerRefresh](bool refreshScheduled) noexcept {
                if (triggerRefresh && !refreshScheduled) {
                  failApplicationRefreshNoThrow(
                      completion, "afterRun refresh startup failure");
                }
                finishApplicationCleanupNoThrow(completion,
                                                "afterRun cleanup completion");
                scheduleLateApplicationRefresh(observer->core, completion);
              });
          if (result.state == OrganizerCore::AfterRunState::Rejected) {
            failApplicationCompletionNoThrow(completion,
                                             /*finishCleanup=*/
                                             observer->lifetimeToken.isEmpty(),
                                             "rejected afterRun");
            if (triggerRefresh) {
              failApplicationRefreshNoThrow(completion,
                                            "rejected afterRun refresh");
            }
          } else if (result.state ==
                     OrganizerCore::AfterRunState::RefreshFailed) {
            failApplicationRefreshNoThrow(completion,
                                          "failed afterRun refresh startup");
          }
        } catch (...) {
          const auto& completion = observer->completion;
          logQueuedProcessFailure("monitored afterRun", completion->rootPid(),
                                  std::current_exception());
          // An exception before OrganizerCore accepts the cleanup has no
          // retry continuation. Do not claim success for a retained managed
          // tracker; a waiting caller remains blocked by cleanupFinished=false.
          failApplicationCompletionNoThrow(
              completion,
              /*finishCleanup=*/observer->lifetimeToken.isEmpty(),
              "failed monitored afterRun");
        }
      },
      Qt::QueuedConnection);
  if (!queued) {
    failApplicationCompletionNoThrow(
        completion, /*finishCleanup=*/observer->lifetimeToken.isEmpty(),
        "unqueued application afterRun");
    if (!observer->lifetimeToken.isEmpty()) {
      log::error("process runner: unable to queue launch cleanup for pid {}; launch "
                 "ownership and cleanupFinished=false remain retained",
                 completion->rootPid());
    }
  }
}

ProcessRunner::ProcessRunner(OrganizerCore& core, IUserInterface* ui)
    : m_core(core), m_ui(ui),
      m_waitFlags(NoFlags), m_handle(INVALID_HANDLE_VALUE)
{
  // all processes started in ProcessRunner are hooked by default
  setHooked(true);
}

ProcessRunner& ProcessRunner::setBinary(const QFileInfo& binary)
{
  m_sp.binary = QFileInfo(MOBase::normalizePathForHost(binary.filePath()));
  return *this;
}

ProcessRunner& ProcessRunner::setArguments(const QString& arguments)
{
  m_sp.arguments = arguments;
  m_sp.argumentList = QProcess::splitCommand(arguments);
  return *this;
}

ProcessRunner& ProcessRunner::setArguments(const QStringList& arguments)
{
  // Preserve the historical string passed to plugin callbacks and logs while
  // keeping the exact list authoritative for execution.
  m_sp.arguments = arguments.join(QLatin1Char(' '));
  m_sp.argumentList = arguments;
  return *this;
}

ProcessRunner& ProcessRunner::setCurrentDirectory(const QDir& directory)
{
  m_sp.currentDirectory.setPath(MOBase::normalizePathForHost(directory.path()));
  return *this;
}

ProcessRunner& ProcessRunner::setSteamID(const QString& steamID)
{
  m_sp.steamAppID = steamID;
  return *this;
}

ProcessRunner& ProcessRunner::setCustomOverwrite(const QString& customOverwrite)
{
  m_customOverwrite = customOverwrite;
  return *this;
}

ProcessRunner& ProcessRunner::setForcedLibraries(const ForcedLibraries& forcedLibraries)
{
  m_forcedLibraries = forcedLibraries;
  return *this;
}

ProcessRunner& ProcessRunner::setProfileName(const QString& profileName)
{
  m_profileName = profileName;
  return *this;
}

ProcessRunner& ProcessRunner::setWaitForCompletion(WaitFlags flags,
                                                   UILocker::Reasons reason)
{
  m_waitFlags  = flags;
  m_lockReason = reason;

  if (m_waitFlags.testFlag(WaitForRefresh) && !m_waitFlags.testFlag(TriggerRefresh)) {
    log::warn("process runner: WaitForRefresh without TriggerRefresh "
              "makes no sense, will be ignored");
  }

  return *this;
}

ProcessRunner& ProcessRunner::setHooked(bool b)
{
  m_sp.hooked = b;
  return *this;
}

ProcessRunner& ProcessRunner::setFromFile(QWidget* parent, const QFileInfo& targetInfo)
{
  if (!parent && m_ui) {
    parent = m_ui->mainWindow();
  }

  // if the file is a .exe, start it directly; if it's anything else, ask the
  // shell to start it
  const auto fec = spawn::getFileExecutionContext(parent, targetInfo);

  switch (fec.type) {
  case spawn::FileExecutionTypes::Executable: {
    setBinary(fec.binary);
    setArguments(fec.arguments);
    setCurrentDirectory(targetInfo.absoluteDir());
    break;
  }

  case spawn::FileExecutionTypes::Other:
  default: {
    m_shellOpen = targetInfo;
    setHooked(false);
    break;
  }
  }

  return *this;
}

ProcessRunner& ProcessRunner::setFromExecutable(const Executable& exe)
{
  const auto profile = m_core.currentProfile();
  if (!profile) {
    throw MyException(QObject::tr("No profile set"));
  }

  const QString customOverwrite =
      profile->setting("custom_overwrites", exe.title()).toString();

  ForcedLibraries forcedLibraries;
  if (profile->forcedLibrariesEnabled(exe.title())) {
    forcedLibraries = profile->determineForcedLibraries(exe.title());
  }

  QString currentDirectory = exe.workingDirectory();
  if (currentDirectory.isEmpty()) {
    currentDirectory = exe.binaryInfo().absolutePath();
  }

  setBinary(exe.binaryInfo());
  setArguments(exe.arguments());
  setCurrentDirectory(currentDirectory);
  setSteamID(exe.steamAppID());
  setCustomOverwrite(customOverwrite);
  setForcedLibraries(forcedLibraries);

  m_sp.useProton    = exe.useProton();
  m_sp.useTerminal  = exe.useTerminal();

  return *this;
}

ProcessRunner& ProcessRunner::setFromShortcut(const MOShortcut& shortcut)
{
  const auto currentInstance = InstanceManager::singleton().currentInstance();

  if (currentInstance) {
    if (shortcut.hasInstance() && !shortcut.isForInstance(*currentInstance)) {
      MOBase::reportError(QObject::tr("This shortcut is for instance '%1' but "
                                      "Mod Organizer is currently "
                                      "running for '%2'. Exit Mod Organizer "
                                      "before running the shortcut or "
                                      "change the active instance.")
                              .arg(shortcut.instanceDisplayName())
                              .arg(currentInstance->displayName()));

      throw std::exception();
    }
  }

  const auto* exes = m_core.executablesList();
  const auto exe   = exes->find(shortcut.executableName());

  if (exe != exes->end()) {
    setFromExecutable(*exe);
  } else {
    MOBase::reportError(QObject::tr("Executable '%1' does not exist in instance '%2'.")
                            .arg(shortcut.executableName())
                            .arg(currentInstance->displayName()));

    throw std::exception();
  }

  return *this;
}

ProcessRunner& ProcessRunner::setFromFileOrExecutable(
    const QString& executable, const QStringList& args, const QString& cwd,
    const QString& profileOverride, const QString& forcedCustomOverwrite,
    bool ignoreCustomOverwrite)
{
  const auto profile = m_core.currentProfile();
  if (!profile) {
    throw MyException(QObject::tr("No profile set"));
  }

  setBinary(QFileInfo(executable));
  setArguments(args);
  setCurrentDirectory(cwd);
  setProfileName(profileOverride);

  if (executable.contains('\\') || executable.contains('/')) {
    if (m_sp.binary.isRelative()) {
      setBinary(QFileInfo(
          m_core.managedGame()->gameDirectory().absoluteFilePath(executable)));
    }

    if (cwd == "") {
      setCurrentDirectory(m_sp.binary.absolutePath());
    }

    try {
      const Executable& exe = m_core.executablesList()->getByBinary(m_sp.binary);

      setSteamID(exe.steamAppID());
      setCustomOverwrite(profile->setting("custom_overwrites", exe.title()).toString());

      if (profile->forcedLibrariesEnabled(exe.title())) {
        setForcedLibraries(profile->determineForcedLibraries(exe.title()));
      }
    } catch (const std::runtime_error&) {
      // nop
    }
  } else {
    try {
      const Executable& exe = m_core.executablesList()->get(executable);

      setSteamID(exe.steamAppID());
      setCustomOverwrite(profile->setting("custom_overwrites", exe.title()).toString());

      if (profile->forcedLibrariesEnabled(exe.title())) {
        setForcedLibraries(profile->determineForcedLibraries(exe.title()));
      }

      if (args.isEmpty()) {
        setArguments(exe.arguments());
      }

      setBinary(exe.binaryInfo());

      if (cwd == "") {
        setCurrentDirectory(exe.workingDirectory());
      }
    } catch (const std::runtime_error&) {
      log::warn("\"{}\" not set up as executable", executable);
    }
  }

  if (ignoreCustomOverwrite) {
    setCustomOverwrite("");
  } else if (!forcedCustomOverwrite.isEmpty()) {
    setCustomOverwrite(forcedCustomOverwrite);
  }

  return *this;
}

bool ProcessRunner::shouldRunShell() const
{
  return !m_shellOpen.filePath().isEmpty();
}

bool ProcessRunner::prepareLaunchObserver(bool ownsVfs)
{
  try {
    auto observer = std::make_shared<PreparedProcessObserver>();
    observer->core = &m_core;
    observer->binary = m_sp.binary;
    observer->expectedExecutables = m_expectedExecutables;
    observer->lifetimeToken = m_sp.lifetimeToken;
    observer->profileName = m_profileName;
    observer->ownsVfs = ownsVfs;
    observer->saveDeployment = m_sp.saveDeployment;
    observer->expectCompanion = !m_companionProcessNames.isEmpty();
    observer->completion = std::make_shared<ApplicationCompletion>(
        0, ownsVfs, m_profileName);

    const auto completion = observer->completion;
    observer->callbacks.updateProcess =
        [completion](pid_t trackedPid, const QString& name) {
          completion->updateProcess(trackedPid, name);
        };
    auto* observerState = observer.get();
    observer->callbacks.control = [observerState, completion]() {
      if (observerState->stopRequested.load(std::memory_order_acquire)) {
        return process_lifetime::Control::Error;
      }
      switch (completion->requestedControl()) {
      case ApplicationCompletion::Control::Cancel:
        return process_lifetime::Control::Cancel;
      case ApplicationCompletion::Control::ForceUnlock:
        return process_lifetime::Control::ForceUnlock;
      case ApplicationCompletion::Control::Continue:
      default:
        return process_lifetime::Control::Continue;
      }
    };

    async_task::ManagedTaskExecutor::Task work =
        [observer](std::stop_token stop) {
          observer->stopRequested.store(stop.stop_requested(),
                                        std::memory_order_release);
          std::stop_callback stopCallback(stop, [observer]() {
            observer->stopRequested.store(true, std::memory_order_release);
          });
          if (observer->mode == PreparedProcessObserver::Mode::Application) {
            runPreparedApplicationObserver(observer, stop);
          } else {
            runPreparedAsyncObserver(observer, stop);
          }
        };
    async_task::ManagedTaskExecutor::FailureHandler failure =
        [observer]() noexcept {
          try {
            if (observer->mode == PreparedProcessObserver::Mode::Application) {
              failApplicationCompletionNoThrow(
                  observer->completion, /*finishCleanup=*/!observer->core,
                  "observer task failure");
              if (!observer->core) {
                return;
              }
              if (!observer->lifetimeToken.isEmpty()) {
                // An unexpected observer failure cannot prove a managed
                // lifetime ended. Retain launch ownership and
                // cleanupFinished=false for both VFS and native launches.
                return;
              }
              const bool queued = QMetaObject::invokeMethod(
                  observer->core,
                  [observer]() noexcept {
                    try {
                      if (observer->core) {
                        observer->core->abandonProcessLaunch(
                            observer->lifetimeToken);
                      }
                      finishApplicationCleanupNoThrow(
                          observer->completion,
                          "observer failure cleanup completion");
                    } catch (...) {
                      logQueuedProcessFailure(
                          "observer failure cleanup", observer->pid,
                          std::current_exception());
                    }
                  },
                  Qt::QueuedConnection);
              if (!queued) {
                if (observer->lifetimeToken.isEmpty()) {
                  finishApplicationCleanupNoThrow(
                      observer->completion,
                      "unqueued raw-pid observer failure cleanup");
                } else {
                  logQueuedProcessFailure("unqueued observer abandonment",
                                          observer->pid, {});
                }
              }
            } else if (observer->core &&
                       observer->lifetimeToken.isEmpty()) {
              QMetaObject::invokeMethod(
                  observer->core,
                  [observer]() noexcept {
                    try {
                      if (observer->core) {
                        observer->core->abandonProcessLaunch(
                            observer->lifetimeToken);
                      }
                    } catch (...) {
                      logQueuedProcessFailure("observer abandonment",
                                              observer->pid,
                                              std::current_exception());
                    }
                  },
                  Qt::QueuedConnection);
            }
          } catch (...) {
            logQueuedProcessFailure("observer failure handler", observer->pid,
                                    std::current_exception());
          }
        };

    if (!async_task::executor().prepare(
            m_asyncTaskLease, std::move(work), std::move(failure))) {
      return false;
    }
    m_preparedObserver = std::move(observer);
    return true;
  } catch (const std::exception& e) {
    log::error("process runner: observer preparation failed: {}", e.what());
  } catch (...) {
    log::error("process runner: observer preparation failed");
  }
  return false;
}

bool ProcessRunner::activatePreparedObserver(bool applicationMode,
                                             DWORD preservedExitCode,
                                             bool triggerRefresh) noexcept
{
  if (m_observerActivated) {
    return true;
  }
  if (!m_preparedObserver || !m_asyncTaskLease) {
    return false;
  }

  m_preparedObserver->mode =
      applicationMode ? PreparedProcessObserver::Mode::Application
                      : PreparedProcessObserver::Mode::AsyncRefresh;
  m_preparedObserver->preservedExitCode = preservedExitCode;
  m_preparedObserver->triggerRefresh = triggerRefresh;
  if (!async_task::executor().activate(m_asyncTaskLease)) {
    return false;
  }
  m_observerActivated = true;
  return true;
}

ProcessRunner::Results ProcessRunner::run()
{
  // check if setHooked() was called after setFromFile(); this needs to modify
  // the settings to run the associated executable instead of using
  // shell::Open()
  if (shouldRunShell() && m_sp.hooked) {
    auto assoc = env::getAssociation(m_shellOpen);
    if (!assoc.executable.filePath().isEmpty()) {
      setBinary(assoc.executable);
      setArguments(assoc.formattedCommandLine);
      setCurrentDirectory(assoc.executable.absoluteDir());
      m_shellOpen = {};
    } else {
      log::error("failed to get the associated executable, running unhooked");
      m_sp.hooked = false;
    }
  } else if (!shouldRunShell() && !m_sp.hooked) {
    m_shellOpen = m_sp.binary;
  }

  std::optional<Results> r;

  if (shouldRunShell()) {
    r = runShell();
  } else {
    r = runBinary();
  }

  if (r) {
    return *r;
  }

  return postRun();
}

std::optional<ProcessRunner::Results> ProcessRunner::runShell()
{
  const auto file = MOBase::normalizePathForHost(m_shellOpen.absoluteFilePath());

  log::debug("executing from shell: '{}'", file);

  auto r = shell::Open(file);
  if (!r.success()) {
    return Error;
  }

  m_handle.reset(r.stealProcessHandle());

  // not all files will return a valid handle even if opening them was
  // successful, such as inproc handlers (like the photo viewer); in that case
  // it's impossible to determine the status, so just say it's still running.
  if (m_handle.get() == INVALID_HANDLE_VALUE) {
    log::debug("shell didn't report an error, but no handle is available");
    return Running;
  }

  return {};
}

std::optional<ProcessRunner::Results> ProcessRunner::runBinary()
{
  // Reserve bounded asynchronous-monitor capacity before any process or VFS
  // launch exists. A successful launch can therefore always transfer its one
  // lifetime task without a UI-thread fallback.
  m_asyncTaskLease = async_task::executor().reserve(&m_core);
  if (!m_asyncTaskLease) {
    log::error("process runner: no asynchronous lifetime monitor capacity is "
               "available");
    return Error;
  }

  if (m_profileName.isEmpty()) {
    const auto profile = m_core.currentProfile();
    if (!profile) {
      throw MyException(QObject::tr("No profile set"));
    }

    m_profileName = profile->name();
  }

  const auto* game = m_core.managedGame();
  auto& settings   = m_core.settings();
  const bool ownsVfs = (game == nullptr) || game->usesVFS();
  m_ownsVfs = ownsVfs;

  m_sp.wineRuntime = WineRuntimeConfig::current();
  // A direct native launch has no Wine authority. Passing the process-global
  // default into OrganizerCore would make OpenMW and other native games enter
  // registry/save/INI preparation for an unrelated prefix.
  if (game != nullptr && game->isNativeLinux() && !m_sp.useProton) {
    m_sp.wineRuntime = {};
  }
  QString wineRuntimeError;
  if (m_sp.useProton &&
      (m_sp.wineRuntime.generation == 0 ||
       !m_sp.wineRuntime.prefixError.isEmpty() ||
       m_sp.wineRuntime.prefixPath.isEmpty() ||
       !m_sp.wineRuntime.protonError.isEmpty() ||
       m_sp.wineRuntime.protonPath.isEmpty() ||
       !WineRuntimeConfig::revalidate(m_sp.wineRuntime,
                                      &wineRuntimeError))) {
    if (wineRuntimeError.isEmpty()) {
      wineRuntimeError = !m_sp.wineRuntime.prefixError.isEmpty()
                             ? m_sp.wineRuntime.prefixError
                         : !m_sp.wineRuntime.protonError.isEmpty()
                             ? m_sp.wineRuntime.protonError
                             : QObject::tr(
                                   "No complete Wine prefix and Proton runtime "
                                   "is configured for this instance.");
    }
    log::error("Refusing Proton launch: {}", wineRuntimeError);
    MOBase::reportError(
        QObject::tr("The selected Wine/Proton runtime is unavailable:\n\n%1\n\n"
                    "Correct the instance override or the application default "
                    "in Settings > Proton, then restart Fluorine Manager.")
            .arg(wineRuntimeError));
    return Error;
  }

  m_sp.lifetimeToken =
      QUuid::createUuid().toString(QUuid::WithoutBraces);
  if (!m_core.reserveProcessLaunch(m_sp.lifetimeToken, m_profileName,
                                   ownsVfs)) {
    log::error("process runner: cannot start '{}' because another launch "
               "owns the organizer VFS",
               m_sp.binary.absoluteFilePath().toStdString());
    return Error;
  }
  // Arm physical rollback immediately after reservation. Any exception or
  // early return from beforeRun, plugin virtuals, observer preparation or
  // spawn reaches the same noexcept cleanup. OrganizerCore retains/retries the
  // exact tracker entry if mandatory VFS cleanup cannot complete.
  auto rollbackPreparedLaunch = makePreparedLaunchRollback(
      [this, ownsVfs]() noexcept {
        m_core.abortProcessLaunchPreparation(
            m_sp.lifetimeToken, m_profileName, ownsVfs,
            std::move(m_sp.usvfsRequestPath),
            std::move(m_sp.saveDeployment));
      });

  // FUSE makes an executable stored under mods/ visible at its virtual game
  // path before adjustForVirtualized runs. USVFS is installed by the Windows
  // helper later, so its request must contain that virtual target from the
  // outset.
  const QSettings instanceIni(settings.filename(), QSettings::IniFormat);
  const bool preparingUsvfs = useUsvfsForLaunch(
      parseVfsBackend(
          instanceIni.value(kVfsBackendSetting, QStringLiteral("fuse"))
              .toString()),
      m_sp.useProton, game == nullptr || game->usesVFS());
  if (preparingUsvfs && game != nullptr) {
    adjustForVirtualized(game, m_sp, settings);
  }

  // saves profile, sets up the VFS, notifies plugins, etc.; can return false
  // if a plugin doesn't want the program to run.
  if (!m_core.beforeRun(m_sp.binary, m_sp.currentDirectory, m_sp.arguments,
                        m_sp.argumentList, m_profileName, m_customOverwrite,
                        m_forcedLibraries, m_sp.useProton, m_sp.lifetimeToken,
                        ownsVfs, &m_sp.usvfsRequestPath,
                        &m_sp.saveDeployment, m_sp.wineRuntime)) {
    return Error;
  }

  QWidget* parent = (m_ui ? m_ui->mainWindow() : nullptr);

  m_sp.gameDirectory = game->gameDirectory();

  if (m_sp.steamAppID.trimmed().isEmpty()) {
    const QString gameSteamId = game->steamAPPId().trimmed();
    if (!gameSteamId.isEmpty()) {
      m_sp.steamAppID = gameSteamId;
      log::debug("process runner: using game steam app id '{}' for launch",
                 m_sp.steamAppID);
    }
  }

  if (!checkSteam(parent, m_sp, game->gameDirectory(), m_sp.steamAppID, settings)) {
    return Error;
  }

  if (!checkBlacklist(parent, m_sp, settings)) {
    return Error;
  }

  // if the executable is inside the mods folder another instance of
  // ModOrganizer is spawned instead to launch it
  if (!preparingUsvfs) {
    adjustForVirtualized(game, m_sp, settings);
  }

  // Query game/plugin lifetime policy exactly once, after the final launch
  // path is known but before a process exists. Python and native plugins may
  // throw here; rollbackPreparedLaunch removes any VFS preparation while
  // retaining ownership if cleanup itself needs a retry.
  try {
    const QStringList requestedCompanions =
        game != nullptr
            ? game->executableProcessNames(m_sp.binary.absoluteFilePath(),
                                           m_sp.argumentList)
            : QStringList{};
    m_companionProcessNames = process_lifetime::buildExpectedExecutables(
        QFileInfo{}, QStringList{}, requestedCompanions);
    m_expectedExecutables = process_lifetime::buildExpectedExecutables(
        m_sp.binary, m_sp.argumentList, m_companionProcessNames);
    m_expectedExecutables = processTrackingExecutables(
        m_expectedExecutables, !m_sp.usvfsRequestPath.isEmpty());
    m_lifetimeTrackingPrepared = true;
  } catch (const std::exception& e) {
    log::error("process runner: failed to resolve companion lifetime policy "
               "for '{}': {}",
               m_sp.binary.absoluteFilePath().toStdString(), e.what());
    return Error;
  } catch (...) {
    log::error("process runner: failed to resolve companion lifetime policy "
               "for '{}' (unknown plugin exception)",
               m_sp.binary.absoluteFilePath().toStdString());
    return Error;
  }

  // Materialize the completion state, final executable list, callbacks and
  // both observer modes before a child process exists. After this point a
  // successful spawn requires only noexcept receipt binding and activation.
  if (!prepareLaunchObserver(ownsVfs)) {
    return Error;
  }

  auto launchReceipt = startBinary(parent, m_sp);
  m_handle.reset(reinterpret_cast<HANDLE>(
      static_cast<intptr_t>(launchReceipt.pid)));

  if (m_handle.get() == INVALID_HANDLE_VALUE) {
    // beforeRun may have deployed Root Builder files for the Wine-side USVFS
    // helper. Use the normal VFS teardown path when process creation fails.
    return Error;
  }

  m_sp.lifetimeRootStartTime = launchReceipt.startTime;
  m_rootCompletion = std::move(launchReceipt.completion);
  m_preparedObserver->pid = launchReceipt.pid;
  m_preparedObserver->rootStartTime = launchReceipt.startTime;
  m_preparedObserver->rootCompletion = m_rootCompletion;
  m_preparedObserver->completion->bindRootPid(launchReceipt.pid);
  rollbackPreparedLaunch.dismiss();

  return {};
}

ProcessRunner::Results ProcessRunner::postRun()
{
  const bool mustWait = (m_waitFlags & ForceWait);

  if (!m_sp.hooked && !mustWait) {
    return Running;
  }

  if (mustWait && m_lockReason == UILocker::NoReason) {
    log::debug("the ForceWait flag is set but the lock reason wasn't, "
               "defaulting to LockUI");

    m_lockReason = UILocker::LockUI;
  }

  // Only a runBinary() generation that reserved the organizer VFS may tear it
  // down. Shell-open and compatibility raw-PID waits have no launch ownership
  // and must not unmount an unrelated active launch.
  const bool ownsVfs = !m_sp.lifetimeToken.isEmpty() && m_ownsVfs;
  if (!mustWait && m_lockReason == UILocker::NoReason &&
      m_preparedObserver) {
    const bool applicationMode = !m_waitFlags.testFlag(TriggerRefresh);
    const bool activated = activatePreparedObserver(
        applicationMode, static_cast<DWORD>(-1),
        m_waitFlags.testFlag(TriggerRefresh));
    Q_ASSERT(activated);
  }
  const bool usingUsvfsHelper = !m_sp.usvfsRequestPath.isEmpty();
  QStringList fallbackExpectedExecutables;
  if (!m_lifetimeTrackingPrepared) {
    fallbackExpectedExecutables = processTrackingExecutables(
        process_lifetime::buildExpectedExecutables(
            m_sp.binary, m_sp.argumentList, {}),
        usingUsvfsHelper);
  }
  const QStringList& expectedExecutables =
      m_lifetimeTrackingPrepared ? m_expectedExecutables
                                 : fallbackExpectedExecutables;

  if (!m_companionProcessNames.isEmpty()) {
    log::info("process runner: root pid {} has companion lifetime processes [{}]",
              getProcessHandle(),
              m_companionProcessNames.join(", ").toStdString());
  }

  if (usingUsvfsHelper) {
    log::debug("process runner: using {} as the USVFS lifetime anchor",
               kUsvfsLauncherExecutable);
  }

  auto scheduleAsyncRefresh = [&](DWORD preservedExitCode) {
    const bool triggerRefresh = m_waitFlags.testFlag(TriggerRefresh);
    const bool activated = activatePreparedObserver(
        /*applicationMode=*/false, preservedExitCode, triggerRefresh);
    Q_ASSERT(activated);

    log::debug(
        "process runner: scheduled async post-run refresh for pid {} "
        "tracking [{}]",
        getProcessHandle(), expectedExecutables.join(", ").toStdString());
  };

  if (!mustWait) {
    if (m_lockReason == UILocker::NoReason) {
      // Main window launches typically use TriggerRefresh without
      // waiting/locking. In that mode we still need post-run refresh/sync once
      // the process exits.
      if (m_waitFlags.testFlag(TriggerRefresh)) {
        scheduleAsyncRefresh(static_cast<DWORD>(-1));
      } else if (m_preparedObserver) {
        // startApplication() obtains its public completion immediately after
        // run(). Start that one observer now, before returning through plugin
        // code that may allocate or throw.
        const bool activated = activatePreparedObserver(
            /*applicationMode=*/true, static_cast<DWORD>(-1), false);
        Q_ASSERT(activated);
      }
      return Running;
    }
  }

  // Only tear down the launched process tree on force-unlock for games that
  // have a wineprefix/FUSE VFS to clean up. Native, non-VFS games (OpenMW)
  // must keep running when the user releases the lock — same usesVFS() gate as
  // the FUSE mount in OrganizerCore (default true = unchanged for every other
  // game).
  const bool killTreeOnUnlock = ownsVfs;

  auto r = Error;
  withLock([&](auto &ls) {
    r = waitForProcess(m_handle.get(), &m_exitCode, &ls, expectedExecutables,
                       killTreeOnUnlock, m_sp.lifetimeToken,
                       !m_companionProcessNames.isEmpty(),
                       m_sp.lifetimeRootStartTime, m_rootCompletion);
  });
  const bool observationFailed = r == Error;

  if (!killTreeOnUnlock && (r == Cancelled || r == ForceUnlocked)) {
    // Cancel/Force Unlock for native non-VFS games releases only the UI lock;
    // the engine intentionally remains alive. Transfer lifecycle ownership to
    // the background so afterRun is emitted once at the real exit, never now.
    scheduleAsyncRefresh(m_exitCode);
    return r;
  }

  const bool needsLaunchCleanup =
      r == Completed || (ownsVfs && (r == Cancelled || r == ForceUnlocked));
  if (needsLaunchCleanup) {
    QEventLoop loop;
    auto refreshWait = std::make_shared<RefreshWaitLatch>(loop);
    const bool triggerRefresh = m_waitFlags.testFlag(TriggerRefresh);
    const bool wait = triggerRefresh && m_waitFlags.testFlag(WaitForRefresh);

    const auto afterRun = invokeAfterRun(
        m_core, m_sp.binary, m_exitCode, getProcessHandle(), ownsVfs,
        m_sp.lifetimeToken, m_profileName, triggerRefresh,
        m_sp.saveDeployment,
        wait ? std::function<void()>(
                   [refreshWait]() noexcept { refreshWait->complete(); })
             : std::function<void()>{},
        wait ? std::function<void(bool)>([refreshWait, triggerRefresh](
                                             bool refreshScheduled) noexcept {
          if (triggerRefresh && !refreshScheduled) {
            refreshWait->fail();
          }
        })
             : std::function<void(bool)>{});

    const bool completionWillArrive =
        afterRun.refreshScheduled ||
        afterRun.state == OrganizerCore::AfterRunState::CleanupPending;
    const bool alreadyComplete = refreshWait->completeBeforeWait();
    if (wait && completionWillArrive && !alreadyComplete) {
      log::debug("process runner: waiting until refresh finishes");
      loop.exec();
      log::debug("process runner: refresh is done");
    } else if (wait && alreadyComplete) {
      log::debug("process runner: refresh completed before wait loop started");
    }
    if (afterRun.state == OrganizerCore::AfterRunState::RefreshFailed ||
        (wait && refreshWait->failed())) {
      log::error("process runner: post-run refresh failed for pid {}",
                 getProcessHandle());
      r = Error;
    }
  }

  if (observationFailed && !m_sp.lifetimeToken.isEmpty()) {
    // A bounded observation failure is not proof that a managed process has
    // exited. Keep the exact generation under background observation for both
    // VFS and native launches; when procfs recovers it will perform the one
    // normal afterRun teardown and release the launch reservation.
    scheduleAsyncRefresh(m_exitCode);
  }

  return r;
}

ProcessRunner::Results ProcessRunner::attachToProcess(pid_t pid)
{
  // Raw PID compatibility has no spawn receipt or QProcess completion. Bind
  // the current procfs generation before preparing or activating an observer,
  // and reject an unreadable/exited PID instead of ever tracking PID alone.
  const auto startTime = process_lifetime::processStartTime(pid);
  if (!startTime ||
      process_lifetime::processIdentityState(pid, *startTime) !=
          process_lifetime::IdentityState::Running) {
    log::error("process runner: cannot capture a live generation for attached "
               "pid {}",
               pid);
    return Error;
  }
  m_sp.lifetimeRootStartTime = *startTime;

  if (!m_asyncTaskLease) {
    m_asyncTaskLease = async_task::executor().reserve(&m_core);
    if (!m_asyncTaskLease) {
      log::error("process runner: no asynchronous lifetime monitor capacity "
                 "is available for attached pid {}",
                 pid);
      return Error;
    }
  }
  if (!m_preparedObserver) {
    m_expectedExecutables =
        processTrackingExecutables(process_lifetime::buildExpectedExecutables(
                                       m_sp.binary, m_sp.argumentList, {}),
                                   !m_sp.usvfsRequestPath.isEmpty());
    m_lifetimeTrackingPrepared = true;
    if (!prepareLaunchObserver(/*ownsVfs=*/false)) {
      return Error;
    }
    m_preparedObserver->pid = pid;
    m_preparedObserver->rootStartTime = *startTime;
    m_preparedObserver->completion->bindRootPid(pid);
  }
  m_handle.reset(reinterpret_cast<HANDLE>(static_cast<intptr_t>(pid)));
  return postRun();
}

std::shared_ptr<ApplicationCompletion> ProcessRunner::monitorApplication() {
  const pid_t pid = getProcessHandle();
  if (pid <= 0 || m_handle.get() == INVALID_HANDLE_VALUE ||
      !m_preparedObserver) {
    return {};
  }

  // runBinary() prepared and postRun() activated this exact observer before
  // returning to OrganizerProxy. No allocation, list construction or
  // std::function storage is permitted in this successful-spawn path.
  const bool activated = activatePreparedObserver(
      /*applicationMode=*/true, static_cast<DWORD>(-1), false);
  Q_ASSERT(activated);
  auto completion = m_preparedObserver->completion;
  m_handle.release();
  m_handle.reset(INVALID_HANDLE_VALUE);
  return completion;
}

ProcessRunner::Results ProcessRunner::waitForApplicationCompletion(
    const std::shared_ptr<ApplicationCompletion> &completion) {
  if (!completion) {
    return Error;
  }

  const bool triggerRefresh = m_waitFlags.testFlag(TriggerRefresh);
  const bool waitForRefresh =
      triggerRefresh && m_waitFlags.testFlag(WaitForRefresh);
  if (triggerRefresh) {
    completion->requestRefresh();
  }

  Results result = Error;
  bool finished = false;
  QPointer<OrganizerCore> core = &m_core;
  withLock([&](auto& lock) {
    while (!finished) {
      scheduleLateApplicationRefresh(core, completion);
      const auto snapshot = completion->snapshot();
      lock.setInfo(static_cast<DWORD>(std::max<pid_t>(0, snapshot.displayPid)),
                   snapshot.displayName);

      if (snapshot.cleanupFinished) {
        result = applicationResult(snapshot.result);
        m_exitCode = snapshot.exitCode;
        if (triggerRefresh && snapshot.refreshFailed) {
          result = Error;
        }
        if (result == Error || !waitForRefresh || snapshot.refreshFinished) {
          finished = true;
          continue;
        }
      }

      // Once process lifetime is terminal, ignore a late button press and let
      // its already-queued launch cleanup complete.
      if (snapshot.result == ApplicationCompletion::Result::Running) {
        switch (UILocker::Session::result()) {
        case UILocker::Cancelled:
        case UILocker::ForceUnlocked: {
          const bool cancelled =
              UILocker::Session::result() == UILocker::Cancelled;
          if (!completion->ownsVfs()) {
            // Native non-VFS launches keep running after the lock is released;
            // their one background monitor still performs eventual cleanup.
            m_exitCode = 0;
            result = cancelled ? Cancelled : ForceUnlocked;
            finished = true;
            continue;
          }
          completion->requestControl(
              cancelled ? ApplicationCompletion::Control::Cancel
                        : ApplicationCompletion::Control::ForceUnlock);
          break;
        }
        case UILocker::StillLocked:
          break;
        case UILocker::NoResult:
        default:
          result = Error;
          finished = true;
          continue;
        }
      }

      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      completion->waitForChange(std::chrono::milliseconds(30));
    }
  });

  return result;
}

DWORD ProcessRunner::exitCode() const
{
  return m_exitCode;
}

pid_t ProcessRunner::getProcessHandle() const
{
  return static_cast<pid_t>(reinterpret_cast<intptr_t>(m_handle.get()));
}

env::HandlePtr ProcessRunner::stealProcessHandle()
{
  auto *h = m_handle.release();
  m_handle.reset(INVALID_HANDLE_VALUE);
  return env::HandlePtr(h);
}

void ProcessRunner::withLock(std::function<void(UILocker::Session&)> f)
{
  auto ls = UILocker::instance().lock(m_lockReason);
  f(*ls);
}
