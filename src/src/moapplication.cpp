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
#include "commandline.h"
#include "instancemanager.h"
#include "loglist.h"
#include "mainwindow.h"
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
#include <QFile>
#include <QPainter>
#include <QProxyStyle>
#include <QSslSocket>
#include <QStringList>
#include <QStyleFactory>
#include <QStyleOption>
#include <QUrl>
#include <iplugingame.h>
#include <log.h>
#include <report.h>
#include <scopeguard.h>
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

// style proxy that changes the appearance of drop indicators
//
class ProxyStyle : public QProxyStyle
{
public:
  ProxyStyle(QStyle* baseStyle = nullptr) : QProxyStyle(baseStyle) {}

  void drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                     QPainter* painter, const QWidget* widget) const override
  {
    if (element == QStyle::PE_IndicatorItemViewItemDrop) {

      // 0. Fix a bug that made the drop indicator sometimes appear on top
      // of the mod list when selecting a mod.
      if (option->rect.height() == 0 && option->rect.bottomRight() == QPoint(-1, -1)) {
        return;
      }

      // 1. full-width drop indicator
      QRect rect(option->rect);
      if (const auto* view = qobject_cast<const QTreeView*>(widget)) {
        rect.setLeft(view->indentation());
        rect.setRight(widget->width());
      }

      // 2. stylish drop indicator
      painter->setRenderHint(QPainter::Antialiasing, true);

      QColor col(option->palette.windowText().color());
      QPen pen(col);
      pen.setWidth(2);
      col.setAlpha(50);

      painter->setPen(pen);
      painter->setBrush(QBrush(col));
      if (rect.height() == 0) {
        QPoint tri[3] = {rect.topLeft(), rect.topLeft() + QPoint(-5, 5),
                         rect.topLeft() + QPoint(-5, -5)};
        painter->drawPolygon(tri, 3);
        painter->drawLine(rect.topLeft(), rect.topRight());
      } else {
        painter->drawRoundedRect(rect, 5, 5);
      }
    } else {
      QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
  }
};

void addLinuxLibrariesToPath()
{
  const auto libsPath =
      QDir::toNativeSeparators(QCoreApplication::applicationDirPath() + "/lib");

  QCoreApplication::setLibraryPaths(QStringList(libsPath) +
                                    QCoreApplication::libraryPaths());

  env::prependToPath(libsPath);
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

  // Ensure the app name is always "ModOrganizer" regardless of the binary
  // filename (settings/profile lookups key off this).
  setApplicationName("ModOrganizer");
  setDesktopFileName(QStringLiteral("com.fluorine.manager"));
  setWindowIcon(QIcon(":/MO/gui/app_icon"));

  qputenv("QML_DISABLE_DISK_CACHE", "true");

  connect(&m_styleWatcher, &QFileSystemWatcher::fileChanged, [&](auto&& file) {
    log::debug("style file '{}' changed, reloading", file);
    updateStyle(file);
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
  // Start with "None" style setting and only apply custom styles from settings
  // later during setup().
  setStyleFile("");
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

  // makes plugin data path available to plugins, see
  // IOrganizer::getPluginDataPath()
  MOBase::details::setPluginDataPath(OrganizerCore::pluginDataPath());

  // figuring out the current instance
  m_instance = getCurrentInstance(forceSelect);
  if (!m_instance) {
    return 1;
  }

  // first time the data path is available, set the global property and log
  // directory, then log a bunch of debug stuff
  const QString dataPath = m_instance->directory();
  setProperty("dataPath", dataPath);

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
  log::getDefault().setLevel(m_settings->diagnostics().logLevel());
  log::debug("using ini at '{}'", m_settings->filename());

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

  m_plugins = std::make_unique<PluginContainer>(m_core.get());
  m_plugins->loadPlugins();
  log::debug("all plugins loaded");

  // instance
  log::debug("entering setupInstanceLoop...");
  if (auto r = setupInstanceLoop(*m_instance, *m_plugins)) {
    log::debug("setupInstanceLoop returned {}", *r);
    return *r;
  }
  log::debug("setupInstanceLoop done");

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

  // styling
  if (!setStyleFile(m_settings->interface().styleName().value_or(""))) {
    // disable invalid stylesheet
    m_settings->interface().setStyleName("");
  }

  int res = 1;

  {
    tt.start("MOApplication::doOneRun() MainWindow setup");
    log::debug("creating MainWindow...");
    MainWindow mainWindow(*m_settings, *m_core, *m_plugins);
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

    tt.stop();

    res = exec();
    mainWindow.close();

    // main window is about to be destroyed
    m_nexus->getAccessManager()->setTopLevelWidget(nullptr);
  }

  // reset geometry if the flag was set from the settings dialog
  m_settings->geometry().resetIfNeeded();

  return res;
}

void MOApplication::externalMessage(const QString& message)
{
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
    if (m_core == nullptr) {
      // This can happen if MO2 is started with the --pick option and no instance has
      // been selected yet.
      reportError(tr("You need to select an instance before trying to download mods."));
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
  LogModel::instance().clear();
  ResetExitFlag();

  // make sure the log file isn't locked in case MO was restarted and
  // the previous instance gets deleted
  log::getDefault().setFile({});

  // clear instance and profile overrides
  InstanceManager::singleton().clearOverrides();

  m_core     = {};
  m_plugins  = {};
  m_nexus    = {};
  m_settings = {};
  m_instance = {};
}

bool MOApplication::setStyleFile(const QString& styleName)
{
  // remove all files from watch
  QStringList const currentWatch = m_styleWatcher.files();
  if (currentWatch.count() != 0) {
    m_styleWatcher.removePaths(currentWatch);
  }
  // set new stylesheet or clear it
  if (styleName.length() != 0) {
    // Search for the stylesheet in multiple locations:
    //   1. applicationDirPath()/stylesheets/ — bundled themes
    //   2. instance baseDir/stylesheets/     — instance/portable themes (modlists)
    //   3. fluorineDataDir()/stylesheets/    — user-installed custom themes
    const QString ssSubdir = MOBase::ToQString(AppConfig::stylesheetsPath());
    QStringList searchDirs;
    searchDirs << applicationDirPath() + "/" + ssSubdir;
    if (m_instance) {
      // Prefer baseDirectory() (populated after readFromIni), fall back to
      // directory() which is always set by the constructor.
      QString base = m_instance->baseDirectory();
      if (base.isEmpty())
        base = m_instance->directory();
      const QString instanceDir = base + "/" + ssSubdir;
      if (!searchDirs.contains(instanceDir))
        searchDirs << instanceDir;
    }
    const QString userDir = fluorineDataDir() + "/stylesheets";
    if (!searchDirs.contains(userDir))
      searchDirs << userDir;

    QString resolved;
    for (const auto& dir : searchDirs) {
      QString const candidate = dir + "/" + styleName;
      if (QFile::exists(candidate)) {
        resolved = candidate;
        break;
      }
    }

    if (!resolved.isEmpty()) {
      m_styleWatcher.addPath(resolved);
      updateStyle(resolved);
    } else {
      // Could be a built-in Qt style name (e.g. "Fusion")
      updateStyle(styleName);
    }
  } else {
    setStyle(new ProxyStyle(QStyleFactory::create(m_defaultStyle)));
    setStyleSheet("");
  }
  return true;
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

namespace
{
QStringList extractTopStyleSheetComments(QFile& stylesheet)
{
  if (!stylesheet.open(QFile::ReadOnly)) {
    log::error("failed to open stylesheet file {}", stylesheet.fileName());
    return {};
  }
  ON_BLOCK_EXIT([&stylesheet]() {
    stylesheet.close();
  });

  QStringList topComments;

  while (true) {
    const auto byteLine = stylesheet.readLine();
    if (byteLine.isNull()) {
      break;
    }

    const auto line = QString(byteLine).trimmed();

    // skip empty lines
    if (line.isEmpty()) {
      continue;
    }

    // only handle single line comments
    if (!line.startsWith("/*")) {
      break;
    }

    topComments.push_back(line.mid(2, line.size() - 4).trimmed());
  }

  return topComments;
}

QString extractBaseStyleFromStyleSheet(QFile& stylesheet, const QString& defaultStyle)
{
  // read the first line of the files that are either empty or comments
  //
  const auto topLines = extractTopStyleSheetComments(stylesheet);

  const auto factoryStyles = QStyleFactory::keys();

  QString style = defaultStyle;

  for (const auto& line : topLines) {
    if (!line.startsWith("mo2-base-style")) {
      continue;
    }

    const auto parts = line.split(":");
    if (parts.size() != 2) {
      log::warn("found invalid top-comment for mo2 in {}: {}", stylesheet.fileName(),
                line);
      continue;
    }

    const auto tmpStyle = parts[1].trimmed();
    const auto index    = factoryStyles.indexOf(tmpStyle, 0, Qt::CaseInsensitive);
    if (index == -1) {
      log::warn("base style '{}' from style '{}' not found", tmpStyle,
                stylesheet.fileName(), line);
      continue;
    }

    style = factoryStyles[index];
    log::info("found base style '{}' for style '{}'", style, stylesheet.fileName());
    break;
  }

  return style;
}

}  // namespace

// Walk a directory and create case-variant symlinks for asset files so QSS
// url(...) references resolve regardless of the case convention the theme
// author used. On case-sensitive filesystems Qt's url() resolver fails when
// the case doesn't match exactly. We cover the two common conventions:
//   - QSS lowercase / disk mixed case  → create  lowercase  → MixedCase symlink
//   - QSS TitleCase / disk lowercase   → create  TitleCase  → lowercase symlink
static void createStylesheetCaseShims(const QString& dirPath)
{
  namespace fs = std::filesystem;
  std::error_code ec;
  if (!fs::exists(dirPath.toStdString(), ec) ||
      !fs::is_directory(dirPath.toStdString(), ec)) {
    return;
  }

  auto tryShim = [](const fs::path& parent, const std::string& target,
                    const std::string& shimName) {
    if (shimName == target) return;
    std::error_code ec;
    const auto shimPath = parent / shimName;
    if (fs::exists(shimPath, ec)) return;
    std::error_code lec;
    fs::create_symlink(target, shimPath, lec);
    if (lec) {
      log::debug("stylesheet shim: failed to link '{}' -> '{}': {}",
                 shimPath.string(), target, lec.message());
    }
  };

  for (auto it = fs::recursive_directory_iterator(
           dirPath.toStdString(),
           fs::directory_options::skip_permission_denied, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    const auto& entry = *it;
    if (!entry.is_regular_file(ec)) continue;

    const std::string name = entry.path().filename().string();
    const auto parent = entry.path().parent_path();

    std::string lowerName = name;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    tryShim(parent, name, lowerName);

    std::string titleName = lowerName;
    if (!titleName.empty()) {
      titleName[0] = static_cast<char>(
          std::toupper(static_cast<unsigned char>(titleName[0])));
    }
    tryShim(parent, name, titleName);
  }
}

void MOApplication::updateStyle(const QString& fileName)
{
  if (QStyleFactory::keys().contains(fileName)) {
    setStyleSheet("");
    setStyle(new ProxyStyle(QStyleFactory::create(fileName)));
  } else {
    QFile stylesheet(fileName);
    if (stylesheet.exists()) {
      // Pre-create lowercase shims so url(foo.svg) in the QSS resolves even
      // when the on-disk file is Foo.svg.
      createStylesheetCaseShims(QFileInfo(fileName).absolutePath());
      setStyle(new ProxyStyle(QStyleFactory::create(
          extractBaseStyleFromStyleSheet(stylesheet, m_defaultStyle))));
      setStyleSheet(QString("file:///%1").arg(fileName));
    } else {
      log::warn("invalid stylesheet: {}", fileName);
    }
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
