#include "protonsettingsedit.h"

#include "settingsmigration.h"
#include "settingswritebarrier.h"

#include <QDir>
#include <QFileInfo>
#include <QLockFile>

#include <utility>

namespace ProtonSettingsEdit
{
namespace
{
SettingsWriteBarrier g_WriteBarrier(SettingsWriteBarrier::Concurrency::Serialized);

Value valueFrom(const SettingsMigration::SettingsSnapshot& values,
                const QString& key)
{
  const auto it = values.constFind(key);
  return it == values.cend() ? Value{} : Value{true, it.value()};
}

SettingsMigration::SettingsSnapshot settingsValues(const Snapshot& snapshot)
{
  SettingsMigration::SettingsSnapshot values;
  if (snapshot.launchWrapper.present) {
    values.insert(LaunchWrapperKey, snapshot.launchWrapper.value);
  }
  if (snapshot.disableVfsCache.present) {
    values.insert(DisableVfsCacheKey, snapshot.disableVfsCache.value);
  }
  return values;
}

bool sameConfigIdentity(const ConfigState& left, const ConfigState& right)
{
  if (left.present != right.present) {
    return false;
  }
  return !left.present ||
         (left.config.app_id == right.config.app_id &&
          left.config.prefix_path == right.config.prefix_path &&
          left.config.created == right.config.created);
}

bool sameProton(const ConfigState& left, const ConfigState& right)
{
  return left.present == right.present &&
         (!left.present ||
          (left.config.proton_name == right.config.proton_name &&
           left.config.proton_path == right.config.proton_path));
}

std::optional<ConfigState> captureConfigUnlocked()
{
  const QString path = FluorineConfig::configFilePath();
  if (!QFileInfo::exists(path)) {
    return ConfigState{};
  }

  auto config = FluorineConfig::load();
  if (!config) {
    return std::nullopt;
  }
  return ConfigState{true, std::move(*config)};
}

bool ensureConfigLockDirectory()
{
  return QDir().mkpath(
      QFileInfo(FluorineConfig::configFilePath()).dir().absolutePath());
}

bool saveConfigUnlocked(const FluorineConfig& config)
{
  return config.save();
}

bool configMatches(const FluorineConfig& left, const FluorineConfig& right)
{
  return left.app_id == right.app_id && left.prefix_path == right.prefix_path &&
         left.proton_name == right.proton_name &&
         left.proton_path == right.proton_path && left.created == right.created;
}

std::optional<Snapshot> captureUnderLocks(QSettings& defaultSettings,
                                          int lockTimeoutMs)
{
  QLockFile settingsLock(
      SettingsMigration::settingsLockPath(defaultSettings.fileName()));
  settingsLock.setStaleLockTime(SettingsMigration::SettingsLockStaleMs);
  if (!settingsLock.tryLock(lockTimeoutMs) ||
      defaultSettings.status() != QSettings::NoError) {
    return std::nullopt;
  }

  QLockFile configLock(SettingsMigration::settingsLockPath(
      FluorineConfig::configFilePath()));
  configLock.setStaleLockTime(SettingsMigration::SettingsLockStaleMs);
  if (!ensureConfigLockDirectory() || !configLock.tryLock(lockTimeoutMs)) {
    return std::nullopt;
  }

  defaultSettings.sync();
  if (defaultSettings.status() != QSettings::NoError) {
    return std::nullopt;
  }
  const auto persisted =
      SettingsMigration::inspectSettingsFile(defaultSettings.fileName());
  if (persisted.status != QSettings::NoError) {
    return std::nullopt;
  }

  auto config = captureConfigUnlocked();
  if (!config) {
    return std::nullopt;
  }

  return Snapshot{valueFrom(persisted.values, LaunchWrapperKey),
                  valueFrom(persisted.values, DisableVfsCacheKey),
                  std::move(*config)};
}

bool valueRestored(const Value& current, const Value& original,
                   const Value& rejected)
{
  if (original == rejected) {
    return true;
  }
  // Exact original is the normal case. A third value means another writer won
  // after the rejected snapshot and must not be overwritten by this rollback.
  return current == original || current != rejected;
}
}  // namespace

std::optional<Snapshot> capture(QSettings& defaultSettings, int lockTimeoutMs)
{
  auto lease = g_WriteBarrier.enterIfAllowed();
  if (!lease) {
    return std::nullopt;
  }
  return captureUnderLocks(defaultSettings, lockTimeoutMs);
}

std::optional<Snapshot> capture(int lockTimeoutMs)
{
  QSettings defaultSettings;
  return capture(defaultSettings, lockTimeoutMs);
}

bool persist(QSettings& defaultSettings, const QString& launchWrapper,
             bool disableVfsCache, const QString& protonName,
             const QString& protonPath, int lockTimeoutMs)
{
  auto lease = g_WriteBarrier.enterIfAllowed();
  if (!lease) {
    return false;
  }

  QLockFile settingsLock(
      SettingsMigration::settingsLockPath(defaultSettings.fileName()));
  settingsLock.setStaleLockTime(SettingsMigration::SettingsLockStaleMs);
  if (!settingsLock.tryLock(lockTimeoutMs) ||
      defaultSettings.status() != QSettings::NoError) {
    return false;
  }

  QLockFile configLock(SettingsMigration::settingsLockPath(
      FluorineConfig::configFilePath()));
  configLock.setStaleLockTime(SettingsMigration::SettingsLockStaleMs);
  if (!ensureConfigLockDirectory() || !configLock.tryLock(lockTimeoutMs)) {
    return false;
  }

  defaultSettings.setValue(LaunchWrapperKey, launchWrapper);
  defaultSettings.setValue(DisableVfsCacheKey, disableVfsCache);
  if (SettingsMigration::syncAndVerifySettingsUnderLock(defaultSettings) !=
      QSettings::NoError) {
    return false;
  }

  auto config = captureConfigUnlocked();
  if (!config) {
    return false;
  }
  if (config->present && !protonName.isEmpty() && !protonPath.isEmpty() &&
      (config->config.proton_name != protonName ||
       config->config.proton_path != protonPath)) {
    config->config.proton_name = protonName;
    config->config.proton_path = protonPath;
    if (!saveConfigUnlocked(config->config)) {
      return false;
    }
    const auto verified = captureConfigUnlocked();
    if (!verified || !verified->present ||
        !configMatches(verified->config, config->config)) {
      return false;
    }
  }

  return true;
}

bool persist(const QString& launchWrapper, bool disableVfsCache,
             const QString& protonName, const QString& protonPath,
             int lockTimeoutMs)
{
  QSettings defaultSettings;
  return persist(defaultSettings, launchWrapper, disableVfsCache, protonName,
                 protonPath, lockTimeoutMs);
}

bool restore(QSettings& defaultSettings, const Snapshot& original,
             const Snapshot& rejected, int lockTimeoutMs)
{
  auto lease = g_WriteBarrier.enterIfAllowed();
  if (!lease) {
    return false;
  }

  QLockFile settingsLock(
      SettingsMigration::settingsLockPath(defaultSettings.fileName()));
  settingsLock.setStaleLockTime(SettingsMigration::SettingsLockStaleMs);
  if (!settingsLock.tryLock(lockTimeoutMs) ||
      defaultSettings.status() != QSettings::NoError) {
    return false;
  }

  QLockFile configLock(SettingsMigration::settingsLockPath(
      FluorineConfig::configFilePath()));
  configLock.setStaleLockTime(SettingsMigration::SettingsLockStaleMs);
  if (!ensureConfigLockDirectory() || !configLock.tryLock(lockTimeoutMs)) {
    return false;
  }

  if (!SettingsMigration::restoreSettingsSnapshot(
          defaultSettings, settingsValues(original), settingsValues(rejected)) ||
      SettingsMigration::syncAndVerifySettingsUnderLock(defaultSettings) !=
          QSettings::NoError) {
    return false;
  }

  auto current = captureConfigUnlocked();
  if (!current) {
    return false;
  }

  // An identity change represents an explicit prefix lifecycle operation.
  // Retain it, including the Proton selected for that operation.
  if (sameConfigIdentity(original.config, rejected.config) &&
      !sameProton(original.config, rejected.config) &&
      sameConfigIdentity(*current, rejected.config) &&
      sameProton(*current, rejected.config)) {
    current->config.proton_name = original.config.config.proton_name;
    current->config.proton_path = original.config.config.proton_path;
    if (!saveConfigUnlocked(current->config)) {
      return false;
    }
  }

  return true;
}

bool restore(const Snapshot& original, const Snapshot& rejected,
             int lockTimeoutMs)
{
  QSettings defaultSettings;
  return restore(defaultSettings, original, rejected, lockTimeoutMs);
}

bool verify(QSettings& defaultSettings, const Snapshot& original,
            const Snapshot& rejected, int lockTimeoutMs)
{
  auto current = capture(defaultSettings, lockTimeoutMs);
  if (!current) {
    return false;
  }

  if (!valueRestored(current->launchWrapper, original.launchWrapper,
                     rejected.launchWrapper) ||
      !valueRestored(current->disableVfsCache, original.disableVfsCache,
                     rejected.disableVfsCache)) {
    return false;
  }

  if (!sameConfigIdentity(original.config, rejected.config) ||
      sameProton(original.config, rejected.config) ||
      !sameConfigIdentity(current->config, rejected.config)) {
    return true;
  }
  return sameProton(current->config, original.config) ||
         !sameProton(current->config, rejected.config);
}

bool verify(const Snapshot& original, const Snapshot& rejected,
            int lockTimeoutMs)
{
  QSettings defaultSettings;
  return verify(defaultSettings, original, rejected, lockTimeoutMs);
}

void suppressWritesForFailedRollback() noexcept
{
  g_WriteBarrier.suppress();
}

bool failedRollbackWritesDrained() noexcept
{
  return g_WriteBarrier.suppressionDrained();
}
}  // namespace ProtonSettingsEdit
