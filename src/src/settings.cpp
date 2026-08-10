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

#include "settings.h"
#include <fluorine_build_info.h>
#include "categoryassignmentpolicy.h"
#include "env.h"
#include "envmetrics.h"
#include "executableslist.h"
#include "instancemanager.h"
#include "modelutils.h"
#include "nxmhandler_linux.h"
#include "serverinfo.h"
#include "settingsmigration.h"
#include "settingsutilities.h"
#include "shared/appconfig.h"
#include <QCoreApplication>
#include <QEvent>
#include <QJsonDocument>
#include <expanderwidget.h>
#include <iplugingame.h>
#include <atomic>
#include <optional>
#include <utility.h>

using namespace MOBase;
using namespace MOShared;

namespace
{
bool usesBaseDirVariable(const QString& path)
{
  return path.contains(PathSettings::BaseDirVariable, Qt::CaseInsensitive);
}

QString loadStoredPath(const QString& path)
{
  QString value = QDir::fromNativeSeparators(path);
  if (!usesBaseDirVariable(value)) {
    value = MOBase::normalizePathForHost(value);
  }
  return value;
}

QString storePathForIni(const QString& path)
{
  if (path.isEmpty()) {
    return path;
  }

  if (usesBaseDirVariable(path)) {
    return QDir::fromNativeSeparators(path);
  }

  return MOBase::normalizePathForWine(path);
}

std::unique_ptr<QSettings> openSettingsBackend(
    const QString& path, QSettings::Status& initialStatus)
{
  QLockFile lock(SettingsMigration::settingsLockPath(path));
  lock.setStaleLockTime(SettingsMigration::SettingsLockStaleMs);
  if (!lock.tryLock(SettingsMigration::SettingsLockTimeoutMs)) {
    initialStatus = QSettings::AccessError;
    return std::make_unique<QSettings>(path, QSettings::IniFormat);
  }

  const auto inspection = SettingsMigration::inspectSettingsFile(path);
  initialStatus         = inspection.status;
  if (inspection.status == QSettings::FormatError) {
    const auto backup =
        SettingsMigration::quarantineCorruptSettingsFileUnlocked(
            path, inspection.bytes);
    if (backup) {
      log::error("settings file '{}' is malformed; the original was quarantined "
                 "at '{}' and a clean settings file will be created",
                 path, *backup);
      initialStatus = QSettings::NoError;
      return std::make_unique<QSettings>(path, QSettings::IniFormat);
    }
    log::error("settings file '{}' is malformed and could not be safely "
               "quarantined",
               path);
  }

  auto settings = std::make_unique<QSettings>(path, QSettings::IniFormat);
  settings->allKeys();
  if (initialStatus == QSettings::NoError) {
    initialStatus = settings->status();
  }
  return settings;
}
}  // namespace

EndorsementState endorsementStateFromString(const QString& s)
{
  if (s == "Endorsed") {
    return EndorsementState::Accepted;
  } else if (s == "Abstained") {
    return EndorsementState::Refused;
  } else {
    return EndorsementState::NoDecision;
  }
}

QString toString(EndorsementState s)
{
  switch (s) {
  case EndorsementState::Accepted:
    return "Endorsed";

  case EndorsementState::Refused:
    return "Abstained";

  case EndorsementState::NoDecision:  // fall-through
  default:
    return {};
  }
}

Settings* Settings::s_Instance = nullptr;

Settings::Settings(const QString& path, bool globalInstance)
    : m_SettingsOwner(openSettingsBackend(path, m_InitialStatus)),
      m_Settings(*m_SettingsOwner),
      m_Game(m_Settings),
      m_Geometry(m_Settings), m_Widgets(m_Settings, globalInstance),
      m_Colors(m_Settings), m_Plugins(m_Settings, m_WriteBarrier), m_Paths(m_Settings),
      m_Network(m_Settings, globalInstance), m_Nexus(*this, m_Settings),
      m_Steam(*this, m_Settings), m_Interface(m_Settings), m_Diagnostics(m_Settings)
{
  if (globalInstance && s_Instance != nullptr) {
    throw std::runtime_error("second instance of \"Settings\" created");
  }

  // m_WriteBarrier is declared before the backend and therefore outlives it.
  // Registration is removed while both objects are still alive in ~Settings.
  registerSettingsWriteBarrier(m_Settings, m_WriteBarrier);
  if (globalInstance) {
    s_Instance = this;
  }
}

Settings::~Settings()
{
  if (m_WriteBarrier.suppressed()) {
    // QSettings synchronizes pending changes from its destructor. A failed
    // rollback must not get a second, uncontrolled write attempt during
    // teardown, so intentionally retain this process-lifetime backend until
    // the operating system reclaims it.
    m_UpdateLock.reset();
    (void)m_SettingsOwner.release();
  } else if (m_UpdateLock) {
    restoreUpdateSnapshot();
  }
  if (s_Instance == this) {
    MOBase::QuestionBoxMemory::setCallbacks({}, {}, {});
    s_Instance = nullptr;
  }
  unregisterSettingsWriteBarrier(m_Settings);
}

void Settings::suppressWritesForFailedRollback() noexcept
{
  m_WriteBarrier.suppress();
  QCoreApplication::removePostedEvents(&m_Settings, QEvent::UpdateRequest);
}

bool Settings::failedRollbackWritesDrained() const noexcept
{
  if (!m_WriteBarrier.suppressionDrained()) {
    return false;
  }

  // An admitted setter may have queued a fresh deferred sync after phase-one
  // suppression removed the earlier event. Zero active writers is the safe
  // point to discard that final event before destructive teardown.
  QCoreApplication::removePostedEvents(&m_Settings, QEvent::UpdateRequest);
  return true;
}

bool Settings::beginUpdates()
{
  if (m_UpdateLock) {
    return true;
  }

  auto lock = std::make_unique<QLockFile>(
      SettingsMigration::settingsLockPath(m_Settings.fileName()));
  lock->setStaleLockTime(SettingsMigration::SettingsLockStaleMs);
  if (!lock->tryLock(SettingsMigration::SettingsLockTimeoutMs)) {
    return false;
  }

  // Refresh while holding the interprocess lock, before any migration mutates
  // the live backend. The snapshot is the rollback state for every failure or
  // incomplete startup.
  m_Settings.sync();
  if (m_Settings.status() != QSettings::NoError) {
    return false;
  }
  m_UpdateSnapshot = SettingsMigration::captureSettingsSnapshot(m_Settings);
  m_UpdateLock = std::move(lock);
  m_UpdateProcessingStarted = false;
  return true;
}

bool Settings::restoreUpdateSnapshot()
{
  if (!m_UpdateLock) {
    return true;
  }
  const auto rejected =
      SettingsMigration::captureSettingsSnapshot(m_Settings);
  return SettingsMigration::restoreSettingsSnapshot(
      m_Settings, m_UpdateSnapshot, rejected);
}

Settings& Settings::instance()
{
  if (s_Instance == nullptr) {
    throw std::runtime_error("no instance of \"Settings\"");
  }

  return *s_Instance;
}

Settings* Settings::maybeInstance()
{
  return s_Instance;
}

void Settings::processUpdates(const QVersionNumber& currentProductVersion,
                              const QVersionNumber& lastProductVersion,
                              int lastSchemaVersion)
{
  m_UpdateProcessingStarted = true;
  if (isNewInstance()) {
    return;
  }

  const bool productChanged = currentProductVersion != lastProductVersion;
  const bool schemaChanged  = SettingsMigration::requiresMigration(
      lastSchemaVersion, SettingsMigration::CurrentSchema);

  if (productChanged) {
    const bool importingUpstream = lastProductVersion.majorVersion() >= 2 &&
                                   currentProductVersion.majorVersion() == 0;
    if (importingUpstream) {
      log::info("adopting Fluorine {} for instance last used by MO2 {}",
                currentProductVersion.toString(), lastProductVersion.toString());
    } else {
      log::info("updating product version from {} to {}",
                lastProductVersion.toString(), currentProductVersion.toString());
    }
  }

  if (schemaChanged) {
    log::info("updating settings schema from {} to {}", lastSchemaVersion,
              SettingsMigration::CurrentSchema);
  }

  auto schema = [&](int target, const char* inheritedVersion, auto&& f) {
    if (SettingsMigration::requiresMigration(lastSchemaVersion, target)) {
      log::debug("processing settings schema {} (inherited from MO2 {})", target,
                 inheritedVersion);
      f();
    }
  };

  schema(SettingsMigration::Schema2_2_0, "2.2.0", [&] {
    remove(m_Settings, "Settings", "steam_password");
    remove(m_Settings, "Settings", "nexus_username");
    remove(m_Settings, "Settings", "nexus_password");
    remove(m_Settings, "Settings", "nexus_login");
    remove(m_Settings, "Settings", "nexus_api_key");
    remove(m_Settings, "Settings", "ask_for_nexuspw");
    remove(m_Settings, "Settings", "nmm_version");

    removeSection(m_Settings, "Servers");
  });

  schema(SettingsMigration::Schema2_2_1, "2.2.1", [&] {
    remove(m_Settings, "General", "mod_info_tabs");
    remove(m_Settings, "General", "mod_info_conflict_expanders");
    remove(m_Settings, "General", "mod_info_conflicts");
    remove(m_Settings, "General", "mod_info_advanced_conflicts");
    remove(m_Settings, "General", "mod_info_conflicts_overwrite");
    remove(m_Settings, "General", "mod_info_conflicts_noconflict");
    remove(m_Settings, "General", "mod_info_conflicts_overwritten");
  });

  schema(SettingsMigration::Schema2_2_2, "2.2.2", [&] {
    // log splitter is gone, it's a dock now
    remove(m_Settings, "General", "log_split");

    // moved to widgets
    remove(m_Settings, "General", "mod_info_conflicts_tab");
    remove(m_Settings, "General", "mod_info_conflicts_general_expanders");
    remove(m_Settings, "General", "mod_info_conflicts_general_overwrite");
    remove(m_Settings, "General", "mod_info_conflicts_general_noconflict");
    remove(m_Settings, "General", "mod_info_conflicts_general_overwritten");
    remove(m_Settings, "General", "mod_info_conflicts_advanced_list");
    remove(m_Settings, "General", "mod_info_conflicts_advanced_options");
    remove(m_Settings, "General", "mod_info_tab_order");
    remove(m_Settings, "General", "mod_info_dialog_images_show_dds");

    // moved to geometry
    remove(m_Settings, "General", "window_geometry");
    remove(m_Settings, "General", "window_state");
    remove(m_Settings, "General", "toolbar_size");
    remove(m_Settings, "General", "toolbar_button_style");
    remove(m_Settings, "General", "menubar_visible");
    remove(m_Settings, "General", "statusbar_visible");
    remove(m_Settings, "General", "window_split");
    remove(m_Settings, "General", "window_monitor");
    remove(m_Settings, "General", "browser_geometry");
    remove(m_Settings, "General", "filters_visible");

    // this was supposed to have been removed above when updating from 2.2.0,
    // but it wasn't in Settings, it was in General
    remove(m_Settings, "General", "ask_for_nexuspw");

    m_Network.updateFromOldMap();
  });

  schema(SettingsMigration::Schema2_4_0, "2.4.0", [&] {
    // removed
    remove(m_Settings, "Settings", "hide_unchecked_plugins");
    remove(m_Settings, "Settings", "load_mechanism");
  });

  if (lastSchemaVersion == SettingsMigration::UnknownSchema) {
    log::warn("settings schema value is invalid; migrations and schema update "
              "were skipped");
  } else if (lastSchemaVersion > SettingsMigration::CurrentSchema) {
    log::warn("settings schema {} is newer than supported schema {}; migrations "
              "were skipped",
              lastSchemaVersion, SettingsMigration::CurrentSchema);
  }
}

QSettings::Status Settings::completeUpdates(
    const QVersionNumber& currentProductVersion, int lastSchemaVersion)
{
  if (!m_UpdateLock) {
    return QSettings::AccessError;
  }

  auto rejected = SettingsMigration::captureSettingsSnapshot(m_Settings);
  if (lastSchemaVersion <= SettingsMigration::CurrentSchema) {
    rejected.insert(SettingsMigration::SettingsSchemaKey,
                    SettingsMigration::CurrentSchema);
  }
  rejected.insert(SettingsMigration::ProductVersionKey,
                  currentProductVersion.toString());

  const auto result = SettingsMigration::writeCompletedUpdatesUnderLock(
      m_Settings, currentProductVersion, lastSchemaVersion);

  if (result == QSettings::NoError) {
    log::debug("settings update completed");
    m_UpdateSnapshot.clear();
    m_UpdateLock.reset();
    m_UpdateProcessingStarted = false;
  } else {
    // QSettings may flush again later or from its destructor. Restore the
    // complete pre-migration value set now so a rejected transaction can
    // never persist only its destructive removals.
    if (!SettingsMigration::finishSettingsRollback(
            m_Settings, m_UpdateSnapshot, rejected, m_UpdateLock)) {
      log::error("failed to restore the pre-migration settings snapshot; "
                 "the application cannot safely continue");
    } else {
      m_UpdateProcessingStarted = false;
    }
  }
  return result;
}

QSettings::Status Settings::finishUpdatesWithoutMigration()
{
  if (!SettingsMigration::mayFinishWithoutMigration(
          m_UpdateLock != nullptr, m_UpdateProcessingStarted)) {
    return QSettings::AccessError;
  }

  const auto status =
      SettingsMigration::syncAndVerifySettingsUnderLock(m_Settings);
  if (status != QSettings::NoError) {
    return status;
  }

  m_UpdateSnapshot.clear();
  m_UpdateLock.reset();
  return QSettings::NoError;
}

QString Settings::filename() const
{
  return m_Settings.fileName();
}

bool Settings::checkForUpdates() const
{
  return get<bool>(m_Settings, "Settings", "check_for_updates", true);
}

void Settings::setCheckForUpdates(bool b)
{
  set(m_Settings, "Settings", "check_for_updates", b);
}

bool Settings::usePrereleases() const
{
  return get<bool>(m_Settings, "Settings", "use_prereleases", false);
}

void Settings::setUsePrereleases(bool b)
{
  set(m_Settings, "Settings", "use_prereleases", b);
}

QString Settings::fluorineUpdateChannel() const
{
#if FLUORINE_IS_NIGHTLY_BUILD
  const QString defaultChannel = QStringLiteral("nightly");
#else
  const QString defaultChannel = QStringLiteral("stable");
#endif
  const QString saved = get<QString>(
      m_Settings, "Settings", "fluorine_update_channel", defaultChannel);
  // Migrate the old name without silently moving rolling-build users back to
  // stable updates.
  return saved.compare(QStringLiteral("beta"), Qt::CaseInsensitive) == 0
             ? QStringLiteral("nightly")
             : saved;
}

void Settings::setFluorineUpdateChannel(const QString& channel)
{
  const QString normalized =
      channel.compare(QStringLiteral("beta"), Qt::CaseInsensitive) == 0
          ? QStringLiteral("nightly")
          : channel;
  set(m_Settings, "Settings", "fluorine_update_channel", normalized);
}

QString Settings::fluorineLastPromptedUpdate() const
{
  return get<QString>(m_Settings, "Settings",
                      "fluorine_last_prompted_update", QString());
}

void Settings::setFluorineLastPromptedUpdate(const QString& updateId)
{
  set(m_Settings, "Settings", "fluorine_last_prompted_update", updateId);
}

bool Settings::profileLocalInis() const
{
  return get<bool>(m_Settings, "Settings", "profile_local_inis", true);
}

void Settings::setProfileLocalInis(bool b)
{
  set(m_Settings, "Settings", "profile_local_inis", b);
}

bool Settings::profileLocalSaves() const
{
  return get<bool>(m_Settings, "Settings", "profile_local_saves", false);
}

void Settings::setProfileLocalSaves(bool b)
{
  set(m_Settings, "Settings", "profile_local_saves", b);
}

bool Settings::profileArchiveInvalidation() const
{
  return get<bool>(m_Settings, "Settings", "profile_archive_invalidation", false);
}

void Settings::setProfileArchiveInvalidation(bool b)
{
  set(m_Settings, "Settings", "profile_archive_invalidation", b);
}

bool Settings::useSplash() const
{
  return get<bool>(m_Settings, "Settings", "use_splash", true);
}

void Settings::setUseSplash(bool b)
{
  set(m_Settings, "Settings", "use_splash", b);
}

std::size_t Settings::refreshThreadCount() const
{
  return get<std::size_t>(m_Settings, "Settings", "refresh_thread_count", 10);
}

void Settings::setRefreshThreadCount(std::size_t n) const
{
  return set(m_Settings, "Settings", "refresh_thread_count", n);
}

std::optional<QVersionNumber> Settings::version() const
{
  const auto& key = SettingsMigration::ProductVersionKey;
  if (m_Settings.contains(key)) {
    const auto parsed =
        SettingsMigration::parseProductVersion(m_Settings.value(key));
    if (!parsed) {
      log::warn("ignoring invalid {} value '{}'", key,
                m_Settings.value(key).toString());
    }
    return parsed;
  }

  return {};
}

int Settings::settingsSchemaVersion() const
{
  if (iniStatus() != QSettings::NoError) {
    return SettingsMigration::UnknownSchema;
  }

  const auto& schemaKey  = SettingsMigration::SettingsSchemaKey;
  const auto& productKey = SettingsMigration::ProductVersionKey;
  const std::optional<QVariant> storedSchema =
      m_Settings.contains(schemaKey)
          ? std::optional<QVariant>(m_Settings.value(schemaKey))
          : std::nullopt;
  const std::optional<QVariant> storedProduct =
      m_Settings.contains(productKey)
          ? std::optional<QVariant>(m_Settings.value(productKey))
          : std::nullopt;
  const int schema = SettingsMigration::resolveStoredSettingsSchema(
      storedSchema, storedProduct);
  if (schema == SettingsMigration::UnknownSchema) {
    const auto key   = storedSchema ? schemaKey : productKey;
    const auto value = storedSchema ? storedSchema : storedProduct;
    log::warn("ignoring invalid {} value '{}'", key,
              value ? value->toString() : QString());
  }
  return schema;
}

bool Settings::categoryMigrationPending(int previousSchema) const
{
  const auto key = settingName("General", "category_migration_version");
  const std::optional<QVariant> stored =
      m_Settings.contains(key) ? std::optional<QVariant>(m_Settings.value(key))
                               : std::nullopt;
  return SettingsMigration::categoryMigrationPending(stored, previousSchema);
}

void Settings::setCategoryMigrationCompleted(bool completed)
{
  set(m_Settings, "General", "category_migration_version",
      completed ? SettingsMigration::CurrentCategoryMigration : 0);
}

int Settings::categoryLocalIdHighWater() const
{
  const auto key = settingName("General", "category_local_id_high_water");
  if (!m_Settings.contains(key)) {
    return 0;
  }
  bool ok          = false;
  const int stored = m_Settings.value(key).toString().trimmed().toInt(&ok, 10);
  if (!ok || stored < 0 ||
      stored >= CategoryAssignmentPolicy::ImportedCategoryIdBase) {
    log::error("invalid durable category ID high-water value '{}'; local "
               "category allocation is disabled",
               m_Settings.value(key).toString());
    return CategoryAssignmentPolicy::ImportedCategoryIdBase - 1;
  }
  return stored;
}

bool Settings::advanceCategoryLocalIdHighWater(int id)
{
  if (id < 0 || id >= CategoryAssignmentPolicy::ImportedCategoryIdBase) {
    return false;
  }
  if (id <= categoryLocalIdHighWater()) {
    return true;
  }
  set(m_Settings, "General", "category_local_id_high_water", id);
  return sync() == QSettings::NoError;
}

bool Settings::firstStart() const
{
  return m_Settings.value(SettingsMigration::FirstStartKey, true).toBool();
}

void Settings::setFirstStart(bool b)
{
  m_WriteBarrier.runIfAllowed([&] {
    if (b) {
      SettingsMigration::markNewInstance(m_Settings);
    } else {
      m_Settings.setValue(SettingsMigration::FirstStartKey, false);
      m_Settings.remove(SettingsMigration::NewInstanceProvenanceKey);
    }
  });
}

bool Settings::isNewInstance() const
{
  return SettingsMigration::isNewInstance(
      firstStart(),
      m_Settings.value(SettingsMigration::NewInstanceProvenanceKey)
              .toInt() == SettingsMigration::NewInstanceProvenance,
      m_Settings.contains(SettingsMigration::SettingsSchemaKey),
      m_Settings.contains(SettingsMigration::ProductVersionKey));
}

QString Settings::executablesBlacklist() const
{
  // USVFS-era setting; FUSE VFS on Linux doesn't hook executables per-process.
  return "";
}

bool Settings::isExecutableBlacklisted(const QString&) const
{
  return false;
}

void Settings::setExecutablesBlacklist(const QString&) {}

QStringList Settings::skipFileSuffixes() const
{
  // Keep the historical default so existing modlists with .mohidden files
  // keep ignoring them; previously this was a user-editable Workarounds entry.
  return {".mohidden"};
}

void Settings::setSkipFileSuffixes(const QStringList&) {}

QStringList Settings::skipDirectories() const
{
  return {".git"};
}

void Settings::setSkipDirectories(const QStringList&) {}

void Settings::setMotdHash(uint hash)
{
  set(m_Settings, "General", "motd_hash", hash);
}

unsigned int Settings::motdHash() const
{
  return get<unsigned int>(m_Settings, "General", "motd_hash", 0);
}

bool Settings::archiveParsing() const
{
  return get<bool>(m_Settings, "Settings", "archive_parsing_experimental", false);
}

void Settings::setArchiveParsing(bool b)
{
  set(m_Settings, "Settings", "archive_parsing_experimental", b);
}

std::vector<std::map<QString, QVariant>> Settings::executables() const
{
  ScopedReadArray sra(m_Settings, "customExecutables");
  std::vector<std::map<QString, QVariant>> v;

  sra.for_each([&] {
    std::map<QString, QVariant> map;

    for (auto&& key : sra.keys()) {
      map[key] = sra.get<QVariant>(key);
    }
    if (map.contains("binary")) {
      map["binary"] = loadStoredPath(map["binary"].toString());
    }
    if (map.contains("workingDirectory")) {
      map["workingDirectory"] =
          loadStoredPath(map["workingDirectory"].toString());
    }

    v.push_back(map);
  });

  return v;
}

void Settings::setExecutables(const std::vector<std::map<QString, QVariant>>& v)
{
  const auto current = executables();

  if (current == v) {
    // no change
    return;
  }

  if (current.size() > v.size()) {
    // Qt can't remove array elements, the section must be cleared
    removeSection(m_Settings, "customExecutables");
  }

  ScopedWriteArray swa(m_Settings, "customExecutables", v.size());

  for (const auto& map : v) {
    swa.next();

    for (auto&& p : map) {
      if ((p.first == "binary") || (p.first == "workingDirectory")) {
        swa.set(p.first, storePathForIni(p.second.toString()));
      } else {
        swa.set(p.first, p.second);
      }
    }
  }
}

bool Settings::keepBackupOnInstall() const
{
  return get<bool>(m_Settings, "General", "backup_install", false);
}

void Settings::setKeepBackupOnInstall(bool b)
{
  set(m_Settings, "General", "backup_install", b);
}

GameSettings& Settings::game()
{
  return m_Game;
}

const GameSettings& Settings::game() const
{
  return m_Game;
}

GeometrySettings& Settings::geometry()
{
  return m_Geometry;
}

const GeometrySettings& Settings::geometry() const
{
  return m_Geometry;
}

WidgetSettings& Settings::widgets()
{
  return m_Widgets;
}

const WidgetSettings& Settings::widgets() const
{
  return m_Widgets;
}

ColorSettings& Settings::colors()
{
  return m_Colors;
}

const ColorSettings& Settings::colors() const
{
  return m_Colors;
}

PluginSettings& Settings::plugins()
{
  return m_Plugins;
}

const PluginSettings& Settings::plugins() const
{
  return m_Plugins;
}

PathSettings& Settings::paths()
{
  return m_Paths;
}

const PathSettings& Settings::paths() const
{
  return m_Paths;
}

NetworkSettings& Settings::network()
{
  return m_Network;
}

const NetworkSettings& Settings::network() const
{
  return m_Network;
}

NexusSettings& Settings::nexus()
{
  return m_Nexus;
}

const NexusSettings& Settings::nexus() const
{
  return m_Nexus;
}

SteamSettings& Settings::steam()
{
  return m_Steam;
}

const SteamSettings& Settings::steam() const
{
  return m_Steam;
}

InterfaceSettings& Settings::interface()
{
  return m_Interface;
}

const InterfaceSettings& Settings::interface() const
{
  return m_Interface;
}

DiagnosticsSettings& Settings::diagnostics()
{
  return m_Diagnostics;
}

const DiagnosticsSettings& Settings::diagnostics() const
{
  return m_Diagnostics;
}

QSettings::Status Settings::sync() const
{
  if (m_InitialStatus != QSettings::NoError) {
    return m_InitialStatus;
  }

  QSettings::Status status = QSettings::AccessError;
  m_WriteBarrier.runIfAllowed([&] {
    m_Settings.sync();
    status = m_Settings.status();
  });
  return status;
}

std::optional<QMap<QString, QVariant>> Settings::captureEditSnapshot() const
{
  if (m_InitialStatus != QSettings::NoError) {
    return std::nullopt;
  }
  return SettingsMigration::captureInteractiveSettingsSnapshot(m_Settings);
}

bool Settings::restoreEditSnapshot(const QMap<QString, QVariant>& snapshot)
{
  if (m_InitialStatus != QSettings::NoError) {
    return false;
  }
  return SettingsMigration::restoreInteractiveSettingsSnapshot(m_Settings, snapshot);
}

bool Settings::verifyEditState() const
{
  if (m_InitialStatus != QSettings::NoError) {
    return false;
  }
  return SettingsMigration::verifyInteractiveSettingsState(m_Settings);
}

QSettings::Status Settings::iniStatus() const
{
  return m_InitialStatus == QSettings::NoError ? m_Settings.status()
                                                : m_InitialStatus;
}

void Settings::dump() const
{
  static const QStringList ignore({"username", "password", "nexus_api_key",
                                   "nexus_username", "nexus_password",
                                   "steam_username"});

  log::debug("settings:");

  {
    ScopedGroup sg(m_Settings, "Settings");

    for (auto k : m_Settings.allKeys()) {
      if (ignore.contains(k, Qt::CaseInsensitive)) {
        continue;
      }

      log::debug("  . {}={}", k, m_Settings.value(k).toString());
    }
  }

  m_Network.dump();
  NexusSettings::dump();
}

void Settings::managedGameChanged(IPluginGame const* gamePlugin)
{
  m_Game.setPlugin(gamePlugin);
}

GameSettings::GameSettings(QSettings& settings)
    : m_Settings(settings)
{}

const MOBase::IPluginGame* GameSettings::plugin()
{
  return m_GamePlugin;
}

void GameSettings::setPlugin(const MOBase::IPluginGame* gamePlugin)
{
  m_GamePlugin = gamePlugin;
}

bool GameSettings::forceEnableCoreFiles() const
{
  // Hardcoded on. Disabling primary-master autoload caused user issue
  // where DLCs went unchecked after a tab refresh; there's no use-case
  // outside Nehrim-style total conversions that justifies the footgun.
  return true;
}

void GameSettings::setForceEnableCoreFiles(bool)
{
  // no-op; see forceEnableCoreFiles().
}

std::optional<QString> GameSettings::directory() const
{
  if (auto v = getOptional<QByteArray>(m_Settings, "General", "gamePath")) {
    return loadStoredPath(QString::fromUtf8(*v));
  }

  return {};
}

void GameSettings::setDirectory(const QString& path)
{
  set(m_Settings, "General", "gamePath", storePathForIni(path).toUtf8());
}

std::optional<QString> GameSettings::name() const
{
  return getOptional<QString>(m_Settings, "General", "gameName");
}

void GameSettings::setName(const QString& name)
{
  set(m_Settings, "General", "gameName", name);
}

std::optional<QString> GameSettings::edition() const
{
  return getOptional<QString>(m_Settings, "General", "game_edition");
}

void GameSettings::setEdition(const QString& name)
{
  set(m_Settings, "General", "game_edition", name);
}

std::optional<QString> GameSettings::selectedProfileName() const
{
  if (auto v = getOptional<QByteArray>(m_Settings, "General", "selected_profile")) {
    return QString::fromUtf8(*v);
  }

  return {};
}

void GameSettings::setSelectedProfileName(const QString& name)
{
  set(m_Settings, "General", "selected_profile", name.toUtf8());
}

GeometrySettings::GeometrySettings(QSettings& s) : m_Settings(s) {}

void GeometrySettings::requestReset()
{
  m_Reset = true;
}

void GeometrySettings::resetIfNeeded()
{
  if (!m_Reset) {
    return;
  }

  removeSection(m_Settings, "Geometry");
}

void GeometrySettings::saveGeometry(const QMainWindow* w)
{
  saveWindowGeometry(w);
}

bool GeometrySettings::restoreGeometry(QMainWindow* w) const
{
  return restoreWindowGeometry(w);
}

void GeometrySettings::saveGeometry(const QDialog* d)
{
  saveWindowGeometry(d);
}

bool GeometrySettings::restoreGeometry(QDialog* d) const
{
  const auto r = restoreWindowGeometry(d);

  if (centerDialogs()) {
    centerOnParent(d);
  }

  return r;
}

void GeometrySettings::saveWindowGeometry(const QWidget* w)
{
  set(m_Settings, "Geometry", geoSettingName(w), w->saveGeometry());
}

bool GeometrySettings::restoreWindowGeometry(QWidget* w) const
{
  if (auto v = getOptional<QByteArray>(m_Settings, "Geometry", geoSettingName(w))) {
    w->restoreGeometry(*v);
    ensureWindowOnScreen(w);
    return true;
  }

  return false;
}

void GeometrySettings::ensureWindowOnScreen(QWidget* w)
{
  // users report that the main window and/or dialogs are displayed off-screen;
  // the usual workaround is keyboard navigation to move it
  //
  // qt should have code that deals with multiple monitors and off-screen
  // geometries, but there seems to be bugs or inconsistencies that can't be
  // reproduced
  //
  // the closest would probably be https://bugreports.qt.io/browse/QTBUG-64498,
  // which is about multiple monitors and high dpi, but it seems fixed as of
  // 5.12.4, which is shipped with 2.2.1
  //
  // without being to reproduce the problem, some simple checks are made in a
  // timer, which may mitigate the issues

  QTimer::singleShot(100, w, [w] {
    const auto borders = 20;

    // desktop geometry, made smaller to make sure there isn't just a few pixels
    const auto originalDg = env::Environment().metrics().desktopGeometry();
    const auto dg         = originalDg.adjusted(borders, borders, -borders, -borders);

    const auto g = w->geometry();

    if (!dg.intersects(g)) {
      log::warn("window '{}' is offscreen, moving to main monitor; geo={}, desktop={}",
                w->objectName(), g, originalDg);

      // widget is off-screen, center it on main monitor
      centerOnMonitor(w, -1);

      log::warn("window '{}' now at {}", w->objectName(), w->geometry());
    }
  });
}

void GeometrySettings::saveState(const QMainWindow* w)
{
  set(m_Settings, "Geometry", stateSettingName(w), w->saveState());
}

bool GeometrySettings::restoreState(QMainWindow* w) const
{
  if (auto v = getOptional<QByteArray>(m_Settings, "Geometry", stateSettingName(w))) {
    w->restoreState(*v);
    return true;
  }

  return false;
}

void GeometrySettings::saveState(const QHeaderView* w)
{
  set(m_Settings, "Geometry", stateSettingName(w), w->saveState());
}

bool GeometrySettings::restoreState(QHeaderView* w) const
{
  if (auto v = getOptional<QByteArray>(m_Settings, "Geometry", stateSettingName(w))) {
    return w->restoreState(*v);
  }

  return false;
}

void GeometrySettings::saveState(const QSplitter* w)
{
  set(m_Settings, "Geometry", stateSettingName(w), w->saveState());
}

bool GeometrySettings::restoreState(QSplitter* w) const
{
  if (auto v = getOptional<QByteArray>(m_Settings, "Geometry", stateSettingName(w))) {
    w->restoreState(*v);
    return true;
  }

  return false;
}

void GeometrySettings::saveState(const ExpanderWidget* expander)
{
  set(m_Settings, "Geometry", stateSettingName(expander), expander->saveState());
}

bool GeometrySettings::restoreState(ExpanderWidget* expander) const
{
  if (auto v =
          getOptional<QByteArray>(m_Settings, "Geometry", stateSettingName(expander))) {
    expander->restoreState(*v);
    return true;
  }

  return false;
}

void GeometrySettings::saveVisibility(const QWidget* w)
{
  set(m_Settings, "Geometry", visibilitySettingName(w), w->isVisible());
}

bool GeometrySettings::restoreVisibility(QWidget* w, std::optional<bool> def) const
{
  if (auto v =
          getOptional<bool>(m_Settings, "Geometry", visibilitySettingName(w), def)) {
    w->setVisible(*v);
    return true;
  }

  return false;
}

void GeometrySettings::restoreToolbars(QMainWindow* w) const
{
  // all toolbars have the same size and button style settings
  const auto size  = getOptional<QSize>(m_Settings, "Geometry", "toolbar_size");
  const auto style = getOptional<int>(m_Settings, "Geometry", "toolbar_button_style");

  for (auto* tb : w->findChildren<QToolBar*>()) {
    if (size) {
      tb->setIconSize(*size);
    }

    if (style) {
      tb->setToolButtonStyle(static_cast<Qt::ToolButtonStyle>(*style));
    }

    restoreVisibility(tb);
  }
}

void GeometrySettings::saveToolbars(const QMainWindow* w)
{
  const auto tbs = w->findChildren<QToolBar*>();

  // save visibility for all
  for (auto* tb : tbs) {
    saveVisibility(tb);
  }

  // all toolbars have the same size and button style settings, just save the
  // first one
  if (!tbs.isEmpty()) {
    const auto* tb = tbs[0];

    set(m_Settings, "Geometry", "toolbar_size", tb->iconSize());
    set(m_Settings, "Geometry", "toolbar_button_style",
        static_cast<int>(tb->toolButtonStyle()));
  }
}

QStringList GeometrySettings::modInfoTabOrder() const
{
  QStringList v;

  if (m_Settings.contains("mod_info_tabs")) {
    // old byte array from 2.2.0
    QDataStream stream(m_Settings.value("mod_info_tabs").toByteArray());

    int count = 0;
    stream >> count;

    for (int i = 0; i < count; ++i) {
      QString s;
      stream >> s;
      v.push_back(s);
    }
  } else {
    // string list since 2.2.1
    QString string = get<QString>(m_Settings, "Widgets", "ModInfoTabOrder", "");
    QTextStream stream(&string);

    while (!stream.atEnd()) {
      QString s;
      stream >> s;
      v.push_back(s);
    }
  }

  return v;
}

void GeometrySettings::setModInfoTabOrder(const QString& names)
{
  set(m_Settings, "Widgets", "ModInfoTabOrder", names);
}

bool GeometrySettings::centerDialogs() const
{
  return get<bool>(m_Settings, "Settings", "center_dialogs", false);
}

void GeometrySettings::setCenterDialogs(bool b)
{
  set(m_Settings, "Settings", "center_dialogs", b);
}

void GeometrySettings::centerOnMainWindowMonitor(QWidget* w) const
{
  const auto monitor =
      getOptional<int>(m_Settings, "Geometry", "MainWindow_monitor").value_or(-1);

  centerOnMonitor(w, monitor);
}

void GeometrySettings::centerOnMonitor(QWidget* w, int monitor)
{
  QPoint center;

  if (monitor >= 0 && monitor < QGuiApplication::screens().size()) {
    center = QGuiApplication::screens().at(monitor)->geometry().center();
  } else {
    center = QGuiApplication::primaryScreen()->geometry().center();
  }

  w->move(center - w->rect().center());
}

void GeometrySettings::centerOnParent(QWidget* w, QWidget* parent)
{
  if (!parent) {
    parent = w->parentWidget();

    if (!parent) {
      parent = qApp->activeWindow();
    }
  }

  if (parent && parent->isVisible()) {
    const auto pr = parent->geometry();
    w->move(pr.center() - w->rect().center());
  }
}

void GeometrySettings::saveMainWindowMonitor(const QMainWindow* w)
{
  if (auto* handle = w->windowHandle()) {
    if (auto* screen = handle->screen()) {
      const int screenId = QGuiApplication::screens().indexOf(screen);
      set(m_Settings, "Geometry", "MainWindow_monitor", screenId);
    }
  }
}

Qt::Orientation dockOrientation(const QMainWindow* mw, const QDockWidget* d)
{
  // docks in these areas are horizontal
  const auto horizontalAreas = Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea;

  if (mw->dockWidgetArea(const_cast<QDockWidget*>(d)) & horizontalAreas) {
    return Qt::Horizontal;
  } else {
    return Qt::Vertical;
  }
}

void GeometrySettings::saveDocks(const QMainWindow* mw)
{
  // this attempts to fix https://bugreports.qt.io/browse/QTBUG-46620 where dock
  // sizes are not restored when the main window is maximized; it is used in
  // MainWindow::readSettings() and MainWindow::storeSettings()
  //
  // there's also https://stackoverflow.com/questions/44005852, which has what
  // seems to be a popular fix, but it breaks the restored size of the window
  // by setting it to the desktop's resolution, so that doesn't work
  //
  // the only fix I could find is to remember the sizes of the docks and manually
  // setting them back; saving is straightforward, but restoring is messy
  //
  // this also depends on the window being visible before the timer in restore()
  // is fired and the timer must be processed by application.exec(); therefore,
  // the splash screen _must_ be closed before readSettings() is called, because
  // it has its own event loop, which seems to interfere with this
  //
  // all of this should become unnecessary when QTBUG-46620 is fixed
  //

  // saves the size of each dock
  for (const auto* dock : mw->findChildren<QDockWidget*>()) {
    int size = 0;

    // save the width for horizontal docks, or the height for vertical
    if (dockOrientation(mw, dock) == Qt::Horizontal) {
      size = dock->size().width();
    } else {
      size = dock->size().height();
    }

    set(m_Settings, "Geometry", dockSettingName(dock), size);
  }
}

void GeometrySettings::restoreDocks(QMainWindow* mw) const
{
  struct DockInfo
  {
    QDockWidget* d;
    int size = 0;
    Qt::Orientation ori;
  };

  std::vector<DockInfo> dockInfos;

  // for each dock
  for (auto* dock : mw->findChildren<QDockWidget*>()) {
    if (auto size = getOptional<int>(m_Settings, "Geometry", dockSettingName(dock))) {
      // remember this dock, its size and orientation
      dockInfos.push_back({dock, *size, dockOrientation(mw, dock)});
    }
  }

  // the main window must have had time to process the settings from
  // readSettings() or it seems to override whatever is set here
  //
  // some people said a single processEvents() call is enough, but it doesn't
  // look like it
  QTimer::singleShot(5, [=] {
    for (const auto& info : dockInfos) {
      mw->resizeDocks({info.d}, {info.size}, info.ori);
    }
  });
}

WidgetSettings::WidgetSettings(QSettings& s, bool globalInstance) : m_Settings(s)
{
  if (globalInstance) {
    MOBase::QuestionBoxMemory::setCallbacks(
        [this](auto&& w, auto&& f) {
          return questionButton(w, f);
        },
        [this](auto&& w, auto&& b) {
          setQuestionWindowButton(w, b);
        },
        [this](auto&& w, auto&& f, auto&& b) {
          setQuestionFileButton(w, f, b);
        });
  }
}

void WidgetSettings::saveTreeCheckState(const QTreeView* tv, int role)
{
  QVariantList data;
  for (auto index : flatIndex(tv->model())) {
    data.append(index.data(role));
  }
  set(m_Settings, "Widgets", indexSettingName(tv), data);
}

void WidgetSettings::restoreTreeCheckState(QTreeView* tv, int role) const
{
  if (auto states =
          getOptional<QVariantList>(m_Settings, "Widgets", indexSettingName(tv))) {
    auto allIndex = flatIndex(tv->model());
    MOBase::log::debug("restoreTreeCheckState: {}, {}", states->size(),
                       allIndex.size());
    if (states->size() != allIndex.size()) {
      return;
    }
    for (int i = 0; i < states->size(); ++i) {
      tv->model()->setData(allIndex[i], states->at(i), role);
    }
  }
}

void WidgetSettings::saveTreeExpandState(const QTreeView* tv, int role)
{
  QVariantList expanded;
  for (auto index : flatIndex(tv->model())) {
    if (tv->isExpanded(index)) {
      expanded.append(index.data(role));
    }
  }
  set(m_Settings, "Widgets", indexSettingName(tv), expanded);
}

void WidgetSettings::restoreTreeExpandState(QTreeView* tv, int role) const
{
  if (auto expanded =
          getOptional<QVariantList>(m_Settings, "Widgets", indexSettingName(tv))) {
    tv->collapseAll();
    for (auto index : flatIndex(tv->model())) {
      if (expanded->contains(index.data(role))) {
        tv->expand(index);
      }
    }
  }
}

std::optional<int> WidgetSettings::index(const QComboBox* cb) const
{
  return getOptional<int>(m_Settings, "Widgets", indexSettingName(cb));
}

void WidgetSettings::saveIndex(const QComboBox* cb)
{
  set(m_Settings, "Widgets", indexSettingName(cb), cb->currentIndex());
}

void WidgetSettings::restoreIndex(QComboBox* cb, std::optional<int> def) const
{
  if (auto v = getOptional<int>(m_Settings, "Widgets", indexSettingName(cb), def)) {
    cb->setCurrentIndex(*v);
  }
}

std::optional<int> WidgetSettings::index(const QTabWidget* w) const
{
  return getOptional<int>(m_Settings, "Widgets", indexSettingName(w));
}

void WidgetSettings::saveIndex(const QTabWidget* w)
{
  set(m_Settings, "Widgets", indexSettingName(w), w->currentIndex());
}

void WidgetSettings::restoreIndex(QTabWidget* w, std::optional<int> def) const
{
  if (auto v = getOptional<int>(m_Settings, "Widgets", indexSettingName(w), def)) {
    w->setCurrentIndex(*v);
  }
}

std::optional<bool> WidgetSettings::checked(const QAbstractButton* w) const
{
  warnIfNotCheckable(w);
  return getOptional<bool>(m_Settings, "Widgets", checkedSettingName(w));
}

void WidgetSettings::saveChecked(const QAbstractButton* w)
{
  warnIfNotCheckable(w);
  set(m_Settings, "Widgets", checkedSettingName(w), w->isChecked());
}

void WidgetSettings::restoreChecked(QAbstractButton* w, std::optional<bool> def) const
{
  warnIfNotCheckable(w);

  if (auto v = getOptional<bool>(m_Settings, "Widgets", checkedSettingName(w), def)) {
    w->setChecked(*v);
  }
}

QuestionBoxMemory::Button WidgetSettings::questionButton(const QString& windowName,
                                                         const QString& filename) const
{
  const QString sectionName("DialogChoices");

  if (!filename.isEmpty()) {
    const auto fileSetting = windowName + "/" + filename;
    if (auto v = getOptional<int>(m_Settings, sectionName, fileSetting)) {
      return static_cast<QuestionBoxMemory::Button>(*v);
    }
  }

  if (auto v = getOptional<int>(m_Settings, sectionName, windowName)) {
    return static_cast<QuestionBoxMemory::Button>(*v);
  }

  return QuestionBoxMemory::NoButton;
}

void WidgetSettings::setQuestionWindowButton(const QString& windowName,
                                             QuestionBoxMemory::Button button)
{
  const QString sectionName("DialogChoices");

  if (button == QuestionBoxMemory::NoButton) {
    remove(m_Settings, sectionName, windowName);
  } else {
    set(m_Settings, sectionName, windowName, button);
  }
}

void WidgetSettings::setQuestionFileButton(const QString& windowName,
                                           const QString& filename,
                                           QuestionBoxMemory::Button button)
{
  const QString sectionName("DialogChoices");
  const QString settingName(windowName + "/" + filename);

  if (button == QuestionBoxMemory::NoButton) {
    remove(m_Settings, sectionName, settingName);
  } else {
    set(m_Settings, sectionName, settingName, button);
  }
}

void WidgetSettings::resetQuestionButtons()
{
  removeSection(m_Settings, "DialogChoices");
}

ColorSettings::ColorSettings(QSettings& s) : m_Settings(s) {}

QColor ColorSettings::modlistOverwrittenLoose() const
{
  return get<QColor>(m_Settings, "Settings", "overwrittenLooseFilesColor",
                     QColor(0, 255, 0, 64));
}

void ColorSettings::setModlistOverwrittenLoose(const QColor& c)
{
  set(m_Settings, "Settings", "overwrittenLooseFilesColor", c);
}

QColor ColorSettings::modlistOverwritingLoose() const
{
  return get<QColor>(m_Settings, "Settings", "overwritingLooseFilesColor",
                     QColor(255, 0, 0, 64));
}

void ColorSettings::setModlistOverwritingLoose(const QColor& c)
{
  set(m_Settings, "Settings", "overwritingLooseFilesColor", c);
}

QColor ColorSettings::modlistOverwrittenArchive() const
{
  return get<QColor>(m_Settings, "Settings", "overwrittenArchiveFilesColor",
                     QColor(0, 255, 255, 64));
}

void ColorSettings::setModlistOverwrittenArchive(const QColor& c)
{
  set(m_Settings, "Settings", "overwrittenArchiveFilesColor", c);
}

QColor ColorSettings::modlistOverwritingArchive() const
{
  return get<QColor>(m_Settings, "Settings", "overwritingArchiveFilesColor",
                     QColor(255, 0, 255, 64));
}

void ColorSettings::setModlistOverwritingArchive(const QColor& c)
{
  set(m_Settings, "Settings", "overwritingArchiveFilesColor", c);
}

QColor ColorSettings::modlistContainsFile() const
{
  return get<QColor>(m_Settings, "Settings", "containsFileColor",
                     QColor(0, 0, 255, 64));
}

void ColorSettings::setModlistContainsFile(const QColor& c)
{
  set(m_Settings, "Settings", "containsFileColor", c);
}

QColor ColorSettings::pluginListContained() const
{
  return get<QColor>(m_Settings, "Settings", "containedColor", QColor(0, 0, 255, 64));
}

void ColorSettings::setPluginListContained(const QColor& c)
{
  set(m_Settings, "Settings", "containedColor", c);
}

QColor ColorSettings::pluginListMaster() const
{
  return get<QColor>(m_Settings, "Settings", "masterColor", QColor(255, 255, 0, 64));
}

void ColorSettings::setPluginListMaster(const QColor& c)
{
  set(m_Settings, "Settings", "masterColor", c);
}

std::optional<QColor> ColorSettings::previousSeparatorColor() const
{
  const auto c = getOptional<QColor>(m_Settings, "General", "previousSeparatorColor");
  if (c && c->isValid()) {
    return c;
  }

  return {};
}

void ColorSettings::setPreviousSeparatorColor(const QColor& c) const
{
  set(m_Settings, "General", "previousSeparatorColor", c);
}

void ColorSettings::removePreviousSeparatorColor()
{
  remove(m_Settings, "General", "previousSeparatorColor");
}

bool ColorSettings::colorSeparatorScrollbar() const
{
  return get<bool>(m_Settings, "Settings", "colorSeparatorScrollbars", true);
}

void ColorSettings::setColorSeparatorScrollbar(bool b)
{
  set(m_Settings, "Settings", "colorSeparatorScrollbars", b);
}

QColor ColorSettings::idealTextColor(const QColor& rBackgroundColor)
{
  if (rBackgroundColor.alpha() < 50)
    return {Qt::black};

  // "inverse' of luminance of the background
  int iLuminance = (rBackgroundColor.red() * 0.299) +
                   (rBackgroundColor.green() * 0.587) +
                   (rBackgroundColor.blue() * 0.114);
  return {iLuminance >= 128 ? Qt::black : Qt::white};
}

PluginSettings::PluginSettings(QSettings& settings,
                               const SettingsWriteBarrier& writeBarrier)
    : m_Settings(settings), m_WriteBarrier(writeBarrier)
{}

void PluginSettings::clearPlugins()
{
  m_Plugins.clear();
  m_PluginSettings.clear();
  m_PluginBlacklist.clear();

  m_PluginBlacklist = readBlacklist();
}

void PluginSettings::registerPlugin(IPlugin* plugin)
{
  m_Plugins.push_back(plugin);
  m_PluginSettings.insert(plugin->name(), QVariantMap());
  m_PluginDescriptions.insert(plugin->name(), QVariantMap());

  for (const PluginSetting& setting : plugin->settings()) {
    const QString settingName = plugin->name() + "/" + setting.key;

    QVariant temp = get<QVariant>(m_Settings, "Plugins", settingName, QVariant());

    // No previous enabled? Skip.
    if (setting.key == "enabled" && (!temp.isValid() || !temp.canConvert<bool>())) {
      continue;
    }

    if (!temp.isValid()) {
      temp = setting.defaultValue;
    } else if (!temp.convert(setting.defaultValue.metaType())) {
      log::warn("failed to interpret \"{}\" as correct type for \"{}\" in plugin "
                "\"{}\", using default",
                temp.toString(), setting.key, plugin->name());

      temp = setting.defaultValue;
    }

    m_PluginSettings[plugin->name()][setting.key] = temp;

    m_PluginDescriptions[plugin->name()][setting.key] =
        QString("%1 (default: %2)")
            .arg(setting.description)
            .arg(setting.defaultValue.toString());
  }

  // Handle previous "enabled" settings:
  if (m_PluginSettings[plugin->name()].contains("enabled")) {
    m_WriteBarrier.runIfAllowed([&] {
      setPersistent(plugin->name(), "enabled",
                    m_PluginSettings[plugin->name()]["enabled"].toBool(), true);
      m_PluginSettings[plugin->name()].remove("enabled");
      m_PluginDescriptions[plugin->name()].remove("enabled");

      // We need to drop it manually in Settings since it is not possible to remove plugin
      // settings:
      remove(m_Settings, "Plugins", plugin->name() + "/enabled");
    });
  }
}

void PluginSettings::unregisterPlugin(IPlugin* plugin)
{
  auto it = std::find(m_Plugins.begin(), m_Plugins.end(), plugin);
  if (it != m_Plugins.end()) {
    m_Plugins.erase(it);
  }
  m_PluginSettings.remove(plugin->name());
  m_PluginDescriptions.remove(plugin->name());
}

std::vector<MOBase::IPlugin*> PluginSettings::plugins() const
{
  return m_Plugins;
}

QVariant PluginSettings::setting(const QString& pluginName, const QString& key) const
{
  auto iterPlugin = m_PluginSettings.find(pluginName);
  if (iterPlugin == m_PluginSettings.end()) {
    return {};
  }

  auto iterSetting = iterPlugin->find(key);
  if (iterSetting == iterPlugin->end()) {
    return {};
  }

  return *iterSetting;
}

void PluginSettings::setSetting(const QString& pluginName, const QString& key,
                                const QVariant& value)
{
  m_WriteBarrier.runIfAllowed([&] {
    auto iterPlugin = m_PluginSettings.find(pluginName);

    if (iterPlugin == m_PluginSettings.end()) {
      throw MyException(QObject::tr("attempt to store setting for unknown plugin \"%1\"")
                            .arg(pluginName));
    }

    QVariant oldValue = m_PluginSettings[pluginName][key];

    // store the new setting both in memory and in the ini
    m_PluginSettings[pluginName][key] = value;
    set(m_Settings, "Plugins", pluginName + "/" + key, value);

    // emit signal:
    emit pluginSettingChanged(pluginName, key, oldValue, value);
  });
}

QVariantMap PluginSettings::settings(const QString& pluginName) const
{
  return m_PluginSettings[pluginName];
}

void PluginSettings::setSettings(const QString& pluginName, const QVariantMap& map)
{
  m_WriteBarrier.runIfAllowed([&] {
    auto iterPlugin = m_PluginSettings.find(pluginName);

    if (iterPlugin == m_PluginSettings.end()) {
      throw MyException(QObject::tr("attempt to store setting for unknown plugin \"%1\"")
                            .arg(pluginName));
    }

    QVariantMap oldSettings      = m_PluginSettings[pluginName];
    m_PluginSettings[pluginName] = map;

    // Emit signals for settings that have been changed or added:
    for (auto& k : map.keys()) {
      // .value() return a default-constructed QVariant if k is not in oldSettings:
      QVariant oldValue = oldSettings.value(k);
      if (oldValue != map[k]) {
        emit pluginSettingChanged(pluginName, k, oldSettings.value(k), map[k]);
      }
    }

    // Emit signals for settings that have been removed:
    for (auto& k : oldSettings.keys()) {
      if (!map.contains(k)) {
        emit pluginSettingChanged(pluginName, k, oldSettings[k], QVariant());
      }
    }
  });
}

QVariantMap PluginSettings::descriptions(const QString& pluginName) const
{
  return m_PluginDescriptions[pluginName];
}

void PluginSettings::setDescriptions(const QString& pluginName, const QVariantMap& map)
{
  m_PluginDescriptions[pluginName] = map;
}

QVariant PluginSettings::persistent(const QString& pluginName, const QString& key,
                                    const QVariant& def) const
{
  if (!m_PluginSettings.contains(pluginName)) {
    return def;
  }

  return get<QVariant>(m_Settings, "PluginPersistance", pluginName + "/" + key, def);
}

void PluginSettings::setPersistent(const QString& pluginName, const QString& key,
                                   const QVariant& value, bool sync)
{
  m_WriteBarrier.runIfAllowed([&] {
    if (!m_PluginSettings.contains(pluginName)) {
      throw MyException(QObject::tr("attempt to store setting for unknown plugin \"%1\"")
                            .arg(pluginName));
    }

    set(m_Settings, "PluginPersistance", pluginName + "/" + key, value);

    if (sync) {
      m_Settings.sync();
    }
  });
}

void PluginSettings::addBlacklist(const QString& fileName)
{
  m_WriteBarrier.runIfAllowed([&] {
    m_PluginBlacklist.insert(fileName);
    writeBlacklist();
  });
}

bool PluginSettings::blacklisted(const QString& fileName) const
{
  return m_PluginBlacklist.contains(fileName);
}

void PluginSettings::setBlacklist(const QStringList& pluginNames)
{
  m_WriteBarrier.runIfAllowed([&] {
    m_PluginBlacklist.clear();

    for (const auto& name : pluginNames) {
      m_PluginBlacklist.insert(name);
    }
  });
}

const QSet<QString>& PluginSettings::blacklist() const
{
  return m_PluginBlacklist;
}

void PluginSettings::save()
{
  m_WriteBarrier.runIfAllowed([&] {
    for (auto iterPlugins = m_PluginSettings.begin();
         iterPlugins != m_PluginSettings.end(); ++iterPlugins) {
      for (auto iterSettings = iterPlugins->begin(); iterSettings != iterPlugins->end();
           ++iterSettings) {
        const auto key = iterPlugins.key() + "/" + iterSettings.key();
        set(m_Settings, "Plugins", key, iterSettings.value());
      }
    }

    writeBlacklist();
  });
}

void PluginSettings::writeBlacklist()
{
  m_WriteBarrier.runIfAllowed([&] {
    const auto current = readBlacklist();

    if (current.size() > m_PluginBlacklist.size()) {
      // Qt can't remove array elements, the section must be cleared
      removeSection(m_Settings, "pluginBlacklist");
    }

    ScopedWriteArray swa(m_Settings, "pluginBlacklist", m_PluginBlacklist.size());

    for (const QString& plugin : m_PluginBlacklist) {
      swa.next();
      swa.set("name", plugin);
    }
  });
}

QSet<QString> PluginSettings::readBlacklist() const
{
  QSet<QString> set;

  ScopedReadArray sra(m_Settings, "pluginBlacklist");
  sra.for_each([&] {
    set.insert(sra.get<QString>("name"));
  });

  return set;
}

const QString PathSettings::BaseDirVariable = "%BASE_DIR%";

PathSettings::PathSettings(QSettings& settings) : m_Settings(settings) {}

std::map<QString, QString> PathSettings::recent() const
{
  std::map<QString, QString> map;

  ScopedReadArray sra(m_Settings, "recentDirectories");

  sra.for_each([&] {
    const QVariant name = sra.get<QVariant>("name");
    const QVariant dir  = sra.get<QVariant>("directory");

    if (name.isValid() && dir.isValid()) {
      map.emplace(name.toString(), loadStoredPath(dir.toString()));
    }
  });

  return map;
}

void PathSettings::setRecent(const std::map<QString, QString>& map)
{
  const auto current = recent();

  if (current.size() > map.size()) {
    // Qt can't remove array elements, the section must be cleared
    removeSection(m_Settings, "recentDirectories");
  }

  ScopedWriteArray swa(m_Settings, "recentDirectories", map.size());

  for (auto&& p : map) {
    swa.next();

    swa.set("name", p.first);
    swa.set("directory", storePathForIni(p.second));
  }
}

QString PathSettings::getConfigurablePath(const QString& key, const QString& def,
                                          bool resolve) const
{
  QString result =
      loadStoredPath(get<QString>(m_Settings, "Settings", key, makeDefaultPath(def)));

  if (resolve) {
    result = PathSettings::resolve(result, base());
  }

  return result;
}

void PathSettings::setConfigurablePath(const QString& key, const QString& path)
{
  if (path.isEmpty()) {
    remove(m_Settings, "Settings", key);
  } else {
    set(m_Settings, "Settings", key, storePathForIni(path));
  }
}

QString PathSettings::resolve(const QString& path, const QString& baseDir)
{
  QString s = path;
  s.replace(BaseDirVariable, baseDir);
  return s;
}

QString PathSettings::makeDefaultPath(const QString dirName)
{
  return BaseDirVariable + "/" + dirName;
}

QString PathSettings::base() const
{
  const QString dataPath = QFileInfo(m_Settings.fileName()).dir().path();

  return loadStoredPath(get<QString>(m_Settings, "Settings", "base_directory",
                                     dataPath));
}

QString PathSettings::downloads(bool resolve) const
{
  return getConfigurablePath("download_directory", ToQString(AppConfig::downloadPath()),
                             resolve);
}

QString PathSettings::cache(bool resolve) const
{
  return getConfigurablePath("cache_directory", ToQString(AppConfig::cachePath()),
                             resolve);
}

QString PathSettings::mods(bool resolve) const
{
  return getConfigurablePath("mod_directory", ToQString(AppConfig::modsPath()),
                             resolve);
}

QString PathSettings::profiles(bool resolve) const
{
  return getConfigurablePath("profiles_directory", ToQString(AppConfig::profilesPath()),
                             resolve);
}

QString PathSettings::overwrite(bool resolve) const
{
  return getConfigurablePath("overwrite_directory",
                             ToQString(AppConfig::overwritePath()), resolve);
}

void PathSettings::setBase(const QString& path)
{
  if (path.isEmpty()) {
    remove(m_Settings, "Settings", "base_directory");
  } else {
    set(m_Settings, "Settings", "base_directory", storePathForIni(path));
  }
}

void PathSettings::setDownloads(const QString& path)
{
  setConfigurablePath("download_directory", path);
}

void PathSettings::setMods(const QString& path)
{
  setConfigurablePath("mod_directory", path);
}

void PathSettings::setCache(const QString& path)
{
  setConfigurablePath("cache_directory", path);
}

void PathSettings::setProfiles(const QString& path)
{
  setConfigurablePath("profiles_directory", path);
}

void PathSettings::setOverwrite(const QString& path)
{
  setConfigurablePath("overwrite_directory", path);
}

NetworkSettings::NetworkSettings(QSettings& settings, bool globalInstance)
    : m_Settings(settings)
{
  if (globalInstance) {
    updateCustomBrowser();
  }
}

void NetworkSettings::updateCustomBrowser() const
{
  // Removed alongside the Workarounds tab; system mimetypes drive URL handling.
  MOBase::shell::SetUrlHandler("");
}

bool NetworkSettings::offlineMode() const
{
  return false;
}

void NetworkSettings::setOfflineMode(bool) {}

bool NetworkSettings::useProxy() const
{
  return false;
}

void NetworkSettings::setUseProxy(bool) {}

void NetworkSettings::setDownloadSpeed(const QString& name, int bytesPerSecond)
{
  auto current = servers();

  for (auto& server : current) {
    if (server.name() == name) {
      server.addDownload(bytesPerSecond);
      updateServers(current);
      return;
    }
  }

  log::error("server '{}' not found while trying to add a download with bps {}", name,
             bytesPerSecond);
}

ServerList NetworkSettings::servers() const
{
  ServerList list;

  {
    ScopedReadArray sra(m_Settings, "Servers");

    sra.for_each([&] {
      ServerInfo::SpeedList lastDownloads;

      const auto lastDownloadsString = sra.get<QString>("lastDownloads", "");

      for (const auto& s : lastDownloadsString.split(" ")) {
        const auto bytesPerSecond = s.toInt();
        if (bytesPerSecond > 0) {
          lastDownloads.push_back(bytesPerSecond);
        }
      }

      ServerInfo server(
          sra.get<QString>("name", ""), sra.get<bool>("premium", false),
          QDate::fromString(sra.get<QString>("lastSeen", ""), Qt::ISODate),
          sra.get<int>("preferred", 0), lastDownloads);

      list.add(std::move(server));
    });
  }

  return list;
}

void NetworkSettings::updateServers(ServerList newServers)
{
  // clean up unavailable servers
  newServers.cleanup();

  const auto current = servers();

  if (current.size() > newServers.size()) {
    // Qt can't remove array elements, the section must be cleared
    removeSection(m_Settings, "Servers");
  }

  ScopedWriteArray swa(m_Settings, "Servers", newServers.size());

  for (const auto& server : newServers) {
    swa.next();

    swa.set("name", server.name());
    swa.set("premium", server.isPremium());
    swa.set("lastSeen", server.lastSeen().toString(Qt::ISODate));
    swa.set("preferred", server.preferred());

    QString lastDownloads;
    for (const auto& speed : server.lastDownloads()) {
      if (speed > 0) {
        lastDownloads += QString("%1 ").arg(speed);
      }
    }

    swa.set("lastDownloads", lastDownloads.trimmed());
  }
}

void NetworkSettings::updateFromOldMap()
{
  // servers used to be a map of byte arrays until 2.2.1, it's now an array of
  // individual values instead
  //
  // so post 2.2.1, only one key is returned: "size", the size of the arrays;
  // in 2.2.1, one key per server is returned

  // sanity check that this is really 2.2.1
  {
    const QStringList keys = ScopedGroup(m_Settings, "Servers").keys();

    for (auto&& k : keys) {
      if (k == "size") {
        // this looks like an array, so the upgrade was probably already done
        return;
      }
    }
  }

  const auto servers = serversFromOldMap();
  removeSection(m_Settings, "Servers");
  updateServers(servers);
}

bool NetworkSettings::useCustomBrowser() const
{
  return false;
}

void NetworkSettings::setUseCustomBrowser(bool) {}

QString NetworkSettings::customBrowserCommand() const
{
  return "";
}

void NetworkSettings::setCustomBrowserCommand(const QString&) {}

ServerList NetworkSettings::serversFromOldMap() const
{
  // for 2.2.1 and before

  ServerList list;
  const ScopedGroup sg(m_Settings, "Servers");

  sg.for_each([&](auto&& serverKey) {
    QVariantMap data = sg.get<QVariantMap>(serverKey);

    ServerInfo server(serverKey, data["premium"].toBool(), data["lastSeen"].toDate(),
                      data["preferred"].toInt(), {});

    // ignoring download count and speed, it's now a list of values instead of
    // a total

    list.add(std::move(server));
  });

  return list;
}

void NetworkSettings::dump() const
{
  log::debug("servers:");

  for (const auto& server : servers()) {
    QString lastDownloads;
    for (auto speed : server.lastDownloads()) {
      lastDownloads += QString("%1 ").arg(speed);
    }

    log::debug("  . {} premium={} lastSeen={} preferred={} lastDownloads={}",
               server.name(), server.isPremium() ? "yes" : "no",
               server.lastSeen().toString(Qt::ISODate), server.preferred(),
               lastDownloads.trimmed());
  }
}

NexusSettings::NexusSettings(Settings& parent, QSettings& settings)
    : m_Parent(parent), m_Settings(settings)
{}

bool NexusSettings::endorsementIntegration() const
{
  return get<bool>(m_Settings, "Settings", "endorsement_integration", true);
}

void NexusSettings::setEndorsementIntegration(bool b) const
{
  set(m_Settings, "Settings", "endorsement_integration", b);
}

EndorsementState NexusSettings::endorsementState() const
{
  return endorsementStateFromString(
      get<QString>(m_Settings, "General", "endorse_state", ""));
}

void NexusSettings::setEndorsementState(EndorsementState s)
{
  const auto v = toString(s);

  if (v.isEmpty()) {
    remove(m_Settings, "General", "endorse_state");
  } else {
    set(m_Settings, "General", "endorse_state", v);
  }
}

bool NexusSettings::trackedIntegration() const
{
  return get<bool>(m_Settings, "Settings", "tracked_integration", true);
}

void NexusSettings::setTrackedIntegration(bool b) const
{
  set(m_Settings, "Settings", "tracked_integration", b);
}

bool NexusSettings::categoryMappings() const
{
  return get<bool>(m_Settings, "Settings", "category_mappings", true);
}

void NexusSettings::setCategoryMappings(bool b) const
{
  set(m_Settings, "Settings", "category_mappings", b);
}

void NexusSettings::registerAsNXMHandler(bool force)
{
  Q_UNUSED(force);
  NxmHandlerLinux::registerHandler();
}

void NexusSettings::unregisterAsNXMHandler()
{
  NxmHandlerLinux::unregisterHandler();
}

std::vector<std::chrono::seconds> NexusSettings::validationTimeouts() const
{
  using namespace std::chrono_literals;

  const auto s = get<QString>(m_Settings, "Settings", "validation_timeouts", "");

  const auto numbers = s.split(" ");
  std::vector<std::chrono::seconds> v;

  for (auto ns : numbers) {
    ns = ns.trimmed();
    if (ns.isEmpty())
      continue;

    bool ok      = false;
    const auto n = ns.toInt(&ok);

    if (!ok || n < 0 || n > 100) {
      log::error("bad validation_timeouts number '{}'", ns);
      continue;
    }

    v.emplace_back(n);
  }

  if (v.empty())
    v = {10s, 15s, 20s};

  return v;
}

void NexusSettings::dump()
{
  const auto iniPath = InstanceManager::singleton().globalInstancesRootPath() + "/" +
                       QString::fromStdWString(AppConfig::nxmHandlerIni());

  if (!QFileInfo(iniPath).exists()) {
    log::debug("nxm ini not found at {}", iniPath);
    return;
  }

  QSettings s(iniPath, QSettings::IniFormat);
  if (const auto st = s.status(); st != QSettings::NoError) {
    log::debug("can't read nxm ini from {}", iniPath);
    return;
  }

  log::debug("nxmhandler settings:");

  QSettings handler(R"(HKEY_CURRENT_USER\Software\Classes\nxm\)",
                    QSettings::NativeFormat);
  log::debug(" . primary: {}", handler.value("shell/open/command/Default").toString());

  const auto noregister = getOptional<bool>(s, "General", "noregister");

  if (noregister) {
    log::debug(" . noregister: {}", *noregister);
  } else {
    log::debug(" . noregister: (not found)");
  }

  ScopedReadArray sra(s, "handlers");

  sra.for_each([&] {
    const auto games      = sra.get<QVariant>("games");
    const auto executable = sra.get<QVariant>("executable");
    const auto arguments  = sra.get<QVariant>("arguments");

    log::debug(" . handler:");
    log::debug("    . games:      {}", games.toString());
    log::debug("    . executable: {}", executable.toString());
    log::debug("    . arguments:  {}", arguments.toString());
  });
}

SteamSettings::SteamSettings(Settings& parent, QSettings& settings)
    : m_Parent(parent), m_Settings(settings)
{}

QString SteamSettings::appID() const
{
  // The user-override `app_id` ini key is no longer exposed; per-game plugins
  // already carry the right value via steamAPPId(), and per-executable overrides
  // live on individual Executable entries.
  return m_Parent.game().plugin()->steamAPPId();
}

void SteamSettings::setAppID(const QString&) {}

bool SteamSettings::login(QString& username, QString& password) const
{
  username.clear();
  password.clear();
  return false;
}

void SteamSettings::setLogin(QString, QString) {}

InterfaceSettings::InterfaceSettings(QSettings& settings) : m_Settings(settings) {}

std::optional<QString> InterfaceSettings::styleName() const
{
  return getOptional<QString>(m_Settings, "Settings", "style");
}

void InterfaceSettings::setStyleName(const QString& name)
{
  set(m_Settings, "Settings", "style", name);
}

int InterfaceSettings::qssFontSize() const
{
  const int size = get<int>(m_Settings, "Settings", "qss_font_size", 0);
  if (size < 0) {
    return 0;
  }
  if (size > 48) {
    return 48;
  }
  return size;
}

void InterfaceSettings::setQssFontSize(int size)
{
  if (size < 0) {
    size = 0;
  } else if (size > 48) {
    size = 48;
  }
  set(m_Settings, "Settings", "qss_font_size", size);
}

QString InterfaceSettings::fontFamily() const
{
  return get<QString>(m_Settings, "Settings", "font_family", "");
}

void InterfaceSettings::setFontFamily(const QString& family)
{
  set(m_Settings, "Settings", "font_family", family);
}

bool InterfaceSettings::collapsibleSeparators(Qt::SortOrder order) const
{
  return get<bool>(m_Settings, "Settings",
                   order == Qt::AscendingOrder ? "collapsible_separators_asc"
                                               : "collapsible_separators_dsc",
                   true);
}

void InterfaceSettings::setCollapsibleSeparators(bool ascending, bool descending)
{
  set(m_Settings, "Settings", "collapsible_separators_asc", ascending);
  set(m_Settings, "Settings", "collapsible_separators_dsc", descending);
}

bool InterfaceSettings::collapsibleSeparatorsHighlightTo() const
{
  return get<bool>(m_Settings, "Settings", "collapsible_separators_conflicts_to", true);
}

void InterfaceSettings::setCollapsibleSeparatorsHighlightTo(bool b)
{
  set(m_Settings, "Settings", "collapsible_separators_conflicts_to", b);
}

bool InterfaceSettings::collapsibleSeparatorsHighlightFrom() const
{
  return get<bool>(m_Settings, "Settings", "collapsible_separators_conflicts_from",
                   true);
}

void InterfaceSettings::setCollapsibleSeparatorsHighlightFrom(bool b)
{
  set(m_Settings, "Settings", "collapsible_separators_conflicts_from", b);
}

bool InterfaceSettings::collapsibleSeparatorsIcons(int column) const
{
  return get<bool>(m_Settings, "Settings",
                   QString("collapsible_separators_icons_%1").arg(column), true);
}

void InterfaceSettings::setCollapsibleSeparatorsIcons(int column, bool show)
{
  set(m_Settings, "Settings", QString("collapsible_separators_icons_%1").arg(column),
      show);
}

bool InterfaceSettings::collapsibleSeparatorsPerProfile() const
{
  return get<bool>(m_Settings, "Settings", "collapsible_separators_per_profile", false);
}

void InterfaceSettings::setCollapsibleSeparatorsPerProfile(bool b)
{
  set(m_Settings, "Settings", "collapsible_separators_per_profile", b);
}

bool InterfaceSettings::saveFilters() const
{
  return get<bool>(m_Settings, "Settings", "save_filters", false);
}

void InterfaceSettings::setSaveFilters(bool b)
{
  set(m_Settings, "Settings", "save_filters", b);
}

bool InterfaceSettings::autoCollapseOnHover() const
{
  return get<bool>(m_Settings, "Settings", "auto_collapse_on_hover", false);
}

void InterfaceSettings::setAutoCollapseOnHover(bool b)
{
  set(m_Settings, "Settings", "auto_collapse_on_hover", b);
}

bool InterfaceSettings::checkUpdateAfterInstallation() const
{
  return get<bool>(m_Settings, "Settings", "autocheck_update_install", true);
}

void InterfaceSettings::setCheckUpdateAfterInstallation(bool b)
{
  set(m_Settings, "Settings", "autocheck_update_install", b);
}

bool InterfaceSettings::compactDownloads() const
{
  return get<bool>(m_Settings, "Settings", "compact_downloads", false);
}

void InterfaceSettings::setCompactDownloads(bool b)
{
  set(m_Settings, "Settings", "compact_downloads", b);
}

bool InterfaceSettings::metaDownloads() const
{
  return get<bool>(m_Settings, "Settings", "meta_downloads", false);
}

void InterfaceSettings::setMetaDownloads(bool b)
{
  set(m_Settings, "Settings", "meta_downloads", b);
}

bool InterfaceSettings::hideDownloadsAfterInstallation() const
{
  return get<bool>(m_Settings, "Settings", "autohide_downloads", false);
}

void InterfaceSettings::setHideDownloadsAfterInstallation(bool b)
{
  set(m_Settings, "Settings", "autohide_downloads", b);
}

bool InterfaceSettings::showDownloadNotifications() const
{
  return get<bool>(m_Settings, "Settings", "download_notifications", false);
}

void InterfaceSettings::setShowDownloadNotifications(bool b)
{
  set(m_Settings, "Settings", "download_notifications", b);
}

bool InterfaceSettings::hideAPICounter() const
{
  return get<bool>(m_Settings, "Settings", "hide_api_counter", false);
}

void InterfaceSettings::setHideAPICounter(bool b)
{
  set(m_Settings, "Settings", "hide_api_counter", b);
}

bool InterfaceSettings::displayForeign() const
{
  return get<bool>(m_Settings, "Settings", "display_foreign", true);
}

void InterfaceSettings::setDisplayForeign(bool b)
{
  set(m_Settings, "Settings", "display_foreign", b);
}

QString InterfaceSettings::language()
{
  QString result = get<QString>(m_Settings, "Settings", "language", "");

  if (result.isEmpty()) {
    QStringList languagePreferences = QLocale::system().uiLanguages();

    if (!languagePreferences.empty()) {
      // the users most favoritest language
      result = languagePreferences.at(0);
    } else {
      // fallback system locale
      result = QLocale::system().name();
    }
  }

  return result;
}

void InterfaceSettings::setLanguage(const QString& name)
{
  set(m_Settings, "Settings", "language", name);
}

bool InterfaceSettings::isTutorialCompleted(const QString& windowName) const
{
  return get<bool>(m_Settings, "CompletedWindowTutorials", windowName, false);
}

void InterfaceSettings::setTutorialCompleted(const QString& windowName, bool b)
{
  set(m_Settings, "CompletedWindowTutorials", windowName, b);
}

bool InterfaceSettings::showChangeGameConfirmation() const
{
  return get<bool>(m_Settings, "Settings", "show_change_game_confirmation", true);
}

void InterfaceSettings::setShowChangeGameConfirmation(bool b)
{
  set(m_Settings, "Settings", "show_change_game_confirmation", b);
}

bool InterfaceSettings::showMenubarOnAlt() const
{
  return get<bool>(m_Settings, "Settings", "show_menubar_on_alt", true);
}

void InterfaceSettings::setShowMenubarOnAlt(bool b)
{
  set(m_Settings, "Settings", "show_menubar_on_alt", b);
}

bool InterfaceSettings::doubleClicksOpenPreviews() const
{
  return get<bool>(m_Settings, "Settings", "double_click_previews", true);
}

void InterfaceSettings::setDoubleClicksOpenPreviews(bool b)
{
  set(m_Settings, "Settings", "double_click_previews", b);
}

FilterWidget::Options InterfaceSettings::filterOptions() const
{
  FilterWidget::Options o;

  o.useRegex = get<bool>(m_Settings, "Settings", "filter_regex", false);
  o.regexCaseSensitive =
      get<bool>(m_Settings, "Settings", "regex_case_sensitive", false);
  o.regexExtended = get<bool>(m_Settings, "Settings", "regex_extended", false);
  o.scrollToSelection =
      get<bool>(m_Settings, "Settings", "filter_scroll_to_selection", false);

  return o;
}

void InterfaceSettings::setFilterOptions(const FilterWidget::Options& o)
{
  set(m_Settings, "Settings", "filter_regex", o.useRegex);
  set(m_Settings, "Settings", "regex_case_sensitive", o.regexCaseSensitive);
  set(m_Settings, "Settings", "regex_extended", o.regexExtended);
  set(m_Settings, "Settings", "filter_scroll_to_selection", o.scrollToSelection);
}

DiagnosticsSettings::DiagnosticsSettings(QSettings& settings) : m_Settings(settings) {}

log::Levels DiagnosticsSettings::logLevel() const
{
  return get<log::Levels>(m_Settings, "Settings", "log_level", log::Levels::Info);
}

void DiagnosticsSettings::setLogLevel(log::Levels level)
{
  set(m_Settings, "Settings", "log_level", level);
}

env::CoreDumpTypes DiagnosticsSettings::coreDumpType() const
{
  return get<env::CoreDumpTypes>(m_Settings, "Settings", "crash_dumps_type",
                                 env::CoreDumpTypes::Mini);
}

void DiagnosticsSettings::setCoreDumpType(env::CoreDumpTypes type)
{
  set(m_Settings, "Settings", "crash_dumps_type", type);
}

int DiagnosticsSettings::maxCoreDumps() const
{
  return get<int>(m_Settings, "Settings", "crash_dumps_max", 5);
}

void DiagnosticsSettings::setMaxCoreDumps(int n)
{
  set(m_Settings, "Settings", "crash_dumps_max", n);
}

std::chrono::seconds DiagnosticsSettings::spawnDelay() const
{
  return std::chrono::seconds(get<int>(m_Settings, "Settings", "spawn_delay", 0));
}

void DiagnosticsSettings::setSpawnDelay(std::chrono::seconds t)
{
  set(m_Settings, "Settings", "spawn_delay", t.count());
}

namespace
{
SettingsWriteBarrier g_GlobalSettingsWriteBarrier;
}

void GlobalSettings::updateRegistryKey()
{
  g_GlobalSettingsWriteBarrier.runIfAllowed([&] {
    const QString OldOrganization  = "Tannin";
    const QString OldApplication   = "Mod Organizer";
    const QString OldInstanceValue = "CurrentInstance";

    const QString OldRootKey = "Software\\" + OldOrganization;

    if (env::registryValueExists(OldRootKey + "\\" + OldApplication,
                                 OldInstanceValue)) {
      QSettings old(OldOrganization, OldApplication);
      settings().setValue("CurrentInstance",
                          old.value(OldInstanceValue).toString());
      old.remove(OldInstanceValue);
    }

    env::deleteRegistryKeyIfEmpty(OldRootKey);
  });
}

QString GlobalSettings::currentInstance()
{
  return settings().value("CurrentInstance", "").toString();
}

void GlobalSettings::setCurrentInstance(const QString& s)
{
  g_GlobalSettingsWriteBarrier.runIfAllowed(
      [&] { settings().setValue("CurrentInstance", s); });
}

QSettings GlobalSettings::settings()
{
  const QString Organization = "Mod Organizer Team";
  const QString Application  = "Mod Organizer";

  return QSettings(Organization, Application);
}

bool GlobalSettings::hideCreateInstanceIntro()
{
  return settings().value("HideCreateInstanceIntro", false).toBool();
}

void GlobalSettings::setHideCreateInstanceIntro(bool b)
{
  g_GlobalSettingsWriteBarrier.runIfAllowed(
      [&] { settings().setValue("HideCreateInstanceIntro", b); });
}

bool GlobalSettings::hideTutorialQuestion()
{
  return settings().value("HideTutorialQuestion", false).toBool();
}

void GlobalSettings::setHideTutorialQuestion(bool b)
{
  g_GlobalSettingsWriteBarrier.runIfAllowed(
      [&] { settings().setValue("HideTutorialQuestion", b); });
}

bool GlobalSettings::hideCategoryReminder()
{
  return settings().value("HideCategoryReminder", false).toBool();
}

void GlobalSettings::setHideCategoryReminder(bool b)
{
  g_GlobalSettingsWriteBarrier.runIfAllowed(
      [&] { settings().setValue("HideCategoryReminder", b); });
}

bool GlobalSettings::hideAssignCategoriesQuestion()
{
  return settings().value("HideAssignCategoriesQuestion", false).toBool();
}

void GlobalSettings::setHideAssignCategoriesQuestion(bool b)
{
  g_GlobalSettingsWriteBarrier.runIfAllowed(
      [&] { settings().setValue("HideAssignCategoriesQuestion", b); });
}

namespace
{
constexpr auto NexusLegacyCredentialKey = "APIKEY";
constexpr auto NexusOAuthCredentialKey  = "NEXUS_OAUTH_TOKENS";

std::optional<NexusOAuthTokens> parseStoredTokens(const QString& raw)
{
  if (raw.isEmpty()) {
    return std::nullopt;
  }

  const auto doc = QJsonDocument::fromJson(raw.toUtf8());
  if (doc.isNull() || !doc.isObject()) {
    return std::nullopt;
  }

  return NexusOAuthTokens::fromJson(doc.object());
}
}  // namespace

bool GlobalSettings::nexusApiKey(QString& apiKey)
{
  QString tempKey = getWindowsCredential(NexusLegacyCredentialKey);
  if (tempKey.isEmpty())
    return false;

  apiKey = tempKey;
  return true;
}

bool GlobalSettings::setNexusApiKey(const QString& apiKey)
{
  bool stored = false;
  g_GlobalSettingsWriteBarrier.runIfAllowed([&] {
    stored = setWindowsCredential(NexusLegacyCredentialKey, apiKey);
    if (!stored) {
      const auto e = GetLastError();
      log::error("Storing API key failed: {}", formatSystemMessage(e));
    }
  });
  return stored;
}

bool GlobalSettings::clearNexusApiKey()
{
  return setNexusApiKey("");
}

bool GlobalSettings::hasNexusApiKey()
{
  return !getWindowsCredential(NexusLegacyCredentialKey).isEmpty();
}

bool GlobalSettings::nexusOAuthTokens(NexusOAuthTokens& tokens)
{
  const auto raw    = getWindowsCredential(NexusOAuthCredentialKey);
  const auto parsed = parseStoredTokens(raw);
  if (!parsed) {
    return false;
  } else {
    tokens = *parsed;
  }
  return true;
}

bool GlobalSettings::setNexusOAuthTokens(const NexusOAuthTokens& tokens)
{
  bool stored = false;
  g_GlobalSettingsWriteBarrier.runIfAllowed([&] {
    const auto payload =
        QJsonDocument(tokens.toJson()).toJson(QJsonDocument::Compact);
    stored = setWindowsCredential(NexusOAuthCredentialKey, payload);
    if (!stored) {
      const auto e = GetLastError();
      log::error("Storing OAuth tokens failed: {}", formatSystemMessage(e));
    }
  });
  return stored;
}

bool GlobalSettings::clearNexusOAuthTokens()
{
  bool cleared = false;
  g_GlobalSettingsWriteBarrier.runIfAllowed(
      [&] { cleared = setWindowsCredential(NexusOAuthCredentialKey, ""); });
  return cleared;
}

bool GlobalSettings::hasNexusOAuthTokens()
{
  return !getWindowsCredential(NexusOAuthCredentialKey).isEmpty();
}

std::optional<NexusCredentialStoreSnapshot>
GlobalSettings::captureNexusCredentials()
{
  if (!syncWindowsCredentials()) {
    log::error("failed to synchronize the Nexus credential store");
    return std::nullopt;
  }

  NexusCredentialStoreSnapshot snapshot;

  QString apiKey;
  if (nexusApiKey(apiKey)) {
    snapshot.apiKey = apiKey;
  }

  if (hasNexusOAuthTokens()) {
    NexusOAuthTokens oauthTokens;
    if (!nexusOAuthTokens(oauthTokens)) {
      // A present entry that cannot be parsed cannot be restored exactly, so
      // fail before opening a dialog that could overwrite it.
      log::error("the stored Nexus OAuth credential could not be parsed");
      return std::nullopt;
    }
    snapshot.oauthTokens = oauthTokens;
  }

  return snapshot;
}

bool GlobalSettings::restoreNexusCredentials(
    const NexusCredentialStoreSnapshot& snapshot)
{
  // Attempt both writes even if the first one fails. This avoids needlessly
  // leaving the other credential in its edited state.
  const bool oauthRestored = snapshot.oauthTokens
                                 ? setNexusOAuthTokens(*snapshot.oauthTokens)
                                 : clearNexusOAuthTokens();
  const bool apiKeyRestored = snapshot.apiKey ? setNexusApiKey(*snapshot.apiKey)
                                               : clearNexusApiKey();

  return oauthRestored && apiKeyRestored && verifyNexusCredentials(snapshot);
}

bool GlobalSettings::verifyNexusCredentials(
    const NexusCredentialStoreSnapshot& snapshot)
{
  const auto current = captureNexusCredentials();
  return current && *current == snapshot;
}

void GlobalSettings::suppressWritesForFailedRollback() noexcept
{
  g_GlobalSettingsWriteBarrier.suppress();
}

bool GlobalSettings::failedRollbackWritesDrained() noexcept
{
  return g_GlobalSettingsWriteBarrier.suppressionDrained();
}

void GlobalSettings::resetDialogs()
{
  setHideCreateInstanceIntro(false);
  setHideTutorialQuestion(false);
}

QStringList GlobalSettings::portableInstances()
{
  return settings().value("PortableInstances").toStringList();
}

void GlobalSettings::addPortableInstance(const QString& path)
{
  g_GlobalSettingsWriteBarrier.runIfAllowed([&] {
    const QString canonical = QDir(path).absolutePath();
    QStringList list = portableInstances();
    if (!list.contains(canonical)) {
      list.append(canonical);
      settings().setValue("PortableInstances", list);
    }
  });
}

void GlobalSettings::removePortableInstance(const QString& path)
{
  g_GlobalSettingsWriteBarrier.runIfAllowed([&] {
    const QString canonical = QDir(path).absolutePath();
    QStringList list = portableInstances();
    if (list.removeAll(canonical) > 0) {
      settings().setValue("PortableInstances", list);
    }
  });
}
