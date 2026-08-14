#include "organizercore.h"
#include "categories.h"
#include "categoriesdialog.h"
#include "credentialsdialog.h"
#include "fluorine_build_info.h"
#include "fluorineupdater.h"
#include "slrmanager.h"
#include "delayedfilewriter.h"
#include "directoryrefresher.h"
#include "env.h"
#include "envfs.h"
#include "envmodule.h"
#include "filedialogmemory.h"
#include "guessedvalue.h"
#include "imodinterface.h"
#include "imoinfo.h"
#include "instancemanager.h"
#include "iplugingame.h"
#include "iuserinterface.h"
#include "launchlifecycle.h"
#include "messagedialog.h"
#include "metainiutils.h"
#include "modlistsortproxy.h"
#include "modrepositoryfileinfo.h"
#include "nexusinterface.h"
#include "nxmaccessmanager.h"
#include "plugincontainer.h"
#include "previewdialog.h"
#include "profile.h"
#include "shared/appconfig.h"
#include "shared/directoryentry.h"
#include "shared/fileentry.h"
#include "shared/filesorigin.h"
#include "shared/util.h"
#include "spawn.h"
#include "syncoverwritedialog.h"
#include "virtualfiletree.h"
#include "usvfsrequest.h"
#include "usvfssnapshot.h"
#include "vfsbackend.h"
#include "vfs/vfscatalog.h"
#include "vfs/permissionrepair.h"
#include "vfs/gamesavemigration.h"
#include <ipluginmodpage.h>
#include <questionboxmemory.h>
#include <uibase/game_features/dataarchives.h>
#include <uibase/game_features/localsavegames.h>
#include <uibase/game_features/scriptextender.h>
#include <uibase/registry.h>
#include <uibase/report.h>
#include <uibase/scopeguard.h>
#include <uibase/filesystemutilities.h>
#include <uibase/utility.h>
#include "fluorineconfig.h"
#include "protonlauncher.h"
#include "wineprefix.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QStandardPaths>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QPointer>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QWidget>
#include <QtConcurrent/QtConcurrentRun>

#include <QtDebug>
#include <QtGlobal>  // for qUtf8Printable, etc

#include <climits>
#include <cstddef>
#include <cstring>  // for memset, wcsrchr

#include <boost/algorithm/string/predicate.hpp>
#include <atomic>
#include <exception>
#include <functional>
#include <iterator>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>  //for wstring
#include <tuple>
#include <utility>
#include <vector>

#include <libbsarch/bs_archive.h>

#include "organizerproxy.h"

using namespace MOShared;
using namespace MOBase;

static env::CoreDumpTypes g_coreDumpType = env::CoreDumpTypes::Mini;

namespace
{
std::atomic<bool> s_slrUpdateCheckInProgress{false};

QSettings globalSettings()
{
  return QSettings(QStringLiteral("Mod Organizer Team"),
                   QStringLiteral("Mod Organizer"));
}

QString uniqueFilePath(const QDir& dir, const QString& fileName)
{
  QString candidate = dir.filePath(fileName);
  if (!QFileInfo::exists(candidate)) {
    return candidate;
  }

  const QFileInfo info(fileName);
  const QString base = info.completeBaseName();
  const QString suffix = info.suffix();
  const QString timestamp =
      QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss"));

  for (int i = 1; i < 1000; ++i) {
    const QString numbered =
        suffix.isEmpty()
            ? QStringLiteral("%1_%2_%3").arg(base, timestamp).arg(i)
            : QStringLiteral("%1_%2_%3.%4").arg(base, timestamp).arg(i).arg(suffix);
    candidate = dir.filePath(numbered);
    if (!QFileInfo::exists(candidate)) {
      return candidate;
    }
  }

  return dir.filePath(
      suffix.isEmpty()
          ? QStringLiteral("%1_%2").arg(base, timestamp)
          : QStringLiteral("%1_%2.%3").arg(base, timestamp, suffix));
}

void installSlrUpdate(QWidget* parent, const SlrUpdateInfo& info);

bool fileContainsAsciiMarker(const QString& path, const QByteArray& marker)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || marker.isEmpty()) return false;

  QByteArray carry;
  while (!file.atEnd()) {
    QByteArray block = carry + file.read(1024 * 1024);
    if (block.contains(marker)) return true;
    const qsizetype keep = std::min<qsizetype>(marker.size() - 1, block.size());
    carry = block.right(keep);
  }
  return false;
}

bool usvfsRuntimeSupportsResolvedSnapshots()
{
  const QByteArray override = qgetenv("FLUORINE_USVFS_RESOLVED_SNAPSHOT").trimmed();
  if (override == "1") return true;
  if (override == "0") return false;

  const QString dll = QDir(QCoreApplication::applicationDirPath())
                          .filePath(QStringLiteral("usvfs/usvfs_x64.dll"));
  return fileContainsAsciiMarker(
      dll, QByteArrayLiteral("usvfsVirtualLinkMappings"));
}

void promptSlrUpdate(QWidget* parent, const SlrUpdateInfo& info)
{
  if (slrOperationAdmissionSuppressed()) {
    return;
  }

  if (parent == nullptr) {
    parent = qApp->activeWindow();
  }

  QMessageBox box(parent);
  box.setIcon(QMessageBox::Information);
  box.setWindowTitle(QObject::tr("Steam Linux Runtime Update"));
  box.setText(QObject::tr("A Steam Linux Runtime update is available."));
  box.setInformativeText(
      QObject::tr("Current build: %1\nLatest build: %2\n\n"
                  "You can update now, be reminded later, or skip this build.")
          .arg(info.localBuildId.isEmpty() ? QObject::tr("unknown") : info.localBuildId,
               info.remoteBuildId));

  QPushButton* updateButton =
      box.addButton(QObject::tr("Update Now"), QMessageBox::AcceptRole);
  box.addButton(QObject::tr("Remind Me Later"), QMessageBox::RejectRole);
  QPushButton* skipButton =
      box.addButton(QObject::tr("Skip This Version"), QMessageBox::DestructiveRole);
  box.setDefaultButton(updateButton);

  box.exec();

  if (slrOperationAdmissionSuppressed()) {
    return;
  }
  if (box.clickedButton() == updateButton) {
    installSlrUpdate(parent, info);
  } else if (box.clickedButton() == skipButton) {
    runSlrUiCommitIfAllowed([&] {
      QSettings settings = globalSettings();
      settings.setValue(QStringLiteral("SteamLinuxRuntime/skipped_build_id"),
                        info.remoteBuildId);
    });
  }
}

void installSlrUpdate(QWidget* parent, const SlrUpdateInfo& info)
{
  if (slrOperationAdmissionSuppressed()) {
    return;
  }

  if (parent == nullptr) {
    parent = qApp->activeWindow();
  }

  auto* progress = new QProgressDialog(
      QObject::tr("Updating Steam Linux Runtime...\n"
                  "Current build: %1\nLatest build: %2")
          .arg(info.localBuildId.isEmpty() ? QObject::tr("unknown") : info.localBuildId,
               info.remoteBuildId),
      QObject::tr("Cancel"), 0, 0, parent);
  progress->setWindowTitle(QObject::tr("Steam Linux Runtime"));
  progress->setWindowModality(Qt::WindowModal);
  progress->setMinimumDuration(0);

  const SlrCancellationSource cancellation;
  QObject::connect(progress, &QProgressDialog::canceled, progress,
                   [cancellation] {
    cancellation.cancel();
  });

  auto* watcher = new QFutureWatcher<QString>(progress);
  QObject::connect(watcher, &QFutureWatcher<QString>::finished, progress,
                   [watcher, progress, cancellation] {
                     progress->close();
                     const QString err = watcher->result();
                     watcher->deleteLater();
                     progress->deleteLater();

                     if (cancellation.isCancellationRequested() ||
                         slrOperationAdmissionSuppressed()) {
                       return;
                     }

                     if (!err.isEmpty()) {
                       MOBase::log::warn("SLR update failed: {}", err);
                       QMessageBox::warning(
                           qApp->activeWindow(),
                           QObject::tr("Steam Linux Runtime"),
                           QObject::tr("Steam Linux Runtime update failed:\n%1\n\n"
                                       "The existing runtime was kept.")
                               .arg(err));
                     } else {
                       if (!runSlrUiCommitIfAllowed([] {
                             QSettings settings = globalSettings();
                             settings.remove(QStringLiteral(
                                 "SteamLinuxRuntime/skipped_build_id"));
                           })) {
                         return;
                       }
                       MOBase::log::info("Steam Linux Runtime updated successfully");
                       QMessageBox::information(
                           qApp->activeWindow(),
                           QObject::tr("Steam Linux Runtime"),
                           QObject::tr("Steam Linux Runtime updated successfully."));
                     }
                   });

  watcher->setFuture(
      QtConcurrent::run([token = cancellation.token()]() -> QString {
        return downloadSlr(nullptr, nullptr, token);
      }));
  progress->show();
}

int cleanupRetryDelayMs(unsigned int retryCount)
{
  constexpr unsigned int maxShift = 5;
  const auto shift = std::min(retryCount, maxShift);
  return std::min(100 * (1 << shift), 5000);
}

QString cleanupFailureMessage(const std::exception_ptr& failure)
{
  if (!failure) {
    return QStringLiteral("unknown cleanup state transition failure");
  }

  try {
    std::rethrow_exception(failure);
  } catch (const std::exception& e) {
    return QString::fromUtf8(e.what());
  } catch (...) {
    return QStringLiteral("unknown exception");
  }
}

void logQueuedCleanupFailure(const char* operation, const QString& launchToken,
                             std::exception_ptr failure) noexcept
{
  try {
    log::error("{} for '{}' escaped unexpectedly: {}; ownership remains retained",
               operation, launchToken.toStdString(),
               cleanupFailureMessage(failure).toStdString());
  } catch (...) {
  }
}

}  // namespace

template <typename InputIterator>
QStringList toStringList(InputIterator current, InputIterator end)
{
  QStringList result;
  for (; current != end; ++current) {
    result.append(*current);
  }
  return result;
}

QString resolveWinePrefixPath(const Settings& settings,
                              const IPluginGame* managedGame)
{
  if (managedGame != nullptr && managedGame->isNativeLinux()) {
    return {};
  }

  if (auto cfg = FluorineConfig::load(); cfg.has_value() && cfg->prefixExists()) {
    return cfg->prefix_path.trimmed();
  }

  // Same precedence rule as spawn.cpp's resolvePrefixPath: explicit
  // fluorine/prefix_path wins over the legacy Settings/* keys, which may
  // have been auto-populated with an external manager's prefix (Heroic,
  // Bottles). Without this, switching instances or rebuilding the Fluorine
  // config can silently drop us onto the wrong prefix (issue #52).
  const QSettings instanceSettings(settings.filename(), QSettings::IniFormat);
  const QString explicitPath =
      instanceSettings.value("fluorine/prefix_path").toString().trimmed();
  if (!explicitPath.isEmpty()) {
    return explicitPath;
  }
  for (const auto& key : {"Settings/proton_prefix_path", "Settings/prefix_path",
                           "Proton/prefix_path"}) {
    const QString value = instanceSettings.value(key).toString().trimmed();
    if (!value.isEmpty()) {
      return value;
    }
  }

  return {};
}

QString resolveWineDataDirName(const IPluginGame* managedGame)
{
  if (managedGame == nullptr) {
    return {};
  }

  // Primary: the My Games subfolder name matches the AppData/Local folder
  // for almost every Bethesda game.
  const QDir docsDir = managedGame->documentsDirectory();
  if (docsDir.exists()) {
    const QString docsLeaf = docsDir.dirName().trimmed();
    if (!docsLeaf.isEmpty() && docsLeaf != QStringLiteral(".")) {
      return docsLeaf;
    }
  }

  // Fallback: gameShortName is used by the base Gamebryo mappings() for
  // the AppData/Local folder and matches for most games.
  const QString shortName = managedGame->gameShortName();
  if (!shortName.isEmpty()) {
    log::warn("resolveWineDataDirName: documentsDirectory() is empty or "
              "invalid, falling back to gameShortName '{}'",
              shortName);
    return shortName;
  }

  log::warn("resolveWineDataDirName: both documentsDirectory() and "
            "gameShortName() are empty, falling back to gameName '{}'",
            managedGame->gameName());
  return managedGame->gameName();
}

QString resolvePrefixGameDocumentsDir(const WinePrefix& prefix,
                                      const QString& dataDirName)
{
  return QDir(prefix.myGamesPath()).filePath(dataDirName);
}

QString resolveAbsoluteSaveDir(const WinePrefix& prefix,
                               const IPluginGame* managedGame,
                               MOBase::LocalSavegames* localSaves,
                               std::shared_ptr<Profile> profile)
{
  if (profile == nullptr || managedGame == nullptr) {
    const QString dataDirName = resolveWineDataDirName(managedGame);
    return QDir(prefix.myGamesPath()).filePath(dataDirName + "/Saves");
  }

  const QString profileSaveDir =
      QDir(profile->absolutePath()).filePath("saves");

  // Strategy 1: Use LocalSavegames::mappings() if available.
  // Extract the user-relative portion of the destination (after drive_c/users/<name>/)
  // and reconstruct it under our prefix's userProfilePath().
  if (localSaves != nullptr) {
    const MappingType mappings = localSaves->mappings(QDir(profileSaveDir));
    for (const auto& mapping : mappings) {
      if (!mapping.isDirectory) {
        continue;
      }

      const QString source = QDir::cleanPath(mapping.source);
      const QString destination = QDir::cleanPath(mapping.destination);
      if (source != QDir::cleanPath(profileSaveDir)) {
        continue;
      }

      // Extract the user-relative path (after drive_c/users/<username>/)
      static const QRegularExpression userDirRe(
          "drive_c/users/[^/]+/(.+)", QRegularExpression::CaseInsensitiveOption);
      const auto match = userDirRe.match(destination);
      if (match.hasMatch()) {
        const QString userRelative = match.captured(1);
        const QString resolved =
            QDir(prefix.userProfilePath()).filePath(userRelative);
        log::debug("resolveAbsoluteSaveDir: from mappings -> '{}'", resolved);
        return resolved;
      }
    }
  }

  // Strategy 2: Use managedGame->savesDirectory()
  {
    const QDir savesDir = managedGame->savesDirectory();
    const QString savesPath = QDir::cleanPath(savesDir.absolutePath());
    static const QRegularExpression userDirRe(
        "drive_c/users/[^/]+/(.+)", QRegularExpression::CaseInsensitiveOption);
    const auto match = userDirRe.match(savesPath);
    if (match.hasMatch()) {
      const QString userRelative = match.captured(1);
      const QString resolved =
          QDir(prefix.userProfilePath()).filePath(userRelative);
      log::debug("resolveAbsoluteSaveDir: from savesDirectory -> '{}'", resolved);
      return resolved;
    }
  }

  // Fallback: Documents/My Games/<dataDirName>/Saves (old Bethesda behavior)
  const QString dataDirName = resolveWineDataDirName(managedGame);
  const QString fallback =
      QDir(prefix.myGamesPath()).filePath(dataDirName + "/Saves");
  log::debug("resolveAbsoluteSaveDir: fallback -> '{}'", fallback);
  return fallback;
}

void migrateGameLocalSavesFromOverwrite(const IPluginGame* game,
                                        const QString& overwriteDir)
{
  if (game == nullptr || overwriteDir.isEmpty()) {
    return;
  }

  const auto stats = MOBase::Vfs::migrateGameLocalSaves(
      game->dataDirectory().absolutePath().toStdString(),
      game->savesDirectory().absolutePath().toStdString(),
      overwriteDir.toStdString());
  if (stats.moved == 0 && stats.failed == 0 && stats.skipped == 0) {
    return;
  }
  log::info("Game-local save migration path='{}' inspected={} moved={} "
            "skipped={} failed={}",
            stats.relativePath, stats.inspected, stats.moved, stats.skipped,
            stats.failed);
}

OrganizerCore::OrganizerCore(Settings& settings)
    :
      m_CurrentProfile(nullptr), m_Settings(settings),
      m_Updater(&NexusInterface::instance()), m_ModList(m_PluginContainer, this),
      m_PluginList(*this),
      m_DirectoryRefresher(new DirectoryRefresher(this, settings.refreshThreadCount())),
      m_DirectoryStructure(new DirectoryEntry(L"data", nullptr, 0)),
      m_VirtualFileTree([this]() {
        return VirtualFileTree::makeTree(m_DirectoryStructure);
      }),
      m_DownloadManager(&NexusInterface::instance(), this), m_DirectoryUpdate(false),

      m_PluginListsWriter(std::bind(&OrganizerCore::savePluginList, this))
{
  env::setHandleCloserThreadCount(settings.refreshThreadCount());
  m_DownloadManager.setOutputDirectory(m_Settings.paths().downloads(), false);

  NexusInterface::instance().setCacheDirectory(m_Settings.paths().cache());

  m_InstallationManager.setModsDirectory(m_Settings.paths().mods());
  m_InstallationManager.setDownloadDirectory(m_Settings.paths().downloads());

  connect(&m_DownloadManager, SIGNAL(downloadSpeed(QString, int)), this,
          SLOT(downloadSpeed(QString, int)));
  connect(m_DirectoryRefresher.get(), &DirectoryRefresher::refreshed, this,
          &OrganizerCore::onDirectoryRefreshed);

  connect(&m_ModList, SIGNAL(removeOrigin(QString)), this, SLOT(removeOrigin(QString)));
  connect(&m_ModList, &ModList::modStatesChanged, [=, this] {
    currentProfile()->writeModlist();
  });
  connect(&m_ModList, &ModList::modPrioritiesChanged, [this](auto&& indexes) {
    modPrioritiesChanged(indexes);
  });

  connect(NexusInterface::instance().getAccessManager(),
          SIGNAL(validateSuccessful(bool)), this, SLOT(loginSuccessful(bool)));
  connect(NexusInterface::instance().getAccessManager(),
          SIGNAL(validateFailed(QString)), this, SLOT(loginFailed(QString)));

  // This seems awfully imperative
  connect(this, SIGNAL(managedGameChanged(MOBase::IPluginGame const*)), &m_Settings,
          SLOT(managedGameChanged(MOBase::IPluginGame const*)));
  connect(this, SIGNAL(managedGameChanged(MOBase::IPluginGame const*)),
          &m_DownloadManager, SLOT(managedGameChanged(MOBase::IPluginGame const*)));
  connect(this, SIGNAL(managedGameChanged(MOBase::IPluginGame const*)), &m_PluginList,
          SLOT(managedGameChanged(MOBase::IPluginGame const*)));

  connect(&m_PluginList, &PluginList::writePluginsList, &m_PluginListsWriter,
          &DelayedFileWriterBase::write);

  // make directory refresher run in a separate thread
  m_RefresherThread.start();
  m_DirectoryRefresher->moveToThread(&m_RefresherThread);

  connect(&settings.plugins(), &PluginSettings::pluginSettingChanged,
          [this](auto const&... args) {
            m_PluginSettingChanged(args...);
          });
}

OrganizerCore::~OrganizerCore()
{
  if (!m_PersistenceSuppressed) {
    try {
      saveCurrentProfileForShutdown();
    } catch (const std::exception& e) {
      log::error("failed to save current profile during OrganizerCore shutdown: {}",
                 e.what());
    } catch (...) {
      log::error("failed to save current profile during OrganizerCore shutdown: "
                 "unknown exception");
    }
  } else {
    m_PluginListsWriter.cancel();
  }

  try {
    m_RefresherThread.exit();
    m_RefresherThread.wait();

    if (m_StructureDeleter.joinable()) {
      m_StructureDeleter.join();
    }
  } catch (const std::exception& e) {
    log::error("failed while stopping OrganizerCore worker threads: {}", e.what());
  } catch (...) {
    log::error("failed while stopping OrganizerCore worker threads: unknown exception");
  }

  try {
    // profile has to be cleaned up before the modinfo-buffer is cleared
    m_CurrentProfile.reset();

    ModInfo::clear();
    m_ModList.setProfile(nullptr);
    //  NexusInterface::instance()->cleanup();

    delete m_DirectoryStructure;
    m_DirectoryStructure = nullptr;
  } catch (const std::exception& e) {
    log::error("failed while clearing OrganizerCore state: {}", e.what());
  } catch (...) {
    log::error("failed while clearing OrganizerCore state: unknown exception");
  }
}

void OrganizerCore::storeSettings()
{
  if (m_PersistenceSuppressed) {
    return;
  }

  if (m_CurrentProfile != nullptr) {
    m_Settings.game().setSelectedProfileName(m_CurrentProfile->name());
  }

  m_ExecutablesList.store(m_Settings);

  FileDialogMemory::save(m_Settings);

  const auto result = m_Settings.sync();

  if (result != QSettings::NoError) {
    QString reason;

    if (result == QSettings::AccessError) {
      reason = tr("File is write protected");
    } else if (result == QSettings::FormatError) {
      reason = tr("Invalid file format (probably a bug)");
    } else {
      reason = tr("Unknown error %1").arg(result);
    }

    QMessageBox::critical(
        qApp->activeWindow(), tr("Failed to write settings"),
        tr("An error occurred trying to write back MO settings to %1: %2")
            .arg(m_Settings.filename(), reason));
  }
}

void OrganizerCore::updateExecutablesList()
{
  if (m_PluginContainer == nullptr) {
    log::error("can't update executables list now");
    return;
  }

  m_ExecutablesList.load(managedGame(), m_Settings);
}

void OrganizerCore::updateModInfoFromDisc()
{
  const QString modsPath = m_Settings.paths().mods();
  log::debug("updateModInfoFromDisc: base='{}', mods='{}'",
             m_Settings.paths().base(), modsPath);
  ModInfo::updateFromDisc(modsPath, *this,
                          m_Settings.interface().displayForeign(),
                          m_Settings.refreshThreadCount());
}

void OrganizerCore::showNotification(const QString& title, const QString& message,
                                     QSystemTrayIcon::MessageIcon icon)
{
  if (m_UserInterface) {
    m_UserInterface->showNotification(title, message, icon);
  }
}

void OrganizerCore::setUserInterface(IUserInterface* ui)
{
  storeSettings();

  m_UserInterface = ui;

  QWidget* w = nullptr;
  if (m_UserInterface) {
    w = m_UserInterface->mainWindow();
  }

  m_InstallationManager.setParentWidget(w);
  m_Updater.setUserInterface(w);
  m_UILocker.setUserInterface(w);
  m_DownloadManager.setParentWidget(w);

  checkForUpdates();
}

void OrganizerCore::suppressPersistenceForFailedRollback() noexcept
{
  m_PluginMutationBarrier->suppress();
  m_DownloadManager.suppressAdmissionForFailedRollback();
  m_ProcessLaunchContext.suppressNewReservations();
  suppressSlrOperationsForFailedRollback();
  m_PersistenceSuppressed = true;
  m_CurrentProfileSavedForShutdown = true;
  CategoryFactory::instance().suppressWritesForFailedRollback();
  ModInfo::suppressAllWritesForFailedRollback();
  Profile::suppressAllWritesForFailedRollback();
  m_Settings.suppressWritesForFailedRollback();
}

void OrganizerCore::cancelPersistenceWritersForFailedRollback() noexcept
{
  try {
    m_PluginListsWriter.cancel();
    if (m_UserInterface != nullptr) {
      m_UserInterface->archivesWriter().cancel();
    }
  } catch (...) {
    // Admission was already closed by suppressPersistenceForFailedRollback;
    // fail-stop shutdown must never be interrupted by cleanup bookkeeping.
  }
}

void OrganizerCore::checkForUpdates()
{
  // this currently wouldn't work reliably if the ui isn't initialized yet to
  // display the result
  if (m_UserInterface != nullptr) {
    m_Updater.testForUpdate(m_Settings);
    checkForFluorineUpdates();
    checkForSlrUpdates();
  }
}

void OrganizerCore::checkForSlrUpdates()
{
  // SLR updates are offered at startup but remain user-controlled. The first
  // install case is still handled at game launch, where Proton cannot proceed
  // without a runtime.
  if (!m_Settings.checkForUpdates()) {
    return;
  }
  if (m_Settings.network().offlineMode()) {
    return;
  }
  if (!isSlrInstalled()) {
    return;
  }
  if (slrOperationAdmissionSuppressed()) {
    return;
  }

  bool expected = false;
  if (!s_slrUpdateCheckInProgress.compare_exchange_strong(expected, true)) {
    return;
  }

  QPointer<OrganizerCore> self(this);
  std::thread([self]() {
    const SlrUpdateInfo info = checkSlrUpdate();
    if (!self) {
      s_slrUpdateCheckInProgress = false;
      return;
    }

    const bool invoked = QMetaObject::invokeMethod(self, [self, info]() {
      if (!self) {
        s_slrUpdateCheckInProgress = false;
        return;
      }

      s_slrUpdateCheckInProgress = false;

      if (slrOperationAdmissionSuppressed()) {
        return;
      }

      if (!info.error.isEmpty()) {
        MOBase::log::warn("SLR update check failed: {}", info.error);
        return;
      }

      if (!info.updateAvailable) {
        MOBase::log::debug("Steam Linux Runtime is up to date ({})",
                           info.localBuildId);
        return;
      }

      QSettings settings = globalSettings();
      const QString skippedBuild =
          settings.value(QStringLiteral("SteamLinuxRuntime/skipped_build_id"))
              .toString();
      if (skippedBuild == info.remoteBuildId) {
        MOBase::log::debug("Skipping SLR update prompt for ignored build {}",
                           info.remoteBuildId);
        return;
      }

      QWidget* parent = nullptr;
      if (self->m_UserInterface) {
        parent = self->m_UserInterface->mainWindow();
      }
      promptSlrUpdate(parent, info);
    }, Qt::QueuedConnection);
    if (!invoked) {
      s_slrUpdateCheckInProgress = false;
    }
  }).detach();
}

void OrganizerCore::checkForFluorineUpdates()
{
  // Set up the Fluorine self-update checker lazily so repeated calls don't
  // leak QNetworkAccessManager instances. The member is forward-declared in
  // the header (pointer-only); the include lives here to keep the header
  // lightweight for its many consumers.
  if (m_FluorineUpdater == nullptr) {
    m_FluorineUpdater = new FluorineUpdater(this);

    connect(m_FluorineUpdater, &FluorineUpdater::updateAvailable, this,
            [](const FluorineUpdater::ReleaseInfo& info) {
              const QString channel =
                  FluorineUpdater::channelToString(info.channel);
              MOBase::log::info(
                  "Fluorine update available ({}): {} at {}",
                  channel,
                  info.tagName.isEmpty() ? info.name : info.tagName,
                  info.htmlUrl);
            });
    connect(m_FluorineUpdater, &FluorineUpdater::upToDate, this,
            [](const FluorineUpdater::ReleaseInfo& info) {
              MOBase::log::debug("Fluorine is up to date ({})",
                                 FluorineUpdater::channelToString(info.channel));
            });
    connect(m_FluorineUpdater, &FluorineUpdater::checkFailed, this,
            [](const QString& reason) {
              MOBase::log::debug("Fluorine update check failed: {}", reason);
            });
  }

  const FluorineUpdater::Channel channel = FluorineUpdater::channelFromString(
      m_Settings.fluorineUpdateChannel(), FluorineUpdater::buildChannel());
  m_FluorineUpdater->checkForUpdates(channel);
}

void OrganizerCore::connectPlugins(PluginContainer* container)
{
  m_PluginContainer = container;
  m_Updater.setPluginContainer(m_PluginContainer);
  m_InstallationManager.setPluginContainer(m_PluginContainer);
  m_DownloadManager.setPluginContainer(m_PluginContainer);
  m_ModList.setPluginContainer(m_PluginContainer);

  if (!m_GameName.isEmpty()) {
    m_GamePlugin = m_PluginContainer->game(m_GameName);
    emit managedGameChanged(m_GamePlugin);
  }

  connect(m_PluginContainer, &PluginContainer::pluginEnabled, [&](IPlugin* plugin) {
    m_PluginEnabled(plugin);
  });
  connect(m_PluginContainer, &PluginContainer::pluginDisabled, [&](IPlugin* plugin) {
    m_PluginDisabled(plugin);
  });

  connect(&m_PluginContainer->gameFeatures(), &GameFeatures::modDataContentUpdated,
          [this](ModDataContent const* contentFeature) {
            if (contentFeature) {
              m_Contents = ModDataContentHolder(contentFeature->getAllContents());
            } else {
              m_Contents = ModDataContentHolder();
            }
          });
}

void OrganizerCore::setManagedGame(MOBase::IPluginGame* game)
{
  m_GameName   = game->gameName();
  m_GamePlugin = game;
  qApp->setProperty("managed_game", QVariant::fromValue(m_GamePlugin));
  emit managedGameChanged(m_GamePlugin);
}

Settings& OrganizerCore::settings()
{
  return m_Settings;
}

bool OrganizerCore::nexusApi(bool retry)
{
  auto* accessManager = NexusInterface::instance().getAccessManager();

  if ((accessManager->validateAttempted() || accessManager->validated()) && !retry) {
    // previous attempt, maybe even successful
    return false;
  } else {
    NexusOAuthTokens tokens;
    GlobalSettings::nexusOAuthTokens(tokens);
    GlobalSettings::nexusApiKey(tokens.apiKey);
    if (tokens.isValid() || !tokens.apiKey.isEmpty()) {
      // credentials stored or user entered them manually
      log::debug("attempt to verify nexus credentials");
      accessManager->apiCheck(tokens);
      return true;
    } else {
      // no credentials stored and user didn't enter them
      accessManager->refuseValidation();
      return false;
    }
  }
}

void OrganizerCore::startMOUpdate()
{
  if (nexusApi()) {
    m_PostLoginTasks.append([&]() {
      m_Updater.startUpdate();
    });
  } else {
    m_Updater.startUpdate();
  }
}

void OrganizerCore::downloadRequestedNXM(const QString& url)
{
  log::debug("download requested: {}", url);
  if (nexusApi()) {
    m_PendingDownloads.append(url);
  } else {
    m_DownloadManager.addNXMDownload(url);
  }
}

void OrganizerCore::userInterfaceInitialized()
{
  m_UserInterfaceInitialized(m_UserInterface->mainWindow());
}

void OrganizerCore::profileCreated(MOBase::IProfile* profile)
{
  m_ProfileCreated(profile);
}

void OrganizerCore::profileRenamed(MOBase::IProfile* profile, QString const& oldName,
                                   QString const& newName)
{
  m_ProfileRenamed(profile, oldName, newName);
}

void OrganizerCore::profileRemoved(QString const& profileName)
{
  m_ProfileRemoved(profileName);
}

void OrganizerCore::downloadRequested(QNetworkReply* reply, QString gameName, int modID,
                                      const QString& fileName)
{
  try {
    if (m_DownloadManager.addDownload(reply, QStringList(), fileName, gameName, modID,
                                      0, new ModRepositoryFileInfo(gameName, modID))) {
      MessageDialog::showMessage(tr("Download started"), qApp->activeWindow());
    }
  } catch (const std::exception& e) {
    MessageDialog::showMessage(tr("Download failed"), qApp->activeWindow());
    log::error("exception starting download: {}", e.what());
  }
}

void OrganizerCore::removeOrigin(const QString& name)
{
  const auto wname = ToWString(name);
  if (m_DirectoryStructure->originExists(wname)) {
    FilesOrigin& origin = m_DirectoryStructure->getOriginByName(wname);
    origin.enable(false);
  }
  refreshLists();
}

void OrganizerCore::downloadSpeed(const QString& serverName, int bytesPerSecond)
{
  m_Settings.network().setDownloadSpeed(serverName, bytesPerSecond);
}

InstallationManager* OrganizerCore::installationManager()
{
  return &m_InstallationManager;
}

bool OrganizerCore::createDirectory(const QString& path)
{
  if (!QDir(path).exists() && !QDir().mkpath(path)) {
    QMessageBox::critical(nullptr, QObject::tr("Error"),
                          QObject::tr("Failed to create \"%1\". Your user "
                                      "account probably lacks permission.")
                              .arg(QDir::toNativeSeparators(path)));
    return false;
  } else {
    return true;
  }
}

bool OrganizerCore::checkPathSymlinks()
{
  const bool hasSymlink = (QFileInfo(m_Settings.paths().profiles()).isSymLink() ||
                           QFileInfo(m_Settings.paths().mods()).isSymLink() ||
                           QFileInfo(m_Settings.paths().overwrite()).isSymLink());

  if (hasSymlink) {
    log::warn(
        "{}",
        QObject::tr(
            "One of the configured MO2 directories (profiles, mods, or overwrite) "
            "is on a path containing a symbolic (or other) link. This is likely to "
            "be incompatible with MO2's virtual filesystem."));

    return false;
  }

  return true;
}

bool OrganizerCore::bootstrap()
{
  const auto dirs = {m_Settings.paths().profiles(), m_Settings.paths().mods(),
                     m_Settings.paths().downloads(), m_Settings.paths().overwrite(),
                     QString::fromStdWString(getGlobalCoreDumpPath())};

  for (auto&& dir : dirs) {
    if (!createDirectory(dir)) {
      return false;
    }
  }

  if (!checkPathSymlinks()) {
    return false;
  }

  if (!cycleDiagnostics()) {
    return false;
  }

  // log if there are any dmp files
  const auto hasCrashDumps = !QDir(QString::fromStdWString(getGlobalCoreDumpPath()))
                                  .entryList({"*.dmp"}, QDir::Files)
                                  .empty();

  if (hasCrashDumps) {
    log::debug("there are crash dumps in '{}'",
               QString::fromStdWString(getGlobalCoreDumpPath()));
  }

  return true;
}

void OrganizerCore::createDefaultProfile()
{
  QString const profilesPath = settings().paths().profiles();
  if (QDir(profilesPath).entryList(QDir::AllDirs | QDir::NoDotAndDotDot).empty()) {
    Profile newProf(QString::fromStdWString(AppConfig::defaultProfileName()),
                    managedGame(), gameFeatures(), false);

    m_ProfileCreated(&newProf);
  }
}

void OrganizerCore::createOverwriteDirectories()
{
  QString const overwritePath = settings().paths().overwrite();
  for (const auto& modDirectory : managedGame()->getModMappings().keys()) {
    if (!modDirectory.isEmpty()) {
      QDir(overwritePath).mkdir(modDirectory);
    }
  }
}

void OrganizerCore::prepareVFS()
{
  {
    const auto instance = InstanceManager::singleton().currentInstance();
    m_USVFS.setIndexPublicationContext(
        {.output_base=basePath().toStdString(),
         .producer=std::string("Fluorine ") + FLUORINE_VERSION_STRING,
         .instance_name=
             instance != nullptr ? instance->displayName().toStdString() : "",
         .profile_name=
             m_CurrentProfile != nullptr
                 ? m_CurrentProfile->name().toStdString()
                 : "",
#ifdef _WIN32
         .consumer_path_style=VfsIndexConsumerPathStyle::NativeWindows
#else
         .consumer_path_style=VfsIndexConsumerPathStyle::Wine
#endif
        });
  }

  // Read the load order and pass it to the FUSE VFS so plugin files get
  // incrementing timestamps matching their position. This prevents LOOT
  // from reporting "ambiguous load order".
  {
    std::vector<std::string> loadOrder;
    QFile loFile(m_CurrentProfile->getLoadOrderFileName());
    if (!loFile.exists()) {
      loFile.setFileName(m_CurrentProfile->getPluginsFileName());
    }
    if (loFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
      QTextStream in(&loFile);
      while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) {
          continue;
        }
        if (line.startsWith('*')) {
          line = line.mid(1);
        }
        loadOrder.push_back(line.toStdString());
      }
    }
    m_USVFS.setPluginLoadOrder(loadOrder);
  }

  // Set up tracked writes file (per-profile, next to the overwrite folder)
  {
    QString const owPath = settings().paths().overwrite();
    QDir const owDir(owPath);
    QString trackPath = owDir.absoluteFilePath("../tracked_writes.json");
    trackPath = QDir::cleanPath(trackPath);
    std::fprintf(stderr, "[VFS] prepareVFS: owPath='%s' trackPath='%s'\n",
                 owPath.toStdString().c_str(), trackPath.toStdString().c_str());
    m_USVFS.setTrackingFilePath(trackPath.toStdString());
  }

  // VFS Root Builder: read per-instance setting and configure.
  {
    bool vfsRootBuilder = false;
    if (const auto* s = Settings::maybeInstance()) {
      const QSettings instanceIni(s->filename(), QSettings::IniFormat);
      vfsRootBuilder = instanceIni.value("fluorine/vfs_root_builder", true).toBool();
    }
    const QString storageDir =
        QDir(QDir::fromNativeSeparators(basePath())).filePath("rootbuilder");
    m_USVFS.setRootBuilderEnabled(vfsRootBuilder, storageDir.toStdString());
  }

  // Recover saves left in Overwrite by an earlier run before the VFS catalog
  // is rebuilt. This is relevant to games such as RPG Maker titles whose save
  // directory lives below the game data root.
  migrateGameLocalSavesFromOverwrite(managedGame(), overwritePath());

  // Diagnostic toggle from Settings > Proton/Wine tab (see settingsdialogproton.cpp).
  m_USVFS.setDisableVfsCache(
      QSettings().value("fluorine/disable_vfs_cache", false).toBool());

  // Games that manage their own VFS (e.g. OpenMW via openmw.cfg) opt out of the
  // FUSE mount; Fluorine must not overlay Data Files for them.
  if (managedGame() == nullptr || managedGame()->usesVFS()) {
    m_USVFS.updateMapping(fileMapping(m_CurrentProfile->name(), QString()));
  } else {
    log::debug("prepareVFS: skipping FUSE mount; managed game manages its own "
               "VFS (usesVFS=false)");
  }
}

void OrganizerCore::unmountVFS()
{
  m_USVFS.unmount();
  movePGPatcherLogsToLogsFolder();
}

OrganizerCore::VfsPreviewSessionResult
OrganizerCore::beginVfsPreviewSession(const QString& launchToken,
                                      const QString& profileName)
{
  if (launchToken.isEmpty() || profileName.isEmpty() ||
      m_CurrentProfile == nullptr || m_CurrentProfile->name() != profileName) {
    log::warn("VFS preview refused: missing or non-current launch profile '{}'; "
              "token='{}'",
              profileName.toStdString(), launchToken.toStdString());
    return VfsPreviewSessionResult::Unavailable;
  }

  ScopedProcessLaunchReservation reservation(
      m_ProcessLaunchContext, launchToken, profileName, /*ownsVfs=*/true);
  if (!reservation) {
    const auto active = m_ProcessLaunchContext.activeLaunches();
    log::warn("VFS preview refused: organizer VFS is already reserved "
              "({} active launch context(s))",
              active.total);
    return VfsPreviewSessionResult::Busy;
  }

  try {
    prepareVFS();
    if (!m_USVFS.isMounted()) {
      log::info("VFS preview unavailable: the managed game does not use the "
                "organizer VFS");
      return VfsPreviewSessionResult::Unavailable;
    }
  } catch (...) {
    // A failed preparation may still have mounted or deployed part of the VFS.
    // Transfer the reservation to the same retrying mandatory-cleanup path used
    // by failed launches; never abandon ownership merely because unmount threw.
    reservation.retain();
    abortProcessLaunchPreparation(launchToken, profileName, /*ownsVfs=*/true);
    throw;
  }

  reservation.retain();
  log::info("VFS preview session '{}' started for profile '{}'",
            launchToken.toStdString(), profileName.toStdString());
  return VfsPreviewSessionResult::Started;
}

bool OrganizerCore::endVfsPreviewSession(const QString& launchToken,
                                         const QString& profileName)
{
  return continueVfsPreviewTeardown(launchToken, profileName, /*retry=*/false,
                                    /*retryCount=*/0);
}

bool OrganizerCore::continueVfsPreviewTeardown(const QString& launchToken,
                                               const QString& profileName,
                                               bool retry,
                                               unsigned int retryCount)
{
  const auto attempt = launch_cleanup::attemptMandatoryCleanup(
      m_ProcessLaunchContext, launchToken, profileName, /*ownsVfs=*/true,
      retry ? launch_cleanup::AttemptKind::Retry
            : launch_cleanup::AttemptKind::Initial,
      [this]() { m_USVFS.unmount(); });

  if (attempt.state == launch_cleanup::AttemptState::Rejected) {
    log::warn("VFS preview close ignored for unknown or mismatched session "
              "'{}' and profile '{}'; mounted VFS left untouched",
              launchToken.toStdString(), profileName.toStdString());
    return false;
  }

  if (attempt.state == launch_cleanup::AttemptState::RetryRequired) {
    const int delay = cleanupRetryDelayMs(retryCount);
    log::error("VFS preview session '{}' cleanup failed: {}; retaining launch "
               "ownership and retrying in {} ms",
               launchToken.toStdString(),
               cleanupFailureMessage(attempt.failure).toStdString(), delay);
    try {
      QTimer::singleShot(
          delay, this,
          [this, launchToken, profileName, retryCount]() {
            try {
              continueVfsPreviewTeardown(launchToken, profileName,
                                         /*retry=*/true, retryCount + 1);
            } catch (...) {
              logQueuedCleanupFailure("VFS preview cleanup retry", launchToken,
                                      std::current_exception());
            }
          });
    } catch (const std::exception& e) {
      log::error("unable to schedule VFS preview cleanup retry for '{}': {}; "
                 "launch ownership remains retained",
                 launchToken.toStdString(), e.what());
    } catch (...) {
      log::error("unable to schedule VFS preview cleanup retry for '{}'; "
                 "launch ownership remains retained",
                 launchToken.toStdString());
    }
    return false;
  }

  m_ProcessLaunchContext.finishCompletion(launchToken);
  if (!m_PersistenceSuppressed) {
    try {
      movePGPatcherLogsToLogsFolder();
    } catch (...) {
      logQueuedCleanupFailure("VFS preview post-cleanup", launchToken,
                              std::current_exception());
    }
  }
  log::info("VFS preview session '{}' ended for profile '{}'",
            launchToken.toStdString(), profileName.toStdString());
  return true;
}

void OrganizerCore::movePGPatcherLogsToLogsFolder()
{
  const QString dataPath = qApp->property("dataPath").toString();
  if (dataPath.isEmpty()) {
    log::warn("PGPatcher log cleanup skipped: dataPath is not set");
    return;
  }

  QDir logsDir(QDir(dataPath).filePath(QString::fromStdWString(AppConfig::logPath())));
  if (!logsDir.exists() && !QDir().mkpath(logsDir.absolutePath())) {
    log::warn("PGPatcher log cleanup skipped: failed to create '{}'",
              logsDir.absolutePath());
    return;
  }

  const QStringList roots = {
      QDir::fromNativeSeparators(m_Settings.paths().overwrite()),
      QDir::fromNativeSeparators(m_Settings.paths().mods()),
  };

  int moved = 0;
  for (const QString& root : roots) {
    if (root.isEmpty() || !QDir(root).exists()) {
      continue;
    }

    QDirIterator it(root, QStringList{QStringLiteral("PGPatcher.log")},
                    QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
      const QString source = it.next();
      const QString destination =
          uniqueFilePath(logsDir, QFileInfo(source).fileName());

      if (QFile::rename(source, destination) ||
          (QFile::copy(source, destination) && QFile::remove(source))) {
        ++moved;
        log::info("Moved PGPatcher log '{}' -> '{}'", source, destination);
      } else {
        log::warn("Failed to move PGPatcher log '{}' -> '{}'", source,
                  destination);
      }
    }
  }

  if (moved > 0) {
    log::info("PGPatcher log cleanup moved {} file(s) to '{}'", moved,
              logsDir.absolutePath());
  }
}

void OrganizerCore::trackOverwriteMove(const QString& relativePath,
                                       const QString& modFolderPath)
{
  auto tw = m_USVFS.trackedWrites();
  if (tw) {
    tw->track(relativePath.toStdString(), modFolderPath.toStdString());
  }
}

void OrganizerCore::discardVFSStagingOnUnmount()
{
  m_USVFS.discardStagingOnUnmount();
}

void OrganizerCore::updateVFSParams(log::Levels logLevel,
                                    env::CoreDumpTypes coreDumpType,
                                    const QString& crashDumpsPath,
                                    std::chrono::seconds spawnDelay,
                                    QString executableBlacklist,
                                    const QStringList& skipFileSuffixes,
                                    const QStringList& skipDirectories)
{
  setGlobalCoreDumpType(coreDumpType);

  m_USVFS.updateParams(logLevel, coreDumpType, crashDumpsPath, spawnDelay,
                       executableBlacklist, skipFileSuffixes, skipDirectories);
}

void OrganizerCore::setLogLevel(log::Levels level)
{
  m_Settings.diagnostics().setLogLevel(level);

  updateVFSParams(
      m_Settings.diagnostics().logLevel(), m_Settings.diagnostics().coreDumpType(),
      QString::fromStdWString(getGlobalCoreDumpPath()),
      m_Settings.diagnostics().spawnDelay(), m_Settings.executablesBlacklist(),
      m_Settings.skipFileSuffixes(), m_Settings.skipDirectories());

  log::getDefault().setLevel(m_Settings.diagnostics().logLevel());
}

bool OrganizerCore::cycleDiagnostics()
{
  const auto maxDumps = settings().diagnostics().maxCoreDumps();
  const auto path     = QString::fromStdWString(getGlobalCoreDumpPath());

  if (maxDumps > 0) {
    removeOldFiles(path, "*.dmp", maxDumps, QDir::Time | QDir::Reversed);
  }

  return true;
}

env::CoreDumpTypes OrganizerCore::getGlobalCoreDumpType()
{
  return g_coreDumpType;
}

void OrganizerCore::setGlobalCoreDumpType(env::CoreDumpTypes type)
{
  g_coreDumpType = type;
}

std::wstring OrganizerCore::getGlobalCoreDumpPath()
{
  if (qApp) {
    const auto dp = qApp->property("dataPath");
    if (!dp.isNull()) {
      return dp.toString().toStdWString() + L"/" + AppConfig::dumpsDir();
    }
  }

  return {};
}

void OrganizerCore::setCurrentProfile(const QString& profileName)
{
  if ((m_CurrentProfile != nullptr) && (profileName == m_CurrentProfile->name())) {
    return;
  }

  log::debug("selecting profile '{}'", profileName);

  QDir const profileBaseDir(settings().paths().profiles());

  const auto subdirs = profileBaseDir.entryList(QDir::AllDirs | QDir::NoDotAndDotDot);

  QString profileDir;

  // the profile name may not have the correct case, which breaks other parts
  // of the ui like the profile combobox, which walks directories on its own
  //
  // find the real name with the correct case by walking the directories
  for (auto&& dirName : subdirs) {
    if (QString::compare(dirName, profileName, Qt::CaseInsensitive) == 0) {
      profileDir = profileBaseDir.absoluteFilePath(dirName);
      break;
    }
  }

  if (profileDir.isEmpty()) {
    log::error("profile '{}' does not exist", profileName);

    // selected profile doesn't exist. Ensure there is at least one profile,
    // then pick any one
    createDefaultProfile();

    profileDir = profileBaseDir.absoluteFilePath(
        profileBaseDir.entryList(QDir::AllDirs | QDir::NoDotAndDotDot).at(0));

    log::error("picked profile '{}' instead", QDir(profileDir).dirName());

    reportError(tr("The selected profile '%1' does not exist. The profile '%2' will be "
                   "used instead")
                    .arg(profileName)
                    .arg(QDir(profileDir).dirName()));
  }

  // Keep the old profile to emit signal-changed:
  auto oldProfile = std::move(m_CurrentProfile);

  m_CurrentProfile =
      std::make_unique<Profile>(QDir(profileDir), managedGame(), gameFeatures());

  m_ModList.setProfile(m_CurrentProfile.get());

  if (m_CurrentProfile->invalidationActive(nullptr)) {
    m_CurrentProfile->activateInvalidation();
  } else {
    m_CurrentProfile->deactivateInvalidation();
  }

  m_Settings.game().setSelectedProfileName(m_CurrentProfile->name());

  connect(m_CurrentProfile.get(), qOverload<uint>(&Profile::modStatusChanged),
          [this](auto&& index) {
            modStatusChanged(index);
          });
  connect(m_CurrentProfile.get(), qOverload<QList<uint>>(&Profile::modStatusChanged),
          [this](auto&& indexes) {
            modStatusChanged(indexes);
          });
  refreshDirectoryStructure();

  m_CurrentProfile->debugDump();

  emit profileChanged(oldProfile.get(), m_CurrentProfile.get());
  m_ProfileChanged(oldProfile.get(), m_CurrentProfile.get());
}

QStringList OrganizerCore::profileNames() const
{
  QDir const profilesDir(m_Settings.paths().profiles());
  return profilesDir.entryList(QDir::AllDirs | QDir::NoDotAndDotDot);
}

std::shared_ptr<const MOBase::IProfile>
OrganizerCore::getProfile(const QString& profileName) const
{
  QDir profileDir(m_Settings.paths().profiles());
  profileDir.cd(profileName);
  if (!profileDir.exists()) {
    return nullptr;
  }

  return std::make_shared<Profile>(profileDir, managedGame(), gameFeatures());
}

MOBase::IModRepositoryBridge* OrganizerCore::createNexusBridge() const
{
  return new NexusBridge(m_PluginContainer);
}

QString OrganizerCore::profileName() const
{
  if (m_CurrentProfile != nullptr) {
    return m_CurrentProfile->name();
  } else {
    return "";
  }
}

QString OrganizerCore::profilePath() const
{
  if (m_CurrentProfile != nullptr) {
    return m_CurrentProfile->absolutePath();
  } else {
    return "";
  }
}

QString OrganizerCore::downloadsPath() const
{
  return QDir::fromNativeSeparators(m_Settings.paths().downloads());
}

QString OrganizerCore::overwritePath() const
{
  return QDir::fromNativeSeparators(m_Settings.paths().overwrite());
}

QString OrganizerCore::basePath() const
{
  return QDir::fromNativeSeparators(m_Settings.paths().base());
}

QString OrganizerCore::modsPath() const
{
  return QDir::fromNativeSeparators(m_Settings.paths().mods());
}

MOBase::Version OrganizerCore::version() const
{
  return m_Updater.getVersion();
}

MOBase::IPluginGame* OrganizerCore::getGame(const QString& name) const
{
  for (IPluginGame* game : m_PluginContainer->plugins<IPluginGame>()) {
    if (game != nullptr &&
        game->gameShortName().compare(name, Qt::CaseInsensitive) == 0)
      return game;
  }
  return nullptr;
}

MOBase::IModInterface* OrganizerCore::createMod(GuessedValue<QString>& name)
{
  auto result = m_InstallationManager.testOverwrite(name);
  if (!result) {
    return nullptr;
  }

  m_InstallationManager.setModsDirectory(m_Settings.paths().mods());

  QString const targetDirectory =
      QDir::fromNativeSeparators(m_Settings.paths().mods()).append("/").append(name);

  const QString metaPath = targetDirectory + "/meta.ini";
  MetaIniUtils::normalizeMetaIniCase(metaPath);
  QSettings settingsFile(metaPath, QSettings::IniFormat);

  if (!result.merged()) {
    settingsFile.setValue("modid", 0);
    settingsFile.setValue("version", "");
    settingsFile.setValue("newestVersion", "");
    settingsFile.setValue("category", 0);
    settingsFile.setValue("installationFile", "");

    settingsFile.remove("installedFiles");
    settingsFile.beginWriteArray("installedFiles", 0);
    settingsFile.endArray();
  }

  // shouldn't this use the existing mod in case of a merge? also, this does not refresh
  // the indices in the ModInfo structure
  return ModInfo::createFrom(QDir(targetDirectory), *this).data();
}

void OrganizerCore::modDataChanged(MOBase::IModInterface*)
{
  refresh(false);
}

QVariant OrganizerCore::pluginSetting(const QString& pluginName,
                                      const QString& key) const
{
  return m_Settings.plugins().setting(pluginName, key);
}

void OrganizerCore::setPluginSetting(const QString& pluginName, const QString& key,
                                     const QVariant& value)
{
  if (m_PersistenceSuppressed) {
    return;
  }
  m_Settings.plugins().setSetting(pluginName, key, value);
}

QVariant OrganizerCore::persistent(const QString& pluginName, const QString& key,
                                   const QVariant& def) const
{
  return m_Settings.plugins().persistent(pluginName, key, def);
}

void OrganizerCore::setPersistent(const QString& pluginName, const QString& key,
                                  const QVariant& value, bool sync)
{
  if (m_PersistenceSuppressed) {
    return;
  }
  m_Settings.plugins().setPersistent(pluginName, key, value, sync);
}

QString OrganizerCore::pluginDataPath()
{
  // Place plugin data in a writable directory so plugin mkdir() calls never
  // hit a read-only filesystem. Prefer the install-relative plugin_data/ dir
  // (portable installs, or any install whose base dir is user-writable);
  // fall back to a user-writable location when the base dir is read-only
  // (e.g. a system-wide /opt install, or a Flatpak /app with read-only base).
  const QString basePath  = AppConfig::basePath();
  const QString candidate = basePath + "/plugin_data";

  // Use the install-relative dir when it is already writable, or when its
  // parent (basePath) is writable so the plugin can create it on demand.
  if (QFileInfo(candidate).isWritable() || QFileInfo(basePath).isWritable()) {
    return candidate;
  }

  // Base dir is not writable — redirect to a user-writable location, keeping
  // plugin data alongside Fluorine's other runtime data
  // (~/.local/share/fluorine/plugin_data).
  return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
         + "/fluorine/plugin_data";
}

MOBase::IModInterface* OrganizerCore::installMod(const QString& archivePath,
                                                 int priority, bool reinstallation,
                                                 ModInfo::Ptr currentMod,
                                                 const QString& initModName)
{
  return installArchive(archivePath, reinstallation ? -1 : priority, reinstallation,
                        currentMod, initModName)
      .get();
}

std::pair<unsigned int, ModInfo::Ptr>
OrganizerCore::doInstall(const QString& archivePath, GuessedValue<QString> modName,
                         ModInfo::Ptr currentMod, int priority, bool reinstallation)
{
  if (m_CurrentProfile == nullptr) {
    return {-1, nullptr};
  }

  if (m_InstallationManager.isRunning()) {
    QMessageBox::information(qApp->activeWindow(), tr("Installation cancelled"),
                             tr("Another installation is currently in progress."),
                             QMessageBox::Ok);
    return {-1, nullptr};
  }

  bool const hasIniTweaks = false;
  m_CurrentProfile->writeModlistNow();
  m_InstallationManager.setModsDirectory(m_Settings.paths().mods());
  m_InstallationManager.notifyInstallationStart(archivePath, reinstallation,
                                                currentMod);
  auto result = m_InstallationManager.install(archivePath, modName, hasIniTweaks);

  if (result) {
    MessageDialog::showMessage(tr("Installation successful"), qApp->activeWindow());

    // we wait for the directory structure to be ready before notifying the mod list,
    // this prevents issue with third-party plugins, e.g., if the installed mod is
    // activated before the structure is ready
    //
    // we need to fetch modIndex() within the call back because the index is only
    // valid after the call to refresh(), but we do not want to connect after
    // refresh()
    //
    connect(
        this, &OrganizerCore::directoryStructureReady, this,
        [=, this] {
          const int modIndex = ModInfo::getIndex(modName);
          if (modIndex != UINT_MAX) {
            const auto modInfo = ModInfo::getByIndex(modIndex);
            m_ModList.notifyModInstalled(modInfo.get());
          }
        },
        Qt::SingleShotConnection);

    refresh();

    const auto modIndex  = ModInfo::getIndex(modName);
    ModInfo::Ptr modInfo = nullptr;
    if (modIndex != UINT_MAX) {
      modInfo = ModInfo::getByIndex(modIndex);

      if (priority != -1 && !result.mergedOrReplaced()) {
        m_ModList.changeModPriority(modIndex, priority);
      }

      if (hasIniTweaks && m_UserInterface != nullptr &&
          (QMessageBox::question(qApp->activeWindow(), tr("Configure Mod"),
                                 tr("This mod contains ini tweaks. Do you "
                                    "want to configure them now?"),
                                 QMessageBox::Yes | QMessageBox::No) ==
           QMessageBox::Yes)) {
        m_UserInterface->displayModInformation(modInfo, modIndex,
                                               ModInfoTabIDs::IniFiles);
      }

      m_InstallationManager.notifyInstallationEnd(result, modInfo);
    } else {
      reportError(tr("mod not found: %1").arg(qUtf8Printable(modName)));
    }
    emit modInstalled(modName);
    return {modIndex, modInfo};
  } else {
    if (result.result() == MOBase::IPluginInstaller::RESULT_CATEGORYREQUESTED) {
      CategoriesDialog dialog(qApp->activeWindow());

      if (dialog.exec() == QDialog::Accepted) {
        if (dialog.commitChanges()) {
          refresh();
        }
      }
    } else {
      m_InstallationManager.notifyInstallationEnd(result, nullptr);
      if (m_InstallationManager.wasCancelled()) {
        QMessageBox::information(
            qApp->activeWindow(), tr("Extraction cancelled"),
            tr("The installation was cancelled while extracting files. "
               "If this was prior to a FOMOD setup, this warning may be ignored. "
               "However, if this was during installation, the mod will likely be "
               "missing "
               "files."),
            QMessageBox::Ok);
        refresh();
      }
    }
  }

  return {-1, nullptr};
}

ModInfo::Ptr OrganizerCore::installDownload(int index, int priority)
{
  ScopedDisableDirWatcher const scopedDirwatcher(&m_DownloadManager);

  try {
    QString const fileName        = m_DownloadManager.getFilePath(index);
    QString const gameName        = m_DownloadManager.getGameName(index);
    int const modID               = m_DownloadManager.getModID(index);
    int const fileID              = m_DownloadManager.getFileInfo(index)->fileID;
    ModInfo::Ptr currentMod = nullptr;
    GuessedValue<QString> modName;

    // see if there already are mods with the specified mod id
    if (modID > 0) {
      std::vector<ModInfo::Ptr> modInfo = ModInfo::getByModID(gameName, modID);
      for (auto iter = modInfo.begin(); iter != modInfo.end(); ++iter) {
        std::vector<ModInfo::EFlag> flags = (*iter)->getFlags();
        if (std::find(flags.begin(), flags.end(), ModInfo::FLAG_BACKUP) ==
            flags.end()) {
          modName.update((*iter)->name(), GUESS_PRESET);
          currentMod = *iter;
          (*iter)->saveMeta();
        }
      }
    }

    const auto [modIndex, modInfo] =
        doInstall(fileName, modName, currentMod, priority, false);

    if (modInfo != nullptr) {
      modInfo->addInstalledFile(modID, fileID);
      m_DownloadManager.markInstalled(index);
      if (settings().interface().hideDownloadsAfterInstallation()) {
        m_DownloadManager.removeDownload(index, false);
      }
    }

    return modInfo;
  } catch (const std::exception& e) {
    log::error("installDownload exception: type={}, what='{}'",
               typeid(e).name(), e.what());
    reportError(tr("Installation failed: %1").arg(e.what()));
  }

  return nullptr;
}

ModInfo::Ptr OrganizerCore::installArchive(const QString& archivePath, int priority,
                                           bool reinstallation, ModInfo::Ptr currentMod,
                                           const QString& initModName)
{
  GuessedValue<QString> modName;
  if (!initModName.isEmpty()) {
    modName.update(initModName, GUESS_USER);
  }
  const auto [modIndex, modInfo] =
      doInstall(archivePath, modName, currentMod, priority, reinstallation);
  if (m_CurrentProfile == nullptr) {
    return nullptr;
  }

  if (modInfo != nullptr) {
    auto dlIdx = m_DownloadManager.indexByName(QFileInfo(archivePath).fileName());
    if (dlIdx != -1) {
      int const modId  = m_DownloadManager.getModID(dlIdx);
      int const fileId = m_DownloadManager.getFileInfo(dlIdx)->fileID;
      modInfo->addInstalledFile(modId, fileId);
    }
    m_DownloadManager.markInstalled(archivePath);
  }
  return modInfo;
}

QString OrganizerCore::resolvePath(const QString& fileName) const
{
  if (m_DirectoryStructure == nullptr) {
    return {};
  }
  const FileEntryPtr file =
      m_DirectoryStructure->searchFile(ToWString(fileName), nullptr);
  if (file.get() != nullptr) {
    return ToQString(file->getFullPath());
  } else {
    return {};
  }
}

QStringList OrganizerCore::listDirectories(const QString& directoryName) const
{
  QStringList result;
  DirectoryEntry* dir = m_DirectoryStructure;
  if (!directoryName.isEmpty())
    dir = dir->findSubDirectoryRecursive(ToWString(directoryName));
  if (dir != nullptr) {
    for (const auto& d : dir->getSubDirectories()) {
      result.append(ToQString(d->getName()));
    }
  }
  return result;
}

QStringList
OrganizerCore::findFiles(const QString& path,
                         const std::function<bool(const QString&)>& filter) const
{
  QStringList result;
  DirectoryEntry* dir = m_DirectoryStructure;
  if (!path.isEmpty() && path != ".")
    dir = dir->findSubDirectoryRecursive(ToWString(path));
  if (dir != nullptr) {
    std::vector<FileEntryPtr> const files = dir->getFiles();
    for (FileEntryPtr const& file : files) {
      QString const fullPath = ToQString(file->getFullPath());
      if (filter(ToQString(file->getName()))) {
        result.append(fullPath);
      }
    }
  }
  return result;
}

QStringList OrganizerCore::getFileOrigins(const QString& fileName) const
{
  QStringList result;
  const FileEntryPtr file =
      m_DirectoryStructure->searchFile(ToWString(fileName), nullptr);

  if (file.get() != nullptr) {
    result.append(
        ToQString(m_DirectoryStructure->getOriginByID(file->getOrigin()).getName()));
    foreach (const auto& i, file->getAlternatives()) {
      result.append(
          ToQString(m_DirectoryStructure->getOriginByID(i.originID()).getName()));
    }
  }
  return result;
}

QList<MOBase::IOrganizer::FileInfo> OrganizerCore::findFileInfos(
    const QString& path,
    const std::function<bool(const MOBase::IOrganizer::FileInfo&)>& filter) const
{
  QList<IOrganizer::FileInfo> result;
  DirectoryEntry* dir = m_DirectoryStructure;
  if (!path.isEmpty() && path != ".")
    dir = dir->findSubDirectoryRecursive(ToWString(path));
  if (dir != nullptr) {
    std::vector<FileEntryPtr> const files = dir->getFiles();
    for (const FileEntryPtr& file : files) {
      IOrganizer::FileInfo info;
      info.filePath    = ToQString(file->getFullPath());
      bool fromArchive = false;
      info.origins.append(ToQString(
          m_DirectoryStructure->getOriginByID(file->getOrigin(fromArchive)).getName()));
      info.archive = fromArchive ? ToQString(file->getArchive().name()) : "";
      for (const auto& idx : file->getAlternatives()) {
        info.origins.append(
            ToQString(m_DirectoryStructure->getOriginByID(idx.originID()).getName()));
      }

      if (filter(info)) {
        result.append(info);
      }
    }
  }
  return result;
}

DownloadManager* OrganizerCore::downloadManager()
{
  return &m_DownloadManager;
}

PluginList* OrganizerCore::pluginList()
{
  return &m_PluginList;
}

ModList* OrganizerCore::modList()
{
  return &m_ModList;
}

bool OrganizerCore::previewFileWithAlternatives(QWidget* parent, QString fileName,
                                                int selectedOrigin)
{
  fileName = QDir::fromNativeSeparators(fileName);

  // what we have is an absolute path to the file in its actual location (for the
  // primary origin) what we want is the path relative to the virtual data directory

  // we need to look in the virtual directory for the file to make sure the info is up
  // to date.

  // check if the file comes from the actual data folder instead of a mod
  QDir const gameDirectory   = managedGame()->dataDirectory().absolutePath();
  QString const relativePath = gameDirectory.relativeFilePath(fileName);
  QDir const dirRelativePath = gameDirectory.relativeFilePath(fileName);

  // if the file is on a different drive the dirRelativePath will actually be an
  // absolute path so we make sure that is not the case
  if (!dirRelativePath.isAbsolute() && !relativePath.startsWith("..")) {
    fileName = relativePath;
  } else {
    // crude: we search for the next slash after the base mod directory to skip
    // everything up to the data-relative directory
    int offset = settings().paths().mods().size() + 1;
    offset     = fileName.indexOf("/", offset);
    fileName   = fileName.mid(offset + 1);
  }

  const FileEntryPtr file =
      directoryStructure()->searchFile(ToWString(fileName), nullptr);

  if (file.get() == nullptr) {
    reportError(tr("file not found: %1").arg(qUtf8Printable(fileName)));
    return false;
  }

  // Standalone top-level window (no QObject parent) to decouple from the
  // enclosing ModInfoDialog stack frame. ApplicationModal stacks it on
  // top of any already-open modal dialog. WA_DeleteOnClose cleans up.
  auto* preview = new PreviewDialog(fileName, nullptr);
  preview->setAttribute(Qt::WA_DeleteOnClose);
  preview->setWindowModality(Qt::ApplicationModal);
  (void)parent;

  auto addFunc = [&](int originId, std::wstring archiveName = L"") {
    FilesOrigin const& origin = directoryStructure()->getOriginByID(originId);
    QString const filePath =
        QDir::fromNativeSeparators(ToQString(origin.getPath())) + "/" + fileName;
    if (QFile::exists(filePath)) {
      // it's very possible the file doesn't exist, because it's inside an archive. we
      // don't support that
      QWidget* wid = m_PluginContainer->previewGenerator().genPreview(filePath);
      if (wid == nullptr) {
        reportError(tr("failed to generate preview for %1").arg(filePath));
      } else {
        preview->addVariant(ToQString(origin.getName()), wid);
      }
    } else if (!archiveName.empty()) {
      auto archiveFile = directoryStructure()->searchFile(archiveName);
      if (archiveFile.get() != nullptr) {
        try {
          libbsarch::bs_archive archiveLoader;
          archiveLoader.load_from_disk(archiveFile->getFullPath());
          libbsarch::memory_blob const fileData =
              archiveLoader.extract_to_memory(fileName.toStdWString());
          QByteArray const convertedFileData((char*)(fileData.data), fileData.size);
          QWidget* wid = m_PluginContainer->previewGenerator().genArchivePreview(
              convertedFileData, filePath);
          if (wid == nullptr) {
            reportError(tr("failed to generate preview for %1").arg(filePath));
          } else {
            preview->addVariant(ToQString(origin.getName()), wid);
          }
        } catch (std::exception& e) {
        }
      }
    }
  };

  if (selectedOrigin == -1) {
    // don't bother with the vector of origins, just add them as they come
    addFunc(file->getOrigin(), file->isFromArchive() ? file->getArchive().name() : L"");
    for (const auto& alt : file->getAlternatives()) {
      addFunc(alt.originID(), alt.isFromArchive() ? alt.archive().name() : L"");
    }
  } else {
    std::vector<int> origins;

    // start with the primary origin
    origins.push_back(file->getOrigin());

    // add other origins, push to front if it's the selected one
    for (const auto& alt : file->getAlternatives()) {
      if (alt.originID() == selectedOrigin) {
        origins.insert(origins.begin(), alt.originID());
      } else {
        origins.push_back(alt.originID());
      }
    }

    // can't be empty; either the primary origin was the selected one, or it
    // was one of the alternatives, which got inserted in front

    if (origins[0] != selectedOrigin) {
      // sanity check, this shouldn't happen unless the caller passed an
      // incorrect id

      log::warn("selected preview origin {} not found in list of alternatives",
                selectedOrigin);
    }

    for (int const id : origins) {
      addFunc(id);
    }
  }

  if (preview->numVariants() > 0) {
    preview->show();
    preview->raise();
    preview->activateWindow();
    return true;
  } else {
    delete preview;
    QMessageBox::information(parent, tr("Sorry"),
                             tr("Sorry, can't preview anything. This function "
                                "currently does not support extracting from bsas."));

    return false;
  }
}

bool OrganizerCore::previewFile(QWidget* parent, const QString& originName,
                                const QString& path)
{
  if (!QFile::exists(path)) {
    reportError(tr("File '%1' not found.").arg(path));
    return false;
  }

  QWidget* wid = m_PluginContainer->previewGenerator().genPreview(path);
  if (wid == nullptr) {
    reportError(tr("Failed to generate preview for %1").arg(path));
    return false;
  }

  // Standalone top-level window — no QObject parent so lifetime is
  // decoupled from the enclosing ModInfoDialog (which is stack-allocated
  // in modlistviewactions.cpp, so passing &dialog as parent makes preview
  // a child of a stack object). ApplicationModal makes preview stack on
  // top of the outer modal ModInfoDialog — newest modal wins. Without
  // modality, ModInfoDialog blocks all input on the preview.
  // WA_DeleteOnClose handles cleanup when user closes it.
  auto* preview = new PreviewDialog(path, nullptr);
  preview->setAttribute(Qt::WA_DeleteOnClose);
  preview->setWindowModality(Qt::ApplicationModal);
  preview->addVariant(originName, wid);
  preview->show();
  preview->raise();
  preview->activateWindow();
  (void)parent;

  return true;
}

bool OrganizerCore::previewFileData(QWidget* parent, const QString& fileName,
                                    const QByteArray& fileData)
{
  if (fileData.isEmpty()) {
    return false;
  }

  const QString ext = QFileInfo(fileName).suffix().toLower();
  if (!m_PluginContainer->previewGenerator().previewSupported(ext, true)) {
    return false;
  }

  QWidget* wid =
      m_PluginContainer->previewGenerator().genArchivePreview(fileData, fileName);
  if (wid == nullptr) {
    return false;
  }

  // Use QDialog::open() instead of exec(). open() is async window-modal —
  // it shows the dialog modal to its parent but returns immediately without
  // starting a nested QEventLoop. Nesting exec() inside a caller that's
  // itself running in QDialog::exec() (mod info filetree → preview_bsa file
  // tree → nif preview) has been causing event-loop unwinding to softlock
  // on close. WA_DeleteOnClose cleans up automatically.
  auto* preview = new PreviewDialog(fileName, parent);
  preview->setAttribute(Qt::WA_DeleteOnClose);
  preview->addVariant(QFileInfo(fileName).fileName(), wid);
  preview->open();
  return true;
}

boost::signals2::connection OrganizerCore::onAboutToRun(
    const std::function<bool(const QString&, const QDir&, const QString&)>& func)
{
  return m_AboutToRun.connect(func);
}

boost::signals2::connection OrganizerCore::onFinishedRun(
    const std::function<void(const QString&, unsigned int)>& func)
{
  return m_FinishedRun.connect(func);
}

boost::signals2::connection
OrganizerCore::onUserInterfaceInitialized(std::function<void(QMainWindow*)> const& func)
{
  return m_UserInterfaceInitialized.connect(func);
}

boost::signals2::connection
OrganizerCore::onProfileCreated(std::function<void(MOBase::IProfile*)> const& func)
{
  return m_ProfileCreated.connect(func);
}

boost::signals2::connection OrganizerCore::onProfileRenamed(
    std::function<void(MOBase::IProfile*, QString const&, QString const&)> const& func)
{
  return m_ProfileRenamed.connect(func);
}

boost::signals2::connection
OrganizerCore::onProfileRemoved(std::function<void(QString const&)> const& func)
{
  return m_ProfileRemoved.connect(func);
}

boost::signals2::connection
OrganizerCore::onProfileChanged(std::function<void(IProfile*, IProfile*)> const& func)
{
  return m_ProfileChanged.connect(func);
}

boost::signals2::connection OrganizerCore::onPluginSettingChanged(
    std::function<void(QString const&, const QString& key, const QVariant&,
                       const QVariant&)> const& func)
{
  return m_PluginSettingChanged.connect(func);
}

boost::signals2::connection
OrganizerCore::onPluginEnabled(std::function<void(const IPlugin*)> const& func)
{
  return m_PluginEnabled.connect(func);
}

boost::signals2::connection
OrganizerCore::onPluginDisabled(std::function<void(const IPlugin*)> const& func)
{
  return m_PluginDisabled.connect(func);
}

boost::signals2::connection
OrganizerCore::onNextRefresh(std::function<void()> const& func,
                             RefreshCallbackGroup group, RefreshCallbackMode mode)
{
  if (m_PersistenceSuppressed) {
    return {};
  }

  if (m_DirectoryUpdate || mode == RefreshCallbackMode::FORCE_WAIT_FOR_REFRESH) {
    return m_OnNextRefreshCallbacks.connect(static_cast<int>(group), func);
  } else {
    func();
    return {};
  }
}

void OrganizerCore::refresh(bool saveChanges)
{
  // don't lose changes!
  if (saveChanges) {
    m_CurrentProfile->writeModlistNow(true);
  }

  updateModInfoFromDisc();
  m_CurrentProfile->refreshModStatus();

  m_ModList.notifyChange(-1);

  refreshDirectoryStructure();

  emit refreshTriggered();
}

void OrganizerCore::refreshESPList(bool force)
{
  onNextRefresh(
      [this, force] {
        TimeThis const tt("OrganizerCore::refreshESPList()");
        try {
          m_CurrentProfile->writeModlist();

          // clear list
          m_PluginList.refresh(m_CurrentProfile->name(), *m_DirectoryStructure,
                               m_CurrentProfile->getLockedOrderFileName(), force);
        } catch (const std::exception& e) {
          reportError(tr("Failed to refresh list of esps: %1").arg(e.what()));
        } catch (...) {
          reportError(tr("Failed to refresh list of esps: unknown error"));
        }
      },
      RefreshCallbackGroup::CORE, RefreshCallbackMode::RUN_NOW_IF_POSSIBLE);
}

void OrganizerCore::refreshBSAList()
{
  TimeThis const tt("OrganizerCore::refreshBSAList()");

  auto archives = gameFeatures().gameFeature<DataArchives>();

  if (archives != nullptr) {
    m_ArchivesInit = false;

    // default archives are the ones enabled outside MO. if the list can't be
    // found (which might
    // happen if ini files are missing) use hard-coded defaults (preferrably the
    // same the game would use)
    m_DefaultArchives = archives->archives(m_CurrentProfile.get());
    if (m_DefaultArchives.empty()) {
      m_DefaultArchives = archives->vanillaArchives();
    }

    m_ActiveArchives.clear();

    auto iter        = enabledArchives();
    m_ActiveArchives = toStringList(iter.begin(), iter.end());
    if (m_ActiveArchives.isEmpty()) {
      m_ActiveArchives = m_DefaultArchives;
    }

    if (m_UserInterface != nullptr) {
      m_UserInterface->updateBSAList(m_DefaultArchives, m_ActiveArchives);
      m_UserInterface->archivesWriter().write();
    }

    m_ArchivesInit = true;
  }
}

void OrganizerCore::refreshLists()
{
  if ((m_CurrentProfile != nullptr) && m_DirectoryStructure->isPopulated()) {
    refreshESPList(true);
    refreshBSAList();
  }  // no point in refreshing lists if no files have been added to the directory
     // tree
}

void OrganizerCore::updateModActiveState(int index, bool active)
{
  QList<unsigned int> modsToUpdate;
  modsToUpdate.append(index);
  updateModsActiveState(modsToUpdate, active);
}

void OrganizerCore::updateModsActiveState(const QList<unsigned int>& modIndices,
                                          bool active)
{
  if (!managedGame()->genericPluginStateFollowsModState()) {
    return;
  }

  int enabled = 0;
  // Use the game's own plugin extensions instead of a hardcoded esm/esl/esp
  // set, so games with their own formats (OpenMW: .omwaddon/.omwgame/
  // .omwscripts) get their plugins toggled along with the mod. Matching is
  // done manually and case-insensitively: QDir name filters are case-sensitive
  // on Linux, which silently skipped e.g. "Foo.ESP".
  QStringList pluginExtensions;
  for (const QString& ext : managedGame()->pluginFileExtensions()) {
    pluginExtensions << ext.toLower();
  }
  for (auto index : modIndices) {
    ModInfo::Ptr const modInfo = ModInfo::getByIndex(index);
    QDir const dir(modInfo->absolutePath());
    for (const QString& fileName : dir.entryList(QDir::Files)) {
      QString matchedExt;
      for (const QString& ext : pluginExtensions) {
        if (fileName.endsWith("." + ext, Qt::CaseInsensitive)) {
          matchedExt = ext;
          break;
        }
      }
      if (matchedExt.isEmpty()) {
        continue;
      }

      const FileEntryPtr file = m_DirectoryStructure->findFile(ToWString(fileName));
      if (file.get() == nullptr) {
        log::warn("failed to activate {}", fileName);
        continue;
      }

      if (active != m_PluginList.isEnabled(fileName) &&
          file->getAlternatives().empty()) {
        m_PluginList.blockSignals(true);
        m_PluginList.enableESP(fileName, active);
        m_PluginList.blockSignals(false);
        // masters don't count towards the "multiple plugins activated" warning
        // (same exemption the old code gave .esm files)
        if (matchedExt != "esm" && matchedExt != "omwgame") {
          ++enabled;
        }
      }
    }
  }
  if (active && (enabled > 1)) {
    MessageDialog::showMessage(
        tr("Multiple esps/esls activated, please check that they don't conflict."),
        qApp->activeWindow());
  }
  m_PluginList.refreshLoadOrder();
  // immediately save affected lists
  m_PluginListsWriter.writeImmediately(false);
}

void OrganizerCore::updateModInDirectoryStructure(unsigned int index,
                                                  ModInfo::Ptr modInfo)
{
  QMap<unsigned int, ModInfo::Ptr> allModInfo;
  allModInfo[index] = modInfo;
  updateModsInDirectoryStructure(allModInfo);
}

void OrganizerCore::updateModsInDirectoryStructure(
    QMap<unsigned int, ModInfo::Ptr> modInfo)
{
  std::vector<DirectoryRefresher::EntryInfo> entries;

  for (auto idx : modInfo.keys()) {
    QString path       = modInfo[idx]->absolutePath();
    QString const modDataDir = managedGame()->modDataDirectory();
    path               = modDataDir.isEmpty() ? path : path + "/" + modDataDir;
    entries.push_back({modInfo[idx]->name(),
                       path,
                       modInfo[idx]->stealFiles(),
                       {},
                       m_CurrentProfile->getModPriority(idx)});
  }

  m_DirectoryRefresher->addMultipleModsFilesToStructure(m_DirectoryStructure, entries);

  DirectoryRefresher::cleanStructure(m_DirectoryStructure);
  // need to refresh plugin list now so we can activate esps
  refreshESPList(true);
  // activate all esps of the specified mod so the bsas get activated along with
  // it
  m_PluginList.blockSignals(true);
  updateModsActiveState(modInfo.keys(), true);
  m_PluginList.blockSignals(false);
  // now we need to refresh the bsa list and save it so there is no confusion
  // about what archives are available and active
  refreshBSAList();
  if (m_UserInterface != nullptr) {
    m_UserInterface->archivesWriter().writeImmediately(false);
  }

  std::vector<QString> archives = enabledArchives();
  m_DirectoryRefresher->setMods(m_CurrentProfile->getActiveMods(),
                                std::set<QString>(archives.begin(), archives.end()));

  // finally also add files from bsas to the directory structure
  for (auto idx : modInfo.keys()) {
    QString path       = modInfo[idx]->absolutePath();
    QString const modDataDir = managedGame()->modDataDirectory();
    path               = modDataDir.isEmpty() ? path : path + "/" + modDataDir;
    m_DirectoryRefresher->addModBSAToStructure(
        m_DirectoryStructure, modInfo[idx]->name(),
        m_CurrentProfile->getModPriority(idx), path, modInfo[idx]->archives());
  }
}

void OrganizerCore::loggedInAction(QWidget* parent, std::function<void()> f)
{
  if (NexusInterface::instance().getAccessManager()->validated()) {
    f();
  } else if (!m_Settings.network().offlineMode()) {
    NexusOAuthTokens tokens;
    if (GlobalSettings::nexusOAuthTokens(tokens)) {
      doAfterLogin([f] {
        f();
      });
      NexusInterface::instance().getAccessManager()->apiCheck(tokens);
    } else {
      MessageDialog::showMessage(tr("You need to be logged in with Nexus"), parent);
    }
  }
}

void OrganizerCore::requestDownload(const QUrl& url, QNetworkReply* reply)
{
  if (!m_PluginContainer) {
    return;
  }
  for (IPluginModPage* modPage : m_PluginContainer->plugins<MOBase::IPluginModPage>()) {
    if (m_PluginContainer->isEnabled(modPage)) {
      ModRepositoryFileInfo* fileInfo = new ModRepositoryFileInfo();
      if (modPage->handlesDownload(url, reply->url(), *fileInfo)) {
        fileInfo->repository = modPage->name();
        m_DownloadManager.addDownload(reply, fileInfo);
        return;
      }
    }
  }

  // no mod found that could handle the download. Is it a nexus mod?
  if (url.host() == "www.nexusmods.com") {
    QString gameName = "";
    int modID        = 0;
    int fileID       = 0;
    QRegularExpression const nameExp(R"(www\.nexusmods\.com/(\a+)/)");
    auto match = nameExp.match(url.toString());
    if (match.hasMatch()) {
      gameName = match.captured(1);
    }
    QRegularExpression const modExp("mods/(\\d+)");
    match = modExp.match(url.toString());
    if (match.hasMatch()) {
      modID = match.captured(1).toInt();
    }
    QRegularExpression const fileExp("fid=(\\d+)");
    match = fileExp.match(url.toString());
    if (match.hasMatch()) {
      fileID = match.captured(1).toInt();
    }
    m_DownloadManager.addDownload(reply,
                                  new ModRepositoryFileInfo(gameName, modID, fileID));
  } else {
    if (QMessageBox::question(qApp->activeWindow(), tr("Download?"),
                              tr("A download has been started but no installed "
                                 "page plugin recognizes it.\n"
                                 "If you download anyway no information (i.e. "
                                 "version) will be associated with the "
                                 "download.\n"
                                 "Continue?"),
                              QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
      m_DownloadManager.addDownload(reply, new ModRepositoryFileInfo());
    }
  }
}

PluginContainer& OrganizerCore::pluginContainer() const
{
  return *m_PluginContainer;
}

GameFeatures& OrganizerCore::gameFeatures() const
{
  return pluginContainer().gameFeatures();
}

IPluginGame const* OrganizerCore::managedGame() const
{
  return m_GamePlugin;
}

IOrganizer const* OrganizerCore::managedGameOrganizer() const
{
  return m_PluginContainer->requirements(m_GamePlugin).m_Organizer;
}

std::vector<QString> OrganizerCore::enabledArchives()
{
  std::vector<QString> result;
  if (settings().archiveParsing()) {
    QFile archiveFile(m_CurrentProfile->getArchivesFileName());
    if (archiveFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
      const QByteArray contents = archiveFile.readAll();
      archiveFile.close();
      const QStringList lines =
          QString::fromUtf8(contents).split('\n', Qt::SkipEmptyParts);
      result.reserve(lines.size());
      for (const QString& line : lines) {
        result.emplace_back(line.trimmed());
      }
    }
  }
  return result;
}

void OrganizerCore::refreshDirectoryStructure()
{
  if (m_DirectoryUpdate) {
    log::debug("can't refresh, already in progress");
    return;
  }

  log::debug("refreshing structure");
  m_DirectoryUpdate = true;
  m_ActiveDirectoryRefreshGeneration = ++m_NextDirectoryRefreshGeneration;

  m_CurrentProfile->writeModlistNow(true);
  const auto activeModList = m_CurrentProfile->getActiveMods();
  const auto archives      = enabledArchives();

  m_DirectoryRefresher->setMods(activeModList,
                                std::set<QString>(archives.begin(), archives.end()));

  // runs refresh() in a thread
  QTimer::singleShot(0, m_DirectoryRefresher.get(), &DirectoryRefresher::refresh);
}

void OrganizerCore::onDirectoryRefreshed()
{
  const auto completedGeneration = m_ActiveDirectoryRefreshGeneration;
  log::debug("directory refreshed, finishing up");
  TimeThis const tt("OrganizerCore::onDirectoryRefreshed()");

  DirectoryEntry* newStructure = m_DirectoryRefresher->stealDirectoryStructure();
  Q_ASSERT(newStructure != m_DirectoryStructure);

  if (newStructure == nullptr) {
    // TODO: don't know why this happens, this slot seems to get called twice
    // with only one emit
    return;
  }

  std::swap(m_DirectoryStructure, newStructure);
  m_VirtualFileTree.invalidate();

  if (m_StructureDeleter.joinable()) {
    m_StructureDeleter.join();
  }

  m_StructureDeleter = MOShared::startSafeThread([=] {
    log::debug("structure deleter thread start");
    delete newStructure;
    log::debug("structure deleter thread done");
  });

  log::debug("clearing caches");
  for (int i = 0; i < m_ModList.rowCount(); ++i) {
    ModInfo::Ptr const modInfo = ModInfo::getByIndex(i);
    modInfo->clearCaches();
  }

  // needs to be done before post refresh tasks
  m_DirectoryUpdate = false;

  if (m_PersistenceSuppressed) {
    // Retire internal generation state and mandatory application-refresh
    // waiters, but never dispatch queued plugin/core callbacks or run list
    // refreshes after fail-stop admission has closed.
    m_OnNextRefreshCallbacks.disconnect_all_slots();
    completeAllAfterRunRefreshForFailStop();
    emit directoryRefreshRetired();
    log::debug("directory refresh retired during fail-stop");
    return;
  }

  log::debug("running post refresh tasks");
  m_OnNextRefreshCallbacks();
  m_OnNextRefreshCallbacks.disconnect_all_slots();

  if (m_CurrentProfile != nullptr) {
    log::debug("refreshing lists");
    refreshLists();
  }

  // Complete only requests assigned to the generation that just finished.
  // A request made while this generation was active is deliberately queued
  // for a subsequent refresh so it cannot be satisfied by stale work.
  // Start that subsequent generation before releasing waiters for this one;
  // a new launch awakened by the signal will then see m_DirectoryUpdate and
  // wait for the queued post-run refresh as well.
  schedulePendingAfterRunRefresh();
  completeAfterRunRefresh(completedGeneration);

  emit directoryRefreshRetired();
  emit directoryStructureReady();

  log::debug("refresh done");
}

void OrganizerCore::clearCaches(std::vector<unsigned int> const& indices) const
{
  const auto insert = [](auto& dest, const auto& from) {
    dest.insert(from.begin(), from.end());
  };
  std::set<unsigned int> allIndices;
  for (const auto index : indices) {
    ModInfo::Ptr const modInfo = ModInfo::getByIndex(index);

    if (m_CurrentProfile->modEnabled(index)) {
      // if the mod is enabled, we need to first clear its cache so that
      // getModOverwrite(), ..., returns the newly conflicting mods (in case
      // the mod just got enabled)
      modInfo->clearCaches();
      insert(allIndices, modInfo->getModOverwrite());
      insert(allIndices, modInfo->getModOverwritten());
      insert(allIndices, modInfo->getModArchiveOverwrite());
      insert(allIndices, modInfo->getModArchiveOverwritten());
      insert(allIndices, modInfo->getModArchiveLooseOverwrite());
      insert(allIndices, modInfo->getModArchiveLooseOverwritten());
    } else {
      // if the mod is disabled, we need to first fetch the conflicting
      // mods, and then clear the cache
      insert(allIndices, modInfo->getModOverwrite());
      insert(allIndices, modInfo->getModOverwritten());
      insert(allIndices, modInfo->getModArchiveOverwrite());
      insert(allIndices, modInfo->getModArchiveOverwritten());
      insert(allIndices, modInfo->getModArchiveLooseOverwrite());
      insert(allIndices, modInfo->getModArchiveLooseOverwritten());
      modInfo->clearCaches();
    }
  }

  for (const auto& index : allIndices) {
    ModInfo::getByIndex(index)->clearCaches();
  }
}

void OrganizerCore::modPrioritiesChanged(const QModelIndexList& indices)
{
  for (unsigned int i = 0; i < currentProfile()->numMods(); ++i) {
    int const priority = currentProfile()->getModPriority(i);
    if (currentProfile()->modEnabled(i)) {
      ModInfo::Ptr const modInfo = ModInfo::getByIndex(i);
      const auto name = MOBase::ToWString(modInfo->internalName());
      // priorities in the directory structure are one higher because data is 0
      if (directoryStructure()->originExists(name)) {
        directoryStructure()->getOriginByName(name).setPriority(priority + 1);
      }
    }
  }
  refreshBSAList();
  currentProfile()->writeModlist();
  directoryStructure()->getFileRegister()->sortOrigins();

  std::vector<unsigned int> vindices;

  for (const auto& idx : indices) {
    vindices.push_back(idx.data(ModList::IndexRole).toInt());
  }

  clearCaches(vindices);
}

void OrganizerCore::modStatusChanged(unsigned int index)
{
  try {
    ModInfo::Ptr const modInfo = ModInfo::getByIndex(index);
    if (m_CurrentProfile->modEnabled(index)) {
      updateModInDirectoryStructure(index, modInfo);
    } else {
      updateModActiveState(index, false);
      if (m_DirectoryStructure->originExists(ToWString(modInfo->name()))) {
        FilesOrigin& origin =
            m_DirectoryStructure->getOriginByName(ToWString(modInfo->name()));
        origin.enable(false);
      }
      if (m_UserInterface != nullptr) {
        m_UserInterface->archivesWriter().write();
      }
    }

    for (unsigned int i = 0; i < m_CurrentProfile->numMods(); ++i) {
      ModInfo::Ptr const modInfo = ModInfo::getByIndex(i);
      int const priority         = m_CurrentProfile->getModPriority(i);
      if (m_DirectoryStructure->originExists(ToWString(modInfo->name()))) {
        // priorities in the directory structure are one higher because data is
        // 0
        m_DirectoryStructure->getOriginByName(ToWString(modInfo->name()))
            .setPriority(priority + 1);
      }
    }
    m_DirectoryStructure->getFileRegister()->sortOrigins();

    refreshLists();
    clearCaches({index});
    m_ModList.notifyModStateChanged({index});

  } catch (const std::exception& e) {
    reportError(tr("failed to update mod list: %1").arg(e.what()));
  }
}

void OrganizerCore::modStatusChanged(QList<unsigned int> index)
{
  try {
    QMap<unsigned int, ModInfo::Ptr> modsToEnable;
    QMap<unsigned int, ModInfo::Ptr> modsToDisable;
    std::vector<unsigned int> vindices;
    for (auto idx : index) {
      if (m_CurrentProfile->modEnabled(idx)) {
        modsToEnable[idx] = ModInfo::getByIndex(idx);
      } else {
        modsToDisable[idx] = ModInfo::getByIndex(idx);
      }
      vindices.push_back(idx);
    }
    if (!modsToEnable.isEmpty()) {
      updateModsInDirectoryStructure(modsToEnable);
    }
    if (!modsToDisable.isEmpty()) {
      updateModsActiveState(modsToDisable.keys(), false);
      for (auto idx : modsToDisable.keys()) {
        if (m_DirectoryStructure->originExists(ToWString(modsToDisable[idx]->name()))) {
          FilesOrigin& origin = m_DirectoryStructure->getOriginByName(
              ToWString(modsToDisable[idx]->name()));
          origin.enable(false);
        }
      }
      if (m_UserInterface != nullptr) {
        m_UserInterface->archivesWriter().write();
      }
    }

    for (unsigned int i = 0; i < m_CurrentProfile->numMods(); ++i) {
      ModInfo::Ptr const modInfo = ModInfo::getByIndex(i);
      int const priority         = m_CurrentProfile->getModPriority(i);
      if (m_DirectoryStructure->originExists(ToWString(modInfo->name()))) {
        // priorities in the directory structure are one higher because data is
        // 0
        m_DirectoryStructure->getOriginByName(ToWString(modInfo->name()))
            .setPriority(priority + 1);
      }
    }
    m_DirectoryStructure->getFileRegister()->sortOrigins();

    refreshLists();
    clearCaches(vindices);
    m_ModList.notifyModStateChanged(index);

  } catch (const std::exception& e) {
    reportError(tr("failed to update mod list: %1").arg(e.what()));
  }
}

void OrganizerCore::loginSuccessful(bool necessary)
{
  if (necessary) {
    MessageDialog::showMessage(tr("login successful"), qApp->activeWindow());
  }
  for (const QString& url : m_PendingDownloads) {
    downloadRequestedNXM(url);
  }
  m_PendingDownloads.clear();
  for (const auto& task : m_PostLoginTasks) {
    task();
  }

  m_PostLoginTasks.clear();
  NexusInterface::instance().loginCompleted();
}

void OrganizerCore::loginSuccessfulUpdate(bool necessary)
{
  if (necessary) {
    MessageDialog::showMessage(tr("login successful"), qApp->activeWindow());
  }
  m_Updater.startUpdate();
}

void OrganizerCore::loginFailed(const QString& message)
{
  qCritical().nospace().noquote() << "Nexus API validation failed: " << message;

  if (QMessageBox::question(qApp->activeWindow(), tr("Login failed"),
                            tr("Login failed, try again?")) == QMessageBox::Yes) {
    if (nexusApi(true)) {
      return;
    }
  }

  if (!m_PendingDownloads.isEmpty()) {
    MessageDialog::showMessage(
        tr("login failed: %1. Download will not be associated with an account")
            .arg(message),
        qApp->activeWindow());
    for (const QString& url : m_PendingDownloads) {
      downloadRequestedNXM(url);
    }
    m_PendingDownloads.clear();
  } else {
    MessageDialog::showMessage(tr("login failed: %1").arg(message),
                               qApp->activeWindow());
    m_PostLoginTasks.clear();
  }
  NexusInterface::instance().loginCompleted();
}

void OrganizerCore::loginFailedUpdate(const QString& message)
{
  MessageDialog::showMessage(
      tr("login failed: %1. You need to log-in with Nexus to update MO.").arg(message),
      qApp->activeWindow());
}

void OrganizerCore::syncOverwrite()
{
  ModInfo::Ptr const modInfo = ModInfo::getOverwrite();

  // Snapshot overwrite before sync so we can detect what was moved.
  QStringList beforeFiles;
  {
    QDirIterator it(modInfo->absolutePath(), QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
      it.next();
      beforeFiles << QDir(modInfo->absolutePath()).relativeFilePath(it.filePath());
    }
  }

  SyncOverwriteDialog syncDialog(modInfo->absolutePath(), m_DirectoryStructure,
                                 qApp->activeWindow());
  if (syncDialog.exec() == QDialog::Accepted) {
    syncDialog.apply(QDir::fromNativeSeparators(m_Settings.paths().mods()));

    // Track files that were moved out of overwrite to mods. Files that existed
    // before but are gone now were synced to a mod. Use profile priority order
    // (highest-priority mod wins) so writes go to the correct mod when
    // multiple mods contain the same path.
    const QString modsDir = QDir::fromNativeSeparators(m_Settings.paths().mods());

    // Mod names in ascending priority order (last = highest priority).
    const QStringList modsByPriority =
        m_ModList.allModsByProfilePriority(m_CurrentProfile.get());

    for (const auto& relPath : beforeFiles) {
      const QString owFile = modInfo->absolutePath() + "/" + relPath;
      if (!QFile::exists(owFile)) {
        QString bestModPath;
        for (const auto& modName : modsByPriority) {
          const QString modPath = modsDir + "/" + modName;
          if (QFile::exists(modPath + "/" + relPath)) {
            bestModPath = modPath;
            // Don't break — keep going to find the highest-priority match.
          }
        }
        if (!bestModPath.isEmpty()) {
          trackOverwriteMove(relPath, bestModPath);
        }
      }
    }

    modInfo->diskContentModified();
    refreshDirectoryStructure();
  }
}

QString OrganizerCore::oldMO1HookDll() const
{
  if (auto extender = gameFeatures().gameFeature<ScriptExtender>()) {
    QString hookdll =
        QDir::toNativeSeparators(managedGame()->dataDirectory().absoluteFilePath(
            extender->PluginPath() + "/hook.dll"));
    if (QFile(hookdll).exists())
      return hookdll;
  }
  return {};
}

std::vector<unsigned int> OrganizerCore::activeProblems() const
{
  std::vector<unsigned int> problems;
  const auto& hookdll = oldMO1HookDll();
  if (!hookdll.isEmpty()) {
    // This warning will now be shown every time the problems are checked, which is a
    // bit of a "log spam". But since this is a sever error which will most likely make
    // the game crash/freeze/etc. and is very hard to diagnose,  this "log spam" will
    // make it easier for the user to notice the warning.
    log::warn("hook.dll found in game folder: {}", hookdll);
    problems.push_back(PROBLEM_MO1SCRIPTEXTENDERWORKAROUND);
  }
  return problems;
}

QString OrganizerCore::shortDescription(unsigned int key) const
{
  switch (key) {
  case PROBLEM_MO1SCRIPTEXTENDERWORKAROUND: {
    return tr(
        "MO1 \"Script Extender\" load mechanism has left hook.dll in your game folder");
  } break;
  default: {
    return tr("Description missing");
  } break;
  }
}

QString OrganizerCore::fullDescription(unsigned int key) const
{
  switch (key) {
  case PROBLEM_MO1SCRIPTEXTENDERWORKAROUND: {
    return tr("<a href=\"%1\">hook.dll</a> has been found in your game folder (right "
              "click to copy the full path). "
              "This is most likely a leftover of setting the ModOrganizer 1 load "
              "mechanism to \"Script Extender\", "
              "in which case you must remove this file either by changing the load "
              "mechanism in ModOrganizer 1 or "
              "manually removing the file, otherwise the game is likely to crash and "
              "burn.")
        .arg(oldMO1HookDll());
    break;
  }
  default: {
    return tr("Description missing");
  } break;
  }
}

bool OrganizerCore::hasGuidedFix(unsigned int) const
{
  return false;
}

void OrganizerCore::startGuidedFix(unsigned int) const {}

bool OrganizerCore::saveCurrentLists()
{
  if (m_PersistenceSuppressed) {
    return false;
  }

  if (m_DirectoryUpdate) {
    log::warn("not saving lists during directory update");
    return false;
  }

  try {
    savePluginList();
    if (m_UserInterface != nullptr) {
      m_UserInterface->archivesWriter().write();
    }
  } catch (const std::exception& e) {
    reportError(tr("failed to save load order: %1").arg(e.what()));
  }

  return true;
}

void OrganizerCore::savePluginList()
{
  if (m_PersistenceSuppressed) {
    return;
  }

  onNextRefresh(
      [this]() {
        if (m_PersistenceSuppressed) {
          return;
        }
        m_PluginList.saveTo(m_CurrentProfile->getLockedOrderFileName());
        m_PluginList.saveLoadOrder(*m_DirectoryStructure);
      },
      RefreshCallbackGroup::CORE, RefreshCallbackMode::RUN_NOW_IF_POSSIBLE);
}

void OrganizerCore::saveCurrentProfile()
{
  if (m_PersistenceSuppressed || m_CurrentProfile == nullptr) {
    return;
  }

  m_CurrentProfile->writeModlist();
  m_CurrentProfile->createTweakedIniFile();
  saveCurrentLists();
  storeSettings();
}

void OrganizerCore::saveCurrentProfileForShutdown()
{
  if (m_PersistenceSuppressed) {
    m_PluginListsWriter.cancel();
    if (m_CurrentProfile != nullptr) {
      m_CurrentProfile->suppressWritesForFailedRollback();
    }
    m_CurrentProfileSavedForShutdown = true;
    return;
  }

  if (m_CurrentProfileSavedForShutdown) {
    return;
  }

  try {
    saveCurrentProfile();
  } catch (const std::exception& e) {
    log::error("failed to save current profile during shutdown: {}", e.what());
  } catch (...) {
    log::error("failed to save current profile during shutdown: unknown exception");
  }

  try {
    if (m_CurrentProfile != nullptr) {
      m_CurrentProfile->writeModlistNow(true);
    }
  } catch (const std::exception& e) {
    log::error("failed to flush mod list during shutdown: {}", e.what());
  } catch (...) {
    log::error("failed to flush mod list during shutdown: unknown exception");
  }

  try {
    m_PluginListsWriter.writeImmediately(true);
  } catch (const std::exception& e) {
    log::error("failed to flush plugin list during shutdown: {}", e.what());
  } catch (...) {
    log::error("failed to flush plugin list during shutdown: unknown exception");
  }

  try {
    if (m_UserInterface != nullptr) {
      m_UserInterface->archivesWriter().writeImmediately(true);
    }
  } catch (const std::exception& e) {
    log::error("failed to flush archive list during shutdown: {}", e.what());
  } catch (...) {
    log::error("failed to flush archive list during shutdown: unknown exception");
  }

  m_CurrentProfileSavedForShutdown = true;
}

ProcessRunner OrganizerCore::processRunner()
{
  return ProcessRunner(*this, m_UserInterface);
}

bool OrganizerCore::checkGameRegistryKey()
{
  // Map of game short names to their registry key info.
  // Format: { shortName, { subKey, valueName } }
  static const QMap<QString, QPair<QString, QString>> gameRegistryKeys = {
    {"Enderal",    {"Software\\SureAI\\Enderal",                            "Install_Path"}},
    {"EnderalSE",  {"Software\\SureAI\\EnderalSE",                          "Install_Path"}},
    {"Fallout3",   {"Software\\Bethesda Softworks\\Fallout3",               "installed path"}},
    {"Fallout4",   {"Software\\Bethesda Softworks\\Fallout4",               "installed path"}},
    {"Fallout4VR", {"Software\\Bethesda Softworks\\Fallout 4 VR",           "installed path"}},
    {"FalloutNV",  {"Software\\Bethesda Softworks\\FalloutNV",              "installed path"}},
    {"Morrowind",  {"Software\\Bethesda Softworks\\Morrowind",              "installed path"}},
    {"Oblivion",   {"Software\\Bethesda Softworks\\Oblivion",               "installed path"}},
    {"Skyrim",     {"Software\\Bethesda Softworks\\Skyrim",                 "installed path"}},
    {"SkyrimSE",   {"Software\\Bethesda Softworks\\Skyrim Special Edition", "installed path"}},
    {"SkyrimVR",   {"Software\\Bethesda Softworks\\Skyrim VR",              "installed path"}},
    {"TTW",        {"Software\\Bethesda Softworks\\FalloutNV",              "installed path"}},
  };

  const auto* game = managedGame();
  if (!game) return true;

  const QString shortName = game->gameShortName();
  auto it = gameRegistryKeys.find(shortName);
  if (it == gameRegistryKeys.end()) {
    return true;  // unknown game, nothing to check
  }

  auto cfg = FluorineConfig::load();
  if (!cfg || cfg->prefix_path.isEmpty()) {
    return true;  // no prefix configured
  }

  WinePrefix const prefix(cfg->prefix_path);
  if (!prefix.isValid()) {
    return true;  // prefix doesn't exist yet
  }

  const QString& subKey    = it.value().first;
  const QString& valueName = it.value().second;

  // The game directory MO2 is configured to use — convert to Wine path
  const QString gameDir = game->gameDirectory().canonicalPath();
  if (gameDir.isEmpty()) {
    return true;
  }

  // Convert Linux path to Wine Z: path for comparison.
  // Trailing backslash required — game launchers expect it (matches Steam's format).
  QString winePath = "Z:" + QString(gameDir).replace("/", "\\");
  if (!winePath.endsWith('\\'))
    winePath += '\\';

  // Read the current registry value (check both normal and Wow6432Node)
  QString registryPath = prefix.readHklmValue(subKey, valueName);
  if (registryPath.isEmpty()) {
    const QString wow64Key = "Software\\Wow6432Node\\" + subKey.mid(9);
    registryPath = prefix.readHklmValue(wow64Key, valueName);
  }

  // Normalize trailing separators before comparing — Steam writes paths with
  // a trailing backslash, so the registry value may differ only in that.
  auto stripTrailingSep = [](QString s) {
    while (s.endsWith('\\') || s.endsWith('/'))
      s.chop(1);
    return s;
  };

  if (registryPath.isEmpty() ||
      stripTrailingSep(registryPath).compare(stripTrailingSep(winePath),
                                             Qt::CaseInsensitive) != 0) {
    const QString displayRegPath = registryPath.isEmpty()
        ? tr("<not set>") : registryPath;

    QWidget* parent = nullptr;
    if (m_UserInterface) {
      parent = m_UserInterface->mainWindow();
    }

    const auto answer = QMessageBox::question(
        parent,
        tr("Registry key does not match"),
        tr("The game's installation path in the Wine registry does not match "
           "the managed game path.\n\n"
           "Registry Game Path:\n\t%1\n"
           "Managed Game Path:\n\t%2\n\n"
           "Change the path in the registry to match the managed game path?")
            .arg(displayRegPath, winePath),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
        QMessageBox::Yes);

    if (answer == QMessageBox::Yes) {
      if (!prefix.writeHklmValue(subKey, valueName, winePath)) {
        log::error("Failed to update game registry key");
      }
      // Also update the Wow6432Node copy (32-bit registry view)
      const QString wow64Key = "Software\\Wow6432Node\\" + subKey.mid(9);  // skip "Software\\"
      prefix.writeHklmValue(wow64Key, valueName, winePath);
    } else if (answer == QMessageBox::Cancel) {
      return false;  // cancel launch
    }
  }

  return true;
}

bool OrganizerCore::beforeRun(
    const QFileInfo& binary, const QDir& cwd, const QString& arguments,
    const QString& profileName, const QString& customOverwrite,
    const QList<MOBase::ExecutableForcedLoadSetting>& forcedLibraries,
    bool useProton, const QString& launchToken, bool ownsVfs,
    QString* usvfsRequestPath,
    QString* saveBindMountSource, QString* saveBindMountTarget)
{
  saveCurrentProfile();

  std::shared_ptr<Profile> launchProfile = m_CurrentProfile;
  if (launchProfile == nullptr || launchProfile->name() != profileName) {
    const QDir profileDir(
        QDir(m_Settings.paths().profiles()).filePath(profileName));
    if (!profileDir.exists()) {
      log::error("beforeRun: launch profile '{}' does not exist",
                 profileName.toStdString());
      return false;
    }
    launchProfile =
        std::make_shared<Profile>(profileDir, managedGame(), gameFeatures());
  }
  const auto setIndexPublicationContext = [this, &profileName]() {
    const auto instance = InstanceManager::singleton().currentInstance();
    m_USVFS.setIndexPublicationContext(
        {.output_base=basePath().toStdString(),
         .producer=std::string("Fluorine ") + FLUORINE_VERSION_STRING,
         .instance_name=
             instance != nullptr ? instance->displayName().toStdString() : "",
         .profile_name=profileName.toStdString(),
#ifdef _WIN32
         .consumer_path_style=VfsIndexConsumerPathStyle::NativeWindows
#else
         .consumer_path_style=VfsIndexConsumerPathStyle::Wine
#endif
        });
  };
  setIndexPublicationContext();
  if (saveBindMountSource) saveBindMountSource->clear();
  if (saveBindMountTarget) saveBindMountTarget->clear();
  if (usvfsRequestPath) usvfsRequestPath->clear();

  // A completed generation can synchronously schedule another generation
  // before waking us. Do not prepare a launch against that intermediate
  // directory state.
  if (!waitForDirectoryRefreshes()) {
    return false;
  }

  movePGPatcherLogsToLogsFolder();

  // need to make sure all data is saved before we start the application
  if (launchProfile != nullptr) {
    launchProfile->writeModlistNow(true);
  }

  // An about-to-run callback may synchronously launch and wait for a helper
  // through the same public API (Cyberpunk REDmod does this). Hand off only
  // this not-yet-started launch's VFS reservation while callbacks run. A
  // synchronous helper cleans up before returning; an asynchronous helper
  // keeps ownership and makes the outer re-reservation fail safely.
  if (ownsVfs &&
      !m_ProcessLaunchContext.handoffVfsReservation(launchToken)) {
    log::error("beforeRun: launch '{}' could not hand off its VFS reservation",
               launchToken.toStdString());
    return false;
  }

  if (!m_AboutToRun(binary.absoluteFilePath(), cwd, arguments)) {
    log::debug("start of \"{}\" cancelled by plugin", binary.absoluteFilePath());
    return false;
  }

  if (ownsVfs &&
      !m_ProcessLaunchContext.reclaimVfsReservation(launchToken)) {
    log::error("beforeRun: launch '{}' could not reclaim VFS ownership after "
               "an about-to-run callback started another application",
               launchToken.toStdString());
    return false;
  }

  // A synchronous nested launch overwrites this shared publication metadata.
  // Restore the outer generation after it has reclaimed VFS ownership.
  setIndexPublicationContext();

  // Pandora Behaviour Engine+ workaround: normalize paths in
  // PreviousOutput.txt to lowercase to avoid case-sensitivity issues
  // on Linux filesystems (issue #110).  Pandora records output paths
  // with the casing from Skyrim's HKX binary data (e.g. "Characters
  // Female"), but the VFS normalizes to lowercase on write, causing
  // the second run's LoadMetaData() to fail to find files to delete.
  if (binary.fileName().contains("pandora", Qt::CaseInsensitive)) {
    QStringList candidates = {
        QDir(overwritePath()).filePath("Pandora_Engine/PreviousOutput.txt"),
    };
    if (!customOverwrite.isEmpty()) {
      candidates.append(
          QDir(modsPath()).filePath(customOverwrite + "/Pandora_Engine/PreviousOutput.txt"));
    }

    log::debug("Pandora fix: checking {} candidate(s) for PreviousOutput.txt",
               candidates.size());

    for (const QString& pandoraPrevOutput : candidates) {
      QFileInfo info(pandoraPrevOutput);
      if (info.exists() && info.isFile()) {
        log::debug("Pandora fix: found PreviousOutput.txt at {}", pandoraPrevOutput);
        QFile file(pandoraPrevOutput);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
          QStringList lines;
          QTextStream in(&file);
          while (!in.atEnd()) {
            lines.append(in.readLine());
          }
          file.close();
          log::debug("Pandora fix: read {} lines", lines.size());

          for (QString& line : lines) {
            int idx = line.indexOf("\\meshes\\", 0, Qt::CaseInsensitive);
            if (idx >= 0) {
              line = line.left(idx) + line.mid(idx).toLower();
            }
          }

          if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            QTextStream out(&file);
            for (const QString& line : lines) {
              out << line << Qt::endl;
            }
            file.close();
            log::debug("Pandora fix: wrote {} lines back", lines.size());
          }
        }
        break;
      } else {
        log::debug("Pandora fix: {} does not exist or is not a file",
                   pandoraPrevOutput);
      }
    }
  }

  // Check the game's registry key in the Wine prefix and fix if needed.
  if (!checkGameRegistryKey()) {
    return false;  // user cancelled
  }

  // VFS Root Builder: read per-instance setting and configure.
  {
    bool vfsRootBuilder = false;
    if (const auto* s = Settings::maybeInstance()) {
      const QSettings instanceIni(s->filename(), QSettings::IniFormat);
      vfsRootBuilder = instanceIni.value("fluorine/vfs_root_builder", true).toBool();
    }
    const QString storageDir =
        QDir(QDir::fromNativeSeparators(basePath())).filePath("rootbuilder");
    m_USVFS.setRootBuilderEnabled(vfsRootBuilder, storageDir.toStdString());
  }

  // beforeRun can mount without the Data tab having called prepareVFS first.
  // Recover any prior in-game saves before this launch builds its VFS catalog.
  migrateGameLocalSavesFromOverwrite(managedGame(), overwritePath());

  // Diagnostic toggle from Settings > Proton/Wine tab (see settingsdialogproton.cpp).
  m_USVFS.setDisableVfsCache(
      QSettings().value("fluorine/disable_vfs_cache", false).toBool());

  try {
    // OpenMW and other self-managed-VFS games skip both organizer VFS
    // backends. USVFS is a Windows hooking library, so native launches always
    // retain the existing FUSE implementation even if this instance prefers
    // USVFS for Wine/Proton.
    const bool gameUsesOrganizerVfs =
        managedGame() == nullptr || managedGame()->usesVFS();
    const QSettings instanceIni(m_Settings.filename(), QSettings::IniFormat);
    const VfsBackend configuredBackend = parseVfsBackend(
        instanceIni.value(kVfsBackendSetting, QStringLiteral("fuse"))
            .toString());
    const bool launchWithUsvfs =
        useUsvfsForLaunch(configuredBackend, useProton, gameUsesOrganizerVfs);

    if (launchWithUsvfs) {
      if (usvfsRequestPath == nullptr) {
        throw std::runtime_error(
            "USVFS selected but no launcher request destination was provided");
      }

      const auto usvfsPreparationStart = std::chrono::steady_clock::now();

      // The Data tab may have mounted a preview FUSE tree earlier. USVFS maps
      // the original physical destinations, so tear the preview down first.
      m_USVFS.unmount();
      const auto usvfsUnmountedAt = std::chrono::steady_clock::now();
      const MappingType mappings = fileMapping(profileName, customOverwrite);
      const auto usvfsMappingsBuiltAt = std::chrono::steady_clock::now();
      m_USVFS.prepareRootFilesForUsvfs(mappings);
      const auto usvfsRootPreparedAt = std::chrono::steady_clock::now();

      bool useResolvedSnapshot = false;
      QString snapshotDataDirectory;
      UsvfsResolvedSnapshot resolvedSnapshot;
      if (usvfsRuntimeSupportsResolvedSnapshots() && managedGame() != nullptr) {
        snapshotDataDirectory = managedGame()->dataDirectory().absolutePath();
        try {
          resolvedSnapshot = buildUsvfsResolvedSnapshotFromMappings(
              mappings, snapshotDataDirectory, m_Settings.skipFileSuffixes(),
              m_Settings.skipDirectories());
          useResolvedSnapshot = true;
          log::info(
              "Prepared resolved USVFS snapshot directly: entries={} "
              "directories={} files={}",
              resolvedSnapshot.mappings.size(), resolvedSnapshot.directoryCount,
              resolvedSnapshot.fileCount);
        } catch (const std::exception& error) {
          log::warn("Unable to prepare resolved USVFS snapshot; using ordinary "
                    "recursive mappings: {}",
                    error.what());
          snapshotDataDirectory.clear();
          resolvedSnapshot = {};
        }
      }

      const QString logsDirectory =
          QDir(qApp->property("dataPath").toString()).filePath("logs");
      QDir().mkpath(logsDirectory);
      UsvfsRequestOptions requestOptions{
          .binary = binary,
          .workingDirectory = cwd,
          .arguments = QProcess::splitCommand(arguments),
          .mappings = mappings,
          .useResolvedSnapshot = useResolvedSnapshot,
          .dataDirectory = snapshotDataDirectory,
          .resolvedMappings = std::move(resolvedSnapshot.mappings),
          .forcedLibraries = forcedLibraries,
          .executableBlacklist =
              m_Settings.executablesBlacklist().split(';', Qt::SkipEmptyParts),
          .skipFileSuffixes = m_Settings.skipFileSuffixes(),
          .skipDirectories = m_Settings.skipDirectories(),
          .logPath =
              QDir(logsDirectory)
                  .filePath(QStringLiteral("usvfs-%1.log")
                                .arg(QDateTime::currentDateTimeUtc().toString(
                                    QStringLiteral("yyyyMMdd-HHmmss-zzz")))),
      };
      const auto mappingPreparedAt = std::chrono::steady_clock::now();
      const auto requestWriteStart = mappingPreparedAt;
      const UsvfsRequestResult request = writeUsvfsRequest(requestOptions);
      if (!request) {
        throw std::runtime_error(request.error.toStdString());
      }
      const auto requestWrittenAt = std::chrono::steady_clock::now();
      {
        QFile benchmarkLog(requestOptions.logPath);
        if (benchmarkLog.open(QIODevice::WriteOnly | QIODevice::Append |
                              QIODevice::Text)) {
          QTextStream stream(&benchmarkLog);
          stream << "[benchmark] format=1 phase=fluorine_prepare elapsed_ms="
                 << std::chrono::duration_cast<std::chrono::milliseconds>(
                        mappingPreparedAt - usvfsPreparationStart)
                        .count()
                 << " mappings=" << mappings.size() << Qt::endl;
          stream << "[benchmark] format=1 phase=fuse_unmount elapsed_ms="
                 << std::chrono::duration_cast<std::chrono::milliseconds>(
                        usvfsUnmountedAt - usvfsPreparationStart)
                        .count()
                 << Qt::endl;
          stream << "[benchmark] format=1 phase=file_mapping elapsed_ms="
                 << std::chrono::duration_cast<std::chrono::milliseconds>(
                        usvfsMappingsBuiltAt - usvfsUnmountedAt)
                        .count()
                 << Qt::endl;
          stream << "[benchmark] format=1 phase=root_builder elapsed_ms="
                 << std::chrono::duration_cast<std::chrono::milliseconds>(
                        usvfsRootPreparedAt - usvfsMappingsBuiltAt)
                        .count()
                 << Qt::endl;
          stream << "[benchmark] format=1 phase=resolved_snapshot elapsed_ms="
                 << std::chrono::duration_cast<std::chrono::milliseconds>(
                        mappingPreparedAt - usvfsRootPreparedAt)
                        .count()
                 << Qt::endl;
          stream << "[benchmark] format=1 phase=request_serialize elapsed_ms="
                 << std::chrono::duration_cast<std::chrono::milliseconds>(
                        requestWrittenAt - requestWriteStart)
                        .count()
                 << Qt::endl;
        }
      }
      *usvfsRequestPath = request.path;
      log::info("beforeRun: using Wine/Proton USVFS backend (instance='{}', "
                "request='{}', mappings={})",
                request.instanceName, request.path, mappings.size());
    } else if (gameUsesOrganizerVfs) {
      m_USVFS.updateMapping(fileMapping(profileName, customOverwrite));
      m_USVFS.updateForcedLibraries(forcedLibraries);
      log::debug("beforeRun: using FUSE backend{}",
                 configuredBackend == VfsBackend::Usvfs && !useProton
                     ? " (native launch override)"
                     : "");
    } else {
      log::debug("beforeRun: skipping organizer VFS; managed game manages its own "
                 "VFS (usesVFS=false)");
    }
  } catch (const FuseConnectorException& e) {
    log::error("VFS mount failed: {}", e.what());
    // ProcessRunner's already-armed preparation guard owns rollback. Preserve
    // the request path and any partial mount so its mandatory cleanup can
    // remove/unmount transactionally and retain the tracker on failure.
    return false;
  } catch (const std::exception& e) {
    QWidget* w = nullptr;
    if (m_UserInterface) {
      w = m_UserInterface->mainWindow();
    }
    QMessageBox::warning(w, tr("Error"), e.what());
    // As above, leave rollback to the caller's preparation guard. Clearing a
    // failed request removal here would make that prepared artifact impossible
    // to retry safely.
    return false;
  }

  // Deploy plugins.txt and loadorder.txt to the Wine prefix before launch.
  if (launchProfile != nullptr) {
    const QString prefixPathStr = resolveWinePrefixPath(m_Settings, managedGame());

    if (!prefixPathStr.isEmpty()) {
      WinePrefix const prefix(prefixPathStr);
      if (prefix.isValid()) {
        const QString dataDirName  = resolveWineDataDirName(managedGame());
        const QString appDataLocal = prefix.appdataLocal();
        const QString pluginsTargetDir =
            QDir(appDataLocal).filePath(dataDirName);
        const QString documentsDir =
            resolvePrefixGameDocumentsDir(prefix, dataDirName);
        log::info("Wine prefix paths: AppData/Local plugins dir='{}', "
                  "Documents/My Games INI dir='{}'",
                  pluginsTargetDir, documentsDir);
        const auto localSavesFeature = gameFeatures().gameFeature<LocalSavegames>();
        const QString absoluteSaveDir =
            resolveAbsoluteSaveDir(prefix, managedGame(),
                                   localSavesFeature.get(), launchProfile);
        log::info("Wine prefix save target: '{}'", absoluteSaveDir);

        // If the prefix's plugin-list file is newer than the profile's
        // (e.g. LOOT ran outside MO2 and edited it), sync back first so
        // external edits aren't clobbered by the deploy below.
        {
          WinePrefix::PluginListMechanism preMech =
              WinePrefix::PluginListMechanism::None;
          switch (managedGame()->loadOrderMechanism()) {
          case IPluginGame::LoadOrderMechanism::PluginsTxt:
            preMech = WinePrefix::PluginListMechanism::PluginsTxt;
            break;
          case IPluginGame::LoadOrderMechanism::FileTime:
            preMech = WinePrefix::PluginListMechanism::FileTime;
            break;
          case IPluginGame::LoadOrderMechanism::None:
            break;
          }
          if (preMech != WinePrefix::PluginListMechanism::None) {
            const QString profilePluginsPath =
                launchProfile->getPluginsFileName();
            const QDateTime prefixMTime =
                prefix.prefixPluginsMTime(dataDirName);
            const QDateTime profileMTime =
                QFileInfo(profilePluginsPath).lastModified();
            if (prefixMTime.isValid() &&
                (!profileMTime.isValid() || prefixMTime > profileMTime)) {
              log::info("Prefix plugins.txt newer than profile "
                        "(prefix={}, profile={}), syncing back before deploy",
                        prefixMTime.toString(Qt::ISODate),
                        profileMTime.toString(Qt::ISODate));
              if (prefix.syncPluginsBack(profilePluginsPath, dataDirName,
                                         preMech)) {
                refreshESPList(true);
              } else {
                log::warn("Pre-deploy sync-back failed; proceeding with "
                          "deploy — external edits may be lost");
              }
            }
          }
        }

        // Read plugin lines from profile's plugins.txt
        QFile pluginsFile(launchProfile->getPluginsFileName());
        if (pluginsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
          QStringList plugins;
          QTextStream stream(&pluginsFile);
          while (!stream.atEnd()) {
            const QString line = stream.readLine().trimmed();
            if (!line.isEmpty() && !line.startsWith('#')) {
              plugins.append(line);
            }
          }
          pluginsFile.close();

          if (!plugins.isEmpty()) {
            WinePrefix::PluginListMechanism wineMech =
                WinePrefix::PluginListMechanism::None;
            switch (managedGame()->loadOrderMechanism()) {
            case IPluginGame::LoadOrderMechanism::PluginsTxt:
              wineMech = WinePrefix::PluginListMechanism::PluginsTxt;
              break;
            case IPluginGame::LoadOrderMechanism::FileTime:
              wineMech = WinePrefix::PluginListMechanism::FileTime;
              break;
            case IPluginGame::LoadOrderMechanism::None:
              wineMech = WinePrefix::PluginListMechanism::None;
              break;
            }
            const QString pluginListFile =
                wineMech == WinePrefix::PluginListMechanism::PluginsTxt
                    ? QString("Plugins.txt")
                    : QString("plugins.txt");
            log::info("Deploying plugin list to '{}'",
                      QDir(pluginsTargetDir).filePath(pluginListFile));
            if (prefix.deployPlugins(plugins, dataDirName, wineMech)) {
              log::debug("Deployed {} plugins to prefix '{}' (dataDirName='{}')",
                         plugins.size(), prefixPathStr, dataDirName);
            } else {
              log::error("Failed to deploy {} plugins to prefix '{}' "
                         "(dataDirName='{}')",
                         plugins.size(), prefixPathStr, dataDirName);
            }
          } else {
            log::warn("Profile plugins.txt is empty or contains only comments, "
                      "skipping plugin deployment");
          }
        }

        if (launchProfile->localSettingsEnabled()) {
          const QString targetIniBase =
              resolvePrefixGameDocumentsDir(prefix, dataDirName);
          int deployedIniCount = 0;
          for (const QString& iniFile : managedGame()->iniFiles()) {
            const QString sourceIni =
                launchProfile->absoluteIniFilePath(iniFile);
            const QString targetIni =
                QDir(targetIniBase).filePath(QFileInfo(iniFile).fileName());
            log::info("INI deploy target: '{}' -> '{}' (exists={})", sourceIni,
                      targetIni, QFileInfo::exists(sourceIni));
            if (QFileInfo::exists(sourceIni) &&
                prefix.deployProfileIni(sourceIni, targetIni)) {
              ++deployedIniCount;
              log::debug("Deployed profile INI '{}' -> '{}'", sourceIni, targetIni);
            }
          }
          if (deployedIniCount > 0) {
            log::debug("Deployed {} profile INI files to prefix '{}'", deployedIniCount,
                       prefixPathStr);
          }
        } else {
          log::debug("Profile local settings not enabled, skipping INI deployment. "
                     "documentsDirectory='{}'",
                     managedGame()->documentsDirectory().absolutePath());
        }

        if (launchProfile->localSavesEnabled()) {
          const QString profileSavesDir =
              QDir(launchProfile->absolutePath()).filePath("saves");

          const bool useBindMount =
              saveBindMountSource && saveBindMountTarget &&
              ProtonLauncher::unprivilegedBindMountSupported();

          if (useBindMount) {
            // Clean up any stale symlink from previous symlink-based runs:
            // mount --bind won't traverse a symlink as the target.
            if (QFileInfo(absoluteSaveDir).isSymLink()) {
              QFile::remove(absoluteSaveDir);
            }
            const QFileInfo leafInfo(absoluteSaveDir);
            const QString lowerSaveDir =
                QDir(leafInfo.dir().absolutePath())
                    .filePath(leafInfo.fileName().toLower());
            if (lowerSaveDir != absoluteSaveDir &&
                QFileInfo(lowerSaveDir).isSymLink()) {
              QFile::remove(lowerSaveDir);
            }

            // Both endpoints must exist before the kernel can bind them.
            QDir().mkpath(profileSavesDir);
            QDir().mkpath(absoluteSaveDir);

            *saveBindMountSource = profileSavesDir;
            *saveBindMountTarget = absoluteSaveDir;
            log::info("Save bind mount: '{}' -> '{}'", profileSavesDir,
                      absoluteSaveDir);
          } else {
            log::info("Save deploy target: '{}' -> '{}'", profileSavesDir,
                      absoluteSaveDir);
            if (!prefix.deployProfileSaves(profileSavesDir, absoluteSaveDir,
                                            true)) {
              log::warn("Failed to deploy profile saves from '{}' to "
                        "prefix '{}'",
                        profileSavesDir, prefixPathStr);
            }
          }

          // Ensure the prefix INI points to __MO_Saves so the game reads
          // profile-specific saves, even when localSettingsEnabled() is off.
          const QStringList iniFiles = managedGame()->iniFiles();
          if (!iniFiles.isEmpty()) {
            const QString prefixIni =
                QDir(resolvePrefixGameDocumentsDir(prefix, dataDirName))
                    .filePath(QFileInfo(iniFiles.first()).fileName());
            MOBase::WriteRegistryValue("General", "sLocalSavePath",
                                       "__MO_Saves\\", prefixIni);
            MOBase::WriteRegistryValue("General", "bUseMyGamesDirectory",
                                       "1", prefixIni);
            log::debug("Patched prefix INI '{}': sLocalSavePath=__MO_Saves\\",
                       prefixIni);
          }
        } else {
          // Local saves disabled — restore default sLocalSavePath in prefix INI
          const QStringList iniFiles = managedGame()->iniFiles();
          if (!iniFiles.isEmpty()) {
            const QString prefixIni =
                QDir(resolvePrefixGameDocumentsDir(prefix, dataDirName))
                    .filePath(QFileInfo(iniFiles.first()).fileName());
            MOBase::WriteRegistryValue("General", "sLocalSavePath",
                                       "Saves\\", prefixIni);
            log::debug("Restored prefix INI '{}': sLocalSavePath=Saves\\",
                       prefixIni);
          }
        }
      } else {
        log::warn("Wine prefix at '{}' is not valid (no drive_c)", prefixPathStr);
      }
    } else {
      log::debug("No Wine prefix configured, skipping plugin deployment");
    }
  }

  return true;
}

bool OrganizerCore::reserveProcessLaunch(const QString& launchToken,
                                         const QString& profileName,
                                         bool ownsVfs)
{
  return m_ProcessLaunchContext.reserve(launchToken, profileName, ownsVfs);
}

OrganizerCore::ConfigurationLease OrganizerCore::tryAcquireConfigurationLease()
{
  return m_ProcessLaunchContext.tryAcquireConfigurationLease();
}

void OrganizerCore::abandonProcessLaunch(const QString& launchToken)
{
  m_ProcessLaunchContext.abandon(launchToken);
}

void OrganizerCore::abortProcessLaunchPreparation(const QString& launchToken,
                                                  const QString& profileName,
                                                  bool ownsVfs,
                                                  QString usvfsRequestPath) noexcept
{
  try {
    continueAbortedLaunchTeardown(launchToken, profileName, ownsVfs,
                                  usvfsRequestPath,
                                  /*retry=*/false, /*retryCount=*/0);
  } catch (const std::exception& e) {
    try {
      log::error("launch preparation rollback failed before retry scheduling for "
                 "'{}': {}; ownership remains retained",
                 launchToken.toStdString(), e.what());
    } catch (...) {
    }
  } catch (...) {
    try {
      log::error("launch preparation rollback failed before retry scheduling for "
                 "'{}'; ownership remains retained",
                 launchToken.toStdString());
    } catch (...) {
    }
  }
}

void OrganizerCore::continueAbortedLaunchTeardown(
    const QString& launchToken, const QString& profileName, bool ownsVfs,
    const QString& usvfsRequestPath, bool retry, unsigned int retryCount)
{
  const auto attempt = launch_cleanup::attemptMandatoryCleanup(
      m_ProcessLaunchContext, launchToken, profileName, ownsVfs,
      retry ? launch_cleanup::AttemptKind::Retry
            : launch_cleanup::AttemptKind::Initial,
      [this, &usvfsRequestPath](bool cleanupVfs) {
        if (!removePreparedLaunchArtifact(usvfsRequestPath)) {
          throw std::runtime_error(
              QStringLiteral("unable to remove aborted USVFS request '%1'")
                  .arg(usvfsRequestPath)
                  .toStdString());
        }
        if (cleanupVfs) {
          m_USVFS.unmount();
        }
      },
      /*additionalCleanupRequired=*/!usvfsRequestPath.isEmpty());

  if (attempt.state == launch_cleanup::AttemptState::Rejected) {
    log::error("launch preparation rollback rejected for token '{}' and profile "
               "'{}'; no foreign VFS cleanup was attempted",
               launchToken.toStdString(), profileName.toStdString());
    return;
  }

  if (attempt.state == launch_cleanup::AttemptState::RetryRequired) {
    const int delay = cleanupRetryDelayMs(retryCount);
    log::error("launch preparation rollback for '{}' failed: {}; retaining "
               "ownership and retrying in {} ms",
               launchToken.toStdString(),
               cleanupFailureMessage(attempt.failure).toStdString(), delay);
    try {
      QTimer::singleShot(
          delay, this,
          [this, launchToken, profileName, ownsVfs, usvfsRequestPath,
           retryCount]() {
            try {
              continueAbortedLaunchTeardown(launchToken, profileName, ownsVfs,
                                            usvfsRequestPath,
                                            /*retry=*/true, retryCount + 1);
            } catch (...) {
              logQueuedCleanupFailure("launch preparation cleanup retry",
                                      launchToken, std::current_exception());
            }
          });
    } catch (const std::exception& e) {
      log::error("unable to schedule launch rollback retry for '{}': {}; launch "
                 "ownership remains retained",
                 launchToken.toStdString(), e.what());
    } catch (...) {
      log::error("unable to schedule launch rollback retry for '{}'; launch "
                 "ownership remains retained",
                 launchToken.toStdString());
    }
    return;
  }

  m_ProcessLaunchContext.finishCompletion(launchToken);
  if (!m_PersistenceSuppressed) {
    try {
      movePGPatcherLogsToLogsFolder();
    } catch (...) {
      logQueuedCleanupFailure("launch preparation post-cleanup", launchToken,
                              std::current_exception());
    }
  }
  log::debug("launch preparation rollback completed for '{}'",
             launchToken.toStdString());
}

struct OrganizerCore::AfterRunWork
{
  QFileInfo binary;
  DWORD exitCode{0};
  bool unmountVfs{false};
  QString launchToken;
  QString profileName;
  bool triggerRefresh{false};
  std::function<void()> refreshComplete;
  std::function<void(bool)> cleanupComplete;
};

OrganizerCore::AfterRunResult OrganizerCore::afterRun(
    const QFileInfo& binary, DWORD exitCode, bool unmountVfs,
    const QString& launchToken, const QString& profileName, bool triggerRefresh,
    std::function<void()> refreshComplete,
    std::function<void(bool refreshScheduled)> cleanupComplete)
{
  auto work = std::make_shared<AfterRunWork>(AfterRunWork{
      binary, exitCode, unmountVfs, launchToken, profileName, triggerRefresh,
      std::move(refreshComplete), std::move(cleanupComplete)});
  return continueAfterRun(work, /*retry=*/false, /*retryCount=*/0);
}

OrganizerCore::AfterRunResult OrganizerCore::continueAfterRun(
    const std::shared_ptr<AfterRunWork>& work, bool retry,
    unsigned int retryCount)
{
  const bool trackedLaunch = !work->launchToken.isEmpty();
  launch_cleanup::CleanupStage cleanupStage(launch_cleanup::AttemptResult{
      launch_cleanup::AttemptState::Complete, {}});
  bool untrackedVfsCleanupPerformed = false;
  if (trackedLaunch) {
    cleanupStage = launch_cleanup::beginMandatoryCleanup(
        m_ProcessLaunchContext, work->launchToken, work->profileName,
        work->unmountVfs,
        retry ? launch_cleanup::AttemptKind::Retry
              : launch_cleanup::AttemptKind::Initial,
        [this]() { m_USVFS.unmount(); });
  } else if (work->unmountVfs) {
    // Compatibility callers with no launch token historically owned no
    // tracker. Keep their behavior, but never let an unmount exception escape
    // the organizer event loop.
    try {
      m_USVFS.unmount();
      untrackedVfsCleanupPerformed = true;
    } catch (...) {
      cleanupStage = launch_cleanup::CleanupStage(
          {launch_cleanup::AttemptState::RetryRequired,
           std::current_exception()});
    }
  }

  const auto& attempt = cleanupStage.attempt();

  if (attempt.state == launch_cleanup::AttemptState::Rejected) {
    log::warn("afterRun: ignoring unknown, mismatched, or already completed "
              "launch '{}' for profile '{}'",
              work->launchToken.toStdString(), work->profileName.toStdString());
    return {AfterRunState::Rejected, false};
  }

  if (attempt.state == launch_cleanup::AttemptState::RetryRequired) {
    const int delay = cleanupRetryDelayMs(retryCount);
    log::error("afterRun: mandatory cleanup for launch '{}' failed: {}; retaining "
               "ownership and retrying in {} ms",
               work->launchToken.toStdString(),
               cleanupFailureMessage(attempt.failure).toStdString(), delay);
    try {
      QTimer::singleShot(delay, this, [this, work, retryCount]() {
        try {
          continueAfterRun(work, /*retry=*/true, retryCount + 1);
        } catch (...) {
          logQueuedCleanupFailure("afterRun cleanup retry", work->launchToken,
                                  std::current_exception());
        }
      });
    } catch (const std::exception& e) {
      log::error("afterRun: unable to schedule cleanup retry for '{}': {}; "
                 "launch ownership remains retained",
                 work->launchToken.toStdString(), e.what());
    } catch (...) {
      log::error("afterRun: unable to schedule cleanup retry for '{}'; launch "
                 "ownership remains retained",
                 work->launchToken.toStdString());
    }
    return {AfterRunState::CleanupPending, false};
  }

  // Mandatory cleanup has reached a known-safe state. Every later operation is
  // best-effort and must neither escape the Qt event loop nor keep this launch
  // registered forever.
  bool refreshScheduled = false;
  const bool vfsCleanupPerformed =
      trackedLaunch ? attempt.vfsCleanupPerformed
                    : untrackedVfsCleanupPerformed;
  std::exception_ptr postRunFailure;
  std::exception_ptr refreshFailure;
  std::exception_ptr finishedRunFailure;
  const auto finalization = cleanupStage.finalize(
      trackedLaunch ? &m_ProcessLaunchContext : nullptr, work->launchToken,
      [&]() {
        if (m_PersistenceSuppressed) {
          // Fail-stop still waits for mandatory physical teardown and exact
          // tracker completion, but no launch-owned profile synchronization,
          // refresh, or arbitrary finished-run callback may write afterward.
          return;
        }
        const auto postRefresh = runPostThenRefresh(
            [&]() {
              std::shared_ptr<Profile> launchProfile = m_CurrentProfile;
              if (!work->profileName.isEmpty() &&
                  (launchProfile == nullptr ||
                   launchProfile->name() != work->profileName)) {
                const QDir profileDir(QDir(m_Settings.paths().profiles())
                                          .filePath(work->profileName));
                if (profileDir.exists()) {
                  try {
                    launchProfile = std::make_shared<Profile>(
                        profileDir, managedGame(), gameFeatures());
                  } catch (const std::exception &e) {
                    log::error(
                        "afterRun: failed to load launch profile '{}': {}; "
                        "continuing launch-owned teardown without profile "
                        "synchronization",
                        work->profileName.toStdString(), e.what());
                    launchProfile.reset();
                  }
                } else {
                  log::error(
                      "afterRun: launch profile '{}' no longer exists; "
                      "continuing "
                      "launch-owned teardown without profile synchronization",
                      work->profileName.toStdString());
                  launchProfile.reset();
                }
              }
              const bool fileTimeLoadOrder =
                  managedGame()->loadOrderMechanism() ==
                  IPluginGame::LoadOrderMechanism::FileTime;

              migrateGameLocalSavesFromOverwrite(managedGame(),
                                                 overwritePath());
              movePGPatcherLogsToLogsFolder();

              // Only a launch that actually completed physical VFS cleanup may
              // repair permissions. Native games and handed-off launch contexts
              // never mounted this game tree and must leave it untouched.
              if (vfsCleanupPerformed) {
                const auto t0 = std::chrono::steady_clock::now();
                const QString gameDir =
                    managedGame()->gameDirectory().absolutePath();
                const PermissionRepairStats repair =
                    repairGameDirectoryPermissions(gameDir.toStdString());
                if (repair.traversal_error == ENOTCONN) {
                  log::warn("afterRun: stale FUSE mount encountered under "
                            "'{}'; cleaning up",
                            gameDir.toStdString());
                  FuseConnector::tryCleanupStaleMount(
                      managedGame()->dataDirectory().absolutePath());
                }
                const auto ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
                switch (permissionRepairOutcome(repair)) {
                case PermissionRepairOutcome::Failed:
                  log::warn(
                      "afterRun: VFS permission repair inspected={} repaired={} "
                      "skipped={} failed={} elapsed_ms={}",
                      repair.inspected, repair.repaired, repair.skipped,
                      repair.failed, ms);
                  break;
                case PermissionRepairOutcome::RepairsApplied:
                  log::info(
                      "afterRun: VFS permission repair inspected={} repaired={} "
                      "skipped={} failed={} elapsed_ms={}",
                      repair.inspected, repair.repaired, repair.skipped,
                      repair.failed, ms);
                  break;
                case PermissionRepairOutcome::NoChanges:
                  log::debug(
                      "afterRun: VFS permission repair inspected={} repaired={} "
                      "skipped={} failed={} elapsed_ms={}",
                      repair.inspected, repair.repaired, repair.skipped,
                      repair.failed, ms);
                  break;
                }
              }

              if (launchProfile != nullptr) {
                const QString prefixPathStr =
                    resolveWinePrefixPath(m_Settings, managedGame());
                if (!prefixPathStr.isEmpty()) {
                  WinePrefix const prefix(prefixPathStr);
                  if (prefix.isValid()) {
                    const QString dataDirName =
                        resolveWineDataDirName(managedGame());
                    const auto localSavesFeature =
                        gameFeatures().gameFeature<LocalSavegames>();
                    const QString absoluteSaveDir = resolveAbsoluteSaveDir(
                        prefix, managedGame(), localSavesFeature.get(),
                        launchProfile);

                    if (launchProfile->localSavesEnabled()) {
                      const QString profileSavesDir =
                          QDir(launchProfile->absolutePath()).filePath("saves");

                      // Bind-mount path: writes already landed live in the
                      // profile via the per-launch mount namespace, which
                      // teardown automatically when the game exited.  Nothing
                      // to sync and nothing to undeploy. The symlink path below
                      // only runs on kernels without unprivileged user
                      // namespaces.
                      const bool usedBindMount =
                          !QFileInfo(absoluteSaveDir).isSymLink() &&
                          ProtonLauncher::unprivilegedBindMountSupported();
                      if (!usedBindMount) {
                        log::info("Save sync target: '{}' <- '{}'",
                                  profileSavesDir, absoluteSaveDir);
                        if (!prefix.syncSavesBack(profileSavesDir,
                                                  absoluteSaveDir)) {
                          log::warn("Failed to sync saves back from prefix "
                                    "'{}' to '{}'",
                                    prefixPathStr, profileSavesDir);
                        }

                        // Remove the __MO_Saves symlinks and revert the prefix
                        // INI so a vanilla launch outside MO2 uses the default
                        // Saves dir.
                        prefix.undeployProfileSaves(absoluteSaveDir);
                      }
                      const QStringList iniFiles = managedGame()->iniFiles();
                      if (!iniFiles.isEmpty()) {
                        const QString prefixIni =
                            QDir(resolvePrefixGameDocumentsDir(prefix,
                                                               dataDirName))
                                .filePath(
                                    QFileInfo(iniFiles.first()).fileName());
                        MOBase::WriteRegistryValue("General", "sLocalSavePath",
                                                   "Saves\\", prefixIni);
                        log::debug(
                            "Restored prefix INI '{}': sLocalSavePath=Saves\\",
                            prefixIni);
                      }
                    }

                    if (launchProfile->localSettingsEnabled()) {
                      const QString targetIniBase =
                          resolvePrefixGameDocumentsDir(prefix, dataDirName);
                      QList<QPair<QString, QString>> iniMappings;
                      for (const QString &iniFile : managedGame()->iniFiles()) {
                        const QString profileIni =
                            launchProfile->absoluteIniFilePath(iniFile);
                        const QString targetIni =
                            QDir(targetIniBase)
                                .filePath(QFileInfo(iniFile).fileName());
                        iniMappings.append({profileIni, targetIni});
                        log::info("INI sync target: '{}' <- '{}'", profileIni,
                                  targetIni);
                      }

                      if (!iniMappings.isEmpty() &&
                          !prefix.syncProfileInisBack(iniMappings)) {
                        log::warn(
                            "Failed to sync profile INIs back from prefix '{}'",
                            prefixPathStr);
                      }
                    }

                    // Sync the game's plugin-list file back from the prefix.
                    // LOOT and similar tools edit the deployed copy in
                    // AppData/Local/<Game>/ — without this sync, their changes
                    // are lost when refreshESPList rereads the untouched
                    // profile copy and savePluginList clobbers them with the
                    // old in-memory order.  loadorder.txt is MO2-internal and
                    // not written to or read from the prefix.
                    WinePrefix::PluginListMechanism wineMech =
                        WinePrefix::PluginListMechanism::None;
                    switch (managedGame()->loadOrderMechanism()) {
                    case IPluginGame::LoadOrderMechanism::PluginsTxt:
                      wineMech = WinePrefix::PluginListMechanism::PluginsTxt;
                      break;
                    case IPluginGame::LoadOrderMechanism::FileTime:
                      wineMech = WinePrefix::PluginListMechanism::FileTime;
                      break;
                    case IPluginGame::LoadOrderMechanism::None:
                      wineMech = WinePrefix::PluginListMechanism::None;
                      break;
                    }
                    const QString profilePluginsPath =
                        launchProfile->getPluginsFileName();
                    // Only sync the prefix Plugins.txt back to the profile when
                    // the game exited cleanly. Bethesda's engine rewrites
                    // Plugins.txt as part of its shutdown sequence, and on a
                    // crash it can serialize a partially cleared "active" set
                    // (file lists every plugin but only a handful still carry
                    // the leading '*'). Trusting that file would propagate the
                    // damage into the profile via refreshESPList +
                    // savePluginList. A non-zero exit code is a strong "do not
                    // trust the prefix file" signal. The active-count guard
                    // inside syncPluginsBack is a belt-and-suspenders backstop
                    // for cases where the engine catches its own crash and
                    // exits 0 anyway.
                    if (work->exitCode != 0) {
                      log::warn("Skipping plugins.txt sync-back: game exited "
                                "with code {} "
                                "(non-zero / abnormal). Prefix Plugins.txt may "
                                "have been "
                                "rewritten with a crash-time active set; "
                                "profile preserved.",
                                work->exitCode);
                    } else if (!prefix.syncPluginsBack(profilePluginsPath,
                                                       dataDirName, wineMech)) {
                      log::warn(
                          "Failed to sync plugins.txt back from prefix '{}'",
                          prefixPathStr);
                    }
                  }
                }
              }

              // FileTime-based games (Skyrim LE, FO3, FNV) derive the load
              // order from plugin file mtimes rather than loadorder.txt.  Drop
              // the profile copy so refreshESPList falls back to file times —
              // LOOT updates those directly on the .esp files via FUSE setattr.
              if (fileTimeLoadOrder && launchProfile != nullptr) {
                log::debug(
                    "removing loadorder.txt (FileTime load order mechanism)");
                QFile::remove(launchProfile->getLoadOrderFileName());
              }
            },
            [&]() {
              // Refresh scheduling is a guaranteed stage after physical VFS
              // teardown. Optional profile/plugin synchronization above may
              // fail, but it must never turn WaitForRefresh into a false
              // success without starting a directory generation.
              if (!work->triggerRefresh) {
                return false;
              }
              return refreshAfterRun(work->profileName, work->refreshComplete);
            });
        postRunFailure = postRefresh.postFailure;
        refreshFailure = postRefresh.refreshFailure;
        refreshScheduled = postRefresh.refreshScheduled;
        if (work->triggerRefresh && !refreshScheduled && !refreshFailure) {
          refreshFailure = std::make_exception_ptr(
              std::runtime_error("post-run refresh was not scheduled"));
        }

        // These callbacks should not fiddle with directory structure and
        // ESPs. The released VFS reservation permits a callback to start its
        // successor, while the still-tracked context prevents in-process
        // restart until this afterRun invocation has fully returned.
        try {
          m_FinishedRun(work->binary.absoluteFilePath(), work->exitCode);
        } catch (...) {
          finishedRunFailure = std::current_exception();
        }
      },
      [&]() {
        if (work->cleanupComplete) {
          work->cleanupComplete(refreshScheduled);
        }
      });

  if (postRunFailure) {
    log::error(
        "afterRun: optional post-run synchronization failed for '{}': {}",
        work->binary.absoluteFilePath().toStdString(),
        cleanupFailureMessage(postRunFailure).toStdString());
  }
  if (refreshFailure) {
    log::error("afterRun: refresh startup failed for '{}': {}",
               work->binary.absoluteFilePath().toStdString(),
               cleanupFailureMessage(refreshFailure).toStdString());
  }
  if (finishedRunFailure) {
    log::error("afterRun: finished-run callback failed for '{}': {}",
               work->binary.absoluteFilePath().toStdString(),
               cleanupFailureMessage(finishedRunFailure).toStdString());
  }
  if (finalization.postFailure) {
    log::error(
        "afterRun: non-mandatory post-run processing failed for '{}': {}",
        work->binary.absoluteFilePath().toStdString(),
        cleanupFailureMessage(finalization.postFailure).toStdString());
  }
  if (finalization.trackerFailure) {
    log::error(
        "afterRun: tracker release failed for '{}': {}; cleanup completion "
        "was withheld and ownership remains retained",
        work->launchToken.toStdString(),
        cleanupFailureMessage(finalization.trackerFailure).toStdString());
  }
  if (finalization.completionFailure) {
    log::error("afterRun: cleanup completion callback failed for '{}': {}",
               work->launchToken.toStdString(),
               cleanupFailureMessage(finalization.completionFailure).toStdString());
  }

  if (!finalization.trackerFinished) {
    return {AfterRunState::CleanupPending, refreshScheduled};
  }
  if (refreshFailure) {
    return {AfterRunState::RefreshFailed, false};
  }
  return {AfterRunState::Complete, refreshScheduled};
}

bool OrganizerCore::refreshAfterRun(const QString& profileName,
                                    std::function<void()> refreshComplete)
{
  if (m_PersistenceSuppressed) {
    // Fail-stop never starts another persistence-capable refresh generation,
    // but WaitForRefresh completion is mandatory launch/mutation cleanup.
    if (refreshComplete) {
      try {
        refreshComplete();
      } catch (const std::exception& e) {
        log::error("fail-stop refresh completion callback failed: {}", e.what());
      } catch (...) {
        log::error("fail-stop refresh completion callback failed");
      }
    }
    return true;
  }

  AfterRunRefreshQueue::Request request{profileName,
                                        std::move(refreshComplete)};
  if (m_DirectoryUpdate) {
    m_AfterRunRefreshQueue.enqueue(std::move(request));
    return true;
  }

  std::vector<AfterRunRefreshQueue::Request> requests;
  requests.push_back(std::move(request));
  startAfterRunRefreshBatch(std::move(requests));
  return true;
}

void OrganizerCore::startAfterRunRefreshBatch(
    std::vector<AfterRunRefreshQueue::Request> requests)
{
  if (requests.empty()) {
    return;
  }

  if (m_DirectoryUpdate) {
    for (auto& request : requests) {
      m_AfterRunRefreshQueue.enqueue(std::move(request));
    }
    return;
  }

  bool refreshCurrentPluginList = false;
  std::vector<std::function<void()>> completions;
  const QString currentProfile =
      m_CurrentProfile != nullptr ? m_CurrentProfile->name() : QString{};
  for (auto& request : requests) {
    if (request.complete) {
      completions.push_back(std::move(request.complete));
    }
    if (m_CurrentProfile != nullptr &&
        (request.profileName.isEmpty() || request.profileName == currentProfile)) {
      refreshCurrentPluginList = true;
    }
  }

  refreshDirectoryStructure();
  m_AfterRunRefreshQueue.assign(m_ActiveDirectoryRefreshGeneration,
                                std::move(completions));

  // The plugin-list model belongs to the active UI profile. A launch for a
  // different profile still gets its own durable prefix/save/INI cleanup,
  // while the directory refresh safely exposes physical mod/overwrite
  // changes without importing that profile's plugin state into the UI.
  if (refreshCurrentPluginList) {
    refreshESPList(true);
    savePluginList();
  }
  cycleDiagnostics();
}

void OrganizerCore::completeAfterRunRefresh(std::uint64_t generation)
{
  auto completions = m_AfterRunRefreshQueue.takeAssigned(generation);
  for (auto& complete : completions) {
    try {
      complete();
    } catch (const std::exception& e) {
      log::error("post-run refresh completion callback failed: {}", e.what());
    } catch (...) {
      log::error("post-run refresh completion callback failed");
    }
  }
}

void OrganizerCore::completeAllAfterRunRefreshForFailStop()
{
  auto completions =
      m_AfterRunRefreshQueue.takeAllCompletionsForFailStop();
  for (auto& complete : completions) {
    try {
      complete();
    } catch (const std::exception& e) {
      log::error("fail-stop refresh completion callback failed: {}", e.what());
    } catch (...) {
      log::error("fail-stop refresh completion callback failed");
    }
  }
}

void OrganizerCore::schedulePendingAfterRunRefresh()
{
  if (!m_AfterRunRefreshQueue.hasPending() || m_DirectoryUpdate) {
    return;
  }

  auto requests = m_AfterRunRefreshQueue.takePending();
  startAfterRunRefreshBatch(std::move(requests));
}

ProcessRunner::Results
OrganizerCore::waitForAllUSVFSProcesses(UILocker::Reasons reason) const
{
  // Lifetime observation is already asynchronous on Linux. Never block (or
  // join a monitor) on the UI thread, but do keep OrganizerCore/FuseConnector
  // alive until every VFS and native launch has completed afterRun cleanup.
  const auto active = m_ProcessLaunchContext.activeLaunches();
  if (!ProcessShutdownPolicy::allowsCoreDestruction(active)) {
    if (reason != UILocker::NoReason) {
      log::warn("exit/restart deferred: {} tracked application launch(es) are "
                "still active ({} VFS, {} native)",
                active.total, active.vfs, active.native);
    }
    return ProcessRunner::Cancelled;
  }

  return ProcessRunner::Completed;
}

void OrganizerCore::prepareForExternalShutdown()
{
  m_ProcessLaunchContext.suppressNewReservations();
  m_DownloadManager.suppressAdmissionForShutdown();
  m_DownloadManager.pauseAll();
}

bool OrganizerCore::waitForDirectoryRefreshes()
{
  QPointer<OrganizerCore> self(this);
  return process_coordination::waitUntilIdle(
      [self]() {
        return self && self->m_DirectoryUpdate.load(std::memory_order_acquire);
      },
      [this, self]() {
        QEventLoop loop;
        connect(this, &OrganizerCore::directoryRefreshRetired, &loop,
                &QEventLoop::quit, Qt::QueuedConnection);
        connect(this, &QObject::destroyed, &loop, &QEventLoop::quit);
        loop.exec();
        return !self.isNull();
      });
}

std::vector<Mapping> OrganizerCore::fileMapping(const QString& profileName,
                                                const QString& customOverwrite)
{
  if (!waitForDirectoryRefreshes()) {
    return {};
  }

  IPluginGame* game = qApp->property("managed_game").value<IPluginGame*>();
  Profile profile(QDir(m_Settings.paths().profiles() + "/" + profileName), game,
                  gameFeatures());

  MappingType result;

  auto dataMaps = game->getModMappings();

  bool overwriteActive = false;

  for (const auto& mod : profile.getActiveMods()) {
    if (std::get<0>(mod).compare("overwrite", Qt::CaseInsensitive) == 0) {
      continue;
    }

    unsigned int const modIndex = ModInfo::getIndex(std::get<0>(mod));
    ModInfo::Ptr const modPtr   = ModInfo::getByIndex(modIndex);

    bool const createTarget = customOverwrite == std::get<0>(mod);
    QDir const modDir       = QDir(std::get<1>(mod));

    overwriteActive |= createTarget;

    if (modPtr->isRegular()) {
      for (auto dataMap : dataMaps.asKeyValueRange()) {
        auto mapDir = QDir(modDir.absoluteFilePath(dataMap.first));
        if (mapDir.exists()) {
          for (const auto& dir : dataMap.second) {
            result.insert(result.end(),
                          {mapDir.absolutePath(), dir, true, createTarget});
          }
        }
      }
    }
  }

  if (!overwriteActive && !customOverwrite.isEmpty()) {
    throw MyException(
        tr("The designated write target \"%1\" is not enabled.").arg(customOverwrite));
  }

  if (profile.localSavesEnabled()) {
    auto localSaves = gameFeatures().gameFeature<LocalSavegames>();
    if (localSaves != nullptr) {
      MappingType saveMap =
          localSaves->mappings(profile.absolutePath() + "/saves");
      result.reserve(result.size() + saveMap.size());
      result.insert(result.end(), saveMap.begin(), saveMap.end());
    } else {
      log::warn("local save games not supported by this game plugin");
    }
  }

  QDir const overwriteDir(m_Settings.paths().overwrite());
  for (auto dataMap : dataMaps.asKeyValueRange()) {
    auto overwriteSubpath = overwriteDir.absoluteFilePath(dataMap.first);
    if (QDir(overwriteSubpath).exists()) {
      for (const auto& dir : dataMap.second) {
        result.insert(result.end(),
                      {overwriteSubpath, dir, true, customOverwrite.isEmpty()});
      }
    }
  }

  for (MOBase::IPluginFileMapper* mapper :
       m_PluginContainer->plugins<MOBase::IPluginFileMapper>()) {
    IPlugin* plugin = dynamic_cast<IPlugin*>(mapper);
    if (m_PluginContainer->isEnabled(plugin)) {
      MappingType pluginMap = mapper->mappings();
      result.reserve(result.size() + pluginMap.size());
      result.insert(result.end(), pluginMap.begin(), pluginMap.end());
    }
  }

  return result;
}

std::vector<Mapping> OrganizerCore::fileMapping(const QString& dataPath,
                                                const QString& relPath,
                                                const DirectoryEntry* base,
                                                const DirectoryEntry* directoryEntry,
                                                int createDestination)
{
  std::vector<Mapping> result;

  for (const FileEntryPtr& current : directoryEntry->getFiles()) {
    bool isArchive = false;
    int const origin     = current->getOrigin(isArchive);
    if (isArchive || (origin == 0)) {
      continue;
    }

    QString const originPath = QString::fromStdWString(base->getOriginByID(origin).getPath());
    QString const fileName   = QString::fromStdWString(current->getName());
    //    QString fileName = ToQString(current->getName());
    QString const source = originPath + relPath + fileName;
    QString const target = dataPath + relPath + fileName;
    if (source != target) {
      result.push_back({source, target, false, false});
    }
  }

  // recurse into subdirectories
  for (const auto& d : directoryEntry->getSubDirectories()) {
    int const origin = d->anyOrigin();

    QString const originPath = QString::fromStdWString(base->getOriginByID(origin).getPath());
    QString const dirName    = QString::fromStdWString(d->getName());
    QString const source     = originPath + relPath + dirName;
    QString const target     = dataPath + relPath + dirName;

    bool const writeDestination = (base == directoryEntry) && (origin == createDestination);

    result.push_back({source, target, true, writeDestination});
    std::vector<Mapping> subRes =
        fileMapping(dataPath, relPath + dirName + "/", base, d, createDestination);
    result.insert(result.end(), subRes.begin(), subRes.end());
  }
  return result;
}
