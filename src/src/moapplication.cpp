/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "moapplication.h"
#include "applicationappearance.h"
#include "commandline.h"
#include "commandlinearguments.h"
#include "instancemanager.h"
#include "loglist.h"
#include "mainwindow.h"
#include "messagedialog.h"
#include "multiprocess.h"
#include "nexusinterface.h"
#include "nxmaccessmanager.h"
#include "nxmrequest.h"
#include "organizercore.h"
#include "sanitychecks.h"
#include "settings.h"
#include "settingsmigration.h"
#include "fluorineconfig.h"
#include "fluorinepaths.h"
#include "fuseconnector.h"
#include "wineprefix.h"

#include <cerrno>
#include <filesystem>
#include <sys/stat.h>
#include "shared/appconfig.h"
#include "shared/util.h"
#include "thread_utils.h"
#include "tutorialmanager.h"
#include <QDebug>
#include <QDesktopServices>
#include <QEvent>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetrics>
#include <QMessageBox>
#include <QSslSocket>
#include <QStringList>
#include <QStyleFactory>
#include <QTimer>
#include <QUrl>
#include <iplugingame.h>
#include <log.h>
#include <report.h>
#include <utility.h>

using namespace MOBase;
using namespace MOShared;

// Forward QDesktopServices::openUrl() (used by QLabel::setOpenExternalLinks
// and QTextBrowser auto-open) through shell::Open, which scrubs our bundled
// LD_LIBRARY_PATH / QT_PLUGIN_PATH before forking xdg-open. Without this the
// child inherits our runtime env and silently fails to launch a browser.
class UrlHandlerProxy : public QObject
{
  Q_OBJECT
public:
  using QObject::QObject;
public slots:
  void open(const QUrl& url) { MOBase::shell::Open(url); }
};

void addLinuxLibrariesToPath()
{
  const auto libsPath =
      QDir::toNativeSeparators(QCoreApplication::applicationDirPath() + "/lib");

  QCoreApplication::setLibraryPaths(QStringList(libsPath) +
                                    QCoreApplication::libraryPaths());

  env::prependToPath(libsPath);
}

void configureApplicationFont()
{
  // Measured legacy packaged baseline. Keep this in point units so Qt
  // continues to scale it for each screen's DPI.
  constexpr qreal PackagedUiPointSize = 9.0;

  const QDir fontDir(QCoreApplication::applicationDirPath() + "/fonts");
  struct ApplicationFont
  {
    const char* fileName;
    bool regular;
  };
  const ApplicationFont applicationFonts[]{
      {"DejaVuSans.ttf", true},
      {"DejaVuSans-Bold.ttf", false},
  };

  QString uiFamily;
  for (const auto& font : applicationFonts) {
    const QString path = fontDir.filePath(QString::fromLatin1(font.fileName));
    const int id = QFontDatabase::addApplicationFont(path);
    if (id < 0) {
      log::warn("failed to load application font '{}'", path);
      continue;
    }

    const QStringList families = QFontDatabase::applicationFontFamilies(id);
    if (!families.contains(QStringLiteral("DejaVu Sans"))) {
      log::warn("application font '{}' reports unexpected families [{}]", path,
                families.join(", "));
      continue;
    }
    log::debug("loaded application font '{}'", path);
    if (font.regular) {
      uiFamily = QStringLiteral("DejaVu Sans");
    }
  }

  if (!uiFamily.isEmpty()) {
    QFont font = QApplication::font();
    font.setFamily(uiFamily);
    font.setPointSizeF(PackagedUiPointSize);
    QApplication::setFont(font);
    log::debug("configured packaged application font '{}' at {} pt", uiFamily,
               PackagedUiPointSize);
  } else {
    const QFont hostFont = QApplication::font();
    log::warn(
        "DejaVu Sans application font is unavailable; preserving host UI font "
        "'{}' (pointSize={}, pixelSize={}).",
        hostFont.family(), hostFont.pointSizeF(), hostFont.pixelSize());
  }

}

#ifdef MO2_WEBENGINE
void configureQtWebEngineProcessPath()
{
  const QString appDir = QCoreApplication::applicationDirPath();

  if (qEnvironmentVariableIsSet("QTWEBENGINEPROCESS_PATH")) {
    // keep user override
  } else {
    const QString candidates[] = {
        appDir + "/QtWebEngineProcess",
        appDir + "/../libexec/QtWebEngineProcess",
        appDir + "/../lib/QtWebEngineProcess",
        "/usr/lib/qt6/QtWebEngineProcess",
        "/usr/lib/qt6/libexec/QtWebEngineProcess",
        "/usr/lib64/qt6/QtWebEngineProcess",
        "/usr/lib64/qt6/libexec/QtWebEngineProcess",
    };

    for (const auto& candidate : candidates) {
      if (QFileInfo::exists(candidate)) {
        qputenv("QTWEBENGINEPROCESS_PATH", candidate.toUtf8());
        break;
      }
    }

    if (!qEnvironmentVariableIsSet("QTWEBENGINEPROCESS_PATH")) {
      const QString fromPath = QStandardPaths::findExecutable("QtWebEngineProcess");
      if (!fromPath.isEmpty()) {
        qputenv("QTWEBENGINEPROCESS_PATH", fromPath.toUtf8());
      }
    }
  }

  if (!qEnvironmentVariableIsSet("QTWEBENGINE_RESOURCES_PATH")) {
    const QString resourceDirs[] = {
        appDir + "/resources",
        appDir + "/../resources",
        "/usr/share/qt6/resources",
        "/usr/lib/qt6/resources",
        "/usr/lib64/qt6/resources",
    };
    for (const auto& dir : resourceDirs) {
      if (QFileInfo::exists(dir + "/qtwebengine_resources.pak")) {
        qputenv("QTWEBENGINE_RESOURCES_PATH", dir.toUtf8());
        break;
      }
    }
  }

  if (!qEnvironmentVariableIsSet("QTWEBENGINE_LOCALES_PATH")) {
    const QString localeDirs[] = {
        appDir + "/translations/qtwebengine_locales",
        appDir + "/../translations/qtwebengine_locales",
        "/usr/share/qt6/translations/qtwebengine_locales",
        "/usr/lib/qt6/translations/qtwebengine_locales",
        "/usr/lib64/qt6/translations/qtwebengine_locales",
    };
    for (const auto& dir : localeDirs) {
      if (QFileInfo::exists(dir)) {
        qputenv("QTWEBENGINE_LOCALES_PATH", dir.toUtf8());
        break;
      }
    }
  }
}
#endif

MOApplication::MOApplication(int& argc, char** argv) : QApplication(argc, argv)
{
  TimeThis const tt("MOApplication()");
  configureApplicationFont();

  // Ensure the app name is always "ModOrganizer" regardless of the binary
  // filename (settings/profile lookups key off this).
  setApplicationName("ModOrganizer");
  setDesktopFileName(QStringLiteral("com.fluorine.manager"));
  setWindowIcon(QIcon(":/MO/gui/app_icon"));

  qputenv("QML_DISABLE_DISK_CACHE", "true");

  connect(&m_styleWatcher, &QFileSystemWatcher::fileChanged, [this](auto&& file) {
    log::debug("style file '{}' changed, reloading", file);
    if (m_requestedAppearance.has_value()) {
      applyAppearance(*m_requestedAppearance, true);
    }
  });

  // Pick a Qt style available on this system. "Fusion" is bundled with Qt and
  // looks identical across distros, so prefer it; fall back to whatever the
  // QStyleFactory advertises first.
  const auto availableStyles = QStyleFactory::keys();
  if (availableStyles.contains("Fusion")) {
    m_defaultStyle = "Fusion";
  } else if (!availableStyles.isEmpty()) {
    m_defaultStyle = availableStyles.first();
  }
  m_appearance = std::make_unique<ApplicationAppearance::Controller>(
      *this, applicationDirPath(), m_defaultStyle, QApplication::font());
  resetAppearance();
  addLinuxLibrariesToPath();
#ifdef MO2_WEBENGINE
  configureQtWebEngineProcessPath();
#endif

  // When MO2 is launched by the nxm handler from a browser, CWD is whatever
  // the browser inherited (often /, $HOME, or the user's Desktop). Reset it
  // to the application directory so that any code path relying on CWD
  // (Qt resource lookup, relative QFile paths, QtWebEngine sandbox helper)
  // behaves the same as a normal launch. Upstream PR #2379.
  QDir::setCurrent(QCoreApplication::applicationDirPath());

  auto* urlProxy = new UrlHandlerProxy(this);
  for (const auto& scheme :
       {QStringLiteral("http"), QStringLiteral("https"),
        QStringLiteral("file"), QStringLiteral("mailto")}) {
    QDesktopServices::setUrlHandler(scheme, urlProxy, "open");
  }
}

MOApplication::~MOApplication()
{
  const qsizetype discarded = m_externalMessages.stop();
  if (discarded > 0) {
    log::warn("discarding {} external message(s) during application shutdown",
              discarded);
  }
}

OrganizerCore& MOApplication::core()
{
  return *m_core;
}

bool MOApplication::finishCommandLineSetup()
{
  if (!m_settings) {
    return false;
  }

  const auto status = m_settings->finishUpdatesWithoutMigration();
  if (status != QSettings::NoError) {
    log::error("failed to durably finish command-line setup for '{}': status {}",
               m_settings->filename(), static_cast<int>(status));
    return false;
  }
  return true;
}

void MOApplication::firstTimeSetup(MOMultiProcess& multiProcess)
{
  // MOMultiProcess receives on the GUI thread. Capture the message before its
  // acknowledgement is sent so restart cleanup cannot remove the only queued
  // Qt delivery event and silently lose accepted work.
  multiProcess.setMessageHandler(
      [this](const QString& message) { return enqueueExternalMessage(message); });
}

int MOApplication::setup(MOMultiProcess& multiProcess, bool forceSelect)
{
  TimeThis tt("MOApplication setup()");
  pauseExternalMessages();

  // makes plugin data path available to plugins, see
  // IOrganizer::getPluginDataPath()
  MOBase::details::setPluginDataPath(OrganizerCore::pluginDataPath());

  // Instance selection is application-level UI: it must not inherit or preview
  // a modlist stylesheet. The selected instance appearance is applied only
  // after the selector closes and its Settings transaction is admitted below.
  resetAppearance();

  // figuring out the current instance
  m_instance = getCurrentInstance(forceSelect);
  if (!m_instance) {
    return 1;
  }

  // first time the data path is available, set the global property and log
  // directory, then log a bunch of debug stuff
  const QString dataPath = m_instance->directory();
  setProperty("dataPath", dataPath);
  setProperty("fluorinePortableInstance", m_instance->isPortable());

  if (!setLogDirectory(dataPath)) {
    reportError(tr("Failed to create log folder."));
    InstanceManager::singleton().clearCurrentInstance();
    return 1;
  }

  log::debug("command line: '{}'", QCoreApplication::arguments().join(' '));

#ifndef GITID
#define GITID "unknown"
#endif
  log::info("starting Mod Organizer version {} revision {} in {}",
            createVersionInfo().string(), GITID, QCoreApplication::applicationDirPath());

  if (multiProcess.secondary()) {
    log::debug("another instance of MO is running but --multiple was given");
  }

  log::info("data path: {}", m_instance->directory());
  log::info("working directory: {}", QDir::currentPath());

  tt.start("MOApplication::doOneRun() settings");

  // deleting old files, only for the main instance
  if (!multiProcess.secondary()) {
    purgeOldFiles();
  }

  // loading settings
  m_settings.reset(new Settings(m_instance->iniPath(), true));
  if (m_settings->iniStatus() != QSettings::NoError) {
    reportError(tr("Cannot open the settings file for instance '%1'. Select "
                   "another instance or repair its INI file.")
                    .arg(m_instance->displayName()));
    InstanceManager::singleton().clearCurrentInstance();
    return ReselectExitCode;
  }
  if (!m_settings->beginUpdates()) {
    reportError(
        tr("Cannot acquire the settings migration transaction for instance "
           "'%1'. Another Fluorine Manager process may still be starting, or "
           "the settings file could not be synchronized. This process will "
           "exit without loading plugins or instance data; try again after "
           "the other process finishes.")
            .arg(m_instance->displayName()));
    return 1;
  }
  log::getDefault().setLevel(m_settings->diagnostics().logLevel());
  log::debug("using ini at '{}'", m_settings->filename());

  // Apply through the authoritative Settings facade after admission. This is
  // the first instance-appearance read and installs the live file watcher.
  if (!setStyleFile(m_settings->interface().styleName().value_or(""))) {
    m_settings->interface().setStyleName("");
  }

  OrganizerCore::setGlobalCoreDumpType(m_settings->diagnostics().coreDumpType());

  tt.start("MOApplication::doOneRun() log and checks");

  // logging and checking
  env::Environment const env;
  env.dump(*m_settings);
  m_settings->dump();
  sanity::checkEnvironment(env);

  m_modules = std::move(env::Environment::onModuleLoaded(qApp, [](auto&& m) {
    if (m.interesting()) {
      log::debug("loaded module {}", m.toString());
    }

    sanity::checkIncompatibleModule(m);
  }));

  auto sslBuildVersion = QSslSocket::sslLibraryBuildVersionString();
  auto sslVersion      = QSslSocket::sslLibraryVersionString();
  log::debug("SSL Build Version: {}, SSL Runtime Version {}", sslBuildVersion,
             sslVersion);

  // nexus interface
  tt.start("MOApplication::doOneRun() NexusInterface");
  log::debug("initializing nexus interface");
  m_nexus.reset(new NexusInterface(m_settings.get()));

  // organizer core
  tt.start("MOApplication::doOneRun() OrganizerCore");
  log::debug("initializing core");

  m_core.reset(new OrganizerCore(*m_settings));
  if (!m_core->bootstrap()) {
    reportError(tr("Failed to set up data paths."));
    InstanceManager::singleton().clearCurrentInstance();
    return 1;
  }

  // plugins
  tt.start("MOApplication::doOneRun() plugins");
  log::debug("initializing plugins");

  // Compatibility rules that must prevent IPlugin::init() cannot wait for
  // OrganizerCore::setManagedGame(), which happens after plugin discovery.
  // The instance setting is already available and is the authoritative hint
  // for this startup generation.
  const QString configuredGameName =
      m_settings->game().name().value_or(QString());
  m_plugins =
      std::make_unique<PluginContainer>(m_core.get(), configuredGameName);
  m_plugins->loadPlugins();
  log::debug("all plugins loaded");

  // instance
  log::debug("entering setupInstanceLoop...");
  if (auto r = setupInstanceLoop(*m_instance, *m_plugins, *m_settings)) {
    log::debug("setupInstanceLoop returned {}", *r);
    return *r;
  }
  log::debug("setupInstanceLoop done");

  // Repair can replace a stale configured game after the plugin generation
  // has already been admitted. If that changes whether pre-init rules apply,
  // persist the repaired identity and start a fresh generation rather than
  // keeping plugins excluded (or initialized) under the obsolete hint.
  const QString resolvedGameName = m_instance->gamePlugin()->gameName();
  if (!m_plugins->preInitCompatibilityMatches(resolvedGameName)) {
    log::info("managed game changed from '{}' to '{}' during instance repair; "
              "restarting plugin discovery with the resolved compatibility policy",
              configuredGameName, resolvedGameName);
    if (m_settings->finishUpdatesWithoutMigration() != QSettings::NoError) {
      log::error("failed to persist repaired game identity before plugin restart");
      return 1;
    }
    return RestartExitCode;
  }

  if (m_instance->isPortable()) {
    log::debug("this is a portable instance");
  }

  tt.start("MOApplication::doOneRun() OrganizerCore setup");

  // setting up organizer core
  m_core->setManagedGame(m_instance->gamePlugin());

  // Clean up stale FUSE mounts from a previous crash BEFORE any game
  // directory access (profile init, BSA invalidation, etc.).
  {
    const auto dataDir = m_instance->gamePlugin()->dataDirectory().absolutePath();
    log::info("checking for stale FUSE mount on '{}'", dataDir);
    FuseConnector::tryCleanupStaleMount(dataDir);
  }

  sanity::checkPaths(*m_instance->gamePlugin(), *m_settings);

  // Restore any stale INI/save backups left by a previous Wine/Proton crash.
  // Native Linux game instances do not use the prefix during launch.
  if (!m_instance->gamePlugin()->isNativeLinux()) {
    auto prefixPath = FluorineConfig::prefixPath();
    if (!prefixPath || prefixPath->isEmpty()) {
      QSettings const instanceSettings(m_settings->filename(), QSettings::IniFormat);
      for (const auto& key : {"Settings/proton_prefix_path", "Settings/prefix_path",
                              "Proton/prefix_path", "fluorine/prefix_path"}) {
        const QString value = instanceSettings.value(key).toString().trimmed();
        if (!value.isEmpty()) {
          prefixPath = value;
          break;
        }
      }
    }
    if (prefixPath && !prefixPath->isEmpty()) {
      WinePrefix const prefix(*prefixPath);
      if (prefix.isValid()) {
        log::info("checking for stale backup files in prefix '{}'", *prefixPath);
        prefix.restoreStaleBackups();
      }
    }
  }

  m_core->createDefaultProfile();
  m_core->createOverwriteDirectories();

  {
    const auto edition = m_settings->game().edition().value_or("");
    const auto variant = edition.isEmpty() ? QString("Steam") : edition;
    log::info("using game plugin '{}' ('{}', variant {}) at {}",
              m_instance->gamePlugin()->gameName(),
              m_instance->gamePlugin()->gameShortName(),
              variant,
              m_instance->gamePlugin()->gameDirectory().absolutePath());
  }

  auto& categories = CategoryFactory::instance();
  if (!categories.loadCategories(m_settings->isNewInstance())) {
    const auto reset = QMessageBox::question(
        nullptr, tr("Category Recovery Required"),
        tr("The category files for this instance cannot be loaded safely. "
           "Fluorine will not load mod metadata because doing so could erase "
           "category assignments.\n\n"
           "Reset the category inventory now? Every existing category and "
           "transaction file will first be moved to a recovery backup."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    QStringList recoveryBackups;
    if (reset != QMessageBox::Yes ||
        !CategoryFactory::resetCategoryStorage(&recoveryBackups) ||
        !categories.loadCategories(true)) {
      log::error("category data is unavailable; refusing to load mod metadata");
      InstanceManager::singleton().clearCurrentInstance();
      return ReselectExitCode;
    }
    log::warn("category storage was explicitly reset; recovery backups: {}",
              recoveryBackups.join(QStringLiteral(", ")));
  }
  m_core->updateExecutablesList();
  m_core->updateModInfoFromDisc();
  m_core->setCurrentProfile(m_instance->profileName());

  return 0;
}

int MOApplication::run(MOMultiProcess& multiProcess)
{
  log::debug("MOApplication::run() entered");
  // checking command line
  TimeThis tt("MOApplication::run()");

  // show splash
  tt.start("MOApplication::doOneRun() splash");

  MOSplash splash(*m_settings, m_instance->directory(), m_instance->gamePlugin());

  tt.start("MOApplication::doOneRun() finishing");

  // start an api check
  NexusOAuthTokens tokens;
  if (GlobalSettings::nexusOAuthTokens(tokens) ||
      GlobalSettings::nexusApiKey(tokens.apiKey)) {
    m_nexus->getAccessManager()->apiCheck(tokens);
  }

  // tutorials
  log::debug("initializing tutorials");
  TutorialManager::init(qApp->applicationDirPath() + "/" +
                            QString::fromStdWString(AppConfig::tutorialsPath()) + "/",
                        m_core.get());

  int res = 1;
  bool mainWindowStartupFailed = false;

  {
    tt.start("MOApplication::doOneRun() MainWindow setup");
    log::debug("creating MainWindow...");
    MainWindow mainWindow(*m_settings, *m_core, *m_plugins);
    log::debug("MainWindow created");

    // A settings transaction failure is known during construction. Do not
    // show the window or attach services in that state. Failures discovered
    // by first-show migrations are still caught by the check below.
    if (!mainWindow.startupFailed()) {
      // the nexus interface can show dialogs, make sure they're parented to the
      // main window
      m_nexus->getAccessManager()->setTopLevelWidget(&mainWindow);

      connect(
          &mainWindow, &MainWindow::styleChanged, this,
          [this](auto&& file) {
            setStyleFile(file);
          },
          Qt::QueuedConnection);

      log::debug("displaying main window");
      mainWindow.show();
    }
    splash.close();

    tt.stop();

    if (mainWindow.startupFailed()) {
      pauseExternalMessages();
      mainWindowStartupFailed = true;
      log::error("main window startup failed; the event loop will not be started");
      mainWindow.hide();
    } else {
      mainWindow.activateWindow();
      const qsizetype staleMessages = m_externalMessages.resume();
      if (staleMessages > 0) {
        log::warn(
            "discarding {} external command(s) from the retired core generation",
            staleMessages);
      }
      scheduleExternalMessageDrain();
      m_mainEventLoopActive = true;
      res = exec();
      m_mainEventLoopActive = false;
      pauseExternalMessages();
      mainWindowStartupFailed = mainWindow.startupFailed();
      mainWindow.close();
    }

    // main window is about to be destroyed
    m_nexus->getAccessManager()->setTopLevelWidget(nullptr);

    if (!mainWindowStartupFailed) {
      try {
        if (m_core != nullptr) {
          m_core->saveCurrentProfileForShutdown();
        }
      } catch (const std::exception& e) {
        log::error("failed to save current profile during shutdown: {}", e.what());
      } catch (...) {
        log::error(
            "failed to save current profile during shutdown: unknown exception");
      }
    }
  }

  // reset geometry if the flag was set from the settings dialog
  if (!mainWindowStartupFailed) {
    m_settings->geometry().resetIfNeeded();
  }

  return res;
}

void MOApplication::beginExternalShutdown()
{
  m_externalMessages.stop();
  m_externalDrainScheduled = false;
  if (m_core != nullptr && !m_externalShutdownStarted) {
    m_externalShutdownStarted = true;
    m_core->prepareForExternalShutdown();
  }
}

bool MOApplication::enqueueExternalMessage(const QString& message)
{
  if (ModOrganizerExiting() || ModOrganizerCanCloseNow()) {
    log::warn("rejecting external message during exit authorization");
    return false;
  }

  const bool nxmMessage = isNxmLink(message);
  if (nxmMessage && !NxmRequest::parse(message)) {
    log::warn("rejecting malformed external download link");
    return false;
  }
  const auto scope = nxmMessage ? ExternalMessageQueue::Scope::AnyGeneration
                                : ExternalMessageQueue::Scope::CurrentGeneration;
  if (!m_externalMessages.enqueue(message, scope)) {
    log::warn("rejecting external message: the application is not ready or its "
              "pending-message limit was reached");
    return false;
  }

  log::debug("captured external IPC message ({} UTF-8 bytes); {} pending",
             message.toUtf8().size(), m_externalMessages.size());
  scheduleExternalMessageDrain();
  return true;
}

void MOApplication::scheduleExternalMessageDrain()
{
  const auto action = m_externalMessages.dispatchAction(
      ModOrganizerExiting(), ModOrganizerCanCloseNow());
  if (action == ExternalMessageQueue::DispatchAction::Stop) {
    pauseExternalMessages();
    return;
  }
  if (m_externalDrainScheduled || m_externalDispatching ||
      action == ExternalMessageQueue::DispatchAction::Wait) {
    // Exit authorization can pump a nested event loop and later be refused.
    // Retry after that transient attempt instead of stranding an accepted
    // message in an otherwise-ready generation.
    if (ModOrganizerExiting() && !m_externalDrainScheduled &&
        m_externalMessages.ready() && m_externalMessages.size() > 0) {
      m_externalDrainScheduled = true;
      QTimer::singleShot(50, this, [this] {
        m_externalDrainScheduled = false;
        scheduleExternalMessageDrain();
      });
    }
    return;
  }

  m_externalDrainScheduled = true;
  QTimer::singleShot(0, this, [this] {
    m_externalDrainScheduled = false;
    dispatchNextExternalMessage();
  });
}

void MOApplication::dispatchNextExternalMessage()
{
  if (ModOrganizerCanCloseNow()) {
    pauseExternalMessages();
    return;
  }
  if (ModOrganizerExiting()) {
    scheduleExternalMessageDrain();
    return;
  }
  if (m_core == nullptr || m_instance == nullptr) {
    pauseExternalMessages();
    return;
  }

  const auto message = m_externalMessages.takeNext();
  if (!message) {
    return;
  }

  m_externalDispatching = true;
  try {
    dispatchExternalMessage(*message);
  } catch (const std::exception& e) {
    log::error("failed to process external message: {}", e.what());
  } catch (...) {
    log::error("failed to process external message: unknown exception");
  }
  m_externalDispatching = false;
  scheduleExternalMessageDrain();
}

void MOApplication::dispatchExternalMessage(const QString& message)
{
  log::debug("processing external IPC message ({} UTF-8 bytes)",
             message.toUtf8().size());

  MOShortcut const moshortcut(message);

  if (moshortcut.isValid()) {
    if (moshortcut.hasExecutable()) {
      try {
        m_core->processRunner()
            .setFromShortcut(moshortcut)
            .setWaitForCompletion(ProcessRunner::TriggerRefresh)
            .run();
      } catch (std::exception&) {
        // user was already warned
      }
    }
  } else if (NxmRequest::parse(message)) {
    MessageDialog::showMessage(tr("Download started"), qApp->activeWindow(), false);
    m_core->downloadRequestedExternalLink(message);
  } else {
    cl::CommandLine cl;

    std::optional<int> result;
    if (cl::isForwardedArgumentsMessage(message)) {
      const auto arguments = cl::decodeForwardedArguments(message);
      if (!arguments) {
        log::warn("rejecting malformed forwarded command arguments");
        return;
      }
      result = cl.processArguments(*arguments);
    } else {
      // Backward compatibility for simple textual messages from older
      // installations and integrations.
      result = cl.process(message.toStdWString());
    }

    if (result) {
      log::debug("while processing external message, command line wants to "
                 "exit; ignoring");

      return;
    }

    if (auto i = cl.instance()) {
      if (*i != m_instance->displayName()) {
        reportError(
            tr("This shortcut or command line is for instance '%1', but the current "
               "instance is '%2'.")
                .arg(*i)
                .arg(m_instance->displayName()));

        return;
      }
    }

    if (auto p = cl.profile()) {
      if (*p != m_core->profileName()) {
        reportError(
            tr("This shortcut or command line is for profile '%1', but the current "
               "profile is '%2'.")
                .arg(*p)
                .arg(m_core->profileName()));

        return;
      }
    }

    cl.runPostOrganizer(*m_core);
  }
}

void MOApplication::pauseExternalMessages()
{
  m_externalMessages.pause();
  // resetForRestart removes posted events. Allow the next successful setup to
  // schedule a fresh drain even when that removal consumed the old callback.
  m_externalDrainScheduled = false;
}

std::unique_ptr<Instance> MOApplication::getCurrentInstance(bool forceSelect)
{
  auto& m              = InstanceManager::singleton();
  auto currentInstance = m.currentInstance();

  if (forceSelect || !currentInstance) {
    // clear any overrides that might have been given on the command line
    m.clearOverrides();
    currentInstance = selectInstance();
  } else {
    if (!QDir(currentInstance->directory()).exists()) {
      // the previously used instance doesn't exist anymore

      // clear any overrides that might have been given on the command line
      m.clearOverrides();

      if (m.hasAnyInstances()) {
        reportError(QObject::tr("Instance at '%1' not found. Select another instance.")
                        .arg(currentInstance->directory()));
      } else {
        reportError(
            QObject::tr("Instance at '%1' not found. You must create a new instance")
                .arg(currentInstance->directory()));
      }

      resetAppearance();
      currentInstance = selectInstance();
    }
  }

  return currentInstance;
}

std::optional<int> MOApplication::setupInstanceLoop(Instance& currentInstance,
                                                    PluginContainer& pc,
                                                    Settings& settings)
{
  for (;;) {
    const auto setupResult = setupInstance(currentInstance, pc, settings);

    if (setupResult == SetupInstanceResults::Okay) {
      return {};
    } else if (setupResult == SetupInstanceResults::TryAgain) {
      continue;
    } else if (setupResult == SetupInstanceResults::SelectAnother) {
      InstanceManager::singleton().clearCurrentInstance();
      return ReselectExitCode;
    } else {
      return 1;
    }
  }
}

void MOApplication::purgeOldFiles()
{
  // remove the temporary backup directory in case we're restarting after an
  // update
  QString const backupDirectory = qApp->applicationDirPath() + "/update_backup";
  if (QDir(backupDirectory).exists()) {
    shellDelete(QStringList(backupDirectory));
  }

  // cycle log file
  removeOldFiles(qApp->property("dataPath").toString() + "/" +
                     QString::fromStdWString(AppConfig::logPath()),
                 "usvfs*.log", 5, QDir::Name);
}

void MOApplication::resetForRestart()
{
  pauseExternalMessages();
  LogModel::instance().clear();
  ResetExitFlag();

  // make sure the log file isn't locked in case MO was restarted and
  // the previous instance gets deleted
  log::getDefault().setFile({});

  // clear instance and profile overrides
  InstanceManager::singleton().clearOverrides();

  if (m_core != nullptr) {
    m_core->saveCurrentProfileForShutdown();
  }

  m_plugins  = {};
  QCoreApplication::removePostedEvents(nullptr);

  m_core     = {};
  m_nexus    = {};
  m_settings = {};
  m_instance = {};

  resetAppearance();

  QCoreApplication::removePostedEvents(nullptr);
}

bool MOApplication::setStyleFile(const QString& styleName)
{
  ApplicationAppearance::Spec appearance;
  appearance.styleName = styleName;
  if (m_settings != nullptr) {
    appearance.fontFamily = m_settings->interface().fontFamily();
    appearance.fontSize   = m_settings->interface().qssFontSize();
  }
  if (m_instance != nullptr && m_instance->isPortable()) {
    appearance.instanceDirectory = m_instance->directory();
  }
  return applyAppearance(appearance, true);
}

bool MOApplication::applyAppearance(
    const ApplicationAppearance::Spec& appearance, bool watchFile)
{
  m_requestedAppearance = appearance;
  QString error;
  const bool applied = m_appearance->apply(appearance, &error);
  updateAppearanceWatcher(watchFile && applied);

  if (!applied) {
    log::warn("{}; restored the default application appearance", error);
  }

  const QFontInfo baseFont(QApplication::font());
  const QFontMetrics baseMetrics(QApplication::font());
  log::debug(
      "application appearance: requested style='{}', active file='{}', "
      "application font='{}', pixelSize={}, pointSize={}, weight={}, "
      "metrics={}x{}, settings font-size override={}",
      appearance.styleName, m_appearance->activeStyleFile(), baseFont.family(),
      baseFont.pixelSize(), baseFont.pointSizeF(), baseFont.weight(),
      baseMetrics.height(),
      baseMetrics.horizontalAdvance(QStringLiteral("Fluorine")),
      appearance.fontSize);
  return applied;
}

void MOApplication::resetAppearance()
{
  m_requestedAppearance = ApplicationAppearance::Spec{};
  m_appearance->reset();
  updateAppearanceWatcher(false);
}

void MOApplication::updateAppearanceWatcher(bool watchFile)
{
  const QStringList watched = m_styleWatcher.files();
  if (!watched.isEmpty()) {
    m_styleWatcher.removePaths(watched);
  }
  const QString active = m_appearance->activeStyleFile();
  if (watchFile && !active.isEmpty()) {
    m_styleWatcher.addPath(active);
  }
}

bool MOApplication::notify(QObject* receiver, QEvent* event)
{
  try {
    return QApplication::notify(receiver, event);
  } catch (const std::filesystem::filesystem_error& fe) {
    log::error("uncaught filesystem exception in handler (object {}, eventtype {}): {}",
               receiver->objectName(), event->type(), fe.what());

    // ENOTCONN = stale FUSE mount. Attempt recovery so MO2 can continue.
    // If we manage to clear the wedged mount, suppress the user-facing
    // dialog — the iteration that failed will be retried by whatever
    // workflow triggered it (refresh, restore, etc.) and showing a hard
    // error on a state we just recovered from is just noise.
    if (fe.code().value() == ENOTCONN) {
      bool attempted       = false;
      bool allPathsCleared = true;
      auto attemptCleanup = [&attempted, &allPathsCleared](
                                const std::filesystem::path& p) {
        if (p.empty()) {
          return;
        }
        attempted = true;
        const QString qpath = QString::fromStdString(p.string());
        log::warn("ENOTCONN on '{}' — attempting stale mount cleanup",
                  p.string());
        FuseConnector::tryCleanupStaleMount(qpath);
        // Probe the path: if stat() no longer returns ENOTCONN, we cleared
        // it. Even if the path is now missing (ENOENT), that's recovery
        // from MO2's perspective — the iterator can succeed (or skip).
        struct stat st;
        const bool stillWedged =
            ::stat(qpath.toLocal8Bit().constData(), &st) != 0 &&
            errno == ENOTCONN;
        allPathsCleared = allPathsCleared && !stillWedged;
      };
      attemptCleanup(fe.path1());
      attemptCleanup(fe.path2());

      if (attempted && allPathsCleared) {
        log::info(
            "stale FUSE mount recovered; suppressing error dialog. "
            "The triggering operation will need to be retried.");
        return false;
      }
    }

    reportError(tr("an error occurred: %1").arg(fe.what()));
    return false;
  } catch (const std::exception& e) {
    log::error("uncaught exception in handler (object {}, eventtype {}): {}",
               receiver->objectName(), event->type(), e.what());
    reportError(tr("an error occurred: %1").arg(e.what()));
    return false;
  } catch (...) {
    log::error("uncaught non-std exception in handler (object {}, eventtype {})",
               receiver->objectName(), event->type());
    reportError(tr("an error occurred"));
    return false;
  }
}

MOSplash::MOSplash(const Settings& settings, const QString& dataPath,
                   const MOBase::IPluginGame* game)
{
  const auto splashPath = getSplashPath(settings, dataPath, game);
  if (splashPath.isEmpty()) {
    return;
  }

  QPixmap const image(splashPath);
  if (image.isNull()) {
    log::error("failed to load splash from {}", splashPath);
    return;
  }

  ss_.reset(new QSplashScreen(image));
  settings.geometry().centerOnMainWindowMonitor(ss_.get());

  ss_->show();
  ss_->activateWindow();
}

void MOSplash::close()
{
  if (ss_) {
    // don't pass mainwindow as it just waits half a second for it
    // instead of proceding
    ss_->finish(nullptr);
  }
}

QString MOSplash::getSplashPath(const Settings& settings, const QString& dataPath,
                                const MOBase::IPluginGame* game)
{
  if (!settings.useSplash()) {
    return {};
  }

  // try splash from instance directory
  const QString splashPath = dataPath + "/splash.png";
  if (QFile::exists(dataPath + "/splash.png")) {
    QImage const image(splashPath);
    if (!image.isNull()) {
      return splashPath;
    }
  }

  // try splash from plugin
  QString pluginSplash = QString(":/%1/splash").arg(game->gameShortName());
  if (QFile::exists(pluginSplash)) {
    QImage const image(pluginSplash);
    if (!image.isNull()) {
      image.save(splashPath);
      return pluginSplash;
    }
  }

  // try default splash from resource
  QString defaultSplash = ":/MO/gui/splash";
  if (QFile::exists(defaultSplash)) {
    QImage const image(defaultSplash);
    if (!image.isNull()) {
      return defaultSplash;
    }
  }

  return splashPath;
}

#include "moapplication.moc"
