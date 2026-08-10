#ifndef SETTINGSMIGRATION_H
#define SETTINGSMIGRATION_H

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QMap>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QTemporaryFile>
#include <QVariant>
#include <QVersionNumber>

#include <limits>
#include <memory>
#include <optional>

namespace SettingsMigration
{
// Settings migrations inherited from MO2 used product-version comparisons.
// Keep their ordering in a product-independent, monotonically increasing
// schema. New migrations must append a new value and advance CurrentSchema.
inline constexpr int BaselineSchema = 0;
inline constexpr int Schema2_1_6    = 1;
inline constexpr int Schema2_2_0    = 2;
inline constexpr int Schema2_2_1    = 3;
inline constexpr int Schema2_2_2    = 4;
inline constexpr int Schema2_3_0    = 5;
inline constexpr int Schema2_4_0    = 6;
inline constexpr int Schema2_5_0    = 7;
inline constexpr int CurrentSchema  = Schema2_5_0;
inline constexpr int CurrentCategoryMigration = 1;
inline constexpr int NewInstanceProvenance = 1;
inline constexpr int SettingsLockTimeoutMs = 5000;
// Settings migrations can span interactive startup. Never expire a live lock
// based on age; QLockFile can still recover locks whose owning process died.
inline constexpr int SettingsLockStaleMs = 0;
inline const QString ProductVersionKey = QStringLiteral("General/version");
inline const QString SettingsSchemaKey =
    QStringLiteral("General/settings_schema_version");
inline const QString FirstStartKey = QStringLiteral("General/first_start");
inline const QString NewInstanceProvenanceKey =
    QStringLiteral("General/new_instance_provenance");
inline const QString CategoryMigrationRequestTag =
    QStringLiteral("fluorine-category-migration");
// A present schema value that cannot be parsed is not equivalent to a missing
// legacy key. Treat it as unknown and skip migrations rather than replaying
// potentially destructive migrations from the baseline.
inline constexpr int UnknownSchema = std::numeric_limits<int>::max();

inline int inferSchemaFromLegacyProductVersion(
    const std::optional<QVersionNumber>& productVersion)
{
  if (!productVersion || productVersion->isNull()) {
    return BaselineSchema;
  }

  const auto& version = *productVersion;

  // Fluorine was forked from MO2 after the 2.5 migration baseline. Older
  // Fluorine releases stored their 0.x product version in General/version but
  // still ran all inherited migrations, so they are already current.
  if (version.majorVersion() == 0 || version >= QVersionNumber(2, 5)) {
    return CurrentSchema;
  }
  if (version >= QVersionNumber(2, 4)) {
    return Schema2_4_0;
  }
  if (version >= QVersionNumber(2, 3)) {
    return Schema2_3_0;
  }
  if (version >= QVersionNumber(2, 2, 2)) {
    return Schema2_2_2;
  }
  if (version >= QVersionNumber(2, 2, 1)) {
    return Schema2_2_1;
  }
  if (version >= QVersionNumber(2, 2)) {
    return Schema2_2_0;
  }
  if (version >= QVersionNumber(2, 1, 6)) {
    return Schema2_1_6;
  }
  return BaselineSchema;
}

inline int resolveSchemaVersion(
    const std::optional<int>& storedSchema,
    const std::optional<QVersionNumber>& legacyProductVersion)
{
  if (storedSchema) {
    return *storedSchema >= BaselineSchema ? *storedSchema : UnknownSchema;
  }
  return inferSchemaFromLegacyProductVersion(legacyProductVersion);
}

inline int resolveStoredSchemaValue(
    const std::optional<QVariant>& storedValue,
    const std::optional<QVersionNumber>& legacyProductVersion)
{
  if (!storedValue) {
    return inferSchemaFromLegacyProductVersion(legacyProductVersion);
  }

  // Parse the complete textual representation. QVariant::value<int>() silently
  // turns malformed and overflowing strings into zero, which would look like
  // a legitimate baseline and replay every inherited migration.
  bool ok          = false;
  const int schema = storedValue->toString().trimmed().toInt(&ok, 10);
  if (!ok || schema < BaselineSchema) {
    return UnknownSchema;
  }
  return schema;
}

inline std::optional<QVersionNumber> parseProductVersion(const QVariant& value)
{
  const QString text = value.toString().trimmed();
  qsizetype suffix   = 0;
  auto version       = QVersionNumber::fromString(text, &suffix).normalized();
  if (version.isNull() || suffix != text.size()) {
    return std::nullopt;
  }
  return version;
}

inline int resolveStoredSettingsSchema(
    const std::optional<QVariant>& storedSchema,
    const std::optional<QVariant>& storedProductVersion)
{
  if (storedSchema) {
    return resolveStoredSchemaValue(storedSchema, std::nullopt);
  }
  if (!storedProductVersion) {
    return BaselineSchema;
  }
  const auto productVersion = parseProductVersion(*storedProductVersion);
  return productVersion ? inferSchemaFromLegacyProductVersion(productVersion)
                        : UnknownSchema;
}

inline bool categoryMigrationPending(
    const std::optional<QVariant>& storedMigration, int previousSchema)
{
  if (!storedMigration) {
    return previousSchema < Schema2_5_0;
  }

  bool ok                 = false;
  const int storedVersion = storedMigration->toString().trimmed().toInt(&ok, 10);
  return !ok || storedVersion < CurrentCategoryMigration;
}

inline bool isExpectedCategoryMigrationResponse(int requestId,
                                                int expectedRequestId,
                                                const QVariant& userData)
{
  return expectedRequestId >= 0 && requestId == expectedRequestId &&
         userData.toString() == CategoryMigrationRequestTag;
}

inline bool isCategoryMigrationResponse(const QVariant& userData)
{
  return userData.toString() == CategoryMigrationRequestTag;
}

inline bool isNewInstance(bool firstStart, bool hasCreationProvenance,
                          bool hasStoredSchema,
                          bool hasStoredProductVersion) noexcept
{
  // Missing version/schema keys are ambiguous in an established or recovered
  // INI. Only the creation path's dedicated provenance marker is authoritative.
  return firstStart && hasCreationProvenance && !hasStoredSchema &&
         !hasStoredProductVersion;
}

inline void markNewInstance(QSettings& settings)
{
  settings.setValue(FirstStartKey, true);
  settings.setValue(NewInstanceProvenanceKey, NewInstanceProvenance);
}

inline constexpr bool mayFinishWithoutMigration(bool transactionActive,
                                                bool processingStarted) noexcept
{
  return transactionActive && !processingStarted;
}

inline QString nextCorruptSettingsPath(const QString& path)
{
  const QString base = path + QStringLiteral(".corrupt");
  if (!QFileInfo::exists(base)) {
    return base;
  }
  for (int suffix = 1;; ++suffix) {
    const QString candidate = base + QStringLiteral(".%1").arg(suffix);
    if (!QFileInfo::exists(candidate)) {
      return candidate;
    }
  }
}

struct SettingsAliasRewrite
{
  QString path;
  QString symlink;
  QString cycleCandidate;
};

inline std::optional<SettingsAliasRewrite> rewriteOneSettingsAlias(
    const QString& path)
{
  QString probe = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
  QString suffix;
  for (;;) {
    const QFileInfo info(probe);
    if (info.isSymLink()) {
      QString target = info.symLinkTarget();
      if (!target.isEmpty() && QDir::isRelativePath(target)) {
        target = info.dir().absoluteFilePath(target);
      }
      if (!target.isEmpty()) {
        const QString rewritten = suffix.isEmpty()
                                      ? target
                                      : QDir(target).filePath(suffix);
        return SettingsAliasRewrite{
            QDir::cleanPath(QFileInfo(rewritten).absoluteFilePath()), probe,
            suffix.isEmpty() ? probe : QDir(probe).filePath(suffix)};
      }
    }

    const QString fileName = info.fileName();
    const QString parent   = info.dir().absolutePath();
    if (fileName.isEmpty() || parent == probe) {
      return std::nullopt;
    }
    suffix = suffix.isEmpty() ? fileName : fileName + QLatin1Char('/') + suffix;
    probe  = parent;
  }
}

inline QString settingsIdentityPath(const QString& path)
{
  QString current = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
  QMap<QString, int> seenSymlinks;
  QStringList cycleCandidates;
  for (;;) {
    const auto rewrite = rewriteOneSettingsAlias(current);
    if (!rewrite) {
      return current;
    }

    const auto previous = seenSymlinks.constFind(rewrite->symlink);
    if (previous != seenSymlinks.cend()) {
      QString stablePath = rewrite->cycleCandidate;
      for (int i = previous.value(); i < cycleCandidates.size(); ++i) {
        if (cycleCandidates[i] < stablePath) {
          stablePath = cycleCandidates[i];
        }
      }
      return QDir::cleanPath(stablePath);
    }
    seenSymlinks.insert(rewrite->symlink, cycleCandidates.size());
    cycleCandidates.push_back(rewrite->cycleCandidate);
    current = rewrite->path;
  }
}

inline QString settingsLockPath(const QString& path)
{
  const QString identityPath = settingsIdentityPath(path);
  const QByteArray identity =
      QCryptographicHash::hash(identityPath.toUtf8(),
                               QCryptographicHash::Sha256)
          .toHex()
          .left(24);
  return QFileInfo(identityPath).dir().filePath(
      QStringLiteral(".fluorine-settings-%1.lock")
          .arg(QString::fromLatin1(identity)));
}

// Callers hold settingsLockPath() while using this helper. Re-read the exact
// bytes under that lock and atomically rename the live pathname; never remove
// it after a copy, which could delete a concurrently replaced settings file.
inline std::optional<QString> quarantineCorruptSettingsFileUnlocked(
    const QString& path, const QByteArray& expectedBytes)
{
  const QString livePath = settingsIdentityPath(path);
  const auto permissions = QFileInfo(livePath).permissions();
  QFile current(livePath);
  if (!current.open(QIODevice::ReadOnly)) {
    return std::nullopt;
  }
  const QByteArray currentBytes = current.readAll();
  if (current.error() != QFileDevice::NoError ||
      currentBytes != expectedBytes) {
    return std::nullopt;
  }
  current.close();

  const QString quarantinePath = nextCorruptSettingsPath(livePath);
  if (!QFile::rename(livePath, quarantinePath)) {
    return std::nullopt;
  }

  // The pathname may have been atomically replaced by a non-cooperating
  // writer between the comparison and rename. Verify what was actually moved;
  // if it was not the inspected corruption, restore it when the original name
  // is still free. QFile::rename never overwrites a concurrently recreated
  // path, so either way the replacement bytes remain preserved.
  QFile quarantined(quarantinePath);
  const QByteArray quarantinedBytes = quarantined.open(QIODevice::ReadOnly)
                                           ? quarantined.readAll()
                                           : QByteArray();
  const bool movedExpectedBytes =
      quarantined.isOpen() && quarantined.error() == QFileDevice::NoError &&
      quarantinedBytes == expectedBytes;
  quarantined.close();
  if (!movedExpectedBytes) {
    if (!QFileInfo::exists(livePath)) {
      QFile::rename(quarantinePath, livePath);
    }
    return std::nullopt;
  }

  // Recreate the canonical target while the caller still holds its lock. A
  // file symlink remains intact and never changes to a separate lock identity.
  QFile replacement(livePath);
  if (!replacement.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
    if (!QFileInfo::exists(livePath)) {
      QFile::rename(quarantinePath, livePath);
    }
    return std::nullopt;
  }
  replacement.close();
  if (!replacement.setPermissions(permissions)) {
    QFile::remove(livePath);
    QFile::rename(quarantinePath, livePath);
    return std::nullopt;
  }
  return quarantinePath;
}

inline std::optional<QString> preserveCorruptSettingsFile(const QString& path)
{
  if (!QFileInfo(path).isFile()) {
    return std::nullopt;
  }
  const QString backupPath = nextCorruptSettingsPath(path);
  return QFile::copy(path, backupPath) ? std::optional<QString>(backupPath)
                                      : std::nullopt;
}

inline std::optional<QString> preserveCorruptSettingsBytes(
    const QString& path, const QByteArray& bytes)
{
  const QString backupPath = nextCorruptSettingsPath(path);
  QSaveFile backup(backupPath);
  if (!backup.open(QIODevice::WriteOnly)) {
    return std::nullopt;
  }
  const QFileInfo source(path);
  if (source.exists() && !backup.setPermissions(source.permissions())) {
    backup.cancelWriting();
    return std::nullopt;
  }
  if (backup.write(bytes) != bytes.size() || !backup.commit()) {
    return std::nullopt;
  }
  return backupPath;
}

inline int schemaFromSettings(const QSettings& settings)
{
  const std::optional<QVariant> storedSchema =
      settings.contains(SettingsSchemaKey)
          ? std::optional<QVariant>(settings.value(SettingsSchemaKey))
          : std::nullopt;
  const std::optional<QVariant> storedProduct =
      settings.contains(ProductVersionKey)
          ? std::optional<QVariant>(settings.value(ProductVersionKey))
          : std::nullopt;
  return resolveStoredSettingsSchema(storedSchema, storedProduct);
}

using SettingsSnapshot = QMap<QString, QVariant>;

inline SettingsSnapshot captureSettingsSnapshot(const QSettings& settings)
{
  SettingsSnapshot snapshot;
  for (const auto& key : settings.allKeys()) {
    snapshot.insert(key, settings.value(key));
  }
  return snapshot;
}

struct DiskSettingsState
{
  QSettings::Status status{QSettings::NoError};
  int schema{BaselineSchema};
  QString productVersion;
  QByteArray bytes;
  SettingsSnapshot values;
};

inline QString inspectionTemporaryTemplate(const QString& settingsPath)
{
  return QDir(QFileInfo(settingsPath).absolutePath())
      .filePath(QStringLiteral(".fluorine-settings-inspect-XXXXXX"));
}

inline DiskSettingsState inspectSettingsFile(const QString& path)
{
  DiskSettingsState result;
  if (QFileInfo::exists(path)) {
    QFile source(path);
    if (!source.open(QIODevice::ReadOnly)) {
      result.status = QSettings::AccessError;
      return result;
    }
    result.bytes = source.readAll();
    if (source.error() != QFileDevice::NoError) {
      result.status = QSettings::AccessError;
      return result;
    }
  }

  // QSettings shares a process-wide cache by filename. Parse an exact copy at
  // a distinct temporary path so this reflects disk, not another live backend.
  // Do not make a valid portable instance depend on the host's system
  // temporary directory. The instance directory must already be writable for
  // QSettings' atomic updates, and the unique adjacent copy also bypasses
  // QSettings' process-wide cache for the live filename.
  QTemporaryFile temporary(inspectionTemporaryTemplate(path));
  if (!temporary.open() || temporary.write(result.bytes) != result.bytes.size()) {
    result.status = QSettings::AccessError;
    return result;
  }
  const QString temporaryPath = temporary.fileName();
  temporary.close();
  {
    QSettings parsed(temporaryPath, QSettings::IniFormat);
    parsed.allKeys();
    result.status         = parsed.status();
    result.schema         = schemaFromSettings(parsed);
    result.productVersion = parsed.value(ProductVersionKey).toString();
    result.values         = captureSettingsSnapshot(parsed);
  }
  return result;
}

inline QSettings::Status syncAndVerifySettingsUnderLock(QSettings& settings)
{
  settings.sync();
  if (settings.status() != QSettings::NoError) {
    return settings.status();
  }

  const auto expected  = captureSettingsSnapshot(settings);
  const auto persisted = inspectSettingsFile(settings.fileName());
  if (persisted.status != QSettings::NoError) {
    return persisted.status;
  }
  if (persisted.values != expected) {
    return QSettings::AccessError;
  }
  return QSettings::NoError;
}

inline std::optional<SettingsSnapshot> captureInteractiveSettingsSnapshot(
    QSettings& settings, int lockTimeoutMs = SettingsLockTimeoutMs)
{
  QLockFile lock(settingsLockPath(settings.fileName()));
  lock.setStaleLockTime(SettingsLockStaleMs);
  if (!lock.tryLock(lockTimeoutMs) || settings.status() != QSettings::NoError) {
    return std::nullopt;
  }

  settings.sync();
  if (settings.status() != QSettings::NoError) {
    return std::nullopt;
  }
  return captureSettingsSnapshot(settings);
}

inline bool snapshotEntryMatches(const SettingsSnapshot& left,
                                 const SettingsSnapshot& right,
                                 const QString& key)
{
  const auto leftEntry  = left.constFind(key);
  const auto rightEntry = right.constFind(key);
  if (leftEntry == left.cend() || rightEntry == right.cend()) {
    return leftEntry == left.cend() && rightEntry == right.cend();
  }
  return leftEntry.value() == rightEntry.value();
}

inline bool restoreSettingsSnapshot(QSettings& settings,
                                    const SettingsSnapshot& original,
                                    const SettingsSnapshot& rejected)
{
  // Read disk without syncing the live backend first: it can still contain
  // rejected, unsynced migration changes that must not overwrite another
  // process. Start from the exact disk state and only undo keys that still
  // match our rejected state; another process's differing value wins.
  const auto current = inspectSettingsFile(settings.fileName());
  if (current.status != QSettings::NoError) {
    return false;
  }

  QSet<QString> migrationKeys;
  for (auto entry = original.cbegin(); entry != original.cend(); ++entry) {
    migrationKeys.insert(entry.key());
  }
  for (auto entry = rejected.cbegin(); entry != rejected.cend(); ++entry) {
    migrationKeys.insert(entry.key());
  }

  SettingsSnapshot restoredValues = current.values;
  QSet<QString> revertedKeys;
  for (const auto& key : migrationKeys) {
    if (snapshotEntryMatches(original, rejected, key) ||
        !snapshotEntryMatches(current.values, rejected, key)) {
      continue;
    }

    const auto originalEntry = original.constFind(key);
    if (originalEntry == original.cend()) {
      restoredValues.remove(key);
    } else {
      restoredValues.insert(key, originalEntry.value());
    }
    revertedKeys.insert(key);
  }

  // Replace the live backend's pending state with the merged disk snapshot so
  // its later sync/destructor cannot replay rejected migration writes.
  settings.clear();
  for (auto entry = restoredValues.cbegin(); entry != restoredValues.cend();
       ++entry) {
    settings.setValue(entry.key(), entry.value());
  }
  settings.sync();

  // The first sync can import an external write that the live backend had not
  // observed when clear() was staged. Retry only rollback-owned keys after
  // that import; never touch independently changed values.
  for (int attempt = 0; attempt < 3; ++attempt) {
    const auto restored = inspectSettingsFile(settings.fileName());
    if (restored.status != QSettings::NoError) {
      return false;
    }

    QSet<QString> remaining;
    for (const auto& key : revertedKeys) {
      if (snapshotEntryMatches(restored.values, rejected, key)) {
        remaining.insert(key);
      }
    }
    if (remaining.isEmpty()) {
      return true;
    }

    for (const auto& key : remaining) {
      const auto originalEntry = original.constFind(key);
      if (originalEntry == original.cend()) {
        settings.remove(key);
      } else {
        settings.setValue(key, originalEntry.value());
      }
    }
    settings.sync();
  }
  return false;
}

inline bool restoreInteractiveSettingsSnapshot(
    QSettings& settings, const SettingsSnapshot& original,
    int lockTimeoutMs = SettingsLockTimeoutMs)
{
  QLockFile lock(settingsLockPath(settings.fileName()));
  lock.setStaleLockTime(SettingsLockStaleMs);
  if (!lock.tryLock(lockTimeoutMs) || settings.status() != QSettings::NoError) {
    return false;
  }

  const auto rejected = captureSettingsSnapshot(settings);
  if (!restoreSettingsSnapshot(settings, original, rejected) ||
      settings.status() != QSettings::NoError) {
    return false;
  }

  return syncAndVerifySettingsUnderLock(settings) == QSettings::NoError;
}

inline bool verifyInteractiveSettingsState(
    QSettings& settings, int lockTimeoutMs = SettingsLockTimeoutMs)
{
  QLockFile lock(settingsLockPath(settings.fileName()));
  lock.setStaleLockTime(SettingsLockStaleMs);
  if (!lock.tryLock(lockTimeoutMs) || settings.status() != QSettings::NoError) {
    return false;
  }
  return syncAndVerifySettingsUnderLock(settings) == QSettings::NoError;
}

inline bool finishSettingsRollback(
    QSettings& settings, SettingsSnapshot& original,
    const SettingsSnapshot& rejected,
    std::unique_ptr<QLockFile>& transactionLock)
{
  if (!restoreSettingsSnapshot(settings, original, rejected)) {
    return false;
  }
  original.clear();
  transactionLock.reset();
  return true;
}

// Writes the completion markers after the caller has established exclusive
// access. This deliberately stages the markers before sync, so any later
// successful retry flushes a complete migration rather than only its removals.
inline QSettings::Status writeCompletedUpdatesUnderLock(
    QSettings& settings, const QVersionNumber& currentProductVersion,
    int previousSchema)
{
  // Unknown provenance must never be converted into an implicit current
  // schema merely by replacing the legacy product-version key.
  if (previousSchema == UnknownSchema) {
    return QSettings::FormatError;
  }

  const auto statusBeforeWrite = settings.status();
  if (previousSchema <= CurrentSchema) {
    settings.setValue(SettingsSchemaKey, CurrentSchema);
  }
  settings.setValue(ProductVersionKey, currentProductVersion.toString());
  settings.sync();

  // AccessError is sticky, so a same-object retry is verified below through a
  // fresh backend. A newly raised error, and every FormatError, are failures.
  if (settings.status() == QSettings::FormatError) {
    const auto malformed = inspectSettingsFile(settings.fileName());
    if (malformed.status == QSettings::FormatError) {
      quarantineCorruptSettingsFileUnlocked(settings.fileName(),
                                             malformed.bytes);
    }
    return QSettings::FormatError;
  }
  if (settings.status() == QSettings::AccessError &&
      statusBeforeWrite != QSettings::AccessError) {
    return settings.status();
  }

  // QSettings status is sticky. Verify the committed values through a fresh
  // backend so a transient AccessError can recover without treating an
  // unwritten marker as successful.
  const auto verification = inspectSettingsFile(settings.fileName());
  bool schemaMatches = true;
  if (previousSchema <= CurrentSchema) {
    schemaMatches = verification.schema == CurrentSchema;
  }

  if (verification.status != QSettings::NoError) {
    return verification.status;
  }
  if (verification.productVersion != currentProductVersion.toString() ||
      !schemaMatches) {
    return QSettings::AccessError;
  }
  return QSettings::NoError;
}

inline QSettings::Status commitCompletedUpdates(
    QSettings& settings, const QVersionNumber& currentProductVersion,
    int previousSchema)
{
  if (previousSchema == UnknownSchema) {
    return QSettings::FormatError;
  }

  QLockFile lock(settingsLockPath(settings.fileName()));
  lock.setStaleLockTime(SettingsLockStaleMs);
  if (!lock.tryLock(SettingsLockTimeoutMs)) {
    return QSettings::AccessError;
  }

  const auto preflight = inspectSettingsFile(settings.fileName());
  if (preflight.status == QSettings::FormatError) {
    quarantineCorruptSettingsFileUnlocked(settings.fileName(), preflight.bytes);
    return QSettings::FormatError;
  }
  if (preflight.status != QSettings::NoError) {
    return preflight.status;
  }
  if (preflight.schema != previousSchema) {
    return QSettings::AccessError;
  }

  return writeCompletedUpdatesUnderLock(settings, currentProductVersion,
                                        previousSchema);
}

inline constexpr bool requiresMigration(int previousSchema,
                                        int targetSchema) noexcept
{
  return previousSchema < targetSchema;
}

inline constexpr bool startupMayContinue(bool updateTransactionStarted) noexcept
{
  // Running without the transaction would expose unmigrated or partially
  // migrated settings to the UI and plugins. Lock contention is therefore a
  // startup failure, not a reason to skip migrations for this session.
  return updateTransactionStarted;
}
}  // namespace SettingsMigration

#endif  // SETTINGSMIGRATION_H
