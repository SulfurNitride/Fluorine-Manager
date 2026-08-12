#ifndef SETTINGSMIGRATION_H
#define SETTINGSMIGRATION_H

#include <QByteArray>
#include <QLockFile>
#include <QMap>
#include <QSettings>
#include <QString>
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

int resolveStoredSchemaValue(
    const std::optional<QVariant>& storedValue,
    const std::optional<QVersionNumber>& legacyProductVersion);
std::optional<QVersionNumber> parseProductVersion(const QVariant& value);
int resolveStoredSettingsSchema(
    const std::optional<QVariant>& storedSchema,
    const std::optional<QVariant>& storedProductVersion);
bool categoryMigrationPending(const std::optional<QVariant>& storedMigration,
                              int previousSchema);
bool isExpectedCategoryMigrationResponse(int requestId, int expectedRequestId,
                                         const QVariant& userData);
bool isCategoryMigrationResponse(const QVariant& userData);
bool isNewInstance(bool firstStart, bool hasCreationProvenance,
                   bool hasStoredSchema, bool hasStoredProductVersion) noexcept;
void markNewInstance(QSettings& settings);

QString settingsIdentityPath(const QString& path);
QString settingsLockPath(const QString& path);
std::optional<QString> quarantineCorruptSettingsFileUnlocked(
    const QString& path, const QByteArray& expectedBytes);

int schemaFromSettings(const QSettings& settings);

using SettingsSnapshot = QMap<QString, QVariant>;

SettingsSnapshot captureSettingsSnapshot(const QSettings& settings);

struct DiskSettingsState
{
  QSettings::Status status{QSettings::NoError};
  int schema{BaselineSchema};
  QString productVersion;
  QByteArray bytes;
  SettingsSnapshot values;
};

QString inspectionTemporaryTemplate(const QString& settingsPath);
DiskSettingsState inspectSettingsFile(const QString& path);
QSettings::Status syncAndVerifySettingsUnderLock(QSettings& settings);
std::optional<SettingsSnapshot> captureInteractiveSettingsSnapshot(
    QSettings& settings, int lockTimeoutMs = SettingsLockTimeoutMs);
bool restoreSettingsSnapshot(QSettings& settings,
                             const SettingsSnapshot& original,
                             const SettingsSnapshot& rejected);
bool restoreInteractiveSettingsSnapshot(
    QSettings& settings, const SettingsSnapshot& original,
    int lockTimeoutMs = SettingsLockTimeoutMs);
bool verifyInteractiveSettingsState(
    QSettings& settings, int lockTimeoutMs = SettingsLockTimeoutMs);
bool finishSettingsRollback(QSettings& settings, SettingsSnapshot& original,
                            const SettingsSnapshot& rejected,
                            std::unique_ptr<QLockFile>& transactionLock);
QSettings::Status writeCompletedUpdatesUnderLock(
    QSettings& settings, const QVersionNumber& currentProductVersion,
    int previousSchema);
QSettings::Status commitCompletedUpdates(
    QSettings& settings, const QVersionNumber& currentProductVersion,
    int previousSchema);
bool requiresMigration(int previousSchema, int targetSchema) noexcept;
}  // namespace SettingsMigration

#endif  // SETTINGSMIGRATION_H
