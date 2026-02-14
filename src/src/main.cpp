#include "commandline.h"
#include "env.h"
#include "fluorinepaths.h"
#include "instancemanager.h"
#include "loglist.h"
#include "moapplication.h"
#include "multiprocess.h"
#include "nxmhandler_linux.h"
#include "organizercore.h"
#include "shared/util.h"
#include "thread_utils.h"
#include <log.h>
#include <nak_ffi.h>
#include <report.h>
#include <QString>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <execinfo.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace MOBase;

#ifdef _WIN32
thread_local LPTOP_LEVEL_EXCEPTION_FILTER g_prevExceptionFilter = nullptr;
thread_local std::terminate_handler g_prevTerminateHandler      = nullptr;
#endif

int run(int argc, char* argv[]);

int main(int argc, char* argv[])
{
  const int r = run(argc, argv);
  std::cout << "mod organizer done\n";
  return r;
}

int run(int argc, char* argv[])
{
#ifndef _WIN32
  if (argc >= 3 && QString(argv[1]) == "nxm-handle") {
    QString nxmUrl = QString::fromLocal8Bit(argv[2]);
    if (nxmUrl == "nxm-handle" && argc >= 4) {
      nxmUrl = QString::fromLocal8Bit(argv[3]);
    }
    return NxmHandlerLinux::sendToSocket(nxmUrl) ? 0 : 1;
  }
#endif

  MOShared::SetThisThreadName("main");
  setExceptionHandlers();

  cl::CommandLine cl;
#ifdef _WIN32
  if (auto r = cl.process(GetCommandLineW())) {
    return *r;
  }
#else
  // Build a wstring from argv for the CommandLine parser.
  // Each argument must be quoted so that po::split_unix() round-trips
  // correctly when paths contain spaces.
  std::wstring cmdLine;
  for (int i = 0; i < argc; ++i) {
    if (i > 0) cmdLine += L' ';
    std::string arg(argv[i]);
    std::wstring warg(arg.begin(), arg.end());
    if (warg.find(L' ') != std::wstring::npos) {
      cmdLine += L'"';
      cmdLine += warg;
      cmdLine += L'"';
    } else {
      cmdLine += warg;
    }
  }
  if (auto r = cl.process(cmdLine)) {
    return *r;
  }
#endif

  fluorineMigrateDataDir();

  initLogging();

  // Route NaK (Rust) log messages through MOBase::log
  nak_init_logging([](const char* level, const char* message) {
    if (!message || !*message) return;
    if (!level || !*level) {
      log::info("[nak] {}", message);
      return;
    }
    // Map NaK levels to MOBase log levels
    if (std::strcmp(level, "error") == 0) {
      log::error("[nak] {}", message);
    } else if (std::strcmp(level, "warning") == 0) {
      log::warn("[nak] {}", message);
    } else {
      log::info("[nak] {}", message);
    }
  });

  // must be after logging
  TimeThis tt("main() multiprocess");

  MOApplication app(argc, argv);

  // check if the command line wants to run something right now
  if (auto r = cl.runPostApplication(app)) {
    return *r;
  }

  // check if there's another process running
  MOMultiProcess multiProcess(cl.multiple());

  if (multiProcess.ephemeral()) {
    // this is not the primary process

    if (cl.forwardToPrimary(multiProcess)) {
      // but there's something on the command line that could be forwarded to
      // it, so just exit
      return 0;
    }

    QMessageBox::information(
        nullptr, QObject::tr("Mod Organizer"),
        QObject::tr("An instance of Mod Organizer is already running"));

    return 1;
  }

  // check if the command line wants to run something right now
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
        pick         = false;

        if (r == RestartExitCode || r == ReselectExitCode) {
          // resets things when MO is "restarted"
          app.resetForRestart();

          // don't reprocess command line
          cl.clear();

          if (r == ReselectExitCode) {
            pick = true;
          }

          continue;
        } else if (r != 0) {
          // something failed, quit
          return r;
        }
      }

      // check if the command line wants to run something right now
      if (auto r = cl.runPostOrganizer(app.core())) {
        return *r;
      }

#ifndef _WIN32
      NxmHandlerLinux nxmHandler;
      if (!nxmHandler.startListener()) {
        log::warn("nxm listener could not be started");
      } else {
        QObject::connect(&nxmHandler, &NxmHandlerLinux::nxmReceived, &app.core(),
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
      }
#endif

      // run the main window
      const auto r = app.run(multiProcess);

      if (r == RestartExitCode) {
        // resets things when MO is "restarted"
        app.resetForRestart();

        // don't reprocess command line
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

#ifdef _WIN32
LONG WINAPI onUnhandledException(_EXCEPTION_POINTERS* ptrs)
{
  const auto path = OrganizerCore::getGlobalCoreDumpPath();
  const auto type = OrganizerCore::getGlobalCoreDumpType();

  const auto r = env::coredump(path.empty() ? nullptr : path.c_str(), type);

  if (r) {
    log::error("ModOrganizer has crashed, core dump created.");
  } else {
    log::error("ModOrganizer has crashed, core dump failed");
  }

  // g_prevExceptionFilter somehow sometimes point to this function, making this
  // recurse and create hundreds of core dump, not sure why
  if (g_prevExceptionFilter && ptrs && g_prevExceptionFilter != onUnhandledException)
    return g_prevExceptionFilter(ptrs);
  else
    return EXCEPTION_CONTINUE_SEARCH;
}

void onTerminate() noexcept
{
  __try {
    // force an exception to get a valid stack trace for this thread
    *(int*)0 = 42;
  } __except (onUnhandledException(GetExceptionInformation()),
              EXCEPTION_EXECUTE_HANDLER) {
  }

  if (g_prevTerminateHandler) {
    g_prevTerminateHandler();
  } else {
    std::abort();
  }
}

void setExceptionHandlers()
{
  if (g_prevExceptionFilter) {
    // already called
    return;
  }

  g_prevExceptionFilter  = SetUnhandledExceptionFilter(onUnhandledException);
  g_prevTerminateHandler = std::set_terminate(onTerminate);
}

#else  // Linux

// Defined in fuseconnector.cpp — returns the current FUSE mount point
// (or nullptr if nothing is mounted).  The backing buffer is a plain
// char[] so reading it in a signal handler is async-signal-safe.
extern const char* getFuseMountPointForCrashCleanup();

// Attempt to unmount FUSE from a signal handler context.
// fork()+exec() is async-signal-safe on Linux.
static void emergencyFuseUnmount()
{
  const char* mp = getFuseMountPointForCrashCleanup();
  if (mp == nullptr) {
    return;
  }

  const pid_t child = fork();
  if (child == 0) {
    // Child — try lazy unmount so it always succeeds even if busy.
    execlp("fusermount3", "fusermount3", "-uz", mp, nullptr);
    execlp("fusermount", "fusermount", "-uz", mp, nullptr);
    _exit(1);
  } else if (child > 0) {
    // Parent — wait briefly for the child to finish.
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

  // Print backtrace
  void* frames[64];
  int count = backtrace(frames, 64);
  fprintf(stderr, "Backtrace (%d frames):\n", count);
  backtrace_symbols_fd(frames, count, STDERR_FILENO);
  fprintf(stderr, "=== END BACKTRACE ===\n");

  // Re-raise for core dump
  raise(sig);
}

static void linuxTermHandler(int sig)
{
  // Graceful shutdown: unmount FUSE and exit cleanly.
  signal(sig, SIG_DFL);
  emergencyFuseUnmount();
  raise(sig);
}

void setExceptionHandlers()
{
  signal(SIGSEGV, linuxCrashHandler);
  signal(SIGABRT, linuxCrashHandler);
  signal(SIGFPE, linuxCrashHandler);
  signal(SIGTERM, linuxTermHandler);
  signal(SIGINT, linuxTermHandler);
}

#endif
