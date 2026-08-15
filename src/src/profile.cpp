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

#include "profile.h"

#include <uibase/filesystemutilities.h>
#include "game_features.h"
#include "modinfo.h"
#include "modinfoforeign.h"
#include "profilelegacymigration.h"
#include "profilerename.h"
#include "profiletweakmerge.h"
#include "settings.h"
#include "settingswritebarrier.h"
#include "profilemodlistrename.h"
#include "shared/appconfig.h"
#include "shared/util.h"
#include <bsainvalidation.h>
#include <dataarchives.h>
#include <iplugingame.h>
#include <questionboxmemory.h>
#include <report.h>
#include <transactionalwritefile.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDirIterator>
#include <QEvent>
#include <QFile>      // for QFile
#include <QFlags>     // for operator|, QFlags
#include <QIODevice>  // for QIODevice, etc
#include <QMessageBox>
#include <QScopedArrayPointer>
#include <QStringList>  // for QStringList
#include <QtGlobal>     // for qUtf8Printable

#include <cassert>  // for assert
#include <climits>  // for UINT_MAX, INT_MAX, etc
#include <cstddef>  // for size_t
#include <cstring>  // for wcslen

#include <algorithm>  // for max, min
#include <exception>  // for exception
#include <functional>
#include <memory>
#include <mutex>
#include <set>  // for set
#include <stdexcept>
#include <unordered_set>
#include <utility>  // for find

using namespace MOBase;
using namespace MOShared;

namespace
{
SettingsWriteBarrier g_ProfileWriteBarrier(
    SettingsWriteBarrier::Concurrency::Serialized);
std::recursive_mutex g_ProfileSettingsMutex;
std::unordered_set<QSettings*> g_ProfileSettings;

void registerProfileSettings(QSettings* settings)
{
  const std::lock_guard lock(g_ProfileSettingsMutex);
  g_ProfileSettings.insert(settings);
}

void unregisterProfileSettings(QSettings* settings) noexcept
{
  try {
    const std::lock_guard lock(g_ProfileSettingsMutex);
    g_ProfileSettings.erase(settings);
  } catch (...) {
  }
}

bool removeAllProfileSettingsSyncEvents() noexcept
{
  try {
    const std::lock_guard lock(g_ProfileSettingsMutex);
    for (auto* settings : g_ProfileSettings) {
      if (settings != nullptr) {
        QCoreApplication::removePostedEvents(settings, QEvent::UpdateRequest);
      }
    }
    return true;
  } catch (...) {
    return false;
  }
}

SettingsWriteBarrier::MutationLease admitProfileConstruction()
{
  auto lease = g_ProfileWriteBarrier.enterIfAllowed();
  if (!lease) {
    throw std::runtime_error("profile construction rejected during fail-stop");
  }
  return lease;
}
}  // namespace

// MOBase::resolveFileCaseInsensitive moved to MOBase::resolveFileCaseInsensitive

void Profile::touchFile(QString fileName)
{
  QFile modList(m_Directory.filePath(fileName));
  if (!modList.open(QIODevice::ReadWrite)) {
    throw std::runtime_error(QObject::tr("failed to create %1")
                                 .arg(m_Directory.filePath(fileName))
                                 .toUtf8()
                                 .constData());
  }
}

Profile::Profile(const QString& name, IPluginGame const* gamePlugin,
                 GameFeatures const& gameFeatures, bool useDefaultSettings)
    : m_ModListWriter(std::bind(&Profile::doWriteModlist, this)),
      m_GamePlugin(gamePlugin), m_GameFeatures(gameFeatures)
{
  auto writeLease = admitProfileConstruction();
  QString profilesDir = Settings::instance().paths().profiles();
  QDir profileBase(profilesDir);
  QString fixedName = name;
  if (!fixDirectoryName(fixedName)) {
    throw MyException(tr("invalid profile name: %1").arg(qUtf8Printable(name)));
  }

  if (!profileBase.exists() || !profileBase.mkdir(fixedName)) {
    throw MyException(tr("failed to create %1").arg(fixedName).toUtf8().constData());
  }
  QString fullPath = profilesDir + "/" + fixedName;
  m_Directory      = QDir(fullPath);
  auto settings = std::make_unique<QSettings>(
      m_Directory.absoluteFilePath("settings.ini"), QSettings::IniFormat);
  m_Settings = settings.get();

  try {
    registerProfileSettings(m_Settings);
    // create files. Needs to happen after m_Directory was set!
    touchFile("modlist.txt");
    touchFile("archives.txt");

    IPluginGame::ProfileSettings settings =
        IPluginGame::CONFIGURATION | IPluginGame::MODS | IPluginGame::SAVEGAMES;

    if (useDefaultSettings) {
      settings |= IPluginGame::PREFER_DEFAULTS;
    }

    gamePlugin->initializeProfile(fullPath, settings);
    findProfileSettings();
    refreshModStatus();
  } catch (...) {
    unregisterProfileSettings(m_Settings);
    settings.reset();
    m_Settings = nullptr;
    // clean up in case of an error
    shellDelete(QStringList(profileBase.absoluteFilePath(fixedName)));
    throw;
  }
  settings.release();
}

Profile::Profile(const QDir& directory, IPluginGame const* gamePlugin,
                 GameFeatures const& gameFeatures)
    : m_Directory(directory), m_GamePlugin(gamePlugin), m_GameFeatures(gameFeatures),
      m_ModListWriter(std::bind(&Profile::doWriteModlist, this))
{
  auto writeLease = admitProfileConstruction();
  assert(gamePlugin != nullptr);

  auto settingsBackend = std::make_unique<QSettings>(
      directory.absoluteFilePath("settings.ini"), QSettings::IniFormat);
  m_Settings = settingsBackend.get();
  registerProfileSettings(m_Settings);
  try {
    findProfileSettings();

    if (!QFile::exists(m_Directory.filePath("modlist.txt"))) {
      log::warn("missing modlist.txt in {}", directory.path());
      touchFile(m_Directory.filePath("modlist.txt"));
    }

    IPluginGame::ProfileSettings settings =
        IPluginGame::MODS | IPluginGame::SAVEGAMES;
    gamePlugin->initializeProfile(directory, settings);

    refreshModStatus();
  } catch (...) {
    unregisterProfileSettings(m_Settings);
    throw;
  }
  settingsBackend.release();
}

Profile::Profile(const Profile& reference)
    : m_Directory(reference.m_Directory),
      m_ModListWriter(std::bind(&Profile::doWriteModlist, this)),
      m_GamePlugin(reference.m_GamePlugin), m_GameFeatures(reference.m_GameFeatures)

{
  auto writeLease = admitProfileConstruction();
  auto settings = std::make_unique<QSettings>(
      m_Directory.absoluteFilePath("settings.ini"), QSettings::IniFormat);
  m_Settings = settings.get();
  registerProfileSettings(m_Settings);
  try {
    findProfileSettings();
    refreshModStatus();
  } catch (...) {
    unregisterProfileSettings(m_Settings);
    throw;
  }
  settings.release();
}

Profile::~Profile()
{
  auto writeLease = g_ProfileWriteBarrier.enterIfAllowed();
  if (!writeLease) {
    try {
      m_ModListWriter.cancel();
    } catch (...) {
    }
    // QSettings synchronizes pending changes from its destructor. Fail-stop
    // intentionally retains this process-lifetime backend until _Exit so a
    // suppressed old profile cannot flush during an admitted profile switch.
    (void)fail_stop::retainBackendForProcessLifetime(m_Settings);
    return;
  }

  unregisterProfileSettings(m_Settings);

  try {
    m_ModListWriter.writeImmediately(true);
  } catch (const std::exception& e) {
    log::error("failed to flush mod list during profile shutdown: {}", e.what());
  } catch (...) {
    log::error("failed to flush mod list during profile shutdown: unknown exception");
  }

  delete m_Settings;
}

void Profile::findProfileSettings()
{
  auto writeLease = g_ProfileWriteBarrier.enterIfAllowed();
  if (!writeLease) {
    throw std::runtime_error("profile migration rejected during fail-stop");
  }

  constexpr auto localSavesIntent = "LegacyMigration/LocalSaves";
  constexpr auto localInisIntent  = "LegacyMigration/LocalSettings";
  constexpr auto savesToBackup    = "saves-to-backup";
  constexpr auto backupToSaves    = "backup-to-saves";
  constexpr auto iniBackupToLive  = "ini-backup-to-live";

  auto migrate = [&](const QString& intentKey, const QString& operation,
                     const QString& source, const QString& destination,
                     ProfileLegacyMigration::EntryKind kind,
                     const QString& finalSetting, const QVariant& finalValue) {
    const auto result = ProfileLegacyMigration::migrate(
        *m_Settings, m_Directory.absolutePath(), intentKey, operation, source,
        destination, kind, finalSetting, finalValue);
    if (!result.succeeded()) {
      throw std::runtime_error(
          tr("Legacy profile migration failed: %1").arg(result.error).toStdString());
    }
  };

  if (setting("", "LocalSaves") == QVariant()) {
    const QString pending =
        ProfileLegacyMigration::pendingOperation(*m_Settings, localSavesIntent);
    if (pending == savesToBackup) {
      migrate(localSavesIntent, savesToBackup, "saves", "_saves",
              ProfileLegacyMigration::EntryKind::Directory, "LocalSaves", false);
    } else if (pending == backupToSaves) {
      migrate(localSavesIntent, backupToSaves, "_saves", "saves",
              ProfileLegacyMigration::EntryKind::Directory, "LocalSaves", true);
    } else if (!pending.isEmpty()) {
      throw std::runtime_error(
          tr("The pending local-save migration record is invalid.").toStdString());
    } else {
      const QFileInfo saves(m_Directory.absoluteFilePath("saves"));
      const QFileInfo backup(m_Directory.absoluteFilePath("_saves"));
      const bool savesPresent  = saves.exists() || saves.isSymLink();
      const bool backupPresent = backup.exists() || backup.isSymLink();
      if (savesPresent && backupPresent) {
        throw std::runtime_error(
            tr("Both the profile save directory and its legacy backup exist: %1 and %2")
                .arg(saves.absoluteFilePath(), backup.absoluteFilePath())
                .toStdString());
      }
      if ((savesPresent && (saves.isSymLink() || !saves.isDir())) ||
          (backupPresent && (backup.isSymLink() || !backup.isDir()))) {
        throw std::runtime_error(
            tr("The profile save migration path is not an ordinary directory.")
                .toStdString());
      }

      if (savesPresent) {
        if (!Settings::instance().profileLocalSaves()) {
          migrate(localSavesIntent, savesToBackup, "saves", "_saves",
                  ProfileLegacyMigration::EntryKind::Directory, "LocalSaves", false);
        } else {
          storeSetting("", "LocalSaves", true);
        }
      } else if (backupPresent) {
        if (Settings::instance().profileLocalSaves()) {
          migrate(localSavesIntent, backupToSaves, "_saves", "saves",
                  ProfileLegacyMigration::EntryKind::Directory, "LocalSaves", true);
        } else {
          storeSetting("", "LocalSaves", false);
        }
      } else {
        storeSetting("", "LocalSaves", Settings::instance().profileLocalSaves());
      }
    }
  }

  if (setting("", "LocalSettings") == QVariant()) {
    const QString liveIni = QFileInfo(getIniFileName()).fileName();
    const QString backup  = liveIni.isEmpty() ? QString{} : liveIni + "_";
    const QString pending =
        ProfileLegacyMigration::pendingOperation(*m_Settings, localInisIntent);
    if (pending == iniBackupToLive && !liveIni.isEmpty()) {
      migrate(localInisIntent, iniBackupToLive, backup, liveIni,
              ProfileLegacyMigration::EntryKind::File, "LocalSettings", true);
    } else if (!pending.isEmpty()) {
      throw std::runtime_error(
          tr("The pending local-settings migration record is invalid.").toStdString());
    } else if (!backup.isEmpty()) {
      const QFileInfo backupInfo(m_Directory.absoluteFilePath(backup));
      if (backupInfo.exists() || backupInfo.isSymLink()) {
        migrate(localInisIntent, iniBackupToLive, backup, liveIni,
                ProfileLegacyMigration::EntryKind::File, "LocalSettings", true);
      } else if (Settings::instance().profileLocalInis()) {
        storeSetting("", "LocalSettings", true);
        enableLocalSettings(true);
      } else {
        storeSetting("", "LocalSettings", false);
      }
    } else if (Settings::instance().profileLocalInis()) {
      storeSetting("", "LocalSettings", true);
      enableLocalSettings(true);
    } else {
      storeSetting("", "LocalSettings", false);
    }
  }

  if (setting("", "AutomaticArchiveInvalidation") == QVariant()) {
    auto invalidation = m_GameFeatures.gameFeature<BSAInvalidation>();
    auto dataArchives = m_GameFeatures.gameFeature<DataArchives>();
    bool found        = false;
    if ((invalidation != nullptr) && (dataArchives != nullptr)) {
      for (const QString& archive : dataArchives->archives(this)) {
        if (invalidation->isInvalidationBSA(archive)) {
          found = true;
          break;
        }
      }
    }
    if (found) {
      if (!Settings::instance().profileArchiveInvalidation()) {
        deactivateInvalidation();
      } else {
        storeSetting("", "AutomaticArchiveInvalidation", true);
      }
    } else {
      if (Settings::instance().profileArchiveInvalidation()) {
        activateInvalidation();
      } else {
        storeSetting("", "AutomaticArchiveInvalidation", false);
      }
    }
  }
}

bool Profile::exists() const
{
  return m_Directory.exists();
}

void Profile::writeModlist()
{
  g_ProfileWriteBarrier.runIfAllowed([&] { m_ModListWriter.write(); });
}

void Profile::writeModlistNow(bool onlyIfPending)
{
  if (!g_ProfileWriteBarrier.runIfAllowed(
          [&] { m_ModListWriter.writeImmediately(onlyIfPending); })) {
    m_ModListWriter.cancel();
  }
}

void Profile::cancelModlistWrite()
{
  m_ModListWriter.cancel();
}

void Profile::suppressWritesForFailedRollback() noexcept
{
  suppressAllWritesForFailedRollback();
  try {
    m_ModListWriter.cancel();
  } catch (...) {
    // Admission remains fail-closed even if delayed-writer cancellation fails.
  }
}

void Profile::suppressAllWritesForFailedRollback() noexcept
{
  g_ProfileWriteBarrier.suppress();
  removeAllProfileSettingsSyncEvents();
}

void Profile::doWriteModlist()
{
  g_ProfileWriteBarrier.runIfAllowed([&] {
    if (!m_Directory.exists())
      return;

    try {
      if (m_ModStatus.empty()) {
        return;
      }

      const QString fileName = getModlistFileName();
      QByteArray contents(
          "# This file was automatically generated by Mod Organizer.\r\n");

      for (auto iter = m_ModIndexByPriority.crbegin();
           iter != m_ModIndexByPriority.crend(); iter++) {
        // the priority order was inverted on load so it has to be inverted again
        const auto index     = iter->second;
        ModInfo::Ptr modInfo = ModInfo::getByIndex(index);
        if (!modInfo->hasAutomaticPriority()) {
          if (modInfo->isForeign()) {
            contents.append('*');
          } else if (m_ModStatus[index].m_Enabled) {
            contents.append('+');
          } else {
            contents.append('-');
          }
          contents.append(modInfo->name().toUtf8());
          contents.append("\r\n");
        }
      }

      TransactionalWriteFile file(fileName);
      if (!file.replaceWith(contents)) {
        reportError(tr("failed to write mod list: %1").arg(file.errorString()));
      }
    } catch (const std::exception& e) {
      reportError(tr("failed to write mod list: %1").arg(e.what()));
    }
  });
}

bool Profile::createTweakedIniFile()
{
  auto writeLease = g_ProfileWriteBarrier.enterIfAllowed();
  if (!writeLease) {
    return false;
  }

  QStringList tweakFiles;

  for (const auto& [priority, index] : m_ModIndexByPriority) {
    if (m_ModStatus[index].m_Enabled) {
      ModInfo::Ptr modInfo = ModInfo::getByIndex(index);
      const auto modTweaks = modInfo->getIniTweaks();
      for (const QString& tweak : modTweaks) {
        tweakFiles.append(tweak);
      }
    }
  }
  tweakFiles.append(getProfileTweaks());

  QString error;
  if (!ProfileTweakMerge::publish(
          tweakFiles, m_Directory.absoluteFilePath("initweaks.ini"), &error)) {
    reportError(tr("failed to create tweaked ini: %1").arg(error));
    return false;
  }
  return true;
}

// static
void Profile::renameModInAllProfiles(const QString& oldName, const QString& newName)
{
  auto writeLease = g_ProfileWriteBarrier.enterIfAllowed();
  if (!writeLease) {
    return;
  }

  QDir profilesDir(Settings::instance().paths().profiles());
  profilesDir.setFilter(QDir::AllDirs | QDir::NoDotAndDotDot);
  QDirIterator profileIter(profilesDir);
  while (profileIter.hasNext()) {
    profileIter.next();
    QFile modList(profileIter.filePath() + "/modlist.txt");
    if (modList.exists())
      renameModInList(modList, oldName, newName);
    else
      log::warn("Profile has no modlist.txt: {}", profileIter.filePath());
  }
}

// static
void Profile::renameModInList(QFile& modList, const QString& oldName,
                              const QString& newName)
{
  auto writeLease = g_ProfileWriteBarrier.enterIfAllowed();
  if (!writeLease) {
    return;
  }

  const auto result =
      ProfileModlistRename::apply(modList.fileName(), oldName, newName);
  if (result.status == ProfileModlistRename::Status::ReadError ||
      result.status == ProfileModlistRename::Status::WriteError) {
    reportError(tr("failed to update mod list '%1': %2")
                    .arg(modList.fileName(), result.error));
    return;
  }

  if (result.status == ProfileModlistRename::Status::Changed) {
    log::debug(R"(Renamed {} "{}" mod to "{}" in {})", result.renamed, oldName,
               newName, modList.fileName());
  }
}

void Profile::refreshModStatus()
{
  auto writeLease = g_ProfileWriteBarrier.enterIfAllowed();
  if (!writeLease) {
    return;
  }

  // this function refreshes mod status (enabled/disabled) and priority
  // using the profile mod list file and the mods in the mods folder using
  // the following steps
  //
  // 1) the mod list file is read and mods status/priority are updated by
  //    considering the content of the file (for status) and the order (for
  //    priority), missing or invalid mods are discarded (with a warning)
  // 2) the priority are reversed to match the plugin list (highest wins)
  //    since the mod list is written in reverse order
  // 3) at the same time, new mods (not in the mod list file) are added
  //    - foreign mods are given low priority (below 0)
  //    - regular mods are given high priority (above mods from the mod list)
  // 4) the priority are shifted to ensure that the minimum priority is 0
  // 5) the priority of backups are computed such that the first backup is
  //    above all regular mods
  //
  // in the context of the profile, "regular mods" means a mod whose priority
  // can be set by the user (i.e. not a backup or overwrite)
  //
  // this method ensures that the mods priority is as follow
  //
  //   0   mod1
  //   1   mod2
  //       ...
  //   K-1 modK (K = m_NumRegularMods)
  //   K   backup1
  //   K+1 backup2
  //       ...
  //   N-2 backupX
  //   N-1 overwrite (N = number of mods)
  //

  writeModlistNow(true);  // if there are pending changes write them first

  QFile file(getModlistFileName());
  if (!file.open(QIODevice::ReadOnly)) {
    throw MyException(
        tr("\"%1\" is missing or inaccessible").arg(getModlistFileName()));
  }

  bool modStatusModified = false;
  m_ModStatus.clear();
  m_ModStatus.resize(ModInfo::getNumMods());
  log::debug("refreshModStatus: ModInfo has {} entries", ModInfo::getNumMods());

  std::set<QString> namesRead;

  bool warnAboutOverwrite = false;
  unsigned int modsNotFound = 0;

  // load mods from file and update enabled state and priority for them
  int index                 = 0;
  const QByteArray contents = file.readAll();
  file.close();
  for (QByteArray& line : contents.split('\n')) {
    // find the mod name and the enabled status
    bool enabled = true;
    QString modName;
    if (line.isEmpty()) {
      // empty line
      continue;
    } else if (line.at(0) == '#') {
      // comment line
      continue;
    } else if (line.at(0) == '-') {
      enabled = false;
      modName = QString::fromUtf8(line.mid(1).trimmed().constData());
    } else if (line.at(0) == '+' || line.at(0) == '*') {
      modName = QString::fromUtf8(line.mid(1).trimmed().constData());
    } else {
      modName = QString::fromUtf8(line.trimmed().constData());
    }

    if (modName.isEmpty()) {
      continue;
    }

    if (modName.compare("overwrite", Qt::CaseInsensitive) == 0) {
      warnAboutOverwrite = true;
    }

    // check if the name was already read
    if (namesRead.contains(modName)) {
      continue;
    }
    namesRead.insert(modName);

    unsigned int modIndex = ModInfo::getIndex(modName);
    if (modIndex == UINT_MAX) {
      if (modsNotFound < 5) {
        log::warn(R"(mod not found: "{}" (profile "{}"))", modName, m_Directory.path());
      }
      ++modsNotFound;
      // need to rewrite the modlist to fix this
      modStatusModified = true;
      continue;
    }

    // find the mod and check that this is a regular mod (and not a backup)
    ModInfo::Ptr info = ModInfo::getByIndex(modIndex);
    if (modIndex < m_ModStatus.size() && !info->hasAutomaticPriority()) {
      m_ModStatus[modIndex].m_Enabled = enabled;
      if (m_ModStatus[modIndex].m_Priority == -1) {
        if (static_cast<size_t>(index) >= m_ModStatus.size()) {
          throw Exception(tr("invalid mod index: %1").arg(index));
        }
        m_ModStatus[modIndex].m_Priority = index++;
      }
    } else {
      log::warn(R"(no mod state for "{}" (profile "{}"))", modName,
                m_Directory.path());
      // need to rewrite the modlist to fix this
      modStatusModified = true;
    }
  }

  if (modsNotFound > 0) {
    log::error("refreshModStatus: {} mods from modlist.txt were not found in "
               "ModInfo (total ModInfo entries: {})",
               modsNotFound, ModInfo::getNumMods());
  }

  const int numKnownMods = index;
  int topInsert          = 0;

  // invert priority order to match that of the pluginlist, also
  // give priorities to mods not referenced in the profile and
  // count the number of regular mods
  m_NumRegularMods = 0;
  for (size_t i = 0; i < m_ModStatus.size(); ++i) {
    ModInfo::Ptr modInfo = ModInfo::getByIndex(static_cast<int>(i));
    if (modInfo->alwaysEnabled()) {
      m_ModStatus[i].m_Enabled = true;
    }

    if (modInfo->isOverwrite()) {
      m_ModStatus[i].m_Priority = m_ModStatus.size() - 1;
      continue;
    }

    if (m_ModStatus[i].m_Priority != -1) {
      m_ModStatus[i].m_Priority = numKnownMods - m_ModStatus[i].m_Priority - 1;
      ++m_NumRegularMods;
    } else {
      if (static_cast<size_t>(index) >= m_ModStatus.size()) {
        throw Exception(tr("invalid mod index: %1").arg(index));
      }

      // skip backups on purpose to avoid inserting backups in-between
      // regular mods
      if (modInfo->isForeign()) {
        m_ModStatus[i].m_Priority = --topInsert;
        ++m_NumRegularMods;
      } else if (!modInfo->isBackup()) {
        m_ModStatus[i].m_Priority = index++;
        ++m_NumRegularMods;
      }

      // also, mark the mod-list as changed
      modStatusModified = true;
    }
  }

  // to support insertion of new mods at the top we may now have mods with negative
  // priority, so shift them all up to align priority with 0
  if (topInsert < 0) {
    int offset = topInsert * -1;
    for (size_t i = 0; i < m_ModStatus.size(); ++i) {
      ModInfo::Ptr modInfo = ModInfo::getByIndex(static_cast<unsigned int>(i));
      if (modInfo->hasAutomaticPriority()) {
        continue;
      }

      m_ModStatus[i].m_Priority += offset;
    }
  }

  // set the backups priority
  int backupPriority = m_NumRegularMods;
  for (size_t i = 0; i < m_ModStatus.size(); ++i) {
    ModInfo::Ptr modInfo = ModInfo::getByIndex(static_cast<unsigned int>(i));
    if (modInfo->isBackup()) {
      m_ModStatus[i].m_Priority = backupPriority++;
    }
  }

  updateIndices();

  // User has a mod named some variation of "overwrite".  Tell them about it.
  if (warnAboutOverwrite) {
    reportError(tr("A mod named \"overwrite\" was detected, disabled, and moved to the "
                   "highest priority on the mod list. "
                   "You may want to rename this mod and enable it again."));
    // also, mark the mod-list as changed
    modStatusModified = true;
  }

  if (modStatusModified) {
    m_ModListWriter.write();
  }
}

void Profile::dumpModStatus() const
{
  for (unsigned int i = 0; i < m_ModStatus.size(); ++i) {
    ModInfo::Ptr info = ModInfo::getByIndex(i);
    log::warn("{}: {} - {} ({})", i, info->name(), m_ModStatus[i].m_Priority,
              m_ModStatus[i].m_Enabled ? "enabled" : "disabled");
  }
}

void Profile::updateIndices()
{
  m_ModIndexByPriority.clear();
  for (unsigned int i = 0; i < m_ModStatus.size(); ++i) {
    int priority                   = m_ModStatus[i].m_Priority;
    m_ModIndexByPriority[priority] = i;
  }
}

std::vector<std::tuple<QString, QString, int>> Profile::getActiveMods()
{
  std::vector<std::tuple<QString, QString, int>> result;
  for (const auto& [priority, index] : m_ModIndexByPriority) {
    if (m_ModStatus[index].m_Enabled) {
      ModInfo::Ptr modInfo = ModInfo::getByIndex(index);
      result.emplace_back(modInfo->internalName(), modInfo->absolutePath(),
                                       m_ModStatus[index].m_Priority);
    }
  }

  return result;
}

void Profile::setModEnabled(unsigned int index, bool enabled)
{
  auto writeLease = g_ProfileWriteBarrier.enterIfAllowed();
  if (!writeLease) {
    return;
  }

  if (index >= m_ModStatus.size()) {
    throw MyException(tr("invalid mod index: %1").arg(index));
  }

  ModInfo::Ptr modInfo = ModInfo::getByIndex(index);

  // we could quit in the following case, this shouldn't be a change anyway,
  // but at least this allows the situation to be fixed in case of an error
  if (modInfo->alwaysEnabled()) {
    enabled = true;
  }
  if (modInfo->alwaysDisabled()) {
    enabled = false;
  }

  if (enabled != m_ModStatus[index].m_Enabled) {
    m_ModStatus[index].m_Enabled = enabled;
    emit modStatusChanged(index);
  }
}

void Profile::setModsEnabled(const QList<unsigned int>& modsToEnable,
                             const QList<unsigned int>& modsToDisable)
{
  auto writeLease = g_ProfileWriteBarrier.enterIfAllowed();
  if (!writeLease) {
    return;
  }

  QList<unsigned int> dirtyMods;
  for (auto idx : modsToEnable) {
    if (idx >= m_ModStatus.size()) {
      log::error("invalid mod index: {}", idx);
      continue;
    }
    if (ModInfo::getByIndex(idx)->alwaysDisabled()) {
      continue;
    }
    if (!m_ModStatus[idx].m_Enabled) {
      m_ModStatus[idx].m_Enabled = true;
      dirtyMods.append(idx);
    }
  }
  for (auto idx : modsToDisable) {
    if (idx >= m_ModStatus.size()) {
      log::error("invalid mod index: {}", idx);
      continue;
    }
    if (ModInfo::getByIndex(idx)->alwaysEnabled()) {
      continue;
    }
    if (m_ModStatus[idx].m_Enabled) {
      m_ModStatus[idx].m_Enabled = false;
      dirtyMods.append(idx);
    }
  }
  if (!dirtyMods.isEmpty()) {
    emit modStatusChanged(dirtyMods);
  }
}

bool Profile::modEnabled(unsigned int index) const
{
  if (index >= m_ModStatus.size()) {
    throw MyException(tr("invalid mod index: %1").arg(index));
  }

  return m_ModStatus[index].m_Enabled;
}

int Profile::getModPriority(unsigned int index) const
{
  if (index >= m_ModStatus.size()) {
    throw MyException(tr("invalid mod index: %1").arg(index));
  }

  return m_ModStatus[index].m_Priority;
}

bool Profile::setModPriority(unsigned int index, int& newPriority)
{
  auto writeLease = g_ProfileWriteBarrier.enterIfAllowed();
  if (!writeLease) {
    return false;
  }

  if (ModInfo::getByIndex(index)->hasAutomaticPriority()) {
    // can't change priority of overwrite/backups
    return false;
  }

  newPriority = std::clamp(newPriority, 0, static_cast<int>(m_NumRegularMods) - 1);

  int oldPriority  = m_ModStatus.at(index).m_Priority;
  int lastPriority = INT_MIN;

  if (newPriority == oldPriority) {
    // nothing to do
    return false;
  }

  for (const auto& [priority, index] : m_ModIndexByPriority) {
    if (newPriority < oldPriority && priority >= newPriority &&
        priority < oldPriority) {
      m_ModStatus.at(index).m_Priority += 1;
    } else if (newPriority > oldPriority && priority <= newPriority &&
               priority > oldPriority) {
      m_ModStatus.at(index).m_Priority -= 1;
    }
    lastPriority = std::max(lastPriority, priority);
  }

  newPriority                      = std::min(newPriority, lastPriority);
  m_ModStatus.at(index).m_Priority = std::min(newPriority, lastPriority);

  updateIndices();
  m_ModListWriter.write();

  return true;
}

Profile* Profile::createPtrFrom(const QString& name, const Profile& reference,
                                MOBase::IPluginGame const* gamePlugin)
{
  auto writeLease = g_ProfileWriteBarrier.enterIfAllowed();
  if (!writeLease) {
    return nullptr;
  }

  QString profileDirectory = Settings::instance().paths().profiles() + "/" + name;
  reference.copyFilesTo(profileDirectory);
  return new Profile(QDir(profileDirectory), gamePlugin, reference.m_GameFeatures);
}

void Profile::copyFilesTo(QString& target) const
{
  g_ProfileWriteBarrier.runIfAllowed(
      [&] { copyDir(m_Directory.absolutePath(), target, false); });
}

std::vector<std::wstring> Profile::splitDZString(const wchar_t* buffer)
{
  std::vector<std::wstring> result;
  const wchar_t* pos = buffer;
  size_t length      = wcslen(pos);
  while (length != 0U) {
    result.emplace_back(pos);
    pos += length + 1;
    length = wcslen(pos);
  }
  return result;
}

bool Profile::invalidationActive(bool* supported) const
{
  auto invalidation = m_GameFeatures.gameFeature<BSAInvalidation>();
  auto dataArchives = m_GameFeatures.gameFeature<DataArchives>();

  if (supported != nullptr) {
    *supported = ((invalidation != nullptr) && (dataArchives != nullptr));
  }

  return setting("", "AutomaticArchiveInvalidation",
                 Settings::instance().profileArchiveInvalidation())
      .toBool();
}

void Profile::deactivateInvalidation()
{
  auto writeLease = g_ProfileWriteBarrier.enterIfAllowed();
  if (!writeLease) {
    return;
  }

  auto invalidation = m_GameFeatures.gameFeature<BSAInvalidation>();

  if (invalidation != nullptr) {
    invalidation->deactivate(this);
  }

  storeSetting("", "AutomaticArchiveInvalidation", false);
}

void Profile::activateInvalidation()
{
  auto writeLease = g_ProfileWriteBarrier.enterIfAllowed();
  if (!writeLease) {
    return;
  }

  auto invalidation = m_GameFeatures.gameFeature<BSAInvalidation>();

  if (invalidation != nullptr) {
    invalidation->activate(this);
  }

  storeSetting("", "AutomaticArchiveInvalidation", true);
}

bool Profile::localSavesEnabled() const
{
  return setting("", "LocalSaves", Settings::instance().profileLocalSaves()).toBool();
}

bool Profile::enableLocalSaves(bool enable)
{
  auto writeLease = g_ProfileWriteBarrier.enterIfAllowed();
  if (!writeLease) {
    return false;
  }

  if (enable) {
    if (!m_Directory.exists("saves")) {
      m_Directory.mkdir("saves");
    }
  } else {
    QDialogButtonBox::StandardButton res;
    res = QuestionBoxMemory::query(
        QApplication::activeModalWidget(), "deleteSavesQuery",
        tr("Delete profile-specific save games?"),
        tr("Do you want to delete the profile-specific save games? (If you select "
           "\"No\", the "
           "save games will show up again if you re-enable profile-specific save "
           "games)"),
        QDialogButtonBox::No | QDialogButtonBox::Yes | QDialogButtonBox::Cancel,
        QDialogButtonBox::No);
    if (res == QDialogButtonBox::Yes) {
      shellDelete(QStringList(m_Directory.absoluteFilePath("saves")));
    } else if (res == QDialogButtonBox::No) {
      // No action
    } else {
      return false;
    }
  }
  storeSetting("", "LocalSaves", enable);
  return true;
}

bool Profile::localSettingsEnabled() const
{
  bool enabled =
      setting("", "LocalSettings", Settings::instance().profileLocalInis()).toBool();
  auto writeLease = g_ProfileWriteBarrier.enterIfAllowed();
  if (!writeLease) {
    return enabled;
  }

  if (enabled) {
    QStringList missingFiles;
    for (QString file : m_GamePlugin->iniFiles()) {
      QString fileName = QFileInfo(file).fileName();
      // Use case-insensitive lookup on Linux — the file may exist with
      // different casing (e.g. "skyrimprefs.ini" vs "SkyrimPrefs.ini").
      QString resolved = MOBase::resolveFileCaseInsensitive(
          m_Directory.filePath(fileName));
      if (!QFile::exists(resolved)) {
        log::warn("missing {} in {}", fileName, m_Directory.path());
        missingFiles << fileName;
      }
    }
    if (!missingFiles.empty()) {
      m_GamePlugin->initializeProfile(m_Directory, IPluginGame::CONFIGURATION);
      QMessageBox::warning(QApplication::activeModalWidget(),
                           tr("Missing profile-specific game INI files!"),
                           tr("Some of your profile-specific game INI files were "
                              "missing.  They will now be copied "
                              "from the vanilla game folder.  You might want to "
                              "double-check your settings.\n\n"
                              "Missing files:\n") +
                               missingFiles.join("\n"));
    }
  }
  return enabled;
}

bool Profile::enableLocalSettings(bool enable)
{
  auto writeLease = g_ProfileWriteBarrier.enterIfAllowed();
  if (!writeLease) {
    return false;
  }

  if (enable) {
    m_GamePlugin->initializeProfile(m_Directory.absolutePath(),
                                    IPluginGame::CONFIGURATION);
  } else {
    QDialogButtonBox::StandardButton res;
    res = QuestionBoxMemory::query(QApplication::activeModalWidget(), "deleteINIQuery",
                                   tr("Delete profile-specific game INI files?"),
                                   tr("Do you want to delete the profile-specific game "
                                      "INI files? (If you select \"No\", the "
                                      "INI files will be used again if you re-enable "
                                      "profile-specific game INI files.)"),
                                   QDialogButtonBox::No | QDialogButtonBox::Yes |
                                       QDialogButtonBox::Cancel,
                                   QDialogButtonBox::No);
    if (res == QDialogButtonBox::Yes) {
      QStringList filesToDelete;
      for (QString file : m_GamePlugin->iniFiles()) {
        QString resolved = MOBase::resolveFileCaseInsensitive(
            m_Directory.absoluteFilePath(QFileInfo(file).fileName()));
        filesToDelete << resolved;
      }
      shellDelete(filesToDelete, true);
    } else if (res == QDialogButtonBox::No) {
      // No action
    } else {
      return false;
    }
  }
  storeSetting("", "LocalSettings", enable);
  return true;
}

QString Profile::getModlistFileName() const
{
  return QDir::cleanPath(m_Directory.absoluteFilePath("modlist.txt"));
}

QString Profile::getPluginsFileName() const
{
  return QDir::cleanPath(m_Directory.absoluteFilePath("plugins.txt"));
}

QString Profile::getLoadOrderFileName() const
{
  return QDir::cleanPath(m_Directory.absoluteFilePath("loadorder.txt"));
}

QString Profile::getLockedOrderFileName() const
{
  return QDir::cleanPath(m_Directory.absoluteFilePath("lockedorder.txt"));
}

QString Profile::getArchivesFileName() const
{
  return QDir::cleanPath(m_Directory.absoluteFilePath("archives.txt"));
}

QString Profile::getIniFileName() const
{
  auto iniFiles = m_GamePlugin->iniFiles();
  if (iniFiles.isEmpty())
    return "";
  else
    return MOBase::resolveFileCaseInsensitive(
        m_Directory.absoluteFilePath(QFileInfo(iniFiles[0]).fileName()));
}

QString Profile::absoluteIniFilePath(QString iniFile) const
{
  // This is the file to which the given iniFile would be mapped, as
  // an absolute file path:
  QFileInfo targetIniFile(m_GamePlugin->documentsDirectory(), iniFile);

  bool isGameIni = false;
  for (auto gameIni : m_GamePlugin->iniFiles()) {
    // We compare the target file, not the actual ones:
    if (QFileInfo(m_GamePlugin->documentsDirectory(), gameIni) == targetIniFile) {
      isGameIni = true;
      break;
    }
  }

  // Local-settings are not enabled, or the iniFile is not in the list of INI
  // files for the current game.
  if (!localSettingsEnabled() || !isGameIni) {
    return MOBase::resolveFileCaseInsensitive(targetIniFile.absoluteFilePath());
  }

  // If we reach here, the file is in the profile:
  return MOBase::resolveFileCaseInsensitive(
      m_Directory.absoluteFilePath(targetIniFile.fileName()));
}

QString Profile::getProfileTweaks() const
{
  return QDir::cleanPath(
      m_Directory.absoluteFilePath(ToQString(AppConfig::profileTweakIni())));
}

QString Profile::absolutePath() const
{
  return QDir::cleanPath(m_Directory.absolutePath());
}

QString Profile::savePath() const
{
  return QDir::cleanPath(m_Directory.absoluteFilePath("saves"));
}

void Profile::rename(const QString& newName)
{
  QString error;
  if (!tryRename(newName, &error)) {
    log::error("failed to rename profile '{}': {}", name(), error);
  }
}

bool Profile::tryRename(const QString& newName, QString* error,
                        bool* restartRequired)
{
  if (restartRequired != nullptr) {
    *restartRequired = false;
  }
  auto writeLease = g_ProfileWriteBarrier.enterIfAllowed();
  if (!writeLease) {
    if (error != nullptr) {
      *error = tr("Profile writes are currently disabled.");
    }
    return false;
  }

  const std::lock_guard settingsLock(g_ProfileSettingsMutex);
  if (!g_ProfileSettings.contains(m_Settings)) {
    if (error != nullptr) {
      *error = tr("The profile settings backend is not registered.");
    }
    return false;
  }

  const QString settingsPath =
      QDir::cleanPath(QFileInfo(m_Settings->fileName()).absoluteFilePath());
  for (auto* settings : g_ProfileSettings) {
    if (settings != m_Settings && settings != nullptr &&
        QDir::cleanPath(QFileInfo(settings->fileName()).absoluteFilePath()) ==
            settingsPath) {
      if (error != nullptr) {
        *error = tr("Another component is still using this profile. Close it and "
                    "try again.");
      }
      return false;
    }
  }

  auto result = ProfileRename::apply(m_Directory, *m_Settings, newName);
  if (!result.succeeded()) {
    if (error != nullptr) {
      *error = result.error;
    }
    log::error("failed to rename profile from {} to {}: {}", result.sourcePath,
               result.targetPath, result.error);
    if (result.status == ProfileRename::Status::RollbackFailed) {
      if (restartRequired != nullptr) {
        *restartRequired = true;
      }
      suppressWritesForFailedRollback();
    }
    return false;
  }

  if (result.status == ProfileRename::Status::NoChange) {
    return true;
  }

  auto* previousSettings = m_Settings;
  auto* replacement      = result.replacementSettings.release();
  auto registration      = g_ProfileSettings.extract(previousSettings);
  registration.value()   = replacement;
  g_ProfileSettings.insert(std::move(registration));
  m_Settings = replacement;
  m_Directory.setPath(result.targetPath);
  delete previousSettings;
  return true;
}

QString keyName(const QString& section, const QString& name)
{
  QString key = section;

  if (!name.isEmpty()) {
    if (!key.isEmpty()) {
      key += "/";
    }

    key += name;
  }

  return key;
}

QVariant Profile::setting(const QString& section, const QString& name,
                          const QVariant& fallback) const
{
  return m_Settings->value(keyName(section, name), fallback);
}

void Profile::storeSetting(const QString& section, const QString& name,
                           const QVariant& value)
{
  g_ProfileWriteBarrier.runIfAllowed(
      [&] { m_Settings->setValue(keyName(section, name), value); });
}

void Profile::removeSetting(const QString& section, const QString& name)
{
  g_ProfileWriteBarrier.runIfAllowed(
      [&] { m_Settings->remove(keyName(section, name)); });
}

QVariantMap Profile::settingsByGroup(const QString& section) const
{
  QVariantMap results;
  m_Settings->beginGroup(section);
  for (auto key : m_Settings->childKeys()) {
    results[key] = m_Settings->value(key);
  }
  m_Settings->endGroup();
  return results;
}

void Profile::storeSettingsByGroup(const QString& section, const QVariantMap& values)
{
  g_ProfileWriteBarrier.runIfAllowed([&] {
    m_Settings->beginGroup(section);
    for (auto key : values.keys()) {
      m_Settings->setValue(key, values[key]);
    }
    m_Settings->endGroup();
  });
}

QList<QVariantMap> Profile::settingsByArray(const QString& prefix) const
{
  QList<QVariantMap> results;
  int size = m_Settings->beginReadArray(prefix);
  for (int i = 0; i < size; i++) {
    m_Settings->setArrayIndex(i);
    QVariantMap item;
    for (auto key : m_Settings->childKeys()) {
      item[key] = m_Settings->value(key);
    }
    results.append(item);
  }
  m_Settings->endArray();
  return results;
}

void Profile::storeSettingsByArray(const QString& prefix,
                                   const QList<QVariantMap>& values)
{
  g_ProfileWriteBarrier.runIfAllowed([&] {
    m_Settings->beginWriteArray(prefix);
    for (int i = 0; i < values.length(); i++) {
      m_Settings->setArrayIndex(i);
      for (auto key : values.at(i).keys()) {
        m_Settings->setValue(key, values.at(i)[key]);
      }
    }
    m_Settings->endArray();
  });
}

bool Profile::forcedLibrariesEnabled(const QString& executable) const
{
  return setting("forced_libraries", executable + "/enabled", true).toBool();
}

void Profile::setForcedLibrariesEnabled(const QString& executable, bool enabled)
{
  storeSetting("forced_libraries", executable + "/enabled", enabled);
}

QList<ExecutableForcedLoadSetting>
Profile::determineForcedLibraries(const QString& executable) const
{
  QList<ExecutableForcedLoadSetting> results;

  auto rawSettings = settingsByArray("forced_libraries/" + executable);
  auto forcedLoads = m_GamePlugin->executableForcedLoads();

  // look for enabled status on forced loads and add those
  for (auto forcedLoad : forcedLoads) {
    bool found = false;
    for (auto rawSetting : rawSettings) {
      if ((rawSetting.value("process").toString().compare(forcedLoad.process(),
                                                          Qt::CaseInsensitive) == 0) &&
          (rawSetting.value("library").toString().compare(forcedLoad.library(),
                                                          Qt::CaseInsensitive) == 0)) {
        results.append(
            forcedLoad.withEnabled(rawSetting.value("enabled", false).toBool()));
        found = true;
      }
    }
    if (!found) {
      results.append(forcedLoad);
    }
  }

  // add everything else
  for (auto rawSetting : rawSettings) {
    bool add = true;
    for (auto forcedLoad : forcedLoads) {
      if ((rawSetting.value("process").toString().compare(forcedLoad.process(),
                                                          Qt::CaseInsensitive) == 0) &&
          (rawSetting.value("library").toString().compare(forcedLoad.library(),
                                                          Qt::CaseInsensitive) == 0)) {
        add = false;
      }
    }
    if (add) {
      results.append(ExecutableForcedLoadSetting(rawSetting.value("process").toString(),
                                                 rawSetting.value("library").toString())
                         .withEnabled(rawSetting.value("enabled", false).toBool()));
    }
  }

  return results;
}

void Profile::storeForcedLibraries(const QString& executable,
                                   const QList<ExecutableForcedLoadSetting>& values)
{
  QList<QVariantMap> rawSettings;
  for (auto setting : values) {
    QVariantMap rawSetting;
    rawSetting["enabled"] = setting.enabled();
    rawSetting["process"] = setting.process();
    rawSetting["library"] = setting.library();
    rawSettings.append(rawSetting);
  }
  storeSettingsByArray("forced_libraries/" + executable, rawSettings);
}

void Profile::removeForcedLibraries(const QString& executable)
{
  removeSetting("forced_libraries", executable);
}

void Profile::debugDump() const
{
  struct Pair
  {
    std::size_t enabled = 0;
    std::size_t total   = 0;
  };

  Pair total;
  Pair real;
  Pair backup;
  Pair separators;
  Pair dlc;
  Pair cc;
  Pair unmanaged;

  auto add = [](Pair& p, const ModStatus& status) {
    ++p.total;

    if (status.m_Enabled) {
      ++p.enabled;
    }
  };

  for (const auto& status : m_ModStatus) {
    auto index = m_ModIndexByPriority.find(status.m_Priority);
    if (index == m_ModIndexByPriority.end()) {
      log::error("mod with priority {} not in priority map", status.m_Priority);
      continue;
    }

    auto m = ModInfo::getByIndex(index->second);
    if (!m) {
      log::error("mod index {} with priority {} not found", index->second,
                 status.m_Priority);
      continue;
    }

    if (m->hasFlag(ModInfo::FLAG_OVERWRITE)) {
      continue;
    }

    add(total, status);

    if (m->hasFlag(ModInfo::FLAG_BACKUP)) {
      add(backup, status);
    }

    if (m->hasFlag(ModInfo::FLAG_SEPARATOR)) {
      add(separators, status);
    }

    if (m->hasFlag(ModInfo::FLAG_FOREIGN)) {
      if (auto* f = dynamic_cast<ModInfoForeign*>(m.get())) {
        switch (f->modType()) {
        case ModInfo::MOD_DLC:
          add(dlc, status);
          break;

        case ModInfo::MOD_CC:
          add(cc, status);
          break;

        default:
          add(unmanaged, status);
          break;
        }
      }
    }

    if (!m->hasAnyOfTheseFlags({ModInfo::FLAG_BACKUP, ModInfo::FLAG_FOREIGN,
                                ModInfo::FLAG_SEPARATOR, ModInfo::FLAG_OVERWRITE})) {
      add(real, status);
    }
  }

  log::debug("profile '{}' in '{}': "
             "mods={}/{} backup={}/{} separators={}/{} real={}/{} dlc={}/{} "
             "cc={}/{} unmanaged={}/{} localsaves={}, localsettings={}",
             name(), absolutePath(), total.enabled, total.total, backup.enabled,
             backup.total, separators.enabled, separators.total, real.enabled,
             real.total, dlc.enabled, dlc.total, cc.enabled, cc.total,
             unmanaged.enabled, unmanaged.total, localSavesEnabled() ? "yes" : "no",
             localSettingsEnabled() ? "yes" : "no");
}
