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
#include "envmodule.h"
#include "applicationappearance.h"
#include "curatedguidenxmbroker.h"
#include "commandline.h"
#include "instancemanager.h"
#include "loglist.h"
#include "mainwindow.h"
#include "memorydiagnostics.h"
#include "messagedialog.h"
#include "multiprocess.h"
#include "nexusinterface.h"
#include "nxmaccessmanager.h"
#include "organizercore.h"
#include "sanitychecks.h"
#include "settings.h"
#include "fluorineconfig.h"
#include "fluorinepaths.h"
#include "fuseconnector.h"
#include "headless.h"
#include "wineprefix.h"

#include <cerrno>
#include <filesystem>
#include <sys/stat.h>
#include "shared/appconfig.h"
#include "shared/util.h"
#include "thread_utils.h"
#include "tutorialmanager.h"
#include <QByteArray>
#include <QDebug>
#include <QDesktopServices>
#include <QEvent>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetrics>
#include <QSslSocket>
#include <QStringList>
#include <QStyleFactory>
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
  // The runtime entry point selects the XDG portal theme before QApplication
  // is constructed so QFileDialog can use the desktop-native portal. Restore
  // the caller's environment immediately afterward; launched games, xdg-open
  // and helper processes must not inherit Fluorine's selection.
  constexpr auto OriginalPlatformTheme =
      "FLUORINE_ORIG_QT_QPA_PLATFORMTHEME";
  if (qEnvironmentVariableIsSet(OriginalPlatformTheme)) {
    const QByteArray original = qgetenv(OriginalPlatformTheme);
    if (original.isEmpty()) {
      qunsetenv("QT_QPA_PLATFORMTHEME");
    } else {
      qputenv("QT_QPA_PLATFORMTHEME", original);
    }
    qunsetenv(OriginalPlatformTheme);
  }

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

OrganizerCore& MOApplication::core()
{
  return *m_core;
}

void MOApplication::firstTimeSetup(MOMultiProcess& multiProcess)
{
  connect(
      &multiProcess, &MOMultiProcess::messageSent, this,
      [this](auto&& s) {
        externalMessage(s);
      },
      Qt::QueuedConnection);
}

int MOApplication::setup(MOMultiProcess& multiProcess, bool forceSelect)
{
  TimeThis tt("MOApplication setup()");
  m_coreReady = false;

  // makes plugin data path available to plugins, see
  // IOrganizer::getPluginDataPath()
  MOBase::details::setPluginDataPath(OrganizerCore::pluginDataPath());

  // Keep instance selection on the application baseline until a selection
  // is accepted and its settings are loaded.
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
    if (!headless::active()) {
      InstanceManager::singleton().clearCurrentInstance();
    }
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
  MemoryDiagnostics::snapshot("startup.settings_loaded");
  log::getDefault().setLevel(m_settings->diagnostics().logLevel());
  log::debug("using ini at '{}'", m_settings->filename());

  // The selector has closed: apply the selected instance appearance through
  // its Settings facade before creating instance-specific dialogs.
  if (!setStyleFile(m_settings->interface().styleName().value_or(""))) {
    m_settings->interface().setStyleName("");
  }

  // Recover before loading/validating the game plugin. A dead FUSE mount makes
  // looksValid(gamePath) fail with ENOTCONN, so cleanup performed after
  // setupInstanceLoop() can never be reached. Only the primary process owns
  // startup recovery; a deliberately secondary --multiple process must not
  // detach a healthy mount owned by the primary instance.
  if (!multiProcess.secondary()) {
    const auto configuredGameDir = m_settings->game().directory();
    if (configuredGameDir && !configuredGameDir->trimmed().isEmpty()) {
      log::info("checking for stale FUSE mounts before game validation using '{}'",
                *configuredGameDir);
      FuseConnector::tryCleanupStaleMount(*configuredGameDir);
    }
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
    if (!headless::active()) {
      InstanceManager::singleton().clearCurrentInstance();
    }
    return 1;
  }
  MemoryDiagnostics::snapshot("startup.core_bootstrapped");

  // plugins
  tt.start("MOApplication::doOneRun() plugins");
  log::debug("initializing plugins");

  m_plugins = std::make_unique<PluginContainer>(m_core.get());
  m_plugins->loadPlugins();
  MemoryDiagnostics::snapshot("startup.plugins_loaded");
  log::debug("all plugins loaded");

  // instance
  log::debug("entering setupInstanceLoop...");
  if (auto r = setupInstanceLoop(*m_instance, *m_plugins)) {
    log::debug("setupInstanceLoop returned {}", *r);
    return *r;
  }
  log::debug("setupInstanceLoop done");
  MemoryDiagnostics::snapshot("startup.instance_configured");

  if (m_instance->isPortable()) {
    log::debug("this is a portable instance");
  }

  tt.start("MOApplication::doOneRun() OrganizerCore setup");

  sanity::checkPaths(*m_instance->gamePlugin(), *m_settings);

  // setting up organizer core
  m_core->setManagedGame(m_instance->gamePlugin());

  // Clean up stale FUSE mounts from a previous crash BEFORE any game
  // directory access (profile init, BSA invalidation, etc.).
  {
    const auto dataDir = m_instance->gamePlugin()->dataDirectory().absolutePath();
    log::info("checking for stale FUSE mount on '{}'", dataDir);
    FuseConnector::tryCleanupStaleMount(dataDir);
  }

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

  CategoryFactory::instance().loadCategories();
  m_core->updateExecutablesList();
  m_core->updateModInfoFromDisc();
  MemoryDiagnostics::snapshot("startup.mods_scanned");
  m_core->setCurrentProfile(m_instance->profileName());
  MemoryDiagnostics::snapshot("startup.profile_selected");
  m_coreReady = true;

  // The single-instance listener is active before setup() finishes. Preserve
  // NXM links received during that window and start them as soon as the core
  // is ready instead of rejecting or losing them.
  processPendingExternalLinks();

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
  MemoryDiagnostics::snapshot("startup.splash_constructed");

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

  {
    tt.start("MOApplication::doOneRun() MainWindow setup");
    log::debug("creating MainWindow...");
    MainWindow mainWindow(*m_settings, *m_core, *m_plugins);
    MemoryDiagnostics::snapshot("startup.main_window_constructed");
    log::debug("MainWindow created, showing...");

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
    mainWindow.activateWindow();
    splash.close();
    MemoryDiagnostics::snapshot("startup.main_window_shown");

    tt.stop();

    res = exec();
    mainWindow.close();

    // main window is about to be destroyed
    m_nexus->getAccessManager()->setTopLevelWidget(nullptr);

    try {
      if (m_core != nullptr) {
        m_core->saveCurrentProfileForShutdown();
      }
    } catch (const std::exception& e) {
      log::error("failed to save current profile during shutdown: {}", e.what());
    } catch (...) {
      log::error("failed to save current profile during shutdown: unknown exception");
    }
  }

  // reset geometry if the flag was set from the settings dialog
  m_settings->geometry().resetIfNeeded();

  return res;
}

void MOApplication::externalMessage(const QString& message)
{
  if (headless::active()) {
    // acting on it would remap the VFS or nest a dialog under the running game
    log::info("headless: ignoring forwarded message");
    return;
  }

  log::debug("received external message '{}'", message);

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
  } else if (isNxmLink(message)) {
    if (CuratedGuideNxmBroker::instance().tryConsume(message)) {
      MessageDialog::showMessage(tr("Download authorization accepted"),
                                 qApp->activeWindow(), false);
      return;
    }
    if (!m_coreReady) {
      log::info("queueing external download link until instance setup completes");
      m_pendingExternalLinks.append(message);
    } else {
      MessageDialog::showMessage(tr("Download started"), qApp->activeWindow(), false);
      m_core->downloadRequestedNXM(message);
    }
  } else {
    cl::CommandLine cl;

    if (auto r = cl.process(message.toStdWString())) {
      log::debug("while processing external message, command line wants to "
                 "exit; ignoring");

      return;
    }

    if (auto i = cl.instance()) {
      const auto ci = InstanceManager::singleton().currentInstance();

      if (*i != ci->displayName()) {
        reportError(
            tr("This shortcut or command line is for instance '%1', but the current "
               "instance is '%2'.")
                .arg(*i)
                .arg(ci->displayName()));

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

void MOApplication::routeNxmMessage(const QString& message)
{
  externalMessage(message);
}

void MOApplication::processPendingExternalLinks()
{
  if (!m_coreReady || m_core == nullptr || m_pendingExternalLinks.isEmpty()) {
    return;
  }

  QStringList links;
  links.swap(m_pendingExternalLinks);
  log::info("processing {} download link(s) queued during startup", links.size());
  for (const QString& link : links) {
    if (!CuratedGuideNxmBroker::instance().tryConsume(link)) {
      m_core->downloadRequestedNXM(link);
    }
  }
}

std::unique_ptr<Instance> MOApplication::getCurrentInstance(bool forceSelect)
{
  auto& m              = InstanceManager::singleton();
  auto currentInstance = m.currentInstance();

  if (headless::active() && (forceSelect || !currentInstance)) {
    headless::fail(
        QObject::tr("Instance '%1' not found.")
            .arg(currentInstance ? currentInstance->displayName() : QString()));
    return {};
  }

  if (forceSelect || !currentInstance) {
    // clear any overrides that might have been given on the command line
    m.clearOverrides();
    currentInstance = selectInstance();
  } else {
    if (!QDir(currentInstance->directory()).exists()) {
      // the previously used instance doesn't exist anymore

      if (headless::active()) {
        headless::fail(QObject::tr("Instance '%1' not found.")
                           .arg(currentInstance->displayName()));
        return {};
      }

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
                                                    PluginContainer& pc)
{
  for (;;) {
    const auto setupResult = setupInstance(currentInstance, pc);

    if (setupResult == SetupInstanceResults::Okay) {
      return {};
    } else if (setupResult == SetupInstanceResults::TryAgain) {
      continue;
    } else if (setupResult == SetupInstanceResults::SelectAnother) {
      if (headless::active()) {
        return 1;
      }
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
  m_coreReady = false;
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
  setProperty("fluorinePortableInstance", false);
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
      bool recovered = false;
      auto attemptCleanup = [&recovered](const std::filesystem::path& p) {
        if (p.empty()) {
          return;
        }
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
        if (!stillWedged) {
          recovered = true;
        }
      };
      attemptCleanup(fe.path1());
      attemptCleanup(fe.path2());

      if (recovered) {
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
  MemoryDiagnostics::snapshot("startup.splash.begin");
  const auto splashPath = getSplashPath(settings, dataPath, game);
  MemoryDiagnostics::snapshot("startup.splash.path_resolved");
  if (splashPath.isEmpty()) {
    return;
  }

  QPixmap const image(splashPath);
  MemoryDiagnostics::snapshot("startup.splash.pixmap_loaded");
  if (image.isNull()) {
    log::error("failed to load splash from {}", splashPath);
    return;
  }

  ss_.reset(new QSplashScreen(image));
  MemoryDiagnostics::snapshot("startup.splash.widget_created");
  settings.geometry().centerOnMainWindowMonitor(ss_.get());
  MemoryDiagnostics::snapshot("startup.splash.widget_centered");

  ss_->show();
  ss_->activateWindow();
  MemoryDiagnostics::snapshot("startup.splash.widget_shown");
}

void MOSplash::close()
{
  if (ss_) {
    // don't pass mainwindow as it just waits half a second for it
    // instead of proceeding. Destroy the splash immediately after hiding it
    // so its image and platform surface are not retained across exec().
    ss_->finish(nullptr);
    ss_.reset();
    MemoryDiagnostics::snapshot("startup.splash.destroyed");
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
