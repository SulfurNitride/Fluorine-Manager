#include "commandline.h"
#include "commandlinearguments.h"
#include "env.h"
#include "fluorinepaths.h"
#include "instancemanager.h"
#include "loglist.h"
#include "moapplication.h"
#include "multiprocess.h"
#include "nxmhandler_linux.h"
#include "organizercore.h"
#include "runtimelock.h"
#include "shared/util.h"
#include "thread_utils.h"
#include "unixtermination.h"

#include <log.h>
#include <report.h>

#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QSocketNotifier>
#include <QTimer>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <optional>
#include <unistd.h>

using namespace MOBase;

int run(int argc, char* argv[]);
int runApplication(int argc, char* argv[], const QStringList& arguments,
                   UnixTerminationBridge& termination);

int main(int argc, char* argv[])
{
  const int r = run(argc, argv);
  std::cout << "mod organizer done\n";
  return r;
}

int run(int argc, char* argv[])
{
  const QString lockPath = fluorineDataDir() + QStringLiteral("/runtime.lock");

  QString argumentError;
  const auto arguments =
      cl::decodeUnixArguments(argc, argv, &argumentError);
  if (!arguments) {
    std::fprintf(stderr, "ERROR: invalid process arguments: %s\n",
                 argumentError.toUtf8().constData());
    return 1;
  }

  if (!arguments->isEmpty() && arguments->at(0) == "nxm-handle") {
    QString nxmUrl;
    if (arguments->size() == 2) {
      nxmUrl = arguments->at(1);
    } else if (arguments->size() == 3 &&
               arguments->at(1) == "nxm-handle") {
      nxmUrl = arguments->at(2);
    } else {
      std::fprintf(stderr, "ERROR: nxm-handle requires exactly one NXM link\n");
      return 1;
    }
    if (!isNxmLink(nxmUrl) || nxmUrl.contains(QLatin1Char('\r')) ||
        nxmUrl.contains(QLatin1Char('\n'))) {
      std::fprintf(stderr, "ERROR: invalid NXM link\n");
      return 1;
    }
    if (NxmHandlerLinux::sendToSocket(nxmUrl)) {
      return 0;
    }

    // Older registered handler scripts only invoke `nxm-handle`; they do not
    // contain the newer shell fallback that starts Fluorine when its socket is
    // absent. Replace this lightweight handler process with a normal Fluorine
    // invocation so existing registrations also open the last-used instance
    // and process the URL. /proc/self/exe avoids relying on argv[0] or PATH.
    // A packaged launcher descriptor must not survive the fallback exec. Adopt
    // it now so it becomes close-on-exec and the managed launcher can establish
    // exactly one fresh lease. Legacy bare handlers have no descriptor and are
    // still allowed to reach that launcher compatibility path.
    RuntimeLockLease inheritedRuntimeLock;
    QString runtimeLockError;
    if (qEnvironmentVariableIsSet("FLUORINE_RUNTIME_LOCK_FD") &&
        !RuntimeLockLease::adoptFromEnvironment(
            lockPath, &inheritedRuntimeLock, &runtimeLockError)) {
      std::fprintf(stderr, "ERROR: %s\n",
                   runtimeLockError.toLocal8Bit().constData());
      return 1;
    }

    const QString executable = QFileInfo(QStringLiteral("/proc/self/exe"))
                                   .canonicalFilePath();
    const QString managedLauncher =
        RuntimeLockLease::managedLauncherForExecutable(lockPath, executable);
    const QByteArray encodedUrl = nxmUrl.toLocal8Bit();
    if (!managedLauncher.isEmpty()) {
      const QByteArray launcher = QFile::encodeName(managedLauncher);
      ::execl(launcher.constData(), launcher.constData(), encodedUrl.constData(),
              static_cast<char*>(nullptr));
      std::perror("failed to relaunch the managed Fluorine launcher for NXM link");
      return 1;
    }
    ::execl("/proc/self/exe", argv[0], encodedUrl.constData(),
            static_cast<char*>(nullptr));
    std::perror("failed to relaunch Fluorine for NXM link");
    return 1;
  }

  RuntimeLockLease runtimeLock;
  QString runtimeLockError;
  if (!RuntimeLockLease::adoptFromEnvironment(lockPath, &runtimeLock,
                                               &runtimeLockError)) {
    std::fprintf(stderr, "ERROR: %s\n",
                 runtimeLockError.toLocal8Bit().constData());
    return 1;
  }

  try {
    UnixTerminationBridge termination;
    const int result = runApplication(argc, argv, *arguments, termination);

    // The bridge outlives MOApplication so its hard deadline also covers core,
    // FUSE and Qt member destruction. Disarm only after that teardown returns.
    termination.complete();
    return termination.requested() ? termination.exitCode() : result;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "ERROR: failed to initialize process shutdown: %s\n",
                 error.what());
    return 1;
  }
}

int runApplication(int argc, char* argv[], const QStringList& arguments,
                   UnixTerminationBridge& termination)
{

  MOShared::SetThisThreadName("main");
  setExceptionHandlers();

  cl::CommandLine cl;
  if (auto r = cl.processArguments(arguments)) {
    return *r;
  }
  if (termination.requested()) {
    return termination.exitCode();
  }

  fluorineMigrateDataDir();

  initLogging();

  // must be after logging
  TimeThis tt("main() multiprocess");

  MOApplication app(argc, argv);

  QTimer terminationRetry;
  terminationRetry.setInterval(100);
  auto requestTermination = [&] {
    termination.drainNotifications();
    if (!termination.requested()) {
      return;
    }

    // Repeat this idempotent closure because the first notification may arrive
    // before app.setup() has constructed the core.
    app.beginExternalShutdown();

    // Startup and instance-selection dialogs run nested event loops before a
    // MainWindow exists. Close those dialogs, preserve the pending request and
    // let the next top-level safe point unwind instead of authorizing a Qt exit
    // that exec() would forget.
    if (!app.mainEventLoopActive()) {
      QApplication::closeAllWindows();
      terminationRetry.start();
      return;
    }

    const auto result =
        ExitModOrganizer(Exit::Force, /*silentActiveLaunch=*/true);
    if (result == ExitRequestResult::Authorized) {
      qApp->exit(termination.exitCode());
    } else {
      // Existing launches retain ownership of mandatory afterRun/VFS cleanup.
      // Their asynchronous completion eventually makes authorization succeed.
      terminationRetry.start();
    }
  };

  QSocketNotifier terminationNotifier(termination.notificationFd(),
                                      QSocketNotifier::Read);
  QObject::connect(&terminationNotifier, &QSocketNotifier::activated, &app,
                   [&](QSocketDescriptor, QSocketNotifier::Type) {
                     requestTermination();
                   });
  QObject::connect(&terminationRetry, &QTimer::timeout, &app,
                   requestTermination);
  if (termination.requested()) {
    requestTermination();
  }

  auto terminationExit = [&]() -> std::optional<int> {
    if (!termination.requested()) {
      return std::nullopt;
    }
    requestTermination();
    return termination.exitCode();
  };

  if (auto r = cl.runPostApplication(app)) {
    return *r;
  }
  if (auto r = terminationExit()) {
    return *r;
  }

  MOMultiProcess multiProcess(cl.multiple());

  if (multiProcess.ephemeral()) {
    // not the primary process

    if (const auto forwarded = cl.forwardToPrimary(multiProcess)) {
      // Delivery failures already produce a specific transport error.
      return *forwarded ? 0 : 1;
    }

    QMessageBox::information(
        nullptr, QObject::tr("Mod Organizer"),
        QObject::tr("An instance of Mod Organizer is already running"));

    return 1;
  }

  // Install the primary's receiver before any command-line phase can open a
  // nested event loop. Messages remain behind MOApplication's readiness
  // barrier until a complete OrganizerCore generation exists.
  app.firstTimeSetup(multiProcess);
  if (auto r = terminationExit()) {
    return *r;
  }

  if (auto r = cl.runPostMultiProcess(multiProcess)) {
    return *r;
  }

  tt.stop();

  // force the "Select instance" dialog on startup, only for first loop or when
  // the current instance cannot be used
  bool pick = cl.pick();

  // MO runs in a loop because it can be restarted in several ways, such as
  // when switching instances or changing some settings
  for (;;) {
    try {
      if (auto r = terminationExit()) {
        return *r;
      }
      auto& m = InstanceManager::singleton();

      if (cl.instance()) {
        m.overrideInstance(*cl.instance());
      }

      if (cl.profile()) {
        m.overrideProfile(*cl.profile());
      }

      // set up plugins, OrganizerCore, etc.
      {
        const auto r = app.setup(multiProcess, pick);
        pick         = false;

        if (auto terminationResult = terminationExit()) {
          return *terminationResult;
        }

        if (r == RestartExitCode || r == ReselectExitCode) {
          app.resetForRestart();
          cl.clear();

          if (r == ReselectExitCode) {
            pick = true;
          }

          continue;
        } else if (r != 0) {
          return r;
        }
      }

      if (auto r = cl.runPostOrganizer(app.core())) {
        if (!app.finishCommandLineSetup()) {
          return 1;
        }
        return *r;
      }
      if (auto r = terminationExit()) {
        return *r;
      }

      NxmHandlerLinux nxmHandler;
      if (!nxmHandler.startListener()) {
        log::warn("nxm listener could not be started");
      } else {
        QObject::connect(
            &nxmHandler, &NxmHandlerLinux::nxmReceived, &app.core(),
            [&](const NxmLink& link) {
              app.core().downloadRequestedNXM(
                  QString("nxm://%1/mods/%2/files/%3?key=%4&expires=%5&user_id=%6")
                      .arg(link.game_domain)
                      .arg(link.mod_id)
                      .arg(link.file_id)
                      .arg(link.key)
                      .arg(link.expires)
                      .arg(link.user_id));
            });

        QObject::connect(&nxmHandler, &NxmHandlerLinux::directDownloadReceived,
                         &app.core(), [&](const QString& url, const QString&) {
                           QMetaObject::invokeMethod(
                               &app.core(),
                               [&app, url] {
                                 app.core().downloadManager()->startDownloadURLs(
                                     QStringList{url});
                               },
                               Qt::QueuedConnection);
                         });
      }

      if (auto terminationResult = terminationExit()) {
        return *terminationResult;
      }

      const auto r = app.run(multiProcess);

      if (auto terminationResult = terminationExit()) {
        return *terminationResult;
      }

      if (r == RestartExitCode) {
        app.resetForRestart();
        cl.clear();
        continue;
      }

      return r;
    } catch (const std::exception& e) {
      reportError(e.what());
      return 1;
    }
  }
}

static void linuxCrashHandler(int sig)
{
  static std::atomic<int> entered{0};
  int expected = 0;
  if (!entered.compare_exchange_strong(expected, sig,
                                       std::memory_order_relaxed)) {
    _exit(128 + sig);
  }

  constexpr char message[] =
      "\n=== Fluorine fatal signal; terminating for external crash capture ===\n";
  const ssize_t ignored = ::write(STDERR_FILENO, message, sizeof(message) - 1);
  (void)ignored;

  struct sigaction action{};
  action.sa_handler = SIG_DFL;
  sigemptyset(&action.sa_mask);
  (void)::sigaction(sig, &action, nullptr);
  sigset_t unblocked;
  sigemptyset(&unblocked);
  sigaddset(&unblocked, sig);
  (void)::sigprocmask(SIG_UNBLOCK, &unblocked, nullptr);
  (void)::kill(::getpid(), sig);
  _exit(128 + sig);
}

// std::terminate fires for uncaught C++ exceptions (including bad_alloc
// thrown out of a noexcept boundary). Keep recursion bounded and let SIGABRT's
// fatal handler restore the default disposition for external crash capture.
static void linuxTerminateHandler() noexcept
{
  static std::atomic<bool> entered{false};
  if (entered.exchange(true)) {
    // Recursion (terminate during our own cleanup) — die immediately.
    _exit(134);
  }

  std::abort();
}

void setExceptionHandlers()
{
  // sigaction with SA_RESETHAND + no SA_RESTART; preferred over signal()
  // since signal() semantics vary. Don't block other fatal signals while
  // handling one — we want a second crash to terminate immediately rather
  // than recurse.
  struct sigaction sa{};
  sa.sa_handler = linuxCrashHandler;
  sa.sa_flags   = SA_RESETHAND;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGSEGV, &sa, nullptr);
  sigaction(SIGABRT, &sa, nullptr);
  sigaction(SIGFPE, &sa, nullptr);
  sigaction(SIGBUS, &sa, nullptr);

  std::set_terminate(linuxTerminateHandler);
}
