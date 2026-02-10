#include "commandline.h"
#include "env.h"
#include "instancemanager.h"
#include "loglist.h"
#include "moapplication.h"
#include "multiprocess.h"
#include "nxmhandler_linux.h"
#include "organizercore.h"
#include "shared/util.h"
#include "thread_utils.h"
#include <log.h>
#include <report.h>
#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QString>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <cstdlib>
#include <execinfo.h>
#endif

using namespace MOBase;

#ifdef _WIN32
thread_local LPTOP_LEVEL_EXCEPTION_FILTER g_prevExceptionFilter = nullptr;
thread_local std::terminate_handler g_prevTerminateHandler      = nullptr;
#endif

int run(int argc, char* argv[]);

namespace
{
void configureQtPluginPathsEarly(const char* argv0)
{
#ifdef _WIN32
  Q_UNUSED(argv0);
#else
  QString appDir;
  if (argv0 != nullptr && *argv0 != '\0') {
    QFileInfo fi(QString::fromLocal8Bit(argv0));
    if (fi.isRelative()) {
      fi.setFile(QDir::current().absoluteFilePath(fi.filePath()));
    }
    appDir = fi.absolutePath();
  }

  QStringList pluginCandidates;
  const QString envPluginPath = qEnvironmentVariable("QT_PLUGIN_PATH");
  if (!envPluginPath.isEmpty()) {
    for (const auto& path : envPluginPath.split(':', Qt::SkipEmptyParts)) {
      pluginCandidates.append(QDir::cleanPath(path));
    }
  }

  if (!appDir.isEmpty()) {
    pluginCandidates << QDir::cleanPath(appDir + "/plugins");
    pluginCandidates << QDir::cleanPath(appDir + "/qt6/plugins");
  }

  pluginCandidates << QLibraryInfo::path(QLibraryInfo::PluginsPath);
  pluginCandidates << "/usr/lib/qt6/plugins";
  pluginCandidates << "/usr/lib64/qt6/plugins";

  QStringList existingPluginPaths;
  for (const auto& candidate : pluginCandidates) {
    if (!candidate.isEmpty() && QDir(candidate).exists() &&
        !existingPluginPaths.contains(candidate)) {
      existingPluginPaths.append(candidate);
    }
  }

  if (!existingPluginPaths.isEmpty() && !qEnvironmentVariableIsSet("QT_PLUGIN_PATH")) {
    qputenv("QT_PLUGIN_PATH", existingPluginPaths.join(':').toUtf8());
  }

  if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM_PLUGIN_PATH")) {
    for (const auto& pluginPath : existingPluginPaths) {
      const QString platformsPath = QDir(pluginPath).filePath("platforms");
      if (QDir(platformsPath).exists()) {
        qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", platformsPath.toUtf8());
        break;
      }
    }
  }
#endif
}
}  // namespace

int main(int argc, char* argv[])
{
  const int r = run(argc, argv);
  std::cout << "mod organizer done\n";
  return r;
}

int run(int argc, char* argv[])
{
  configureQtPluginPathsEarly((argc > 0) ? argv[0] : nullptr);

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
  // Build a wstring from argv for the CommandLine parser
  std::wstring cmdLine;
  for (int i = 0; i < argc; ++i) {
    if (i > 0) cmdLine += L' ';
    std::string arg(argv[i]);
    cmdLine += std::wstring(arg.begin(), arg.end());
  }
  if (auto r = cl.process(cmdLine)) {
    return *r;
  }
#endif

  initLogging();

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
                               QString("nxm://%1/mods/%2/files/%3?key=%4&expires=%5")
                                   .arg(link.game_domain)
                                   .arg(link.mod_id)
                                   .arg(link.file_id)
                                   .arg(link.key)
                                   .arg(link.expires));
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

static void linuxSignalHandler(int sig)
{
  // Reset to default immediately to avoid recursion
  signal(sig, SIG_DFL);

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

void setExceptionHandlers()
{
  signal(SIGSEGV, linuxSignalHandler);
  signal(SIGFPE, linuxSignalHandler);
}

#endif
