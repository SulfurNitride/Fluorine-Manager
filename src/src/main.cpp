#include "commandline.h"
#include "desktopnotifier.h"
#include "desktopportalpolicy.h"
#include "env.h"
#include "fluorinepaths.h"
#include "fluorine_build_info.h"
#include "headless.h"
#include "headlessstop.h"
#include "instancemanager.h"
#include "launchenvironment.h"
#include "loglist.h"
#include "memorydiagnostics.h"
#include "moapplication.h"
#include "multiprocess.h"
#include "nxmhandler_linux.h"
#include "organizercore.h"
#include "shared/util.h"
#include "thread_utils.h"

#include <log.h>
#include <report.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcessEnvironment>
#include <QString>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <execinfo.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace MOBase;

int run(int argc, char* argv[]);
static void linuxTermHandler(int sig);
static void setTermHandlers();

namespace
{
void selectNativeDialogPlatformTheme()
{
  constexpr auto PlatformTheme         = "QT_QPA_PLATFORMTHEME";
  constexpr auto OriginalPlatformTheme =
      "FLUORINE_ORIG_QT_QPA_PLATFORMTHEME";

  // Direct distro/package entry points may not use the generated launcher,
  // which sets this fallback itself. Preserve a caller's explicit platform
  // theme, and only select the bundled portal plugin when it is available.
  const QString executablePath =
      QFileInfo(QStringLiteral("/proc/self/exe")).symLinkTarget();
  if (executablePath.isEmpty()) {
    return;
  }
  const QString portalPlugin =
      QDir(QFileInfo(executablePath).absolutePath())
          .filePath("qt6plugins/platformthemes/libqxdgdesktopportal.so");
  if (!env::desktopportal::shouldSelectBundledPortalTheme(
          qgetenv(PlatformTheme), QFile::exists(portalPlugin))) {
    return;
  }

  // QApplication reads the theme during construction. Remember the caller's
  // value so MOApplication can restore it immediately after Qt initializes,
  // keeping this UI-only override out of launched child processes.
  if (!qEnvironmentVariableIsSet(OriginalPlatformTheme)) {
    qputenv(OriginalPlatformTheme, qgetenv(PlatformTheme));
  }
  qputenv(PlatformTheme, QByteArrayLiteral("xdgdesktopportal"));
}
}

int main(int argc, char* argv[])
{
  const int r = run(argc, argv);
  std::cout << "mod organizer done\n";
  return r;
}

int run(int argc, char* argv[])
{
  if (argc >= 3 && QString(argv[1]) == "nxm-handle") {
    QString nxmUrl = QString::fromLocal8Bit(argv[2]);
    if (nxmUrl == "nxm-handle" && argc >= 4) {
      nxmUrl = QString::fromLocal8Bit(argv[3]);
    }
    if (NxmHandlerLinux::sendToSocket(nxmUrl)) {
      return 0;
    }

    // Older registered handler scripts only invoke `nxm-handle`; they do not
    // contain the newer shell fallback that starts Fluorine when its socket is
    // absent. Replace this lightweight handler process with a normal Fluorine
    // invocation so existing registrations also open the last-used instance
    // and process the URL. /proc/self/exe avoids relying on argv[0] or PATH.
    const QByteArray encodedUrl = nxmUrl.toLocal8Bit();
    ::execl("/proc/self/exe", argv[0], encodedUrl.constData(),
            static_cast<char*>(nullptr));
    std::perror("failed to relaunch Fluorine for NXM link");
    return 1;
  }

  MOShared::SetThisThreadName("main");
  setExceptionHandlers();
  setTermHandlers();

  cl::CommandLine cl;

  const auto cmdLine = commandLineFromUtf8Arguments(argc, argv);
  if (auto r = cl.process(cmdLine)) {
    return *r;
  }

  if (cl.headless()) {
    // one bus per call keeps the lambda copyable for std::function; failures
    // are rare and one-off
    headless::activate([](const QString& summary, const QString& body) {
      auto bus = notify::sessionBus(QProcessEnvironment::systemEnvironment());
      notify::send(*bus, summary, body);
    });

    headless::installStopHandlers(linuxTermHandler);
  }

  fluorineMigrateDataDir();

  initLogging();
  MemoryDiagnostics::snapshot("startup.logging_ready");

  // must be after logging
  TimeThis tt("main() multiprocess");

  selectNativeDialogPlatformTheme();
  MOApplication app(argc, argv);
  QCoreApplication::setApplicationVersion(QStringLiteral(FLUORINE_DISPLAY_VERSION));
  MemoryDiagnostics::snapshot("startup.application_constructed");

  if (auto r = cl.runPostApplication(app)) {
    return *r;
  }

  // a second process would otherwise become a secondary with its own mount
  MOMultiProcess multiProcess(cl.multiple() && !cl.headless());

  if (multiProcess.ephemeral()) {
    // not the primary process

    if (cl.headless()) {
      // forwarding would remap the VFS under whatever is already running
      if (multiProcess.primaryIsHeadless()) {
        headless::fail(QObject::tr(
            "A game is running from Steam through Fluorine. Quit the game first."));
      } else {
        headless::fail(QObject::tr("Fluorine is open. Close it and press Play again."));
      }

      return headless::LaunchFailedExitCode;
    }

    if (multiProcess.primaryIsHeadless()) {
      QMessageBox::information(
          nullptr, QObject::tr("Mod Organizer"),
          QObject::tr(
              "A game is running from Steam through Fluorine. Quit the game first."));

      return 1;
    }

    if (cl.forwardToPrimary(multiProcess)) {
      // there's something on the command line that could be forwarded — exit
      return 0;
    }

    QMessageBox::information(
        nullptr, QObject::tr("Mod Organizer"),
        QObject::tr("An instance of Mod Organizer is already running"));

    return 1;
  }

  if (cl.headless()) {
    multiProcess.setHeadlessPrimary(true);
  }

  if (auto r = cl.runPostMultiProcess(multiProcess)) {
    return *r;
  }

  tt.stop();

  // stuff that's done only once, even if MO restarts in the loop below
  app.firstTimeSetup(multiProcess);

  // force the "Select instance" dialog on startup, only for first loop or when
  // the current instance cannot be used
  bool pick = cl.pick();

  // MO runs in a loop because it can be restarted in several ways, such as
  // when switching instances or changing some settings
  for (;;) {
    try {
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
        MemoryDiagnostics::snapshot("startup.application_setup_complete");
        pick         = false;

        if (cl.headless() && (r == RestartExitCode || r == ReselectExitCode)) {
          if (!headless::firstFailure()) {
            headless::fail(QObject::tr("Fluorine needed to restart or pick another "
                                       "instance, which it can't do from Steam."));
          }

          return headless::LaunchFailedExitCode;
        }

        if (r == RestartExitCode || r == ReselectExitCode) {
          app.resetForRestart();
          cl.clear();

          if (r == ReselectExitCode) {
            pick = true;
          }

          continue;
        } else if (r != 0) {
          if (cl.headless()) {
            if (!headless::firstFailure()) {
              headless::fail(QObject::tr("Couldn't load instance '%1'.")
                                 .arg(cl.instance().value_or(QString())));
            }

            return headless::LaunchFailedExitCode;
          }

          return r;
        }
      }

      if (auto r = cl.runPostOrganizer(app.core())) {
        return *r;
      }

      NxmHandlerLinux nxmHandler;
      if (!nxmHandler.startListener()) {
        log::warn("nxm listener could not be started");
      } else {
        QObject::connect(
            &nxmHandler, &NxmHandlerLinux::nxmReceived, &app,
            [&](const NxmLink& link) {
              app.routeNxmMessage(
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

      const auto r = app.run(multiProcess);

      if (r == RestartExitCode) {
        app.resetForRestart();
        cl.clear();
        continue;
      }

      return r;
    } catch (const std::exception& e) {
      reportError(e.what());
      return cl.headless() ? headless::LaunchFailedExitCode : 1;
    }
  }
}

// Defined in fuseconnector.cpp — returns the current FUSE mount point (or
// nullptr if nothing is mounted). The backing buffer is a plain char[] so
// reading it in a signal handler is async-signal-safe.
extern const char* getFuseMountPointForCrashCleanup();

// Async-signal-safe-ish: try direct umount2(MNT_DETACH) first (single
// syscall, no fork/malloc), then fall back to fusermount3 -uz. Lazy detach
// is what actually keeps us out of D-state — a normal unmount blocks
// indefinitely if the mount is busy, which is exactly when we crash.
static void emergencyFuseUnmount() noexcept
{
  const char* mp = getFuseMountPointForCrashCleanup();
  if (mp == nullptr || mp[0] == '\0') {
    return;
  }

  // Step 1: direct lazy unmount. Works without the fusermount3 setuid helper
  // if we own the mount (we do — it's our session). Single syscall, no heap.
  if (umount2(mp, MNT_DETACH) == 0) {
    return;
  }

  // Step 2: fall back to fusermount3 -uz. Don't waitpid() on the grandchild —
  // a hung child would wedge the crash handler. Detach with double-fork so
  // PID 1 reaps it.
  const pid_t child = fork();
  if (child == 0) {
    const pid_t grand = fork();
    if (grand == 0) {
      execlp("fusermount3", "fusermount3", "-uz", mp, nullptr);
      execlp("fusermount", "fusermount", "-uz", mp, nullptr);
      _exit(1);
    }
    _exit(0);
  } else if (child > 0) {
    int status = 0;
    waitpid(child, &status, 0);
  }
}

static void linuxCrashHandler(int sig)
{
  // Reset to default immediately to avoid recursion
  signal(sig, SIG_DFL);

  // Best-effort FUSE cleanup before we die
  emergencyFuseUnmount();

  const char* sigName = (sig == SIGSEGV) ? "SIGSEGV"
                        : (sig == SIGABRT) ? "SIGABRT"
                        : (sig == SIGFPE)  ? "SIGFPE"
                                           : "UNKNOWN";

  fprintf(stderr, "\n=== MO2 CRASH: signal %s (%d) ===\n", sigName, sig);

  void* frames[64];
  int const count = backtrace(frames, 64);
  fprintf(stderr, "Backtrace (%d frames):\n", count);
  backtrace_symbols_fd(frames, count, STDERR_FILENO);
  fprintf(stderr, "=== END BACKTRACE ===\n");

  // Force-terminate the whole process group. raise(sig) only delivers to the
  // current thread and depends on the signal not being masked process-wide;
  // libfuse blocks signals on its workers, so raise() can hang. _exit()
  // always terminates immediately and reaps every thread.
  _exit(128 + sig);
}

static void linuxTermHandler(int sig)
{
  // Graceful shutdown: unmount FUSE and exit cleanly.
  signal(sig, SIG_DFL);
  emergencyFuseUnmount();
  _exit(128 + sig);
}

// std::terminate fires for uncaught C++ exceptions (including bad_alloc
// thrown out of a noexcept boundary). The default handler aborts via
// SIGABRT, but if SIGABRT is masked on the throwing thread the process can
// hang. Run our FUSE cleanup first, then call abort() ourselves.
static void linuxTerminateHandler() noexcept
{
  static std::atomic<bool> entered{false};
  if (entered.exchange(true)) {
    // Recursion (terminate during our own cleanup) — die immediately.
    _exit(134);
  }

  emergencyFuseUnmount();

  fprintf(stderr, "\n=== MO2 std::terminate ===\n");
  void* frames[64];
  int const count = backtrace(frames, 64);
  fprintf(stderr, "Backtrace (%d frames):\n", count);
  backtrace_symbols_fd(frames, count, STDERR_FILENO);
  fprintf(stderr, "=== END BACKTRACE ===\n");

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

// Kept apart from setExceptionHandlers(): startSafeThread runs that in every
// thread, and sigaction is process-wide, so installing these there would undo
// the headless stop handlers.
static void setTermHandlers()
{
  struct sigaction term{};
  term.sa_handler = linuxTermHandler;
  term.sa_flags   = SA_RESETHAND;
  sigemptyset(&term.sa_mask);
  sigaction(SIGTERM, &term, nullptr);
  sigaction(SIGINT, &term, nullptr);
}
