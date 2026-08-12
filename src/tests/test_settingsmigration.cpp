#include "categorypersistence.h"
#include "categoryassignmentpolicy.h"
#include "categoryfileparser.h"
#include "settingsmigration.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSettings>
#include <QTemporaryDir>

#include <array>
#include <optional>

#ifdef Q_OS_UNIX
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
constexpr std::array InheritedMigrationSchemas{
    SettingsMigration::Schema2_1_6, SettingsMigration::Schema2_2_0,
    SettingsMigration::Schema2_2_1, SettingsMigration::Schema2_2_2,
    SettingsMigration::Schema2_3_0, SettingsMigration::Schema2_4_0,
    SettingsMigration::Schema2_5_0};

void expectNoInheritedMigrations(int schema)
{
  for (const int target : InheritedMigrationSchemas) {
    EXPECT_FALSE(SettingsMigration::requiresMigration(schema, target));
  }
}

int resolveSchema(const std::optional<int>& storedSchema,
                  const QVersionNumber& legacyProductVersion)
{
  const std::optional<QVariant> stored =
      storedSchema ? std::optional<QVariant>(*storedSchema) : std::nullopt;
  return SettingsMigration::resolveStoredSettingsSchema(
      stored, QVariant(legacyProductVersion.toString()));
}
}  // namespace

TEST(SettingsMigration, ImportingMo252StartsAtCurrentSchema)
{
  const auto schema = resolveSchema(std::nullopt, QVersionNumber(2, 5, 2));

  EXPECT_EQ(schema, SettingsMigration::CurrentSchema);
  expectNoInheritedMigrations(schema);
}

TEST(SettingsMigration, Fluorine033To034KeepsInheritedMigrationsComplete)
{
  // The first run imports an upstream MO2 instance and persists this schema
  // alongside product version 0.3.3.
  const int persistedSchema =
      resolveSchema(std::nullopt, QVersionNumber(2, 5, 2));

  // The 0.3.4 run resolves the explicit schema before considering the previous
  // 0.3.3 product version.
  const int schemaOnUpgrade =
      resolveSchema(persistedSchema, QVersionNumber(0, 3, 3));

  EXPECT_EQ(schemaOnUpgrade, SettingsMigration::CurrentSchema);
  expectNoInheritedMigrations(schemaOnUpgrade);
}

TEST(SettingsMigration, PreSchemaFluorineInstanceIsTreatedAsMigrated)
{
  const auto schema = resolveSchema(std::nullopt, QVersionNumber(0, 3, 3));

  EXPECT_EQ(schema, SettingsMigration::CurrentSchema);
  expectNoInheritedMigrations(schema);
}

TEST(SettingsMigration, CreationHelperWritesAuthoritativeNewInstanceProvenance)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString path = temporaryDirectory.filePath(QStringLiteral("instance.ini"));
  {
    QSettings settings(path, QSettings::IniFormat);
    SettingsMigration::markNewInstance(settings);
    settings.sync();
    ASSERT_EQ(settings.status(), QSettings::NoError);
  }

  QSettings stored(path, QSettings::IniFormat);
  EXPECT_TRUE(SettingsMigration::isNewInstance(
      stored.value(SettingsMigration::FirstStartKey).toBool(),
      stored.value(SettingsMigration::NewInstanceProvenanceKey).toInt() ==
          SettingsMigration::NewInstanceProvenance,
      stored.contains(SettingsMigration::SettingsSchemaKey),
      stored.contains(SettingsMigration::ProductVersionKey)));
}

TEST(SettingsMigration, SetupOnlyWritesAreSyncedAndVerifiedWithoutMarkers)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString path = temporaryDirectory.filePath(QStringLiteral("instance.ini"));
  QSettings settings(path, QSettings::IniFormat);
  settings.setValue(QStringLiteral("selected_profile"),
                    QStringLiteral("CLI Profile"));

  EXPECT_EQ(SettingsMigration::syncAndVerifySettingsUnderLock(settings),
            QSettings::NoError);

  const auto persisted = SettingsMigration::inspectSettingsFile(path);
  ASSERT_EQ(persisted.status, QSettings::NoError);
  EXPECT_EQ(persisted.values.value(QStringLiteral("selected_profile")),
            QStringLiteral("CLI Profile"));
  EXPECT_FALSE(persisted.values.contains(SettingsMigration::SettingsSchemaKey));
  EXPECT_FALSE(persisted.values.contains(SettingsMigration::ProductVersionKey));
}

TEST(SettingsMigration, ExistingIniAliasesShareOneTransactionLock)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString realPath =
      temporaryDirectory.filePath(QStringLiteral("ModOrganizer.ini"));
  {
    QSettings settings(realPath, QSettings::IniFormat);
    settings.setValue(QStringLiteral("version"), QStringLiteral("0.3.3"));
    settings.sync();
    ASSERT_EQ(settings.status(), QSettings::NoError);
  }
  const QString aliasPath =
      temporaryDirectory.filePath(QStringLiteral("instance-alias.ini"));
  ASSERT_TRUE(QFile::link(realPath, aliasPath));

  const QString realLock = SettingsMigration::settingsLockPath(realPath);
  const QString aliasLock = SettingsMigration::settingsLockPath(aliasPath);
  EXPECT_EQ(realLock, aliasLock);

  QLockFile first(realLock);
  QLockFile second(aliasLock);
  first.setStaleLockTime(0);
  second.setStaleLockTime(0);
  ASSERT_TRUE(first.tryLock());
  EXPECT_FALSE(second.tryLock());
}

TEST(SettingsMigration, DanglingFileAliasKeepsTheTargetLockIdentity)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString realPath =
      temporaryDirectory.filePath(QStringLiteral("ModOrganizer.ini"));
  QFile target(realPath);
  ASSERT_TRUE(target.open(QIODevice::WriteOnly));
  target.close();
  const QString aliasPath =
      temporaryDirectory.filePath(QStringLiteral("instance-alias.ini"));
  ASSERT_TRUE(QFile::link(realPath, aliasPath));
  const QString expectedLock = SettingsMigration::settingsLockPath(realPath);

  const QString movedPath = realPath + QStringLiteral(".moved");
  ASSERT_TRUE(QFile::rename(realPath, movedPath));
  EXPECT_EQ(SettingsMigration::settingsLockPath(aliasPath), expectedLock);
  ASSERT_TRUE(QFile::rename(movedPath, realPath));
}

TEST(SettingsMigration, ChainedDanglingAliasesShareTheFinalTargetIdentity)
{
#ifdef Q_OS_UNIX
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString finalPath =
      temporaryDirectory.filePath(QStringLiteral("final.ini"));
  const QString middlePath =
      temporaryDirectory.filePath(QStringLiteral("middle.ini"));
  const QString firstPath =
      temporaryDirectory.filePath(QStringLiteral("first.ini"));
  const QByteArray finalBytes  = QFile::encodeName(finalPath);
  const QByteArray middleBytes = QFile::encodeName(middlePath);
  const QByteArray firstBytes  = QFile::encodeName(firstPath);
  ASSERT_EQ(::symlink(finalBytes.constData(), middleBytes.constData()), 0);
  ASSERT_EQ(::symlink(middleBytes.constData(), firstBytes.constData()), 0);

  const QString expectedLock = SettingsMigration::settingsLockPath(finalPath);
  EXPECT_EQ(SettingsMigration::settingsLockPath(middlePath), expectedLock);
  EXPECT_EQ(SettingsMigration::settingsLockPath(firstPath), expectedLock);

  QLockFile finalLock(expectedLock);
  QLockFile aliasLock(SettingsMigration::settingsLockPath(firstPath));
  finalLock.setStaleLockTime(0);
  aliasLock.setStaleLockTime(0);
  ASSERT_TRUE(finalLock.tryLock());
  EXPECT_FALSE(aliasLock.tryLock());
#else
  GTEST_SKIP() << "requires Unix symbolic links";
#endif
}

TEST(SettingsMigration, DeepAndCyclicAliasesHaveStableTransactionIdentity)
{
#ifdef Q_OS_UNIX
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());

  const QString finalPath =
      temporaryDirectory.filePath(QStringLiteral("chain-final.ini"));
  QString target = finalPath;
  QString firstLink;
  for (int i = 79; i >= 0; --i) {
    const QString link = temporaryDirectory.filePath(
        QStringLiteral("chain-%1.ini").arg(i, 2, 10, QLatin1Char('0')));
    const QByteArray targetBytes = QFile::encodeName(target);
    const QByteArray linkBytes   = QFile::encodeName(link);
    ASSERT_EQ(::symlink(targetBytes.constData(), linkBytes.constData()), 0);
    target    = link;
    firstLink = link;
  }
  EXPECT_EQ(SettingsMigration::settingsLockPath(firstLink),
            SettingsMigration::settingsLockPath(finalPath));

  QString deepDirectory = temporaryDirectory.path();
  for (int i = 0; i < 80; ++i) {
    deepDirectory = QDir(deepDirectory).filePath(QStringLiteral("d"));
  }
  ASSERT_TRUE(QDir().mkpath(deepDirectory));
  const QString directoryAlias =
      temporaryDirectory.filePath(QStringLiteral("deep-alias"));
  const QByteArray deepBytes  = QFile::encodeName(deepDirectory);
  const QByteArray aliasBytes = QFile::encodeName(directoryAlias);
  ASSERT_EQ(::symlink(deepBytes.constData(), aliasBytes.constData()), 0);
  EXPECT_EQ(SettingsMigration::settingsLockPath(
                QDir(deepDirectory).filePath(QStringLiteral("instance.ini"))),
            SettingsMigration::settingsLockPath(
                QDir(directoryAlias).filePath(QStringLiteral("instance.ini"))));

  QStringList cyclePaths;
  for (int i = 0; i < 70; ++i) {
    cyclePaths.push_back(temporaryDirectory.filePath(
        QStringLiteral("cycle-%1.ini").arg(i, 2, 10, QLatin1Char('0'))));
  }
  for (int i = 0; i < cyclePaths.size(); ++i) {
    const QByteArray nextBytes =
        QFile::encodeName(cyclePaths[(i + 1) % cyclePaths.size()]);
    const QByteArray pathBytes = QFile::encodeName(cyclePaths[i]);
    ASSERT_EQ(::symlink(nextBytes.constData(), pathBytes.constData()), 0);
  }
  EXPECT_EQ(SettingsMigration::settingsLockPath(cyclePaths[0]),
            SettingsMigration::settingsLockPath(cyclePaths[1]));
#else
  GTEST_SKIP() << "requires Unix symbolic links";
#endif
}

TEST(SettingsMigration, NewIniDirectoryAliasesShareOneTransactionLock)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString realDirectory =
      temporaryDirectory.filePath(QStringLiteral("instance"));
  ASSERT_TRUE(QDir().mkpath(realDirectory));
  const QString aliasDirectory =
      temporaryDirectory.filePath(QStringLiteral("instance-alias"));
#ifdef Q_OS_UNIX
  const QByteArray realDirectoryBytes  = QFile::encodeName(realDirectory);
  const QByteArray aliasDirectoryBytes = QFile::encodeName(aliasDirectory);
  ASSERT_EQ(::symlink(realDirectoryBytes.constData(),
                      aliasDirectoryBytes.constData()),
            0);
#else
  ASSERT_TRUE(QFile::link(realDirectory, aliasDirectory));
#endif

  const QString realPath =
      QDir(realDirectory).filePath(QStringLiteral("ModOrganizer.ini"));
  const QString aliasPath =
      QDir(aliasDirectory).filePath(QStringLiteral("ModOrganizer.ini"));
  ASSERT_FALSE(QFileInfo::exists(realPath));
  EXPECT_EQ(SettingsMigration::settingsLockPath(realPath),
            SettingsMigration::settingsLockPath(aliasPath));

  const QString movedDirectory =
      temporaryDirectory.filePath(QStringLiteral("instance-moved"));
  ASSERT_TRUE(QDir().rename(realDirectory, movedDirectory));
  EXPECT_EQ(SettingsMigration::settingsLockPath(realPath),
            SettingsMigration::settingsLockPath(aliasPath));
}

TEST(SettingsMigration, OldUpstreamVersionStillRunsRequiredMigrations)
{
  const auto schema = resolveSchema(std::nullopt, QVersionNumber(2, 1, 2));

  EXPECT_EQ(schema, SettingsMigration::BaselineSchema);
  EXPECT_TRUE(SettingsMigration::requiresMigration(
      schema, SettingsMigration::Schema2_1_6));
  EXPECT_TRUE(SettingsMigration::requiresMigration(
      schema, SettingsMigration::Schema2_4_0));
}

TEST(SettingsMigration, ExplicitFutureSchemaIsNeverDowngradedByInference)
{
  const int futureSchema = SettingsMigration::CurrentSchema + 1;
  EXPECT_EQ(resolveSchema(futureSchema, QVersionNumber(0, 3, 3)),
            futureSchema);
}

TEST(SettingsMigration, StoredIniValuesAreStrictlyValidated)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString path = temporaryDirectory.filePath(QStringLiteral("instance.ini"));

  const auto resolve = [&](const std::optional<QVariant>& value) {
    QSettings settings(path, QSettings::IniFormat);
    settings.clear();
    if (value) {
      settings.setValue(SettingsMigration::SettingsSchemaKey, *value);
    }
    settings.sync();

    QSettings reopened(path, QSettings::IniFormat);
    const std::optional<QVariant> stored =
        reopened.contains(SettingsMigration::SettingsSchemaKey)
            ? std::optional<QVariant>(
                  reopened.value(SettingsMigration::SettingsSchemaKey))
            : std::nullopt;
    return SettingsMigration::resolveStoredSchemaValue(
        stored, QVersionNumber(0, 3, 3));
  };

  EXPECT_EQ(resolve(std::nullopt), SettingsMigration::CurrentSchema);
  EXPECT_EQ(resolve(QVariant(SettingsMigration::CurrentSchema)),
            SettingsMigration::CurrentSchema);
  EXPECT_EQ(resolve(QVariant(SettingsMigration::CurrentSchema + 1)),
            SettingsMigration::CurrentSchema + 1);
  EXPECT_EQ(resolve(QVariant(QStringLiteral("corrupt"))),
            SettingsMigration::UnknownSchema);
  EXPECT_EQ(resolve(QVariant(QStringLiteral("2147483648"))),
            SettingsMigration::UnknownSchema);
  EXPECT_EQ(resolve(QVariant(QStringLiteral("-1"))),
            SettingsMigration::UnknownSchema);
}

TEST(SettingsMigration, LegacyProductIniValuesFailClosedWhenPresentInvalid)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString path = temporaryDirectory.filePath(QStringLiteral("instance.ini"));

  const auto resolve = [&](const std::optional<QVariant>& productVersion) {
    QSettings settings(path, QSettings::IniFormat);
    settings.clear();
    if (productVersion) {
      settings.setValue(SettingsMigration::ProductVersionKey, *productVersion);
    }
    settings.sync();

    QSettings reopened(path, QSettings::IniFormat);
    const std::optional<QVariant> storedProduct =
        reopened.contains(SettingsMigration::ProductVersionKey)
            ? std::optional<QVariant>(
                  reopened.value(SettingsMigration::ProductVersionKey))
            : std::nullopt;
    return SettingsMigration::resolveStoredSettingsSchema(std::nullopt,
                                                          storedProduct);
  };

  EXPECT_EQ(resolve(std::nullopt), SettingsMigration::BaselineSchema);
  EXPECT_EQ(resolve(QVariant(QStringLiteral("2.5.2"))),
            SettingsMigration::CurrentSchema);
  EXPECT_EQ(resolve(QVariant(QStringLiteral("0.3.3"))),
            SettingsMigration::CurrentSchema);
  EXPECT_EQ(resolve(QVariant(QStringLiteral("garbage"))),
            SettingsMigration::UnknownSchema);
  EXPECT_EQ(resolve(QVariant(QStringLiteral("2147483648"))),
            SettingsMigration::UnknownSchema);
  EXPECT_EQ(resolve(QVariant(QStringLiteral("0"))),
            SettingsMigration::UnknownSchema);
  EXPECT_EQ(resolve(QVariant(QStringLiteral("2.5.2 trailing"))),
            SettingsMigration::UnknownSchema);
}

TEST(SettingsMigration, CompletionMarkerIsCommittedWithMigratedState)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString path = temporaryDirectory.filePath(QStringLiteral("instance.ini"));

  {
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(SettingsMigration::ProductVersionKey,
                      QStringLiteral("2.1.2"));
    settings.sync();
  }

  // Simulate termination after migrated state was staged but before the
  // completion boundary. The absent marker makes the next run retry.
  {
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(QStringLiteral("Widgets/migrated_ui_state"), true);
    settings.sync();
  }
  {
    QSettings settings(path, QSettings::IniFormat);
    EXPECT_FALSE(
        settings.contains(SettingsMigration::SettingsSchemaKey));
    EXPECT_EQ(SettingsMigration::resolveStoredSchemaValue(
                  std::nullopt, QVersionNumber::fromString(
                                    settings.value(SettingsMigration::ProductVersionKey)
                                        .toString())),
              SettingsMigration::BaselineSchema);
  }

  {
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(QStringLiteral("Widgets/migrated_ui_state"), true);
    EXPECT_EQ(SettingsMigration::commitCompletedUpdates(
                  settings, QVersionNumber(0, 3, 3),
                  SettingsMigration::BaselineSchema),
              QSettings::NoError);
  }
  {
    QSettings settings(path, QSettings::IniFormat);
    EXPECT_TRUE(
        settings.value(QStringLiteral("Widgets/migrated_ui_state")).toBool());
    EXPECT_EQ(settings.value(SettingsMigration::SettingsSchemaKey).toInt(),
              SettingsMigration::CurrentSchema);
    EXPECT_EQ(settings.value(SettingsMigration::ProductVersionKey).toString(),
              QStringLiteral("0.3.3"));
  }
}

TEST(SettingsMigration, ProductionGeneralFixturesCommitCanonicalMarkers)
{
  const std::array<std::optional<QString>, 4> previousVersions{
      QStringLiteral("2.1.2"), QStringLiteral("2.5.2"),
      QStringLiteral("0.3.3"), std::nullopt};

  for (const auto& previousVersion : previousVersions) {
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString path =
        temporaryDirectory.filePath(QStringLiteral("instance.ini"));
    int previousSchema = SettingsMigration::BaselineSchema;
    {
      QSettings settings(path, QSettings::IniFormat);
      if (previousVersion) {
        settings.setValue(SettingsMigration::ProductVersionKey,
                          *previousVersion);
      }
      settings.sync();
      previousSchema = SettingsMigration::schemaFromSettings(settings);
      ASSERT_EQ(SettingsMigration::commitCompletedUpdates(
                    settings, QVersionNumber(0, 3, 3), previousSchema),
                QSettings::NoError);
    }

    QSettings reopened(path, QSettings::IniFormat);
    EXPECT_EQ(reopened.value(SettingsMigration::SettingsSchemaKey).toInt(),
              SettingsMigration::CurrentSchema);
    EXPECT_EQ(reopened.value(SettingsMigration::ProductVersionKey).toString(),
              QStringLiteral("0.3.3"));
    EXPECT_FALSE(
        reopened.contains(QStringLiteral("General/settings_schema_version")));
    EXPECT_FALSE(reopened.contains(QStringLiteral("General/version")));
    EXPECT_EQ(SettingsMigration::schemaFromSettings(reopened),
              SettingsMigration::CurrentSchema);
  }
}

TEST(SettingsMigration, ReadsRealMo2GeneralSectionAndSkipsCategoryPrompt)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString path = temporaryDirectory.filePath(QStringLiteral("instance.ini"));
  {
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    const QByteArray fixture("[General]\n"
                             "first_start=false\n"
                             "version=2.5.2\n"
                             "\n[Settings]\n"
                             "category_mappings=false\n");
    ASSERT_EQ(file.write(fixture), fixture.size());
  }

  QSettings settings(path, QSettings::IniFormat);
  EXPECT_EQ(settings.value(SettingsMigration::ProductVersionKey).toString(),
            QStringLiteral("2.5.2"));
  EXPECT_FALSE(settings.contains(QStringLiteral("General/version")));
  const int schema = SettingsMigration::schemaFromSettings(settings);
  EXPECT_EQ(schema, SettingsMigration::CurrentSchema);
  EXPECT_FALSE(SettingsMigration::categoryMigrationPending(std::nullopt,
                                                            schema));
}

TEST(SettingsMigration, RejectedStagingIsRestoredBeforeBackendDestruction)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString path = temporaryDirectory.filePath(QStringLiteral("instance.ini"));
  {
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(SettingsMigration::ProductVersionKey,
                      QStringLiteral("2.1.2"));
    settings.setValue(QStringLiteral("Settings/steam_password"),
                      QStringLiteral("legacy-secret"));
    settings.sync();

    const auto snapshot =
        SettingsMigration::captureSettingsSnapshot(settings);
    settings.remove(QStringLiteral("Settings/steam_password"));
    EXPECT_EQ(SettingsMigration::writeCompletedUpdatesUnderLock(
                  settings, QVersionNumber(0, 3, 3),
                  SettingsMigration::UnknownSchema),
              QSettings::FormatError);
    const auto rejected =
        SettingsMigration::captureSettingsSnapshot(settings);
    EXPECT_TRUE(SettingsMigration::restoreSettingsSnapshot(
        settings, snapshot, rejected));
  }

  QSettings reopened(path, QSettings::IniFormat);
  EXPECT_EQ(reopened.value(QStringLiteral("Settings/steam_password")).toString(),
            QStringLiteral("legacy-secret"));
  EXPECT_EQ(reopened.value(SettingsMigration::ProductVersionKey).toString(),
            QStringLiteral("2.1.2"));
  EXPECT_FALSE(reopened.contains(SettingsMigration::SettingsSchemaKey));
}

TEST(SettingsMigration, AccessErrorRollbackSurvivesBackendDestruction)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  QDir root(temporaryDirectory.path());
  ASSERT_TRUE(root.mkdir(QStringLiteral("settings")));
  const QString path = root.filePath(QStringLiteral("settings/instance.ini"));

  {
    QSettings initial(path, QSettings::IniFormat);
    initial.setValue(SettingsMigration::ProductVersionKey,
                     QStringLiteral("2.1.2"));
    initial.setValue(QStringLiteral("Settings/steam_password"),
                     QStringLiteral("legacy-secret"));
    initial.sync();
  }

  {
    QSettings settings(path, QSettings::IniFormat);
    const auto snapshot =
        SettingsMigration::captureSettingsSnapshot(settings);
    settings.remove(QStringLiteral("Settings/steam_password"));

    ASSERT_TRUE(root.rename(QStringLiteral("settings"),
                            QStringLiteral("settings-away")));
    QFile blocker(root.filePath(QStringLiteral("settings")));
    ASSERT_TRUE(blocker.open(QIODevice::WriteOnly));
    EXPECT_EQ(SettingsMigration::writeCompletedUpdatesUnderLock(
                  settings, QVersionNumber(0, 3, 3),
                  SettingsMigration::BaselineSchema),
              QSettings::AccessError);
    const auto rejected =
        SettingsMigration::captureSettingsSnapshot(settings);

    blocker.close();
    ASSERT_TRUE(QFile::remove(blocker.fileName()));
    ASSERT_TRUE(root.rename(QStringLiteral("settings-away"),
                            QStringLiteral("settings")));
    EXPECT_TRUE(SettingsMigration::restoreSettingsSnapshot(
        settings, snapshot, rejected));
  }

  QSettings reopened(path, QSettings::IniFormat);
  EXPECT_EQ(reopened.value(QStringLiteral("Settings/steam_password")).toString(),
            QStringLiteral("legacy-secret"));
  EXPECT_EQ(reopened.value(SettingsMigration::ProductVersionKey).toString(),
            QStringLiteral("2.1.2"));
  EXPECT_FALSE(reopened.contains(SettingsMigration::SettingsSchemaKey));
}

TEST(SettingsMigration, CompletedRollbackDoesNotEraseLaterUserSettings)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString path = temporaryDirectory.filePath(QStringLiteral("instance.ini"));

  {
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(SettingsMigration::ProductVersionKey,
                      QStringLiteral("garbage"));
    settings.setValue(QStringLiteral("Settings/legacy_value"),
                      QStringLiteral("preserve-me"));
    settings.sync();

    auto transactionLock = std::make_unique<QLockFile>(
        SettingsMigration::settingsLockPath(path));
    transactionLock->setStaleLockTime(
        SettingsMigration::SettingsLockStaleMs);
    ASSERT_TRUE(transactionLock->tryLock());
    auto snapshot = SettingsMigration::captureSettingsSnapshot(settings);

    settings.remove(QStringLiteral("Settings/legacy_value"));
    EXPECT_EQ(SettingsMigration::writeCompletedUpdatesUnderLock(
                  settings, QVersionNumber(0, 3, 3),
                  SettingsMigration::UnknownSchema),
              QSettings::FormatError);
    const auto rejected =
        SettingsMigration::captureSettingsSnapshot(settings);
    ASSERT_TRUE(SettingsMigration::finishSettingsRollback(
        settings, snapshot, rejected, transactionLock));
    EXPECT_FALSE(transactionLock);
    EXPECT_TRUE(snapshot.isEmpty());

    settings.setValue(QStringLiteral("Geometry/after_failed_migration"),
                      QStringLiteral("new-value"));
    settings.sync();
  }

  QSettings reopened(path, QSettings::IniFormat);
  EXPECT_EQ(reopened.value(QStringLiteral("Settings/legacy_value")).toString(),
            QStringLiteral("preserve-me"));
  EXPECT_EQ(
      reopened.value(QStringLiteral("Geometry/after_failed_migration")).toString(),
      QStringLiteral("new-value"));
  EXPECT_FALSE(reopened.contains(SettingsMigration::SettingsSchemaKey));
}

TEST(SettingsMigration, RollbackPreservesAnotherProcessSettingsChanges)
{
#ifdef Q_OS_UNIX
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString path = temporaryDirectory.filePath(QStringLiteral("instance.ini"));

  QSettings settings(path, QSettings::IniFormat);
  settings.setValue(SettingsMigration::ProductVersionKey,
                    QStringLiteral("2.1.2"));
  settings.setValue(QStringLiteral("Settings/original"),
                    QStringLiteral("keep"));
  settings.sync();

  auto transactionLock = std::make_unique<QLockFile>(
      SettingsMigration::settingsLockPath(path));
  transactionLock->setStaleLockTime(SettingsMigration::SettingsLockStaleMs);
  ASSERT_TRUE(transactionLock->tryLock());
  auto original = SettingsMigration::captureSettingsSnapshot(settings);

  settings.setValue(QStringLiteral("Settings/original"),
                    QStringLiteral("migration-value"));
  settings.setValue(QStringLiteral("Settings/migration_only"), true);
  const auto rejected =
      SettingsMigration::captureSettingsSnapshot(settings);

  const pid_t child = ::fork();
  ASSERT_NE(child, -1);
  if (child == 0) {
    QSettings writer(path, QSettings::IniFormat);
    writer.setValue(QStringLiteral("Settings/original"),
                    QStringLiteral("concurrent-change"));
    writer.setValue(QStringLiteral("Settings/concurrent_only"),
                    QStringLiteral("preserve-me"));
    writer.sync();
    ::_exit(writer.status() == QSettings::NoError ? 0 : 1);
  }

  int status = 0;
  ASSERT_EQ(::waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(WEXITSTATUS(status), 0);

  const bool rolledBack = SettingsMigration::finishSettingsRollback(
      settings, original, rejected, transactionLock);

  QSettings reopened(path, QSettings::IniFormat);
  EXPECT_EQ(reopened.value(QStringLiteral("Settings/original")).toString(),
            QStringLiteral("concurrent-change"));
  EXPECT_EQ(reopened.value(QStringLiteral("Settings/concurrent_only")).toString(),
            QStringLiteral("preserve-me"));
  EXPECT_FALSE(reopened.contains(QStringLiteral("Settings/migration_only")));
  EXPECT_TRUE(rolledBack);
  EXPECT_FALSE(transactionLock);
#else
  GTEST_SKIP() << "requires Unix process concurrency";
#endif
}

TEST(SettingsMigration, InteractiveEditSnapshotAndRollbackRespectCanonicalLock)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString path = temporaryDirectory.filePath(QStringLiteral("instance.ini"));

  QSettings settings(path, QSettings::IniFormat);
  settings.setValue(QStringLiteral("Settings/value"), QStringLiteral("original"));
  settings.sync();
  ASSERT_EQ(settings.status(), QSettings::NoError);

  const auto original =
      SettingsMigration::captureInteractiveSettingsSnapshot(settings, 0);
  ASSERT_TRUE(original);
  settings.setValue(QStringLiteral("Settings/value"), QStringLiteral("rejected"));

  QLockFile competing(SettingsMigration::settingsLockPath(path));
  competing.setStaleLockTime(SettingsMigration::SettingsLockStaleMs);
  ASSERT_TRUE(competing.tryLock());
  EXPECT_FALSE(SettingsMigration::captureInteractiveSettingsSnapshot(settings, 0));
  EXPECT_FALSE(SettingsMigration::verifyInteractiveSettingsState(settings, 0));
  EXPECT_FALSE(
      SettingsMigration::restoreInteractiveSettingsSnapshot(settings, *original, 0));
  EXPECT_EQ(settings.value(QStringLiteral("Settings/value")).toString(),
            QStringLiteral("rejected"));

  competing.unlock();
  EXPECT_TRUE(
      SettingsMigration::restoreInteractiveSettingsSnapshot(settings, *original, 0));
  EXPECT_TRUE(SettingsMigration::verifyInteractiveSettingsState(settings, 0));
  EXPECT_EQ(settings.value(QStringLiteral("Settings/value")).toString(),
            QStringLiteral("original"));

  const auto persisted = SettingsMigration::inspectSettingsFile(path);
  ASSERT_EQ(persisted.status, QSettings::NoError);
  EXPECT_EQ(persisted.values.value(QStringLiteral("Settings/value")).toString(),
            QStringLiteral("original"));
}

TEST(SettingsMigration, LiveMigrationLockDoesNotExpireByAge)
{
  EXPECT_EQ(SettingsMigration::SettingsLockStaleMs, 0);

  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString path = temporaryDirectory.filePath(QStringLiteral("instance.ini"));
  const QString lockPath = SettingsMigration::settingsLockPath(path);

  QLockFile owner(lockPath);
  owner.setStaleLockTime(SettingsMigration::SettingsLockStaleMs);
  ASSERT_TRUE(owner.tryLock());

  QFile lockFile(lockPath);
  ASSERT_TRUE(lockFile.open(QIODevice::ReadWrite));
  ASSERT_TRUE(lockFile.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-60),
                                   QFileDevice::FileModificationTime));
  lockFile.close();

  QLockFile contender(lockPath);
  contender.setStaleLockTime(SettingsMigration::SettingsLockStaleMs);
  EXPECT_FALSE(contender.tryLock(0));
  owner.unlock();
}

TEST(SettingsMigration, InspectionCopyUsesTheInstanceDirectory)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString path = temporaryDirectory.filePath(QStringLiteral("instance.ini"));
  {
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(SettingsMigration::ProductVersionKey,
                      QStringLiteral("2.5.2"));
    settings.sync();
  }

  const bool hadTmpDir = qEnvironmentVariableIsSet("TMPDIR");
  const QByteArray oldTmpDir = qgetenv("TMPDIR");
  qputenv("TMPDIR", temporaryDirectory.filePath(
                         QStringLiteral("unavailable-system-temp"))
                         .toUtf8());

  const auto temporaryTemplate =
      SettingsMigration::inspectionTemporaryTemplate(path);
  const auto inspection = SettingsMigration::inspectSettingsFile(path);

  if (hadTmpDir) {
    qputenv("TMPDIR", oldTmpDir);
  } else {
    qunsetenv("TMPDIR");
  }

  EXPECT_EQ(QFileInfo(temporaryTemplate).absolutePath(),
            QFileInfo(path).absolutePath());
  EXPECT_EQ(inspection.status, QSettings::NoError);
  EXPECT_EQ(inspection.schema, SettingsMigration::CurrentSchema);
}

TEST(SettingsMigration, UnknownSchemaCannotBeAcknowledgedByProductVersion)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString path = temporaryDirectory.filePath(QStringLiteral("instance.ini"));

  {
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(SettingsMigration::ProductVersionKey,
                      QStringLiteral("garbage"));
    settings.setValue(QStringLiteral("Settings/steam_password"),
                      QStringLiteral("legacy-value"));
    settings.sync();
    EXPECT_EQ(SettingsMigration::commitCompletedUpdates(
                  settings, QVersionNumber(0, 3, 4),
                  SettingsMigration::UnknownSchema),
              QSettings::FormatError);
  }

  QSettings reopened(path, QSettings::IniFormat);
  EXPECT_EQ(reopened.value(SettingsMigration::ProductVersionKey).toString(),
            QStringLiteral("garbage"));
  EXPECT_FALSE(reopened.contains(SettingsMigration::SettingsSchemaKey));
  EXPECT_EQ(SettingsMigration::schemaFromSettings(reopened),
            SettingsMigration::UnknownSchema);
  EXPECT_EQ(reopened.value(QStringLiteral("Settings/steam_password")).toString(),
            QStringLiteral("legacy-value"));
}

TEST(SettingsMigration, FutureSchemaIsNotOverwritten)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString path = temporaryDirectory.filePath(QStringLiteral("instance.ini"));
  const int futureSchema = SettingsMigration::CurrentSchema + 1;

  {
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(SettingsMigration::SettingsSchemaKey, futureSchema);
    settings.sync();
    EXPECT_EQ(SettingsMigration::commitCompletedUpdates(
                  settings, QVersionNumber(0, 3, 4), futureSchema),
              QSettings::NoError);
  }

  {
    QSettings reopened(path, QSettings::IniFormat);
    EXPECT_EQ(reopened.value(SettingsMigration::SettingsSchemaKey).toInt(),
              futureSchema);
    EXPECT_EQ(reopened.value(SettingsMigration::ProductVersionKey).toString(),
              QStringLiteral("0.3.4"));
  }
}

TEST(SettingsMigration, CategoryImportFailureSurvivesSchemaCompletion)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString path = temporaryDirectory.filePath(QStringLiteral("instance.ini"));

  {
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(SettingsMigration::ProductVersionKey,
                      QStringLiteral("2.4.0"));
    settings.setValue(QStringLiteral("category_migration_version"), 0);
    settings.sync();
    EXPECT_EQ(SettingsMigration::commitCompletedUpdates(
                  settings, QVersionNumber(0, 3, 3),
                  SettingsMigration::Schema2_4_0),
              QSettings::NoError);
  }

  // A failed or interrupted asynchronous import leaves its independent marker
  // pending even though the general schema transition completed.
  {
    QSettings settings(path, QSettings::IniFormat);
    EXPECT_EQ(settings.value(SettingsMigration::SettingsSchemaKey).toInt(),
              SettingsMigration::CurrentSchema);
    EXPECT_TRUE(SettingsMigration::categoryMigrationPending(
        settings.value(QStringLiteral("category_migration_version")),
        SettingsMigration::CurrentSchema));

    settings.setValue(QStringLiteral("category_migration_version"),
                      SettingsMigration::CurrentCategoryMigration);
    settings.sync();
  }

  {
    QSettings settings(path, QSettings::IniFormat);
    EXPECT_FALSE(SettingsMigration::categoryMigrationPending(
        settings.value(QStringLiteral("category_migration_version")),
        SettingsMigration::CurrentSchema));
  }
}

TEST(SettingsMigration, CategoryReminderLegacyFallbackAndInvalidMarkersAreSafe)
{
  EXPECT_TRUE(SettingsMigration::categoryMigrationPending(
      std::nullopt, SettingsMigration::Schema2_4_0));
  EXPECT_FALSE(SettingsMigration::categoryMigrationPending(
      std::nullopt, SettingsMigration::Schema2_5_0));
  EXPECT_TRUE(SettingsMigration::categoryMigrationPending(
      QVariant(QStringLiteral("corrupt")), SettingsMigration::CurrentSchema));
  EXPECT_TRUE(SettingsMigration::categoryMigrationPending(
      QVariant(-1), SettingsMigration::CurrentSchema));
  EXPECT_FALSE(SettingsMigration::categoryMigrationPending(
      QVariant(SettingsMigration::CurrentCategoryMigration),
      SettingsMigration::BaselineSchema));
  EXPECT_FALSE(SettingsMigration::categoryMigrationPending(
      QVariant(SettingsMigration::CurrentCategoryMigration + 1),
      SettingsMigration::BaselineSchema));
}

TEST(SettingsMigration, CategoryResponsesMustMatchTheActiveTaggedRequest)
{
  const QVariant tag(SettingsMigration::CategoryMigrationRequestTag);
  EXPECT_TRUE(
      SettingsMigration::isExpectedCategoryMigrationResponse(42, 42, tag));
  EXPECT_FALSE(
      SettingsMigration::isExpectedCategoryMigrationResponse(41, 42, tag));
  EXPECT_FALSE(SettingsMigration::isExpectedCategoryMigrationResponse(
      42, 42, QVariant(QStringLiteral("another-request"))));
  EXPECT_FALSE(
      SettingsMigration::isExpectedCategoryMigrationResponse(-1, -1, tag));
  EXPECT_TRUE(SettingsMigration::isCategoryMigrationResponse(tag));
  EXPECT_FALSE(SettingsMigration::isCategoryMigrationResponse(
      QVariant(QStringLiteral("another-request"))));
}

TEST(SettingsMigration, NewInstanceRequiresBlankMigrationProvenance)
{
  EXPECT_TRUE(SettingsMigration::isNewInstance(true, true, false, false));
  EXPECT_FALSE(SettingsMigration::isNewInstance(true, false, false, false));
  EXPECT_FALSE(SettingsMigration::isNewInstance(true, true, false, true));
  EXPECT_FALSE(SettingsMigration::isNewInstance(true, true, true, false));
  EXPECT_FALSE(SettingsMigration::isNewInstance(false, true, false, false));
}

TEST(SettingsMigration, LegacyIniWithoutFirstStartStillRequiresMigrations)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString path = temporaryDirectory.filePath(QStringLiteral("instance.ini"));
  {
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(SettingsMigration::ProductVersionKey,
                      QStringLiteral("2.1.2"));
    settings.setValue(QStringLiteral("Settings/steam_password"),
                      QStringLiteral("legacy-value"));
    settings.sync();
  }

  QSettings settings(path, QSettings::IniFormat);
  const bool firstStart =
      settings.value(SettingsMigration::FirstStartKey, true).toBool();
  EXPECT_FALSE(SettingsMigration::isNewInstance(
      firstStart,
      settings.value(SettingsMigration::NewInstanceProvenanceKey).toInt() ==
          SettingsMigration::NewInstanceProvenance,
      settings.contains(SettingsMigration::SettingsSchemaKey),
      settings.contains(SettingsMigration::ProductVersionKey)));
  const int schema = SettingsMigration::schemaFromSettings(settings);
  EXPECT_EQ(schema, SettingsMigration::BaselineSchema);
  EXPECT_TRUE(SettingsMigration::requiresMigration(
      schema, SettingsMigration::Schema2_2_0));
}

TEST(SettingsMigration, VersionlessNonblankIniIsNeverInferredAsNew)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString path = temporaryDirectory.filePath(QStringLiteral("instance.ini"));
  QSettings settings(path, QSettings::IniFormat);
  settings.setValue(QStringLiteral("Settings/steam_password"),
                    QStringLiteral("legacy-secret"));
  settings.sync();

  EXPECT_FALSE(SettingsMigration::isNewInstance(
      true,
      settings.value(SettingsMigration::NewInstanceProvenanceKey)
              .toInt() == SettingsMigration::NewInstanceProvenance,
      settings.contains(SettingsMigration::SettingsSchemaKey),
      settings.contains(SettingsMigration::ProductVersionKey)));
  const int schema = SettingsMigration::resolveStoredSettingsSchema(
      std::nullopt, std::nullopt);
  EXPECT_EQ(schema, SettingsMigration::BaselineSchema);
  EXPECT_TRUE(SettingsMigration::requiresMigration(
      schema, SettingsMigration::Schema2_2_0));
}

TEST(SettingsMigration, FailedCompletionDoesNotCreateSchemaMarker)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString blocker =
      temporaryDirectory.filePath(QStringLiteral("blocked-parent"));
  QFile blockerFile(blocker);
  ASSERT_TRUE(blockerFile.open(QIODevice::WriteOnly));
  blockerFile.close();
  const QString path = blocker + QStringLiteral("/instance.ini");

  QSettings settings(path, QSettings::IniFormat);
  EXPECT_EQ(SettingsMigration::commitCompletedUpdates(
                settings, QVersionNumber(0, 3, 3),
                SettingsMigration::BaselineSchema),
            QSettings::AccessError);
  EXPECT_FALSE(QFileInfo::exists(path));

  ASSERT_TRUE(QFile::remove(blocker));
  ASSERT_TRUE(QDir().mkpath(blocker));
  EXPECT_EQ(SettingsMigration::commitCompletedUpdates(
                settings, QVersionNumber(0, 3, 3),
                SettingsMigration::BaselineSchema),
            QSettings::NoError);
  QSettings reopened(path, QSettings::IniFormat);
  EXPECT_EQ(reopened.value(SettingsMigration::SettingsSchemaKey).toInt(),
            SettingsMigration::CurrentSchema);
}

TEST(SettingsMigration, MidRunMalformedIniIsQuarantinedAndNotAcknowledged)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString path = temporaryDirectory.filePath(QStringLiteral("instance.ini"));
  {
    QSettings initial(path, QSettings::IniFormat);
    initial.setValue(SettingsMigration::ProductVersionKey,
                     QStringLiteral("2.5.2"));
    initial.sync();
  }

  const QByteArray malformed =
      QByteArrayLiteral("[General\nversion=DIFFERENT-CORRUPT\nunknown=keep-me\n");
  QFile file(path);
  {
    QSettings settings(path, QSettings::IniFormat);
    ASSERT_EQ(settings.value(SettingsMigration::ProductVersionKey).toString(),
              QStringLiteral("2.5.2"));
    settings.setValue(QStringLiteral("Widgets/migrated_ui_state"), true);

    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(file.write(malformed), malformed.size());
    file.close();

    EXPECT_EQ(SettingsMigration::commitCompletedUpdates(
                  settings, QVersionNumber(0, 3, 3),
                  SettingsMigration::CurrentSchema),
              QSettings::FormatError);
  }

  const QStringList backups = QDir(temporaryDirectory.path())
                                  .entryList(
                                      {QStringLiteral("instance.ini.corrupt*")},
                                      QDir::Files);
  ASSERT_EQ(backups.size(), 1);
  QFile backup(temporaryDirectory.filePath(backups.front()));
  ASSERT_TRUE(backup.open(QIODevice::ReadOnly));
  EXPECT_EQ(backup.readAll(), malformed);

  QSettings reopened(path, QSettings::IniFormat);
  EXPECT_FALSE(reopened.contains(SettingsMigration::SettingsSchemaKey));
  EXPECT_NE(reopened.value(SettingsMigration::ProductVersionKey).toString(),
            QStringLiteral("0.3.3"));
}

TEST(SettingsMigration, QuarantineAtomicallyRenamesTheInspectedFile)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString path = temporaryDirectory.filePath(QStringLiteral("instance.ini"));
  const QByteArray malformed = QByteArrayLiteral("[General\nkeep=this-copy\n");
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write(malformed), malformed.size());
  file.close();

  QLockFile lock(SettingsMigration::settingsLockPath(path));
  ASSERT_TRUE(lock.tryLock());
  const auto backup =
      SettingsMigration::quarantineCorruptSettingsFileUnlocked(path, malformed);
  lock.unlock();

  ASSERT_TRUE(backup);
  EXPECT_TRUE(QFileInfo::exists(path));
  EXPECT_EQ(QFileInfo(path).size(), 0);
  file.setFileName(*backup);
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  EXPECT_EQ(file.readAll(), malformed);
}

TEST(SettingsMigration, QuarantinePreservesFileAliasAndTransactionIdentity)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString realPath =
      temporaryDirectory.filePath(QStringLiteral("ModOrganizer.ini"));
  const QByteArray malformed = QByteArrayLiteral("[General\nkeep=this-copy\n");
  QFile file(realPath);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write(malformed), malformed.size());
  file.close();
  const QString aliasPath =
      temporaryDirectory.filePath(QStringLiteral("instance-alias.ini"));
  ASSERT_TRUE(QFile::link(realPath, aliasPath));

  const QString lockPath = SettingsMigration::settingsLockPath(realPath);
  ASSERT_EQ(SettingsMigration::settingsLockPath(aliasPath), lockPath);
  QLockFile lock(lockPath);
  lock.setStaleLockTime(0);
  ASSERT_TRUE(lock.tryLock());
  const auto backup =
      SettingsMigration::quarantineCorruptSettingsFileUnlocked(aliasPath,
                                                               malformed);
  ASSERT_TRUE(backup);
  EXPECT_TRUE(QFileInfo(aliasPath).isSymLink());
  EXPECT_TRUE(QFileInfo(realPath).isFile());
  EXPECT_EQ(QFileInfo(realPath).size(), 0);
  EXPECT_FALSE(QFileInfo(*backup).isSymLink());
  file.setFileName(*backup);
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  EXPECT_EQ(file.readAll(), malformed);
  file.close();
  EXPECT_EQ(SettingsMigration::settingsLockPath(aliasPath), lockPath);

#ifdef Q_OS_UNIX
  const pid_t child = ::fork();
  ASSERT_NE(child, -1);
  if (child == 0) {
    QLockFile competing(SettingsMigration::settingsLockPath(aliasPath));
    competing.setStaleLockTime(0);
    ::_exit(competing.tryLock() ? 1 : 0);
  }
  int status = 0;
  ASSERT_EQ(::waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
#endif
}

TEST(SettingsMigration, CompletionWaitsForACompetingSettingsRepair)
{
#ifdef Q_OS_UNIX
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString path = temporaryDirectory.filePath(QStringLiteral("instance.ini"));
  QSettings settings(path, QSettings::IniFormat);
  settings.setValue(SettingsMigration::SettingsSchemaKey,
                    SettingsMigration::CurrentSchema);
  settings.setValue(SettingsMigration::ProductVersionKey,
                    QStringLiteral("2.5.2"));
  settings.sync();

  int readyPipe[2];
  ASSERT_EQ(::pipe(readyPipe), 0);
  const pid_t child = ::fork();
  ASSERT_NE(child, -1);
  if (child == 0) {
    ::close(readyPipe[0]);
    QLockFile lock(SettingsMigration::settingsLockPath(path));
    const char result = lock.tryLock() ? '1' : '0';
    if (::write(readyPipe[1], &result, 1) != 1) {
      ::_exit(1);
    }
    if (result == '1') {
      ::usleep(200000);
      QSettings repaired(path, QSettings::IniFormat);
      repaired.setValue(SettingsMigration::SettingsSchemaKey,
                        SettingsMigration::CurrentSchema);
      repaired.setValue(SettingsMigration::ProductVersionKey,
                        QStringLiteral("2.5.2"));
      repaired.setValue(QStringLiteral("repair_token"),
                        QStringLiteral("preserve-me"));
      repaired.sync();
      if (repaired.status() != QSettings::NoError) {
        ::_exit(1);
      }
      lock.unlock();
    }
    ::close(readyPipe[1]);
    ::_exit(result == '1' ? 0 : 1);
  }

  ::close(readyPipe[1]);
  char childLocked = '0';
  ASSERT_EQ(::read(readyPipe[0], &childLocked, 1), 1);
  ::close(readyPipe[0]);
  ASSERT_EQ(childLocked, '1');
  EXPECT_EQ(SettingsMigration::commitCompletedUpdates(
                settings, QVersionNumber(0, 3, 3),
                SettingsMigration::CurrentSchema),
            QSettings::NoError);
  int status = 0;
  ASSERT_EQ(::waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(WEXITSTATUS(status), 0);

  QSettings reopened(path, QSettings::IniFormat);
  EXPECT_EQ(reopened.value(QStringLiteral("repair_token")).toString(),
            QStringLiteral("preserve-me"));
#else
  GTEST_SKIP() << "requires Unix process locking";
#endif
}

TEST(SettingsMigration, MalformedIniIsNeverRemovedWhenBackupCreationFails)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString fileName(250, QLatin1Char('x'));
  const QString path = temporaryDirectory.filePath(fileName);
  const QByteArray malformed = QByteArrayLiteral("[General\nvalue=keep-me\n");
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write(malformed), malformed.size());
  file.close();

  {
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(QStringLiteral("Widgets/pending_migration"), true);
    EXPECT_EQ(SettingsMigration::commitCompletedUpdates(
                  settings, QVersionNumber(0, 3, 3),
                  SettingsMigration::CurrentSchema),
              QSettings::FormatError);
  }

  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  EXPECT_EQ(file.readAll(), malformed);
  EXPECT_EQ(file.error(), QFileDevice::NoError);
  EXPECT_TRUE(QDir(temporaryDirectory.path())
                  .entryList({fileName + QStringLiteral(".corrupt*")},
                             QDir::Files)
                  .isEmpty());
}

TEST(SettingsMigration, DistinctMalformedFilesReceiveDistinctBackups)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString path = temporaryDirectory.filePath(QStringLiteral("instance.ini"));
  QFile file(path);
  const auto preserve = [&](const QByteArray& data) {
    file.setFileName(path);
    EXPECT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    EXPECT_EQ(file.write(data), data.size());
    file.close();
    QLockFile transactionLock(SettingsMigration::settingsLockPath(path));
    if (!transactionLock.tryLock()) {
      return std::optional<QString>{};
    }
    return SettingsMigration::quarantineCorruptSettingsFileUnlocked(path,
                                                                     data);
  };

  const QByteArray first = QByteArrayLiteral("first-corruption");
  const QByteArray second = QByteArrayLiteral("second-different-corruption");
  const auto firstBackup = preserve(first);
  const auto secondBackup = preserve(second);
  ASSERT_TRUE(firstBackup);
  ASSERT_TRUE(secondBackup);
  EXPECT_NE(*firstBackup, *secondBackup);

  QFile firstFile(*firstBackup);
  ASSERT_TRUE(firstFile.open(QIODevice::ReadOnly));
  EXPECT_EQ(firstFile.readAll(), first);
  QFile secondFile(*secondBackup);
  ASSERT_TRUE(secondFile.open(QIODevice::ReadOnly));
  EXPECT_EQ(secondFile.readAll(), second);
}

TEST(SettingsMigration, Utf8BomIsNotMistakenForCorruptionOnQt6)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString path = temporaryDirectory.filePath(QStringLiteral("instance.ini"));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  const QByteArray contents = QByteArray::fromHex("efbbbf") +
                              QByteArrayLiteral("version=2.5.2\n");
  ASSERT_EQ(file.write(contents), contents.size());
  file.close();

  QSettings settings(path, QSettings::IniFormat);
  EXPECT_EQ(settings.value(QStringLiteral("version")).toString(),
            QStringLiteral("2.5.2"));
  ASSERT_EQ(settings.status(), QSettings::NoError);
  EXPECT_EQ(SettingsMigration::inspectSettingsFile(path).status,
            QSettings::NoError);
}

TEST(CategoryPersistence, WritesCompleteFilesAndReportsPartialPairFailure)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString categoriesPath =
      temporaryDirectory.filePath(QStringLiteral("categories.dat"));
  const QString nexusPath =
      temporaryDirectory.filePath(QStringLiteral("nexuscatmap.dat"));

  EXPECT_EQ(CategoryPersistence::writeFiles(
                categoriesPath, QByteArrayLiteral("categories\n"), nexusPath,
                QByteArrayLiteral("mappings\n")),
            CategoryPersistence::WriteResult::Success);

  QFile categories(categoriesPath);
  ASSERT_TRUE(categories.open(QIODevice::ReadOnly));
  EXPECT_EQ(categories.readAll(), QByteArrayLiteral("categories\n"));
  QFile mappings(nexusPath);
  ASSERT_TRUE(mappings.open(QIODevice::ReadOnly));
  EXPECT_EQ(mappings.readAll(), QByteArrayLiteral("mappings\n"));

  // The caller receives a failure and must leave the independent migration
  // marker pending if the second file cannot be committed.
  EXPECT_EQ(CategoryPersistence::writeFiles(
                categoriesPath, QByteArrayLiteral("new categories\n"),
                QStringLiteral("/dev/null/nexuscatmap.dat"),
                QByteArrayLiteral("new mappings\n")),
            CategoryPersistence::WriteResult::NexusMapFailed);
  categories.close();
  ASSERT_TRUE(categories.open(QIODevice::ReadOnly));
  EXPECT_EQ(categories.readAll(), QByteArrayLiteral("categories\n"));
  EXPECT_FALSE(
      QFileInfo::exists(CategoryPersistence::journalPath(categoriesPath)));
}

TEST(CategoryPersistence, RecoversAnInterruptedPairBeforeLoading)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString categoriesPath =
      temporaryDirectory.filePath(QStringLiteral("categories.dat"));
  const QString nexusPath =
      temporaryDirectory.filePath(QStringLiteral("nexuscatmap.dat"));
  ASSERT_EQ(CategoryPersistence::writeFiles(
                categoriesPath, QByteArrayLiteral("old categories\n"), nexusPath,
                QByteArrayLiteral("old mappings\n")),
            CategoryPersistence::WriteResult::Success);

  const auto oldCategories = CategoryPersistence::snapshot(categoriesPath);
  const auto oldNexusMap   = CategoryPersistence::snapshot(nexusPath);
  ASSERT_TRUE(oldCategories);
  ASSERT_TRUE(oldNexusMap);
  ASSERT_TRUE(CategoryPersistence::writeJournal(
      CategoryPersistence::journalPath(categoriesPath), *oldCategories,
      *oldNexusMap));
  ASSERT_TRUE(CategoryPersistence::writeAtomic(
      categoriesPath, QByteArrayLiteral("partial new categories\n")));
  ASSERT_TRUE(CategoryPersistence::writeAtomic(
      nexusPath, QByteArrayLiteral("partial new mappings\n")));

  ASSERT_TRUE(CategoryPersistence::recoverFiles(categoriesPath, nexusPath));
  const auto recoveredCategories = CategoryPersistence::snapshot(categoriesPath);
  const auto recoveredNexusMap   = CategoryPersistence::snapshot(nexusPath);
  ASSERT_TRUE(recoveredCategories);
  ASSERT_TRUE(recoveredNexusMap);
  EXPECT_EQ(recoveredCategories->data, QByteArrayLiteral("old categories\n"));
  EXPECT_EQ(recoveredNexusMap->data, QByteArrayLiteral("old mappings\n"));
  EXPECT_FALSE(
      QFileInfo::exists(CategoryPersistence::journalPath(categoriesPath)));
}

TEST(CategoryPersistence, CorruptJournalFailsClosedUntilExplicitlyRepaired)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString categoriesPath =
      temporaryDirectory.filePath(QStringLiteral("categories.dat"));
  const QString nexusPath =
      temporaryDirectory.filePath(QStringLiteral("nexuscatmap.dat"));
  const QString journalPath =
      CategoryPersistence::journalPath(categoriesPath);
  const QByteArray corrupt = QByteArrayLiteral("truncated journal");
  QFile journal(journalPath);
  ASSERT_TRUE(journal.open(QIODevice::WriteOnly));
  ASSERT_EQ(journal.write(corrupt), corrupt.size());
  journal.close();

  QString quarantinedPath;
  EXPECT_FALSE(CategoryPersistence::recoverFiles(
      categoriesPath, nexusPath, &quarantinedPath));
  EXPECT_FALSE(quarantinedPath.isEmpty());
  EXPECT_FALSE(QFileInfo::exists(journalPath));
  QFile quarantined(quarantinedPath);
  ASSERT_TRUE(quarantined.open(QIODevice::ReadOnly));
  EXPECT_EQ(quarantined.readAll(), corrupt);

  EXPECT_EQ(CategoryPersistence::writeFiles(
                categoriesPath, QByteArrayLiteral("categories\n"), nexusPath,
                QByteArrayLiteral("mappings\n")),
            CategoryPersistence::WriteResult::CategoriesFailed);

  ASSERT_TRUE(QFile::remove(quarantinedPath));
  EXPECT_EQ(CategoryPersistence::writeFiles(
                categoriesPath, QByteArrayLiteral("categories\n"), nexusPath,
                QByteArrayLiteral("mappings\n")),
            CategoryPersistence::WriteResult::Success);
}

TEST(CategoryPersistence, ExplicitResetPreservesEveryCategoryArtifact)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString categoriesPath =
      temporaryDirectory.filePath(QStringLiteral("categories.dat"));
  const QString nexusPath =
      temporaryDirectory.filePath(QStringLiteral("nexuscatmap.dat"));
  const QString transactionPath =
      CategoryPersistence::journalPath(categoriesPath);
  ASSERT_TRUE(CategoryPersistence::writeAtomic(
      categoriesPath, QByteArrayLiteral("half-new categories\n")));
  ASSERT_TRUE(CategoryPersistence::writeAtomic(
      nexusPath, QByteArrayLiteral("old mappings\n")));
  ASSERT_TRUE(CategoryPersistence::writeAtomic(
      transactionPath + QStringLiteral(".corrupt"),
      QByteArrayLiteral("damaged journal\n")));

  QStringList backups;
  ASSERT_TRUE(CategoryPersistence::resetFiles(categoriesPath, nexusPath,
                                               &backups));
  EXPECT_EQ(backups.size(), 3);
  EXPECT_FALSE(CategoryPersistence::pathPresent(categoriesPath));
  EXPECT_FALSE(CategoryPersistence::pathPresent(nexusPath));
  EXPECT_FALSE(CategoryPersistence::quarantinedJournalPath(transactionPath));
  for (const auto& backup : backups) {
    EXPECT_TRUE(CategoryPersistence::pathPresent(backup));
  }

  EXPECT_EQ(CategoryPersistence::writeFiles(
                categoriesPath, QByteArrayLiteral("fresh categories\n"),
                nexusPath, QByteArrayLiteral("fresh mappings\n")),
            CategoryPersistence::WriteResult::Success);
}

TEST(CategoryPersistence, QuarantinesJournalWithInvalidChecksum)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString categoriesPath =
      temporaryDirectory.filePath(QStringLiteral("categories.dat"));
  const QString nexusPath =
      temporaryDirectory.filePath(QStringLiteral("nexuscatmap.dat"));
  const QString journalPath =
      CategoryPersistence::journalPath(categoriesPath);
  const CategoryPersistence::Snapshot categories{
      true, QByteArrayLiteral("old categories\n")};
  const CategoryPersistence::Snapshot nexusMap{
      true, QByteArrayLiteral("old mappings\n")};
  ASSERT_TRUE(
      CategoryPersistence::writeJournal(journalPath, categories, nexusMap));

  QFile journal(journalPath);
  ASSERT_TRUE(journal.open(QIODevice::ReadWrite));
  QByteArray data = journal.readAll();
  const qsizetype contentOffset = data.indexOf("old categories");
  ASSERT_NE(contentOffset, -1);
  data[contentOffset] ^= 1;
  ASSERT_TRUE(journal.resize(0));
  ASSERT_TRUE(journal.seek(0));
  ASSERT_EQ(journal.write(data), data.size());
  journal.close();

  QString quarantinedPath;
  EXPECT_FALSE(CategoryPersistence::recoverFiles(
      categoriesPath, nexusPath, &quarantinedPath));
  EXPECT_FALSE(quarantinedPath.isEmpty());
  EXPECT_TRUE(QFileInfo::exists(quarantinedPath));
  EXPECT_FALSE(QFileInfo::exists(journalPath));
}

TEST(CategoryPersistence, CorruptJournalNeverExposesHalfCommittedPair)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString categoriesPath =
      temporaryDirectory.filePath(QStringLiteral("categories.dat"));
  const QString nexusPath =
      temporaryDirectory.filePath(QStringLiteral("nexuscatmap.dat"));
  ASSERT_TRUE(CategoryPersistence::writeAtomic(
      categoriesPath, QByteArrayLiteral("half-new categories\n")));
  ASSERT_TRUE(CategoryPersistence::writeAtomic(
      nexusPath, QByteArrayLiteral("old mappings\n")));
  ASSERT_TRUE(CategoryPersistence::writeAtomic(
      CategoryPersistence::journalPath(categoriesPath),
      QByteArrayLiteral("corrupt transaction")));

  QString quarantinedPath;
  EXPECT_FALSE(CategoryPersistence::readFiles(
      categoriesPath, nexusPath, &quarantinedPath));
  EXPECT_FALSE(quarantinedPath.isEmpty());
  EXPECT_FALSE(CategoryPersistence::readFiles(categoriesPath, nexusPath));

  const auto categories = CategoryPersistence::snapshot(categoriesPath);
  const auto mappings   = CategoryPersistence::snapshot(nexusPath);
  ASSERT_TRUE(categories);
  ASSERT_TRUE(mappings);
  EXPECT_EQ(categories->data, QByteArrayLiteral("half-new categories\n"));
  EXPECT_EQ(mappings->data, QByteArrayLiteral("old mappings\n"));
}

TEST(CategoryPersistence, RejectsReadErrorsAsRollbackAndJournalData)
{
#ifdef Q_OS_LINUX
  // Reading this seekable pseudo-file from offset zero fails with EIO. It
  // exercises the post-read QFileDevice error checks without a custom FUSE
  // fixture.
  const QString erroringFile = QStringLiteral("/proc/self/mem");
  EXPECT_FALSE(CategoryPersistence::snapshot(erroringFile));
  EXPECT_EQ(CategoryPersistence::readJournal(erroringFile).status,
            CategoryPersistence::JournalReadStatus::IoError);
#else
  GTEST_SKIP() << "requires Linux /proc/self/mem";
#endif
}

TEST(CategoryPersistence, SerializesConcurrentProcesses)
{
#ifdef Q_OS_UNIX
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString categoriesPath =
      temporaryDirectory.filePath(QStringLiteral("categories.dat"));
  const QString nexusPath =
      temporaryDirectory.filePath(QStringLiteral("nexuscatmap.dat"));

  int readyPipe[2];
  ASSERT_EQ(::pipe(readyPipe), 0);
  const pid_t child = ::fork();
  ASSERT_NE(child, -1);
  if (child == 0) {
    ::close(readyPipe[0]);
    QLockFile lock(CategoryPersistence::lockPath(categoriesPath));
    lock.setStaleLockTime(CategoryPersistence::TransactionLockStaleMs);
    const char result = lock.tryLock() ? '1' : '0';
    if (::write(readyPipe[1], &result, 1) != 1) {
      ::_exit(1);
    }
    if (result == '1') {
      ::usleep(250000);
      lock.unlock();
    }
    ::close(readyPipe[1]);
    ::_exit(result == '1' ? 0 : 1);
  }

  ::close(readyPipe[1]);
  char childLocked = '0';
  ASSERT_EQ(::read(readyPipe[0], &childLocked, 1), 1);
  ::close(readyPipe[0]);
  ASSERT_EQ(childLocked, '1');
  EXPECT_EQ(CategoryPersistence::writeFiles(
                categoriesPath, QByteArrayLiteral("categories\n"), nexusPath,
                QByteArrayLiteral("mappings\n")),
            CategoryPersistence::WriteResult::Success);
  int status = 0;
  ASSERT_EQ(::waitpid(child, &status, 0), child);
  EXPECT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
#else
  GTEST_SKIP() << "requires a Unix process lock";
#endif
}

TEST(CategoryPersistence, RejectsAStaleWriterWithoutLosingTheNewerPair)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString categoriesPath =
      temporaryDirectory.filePath(QStringLiteral("categories.dat"));
  const QString nexusPath =
      temporaryDirectory.filePath(QStringLiteral("nexuscatmap.dat"));
  ASSERT_EQ(CategoryPersistence::writeFiles(
                categoriesPath, QByteArrayLiteral("base categories\n"),
                nexusPath, QByteArrayLiteral("base mappings\n")),
            CategoryPersistence::WriteResult::Success);

  const auto loaded =
      CategoryPersistence::readFiles(categoriesPath, nexusPath);
  ASSERT_TRUE(loaded);
  const QByteArray generation =
      CategoryPersistence::storageVersion(loaded->first, loaded->second);
  ASSERT_FALSE(generation.isEmpty());

  EXPECT_EQ(CategoryPersistence::writeFiles(
                categoriesPath, QByteArrayLiteral("process one categories\n"),
                nexusPath, QByteArrayLiteral("process one mappings\n"),
                generation),
            CategoryPersistence::WriteResult::Success);
  EXPECT_EQ(CategoryPersistence::writeFiles(
                categoriesPath, QByteArrayLiteral("stale process categories\n"),
                nexusPath, QByteArrayLiteral("stale process mappings\n"),
                generation),
            CategoryPersistence::WriteResult::Conflict);

  const auto current =
      CategoryPersistence::readFiles(categoriesPath, nexusPath);
  ASSERT_TRUE(current);
  EXPECT_EQ(current->first.data,
            QByteArrayLiteral("process one categories\n"));
  EXPECT_EQ(current->second.data,
            QByteArrayLiteral("process one mappings\n"));
}

TEST(CategoryPersistence, RecoversAStaleProcessLock)
{
#ifdef Q_OS_UNIX
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString categoriesPath =
      temporaryDirectory.filePath(QStringLiteral("categories.dat"));
  const QString nexusPath =
      temporaryDirectory.filePath(QStringLiteral("nexuscatmap.dat"));

  const pid_t child = ::fork();
  ASSERT_NE(child, -1);
  if (child == 0) {
    QLockFile lock(CategoryPersistence::lockPath(categoriesPath));
    lock.setStaleLockTime(CategoryPersistence::TransactionLockStaleMs);
    ::_exit(lock.tryLock() ? 0 : 1);
  }
  int status = 0;
  ASSERT_EQ(::waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(WEXITSTATUS(status), 0);

  EXPECT_EQ(CategoryPersistence::writeFiles(
                categoriesPath, QByteArrayLiteral("categories\n"), nexusPath,
                QByteArrayLiteral("mappings\n")),
            CategoryPersistence::WriteResult::Success);
#else
  GTEST_SKIP() << "requires a Unix process lock";
#endif
}

TEST(CategoryAssignmentPolicy, ImportedIdsHaveStableNonCollidingSemantics)
{
  EXPECT_EQ(*CategoryAssignmentPolicy::importedCategoryId(7), 1'000'007);
  EXPECT_NE(*CategoryAssignmentPolicy::importedCategoryId(7), 7);
  EXPECT_FALSE(CategoryAssignmentPolicy::importedCategoryId(0));
  EXPECT_FALSE(CategoryAssignmentPolicy::importedCategoryId(
      std::numeric_limits<int>::max()));
  EXPECT_TRUE(CategoryAssignmentPolicy::isSafeSerializedName(
      QStringLiteral("Armour")));
  EXPECT_FALSE(CategoryAssignmentPolicy::isSafeSerializedName(
      QStringLiteral("unsafe|category")));
  EXPECT_FALSE(CategoryAssignmentPolicy::isSafeSerializedName(
      QStringLiteral("unsafe\tcategory")));
}

TEST(CategoryAssignmentPolicy, RetiredLocalIdsAreNeverReused)
{
  const std::set<int> importedOnly{1'000'007, 1'000'012};
  EXPECT_EQ(CategoryAssignmentPolicy::nextLocalCategoryId(52, importedOnly),
            53);

  const std::set<int> occupied{53, 54, 1'000'007};
  EXPECT_EQ(CategoryAssignmentPolicy::nextLocalCategoryId(52, occupied), 55);
  EXPECT_FALSE(CategoryAssignmentPolicy::nextLocalCategoryId(
      CategoryAssignmentPolicy::ImportedCategoryIdBase - 1, {}));
  EXPECT_FALSE(
      CategoryAssignmentPolicy::mayAssignCategoryId(52, 39, std::nullopt));
  EXPECT_TRUE(CategoryAssignmentPolicy::mayAssignCategoryId(52, 39, 39));
  EXPECT_TRUE(
      CategoryAssignmentPolicy::mayAssignCategoryId(52, 53, std::nullopt));
  EXPECT_FALSE(CategoryAssignmentPolicy::mayAssignCategoryId(
      52, CategoryAssignmentPolicy::ImportedCategoryIdBase, std::nullopt));
}

TEST(CategoryFileParser, AcceptsValidInventoryAndMigratesLegacyDuplicate)
{
  const auto parsed = CategoryFileParser::parse(
      QByteArrayLiteral("39|Voice|0\n10|Body|0\n39|Tattoos|10\n"),
      QByteArrayLiteral("39|Tattoos|7\n-1|Unassigned|8\n"));
  ASSERT_TRUE(parsed) << parsed.error.toStdString();
  ASSERT_EQ(parsed.inventory->categories.size(), 3);
  EXPECT_EQ(parsed.inventory->categories[0].id, 59);
  EXPECT_EQ(parsed.inventory->categories[2].id, 39);
  ASSERT_EQ(parsed.inventory->nexusMappings.size(), 2);
  EXPECT_EQ(parsed.inventory->nexusMappings[0].categoryId, 39);
  EXPECT_EQ(parsed.inventory->nexusMappings[1].categoryId, -1);
}

TEST(CategoryFileParser, RejectsPartialDuplicateAndUnsafeInventories)
{
  const auto expectInvalid = [](const QByteArray& categories,
                                const QByteArray& mappings = QByteArray()) {
    const auto parsed = CategoryFileParser::parse(categories, mappings);
    EXPECT_FALSE(parsed) << parsed.error.toStdString();
  };

  expectInvalid(QByteArrayLiteral("1|Valid|0\ntruncated"));
  expectInvalid(QByteArrayLiteral("1|One|0\n1|Two|0\n"));
  expectInvalid(QByteArrayLiteral("1|One|2\n"));
  expectInvalid(QByteArrayLiteral("1|One|2\n2|Two|1\n"));
  expectInvalid(QByteArrayLiteral("1|unsafe|name|0\n"));
  expectInvalid(QByteArrayLiteral("1|One|0\n"),
                QByteArrayLiteral("2|Missing|7\n"));
  expectInvalid(QByteArrayLiteral("1|One|0\n"),
                QByteArrayLiteral("1|One|7\n1|Duplicate|7\n"));
}

TEST(CategoryFileParser, MigratesLegacyVoiceWithoutReusingACustomId)
{
  const auto parsed = CategoryFileParser::parse(
      QByteArrayLiteral(
          "39|Voice|0\n39|Tattoos|0\n59|Existing Custom Category|0\n"),
      QByteArray());
  ASSERT_TRUE(parsed) << parsed.error.toStdString();
  ASSERT_EQ(parsed.inventory->categories.size(), 3);
  EXPECT_EQ(parsed.inventory->categories[0].id, 60);
  EXPECT_EQ(parsed.inventory->categories[1].id, 39);
  EXPECT_EQ(parsed.inventory->categories[2].id, 59);
}

TEST(CategoryFileParser, AllowsAnExplicitlySavedBlankInventory)
{
  const auto parsed = CategoryFileParser::parse(QByteArray(), QByteArray());
  ASSERT_TRUE(parsed) << parsed.error.toStdString();
  EXPECT_TRUE(parsed.inventory->categories.empty());
  EXPECT_TRUE(parsed.inventory->nexusMappings.empty());
}

TEST(CategoryFileParser, RejectsOversizedInputAndExcessiveParentDepth)
{
  QByteArray oversized(CategoryFileParser::MaximumInputBytes + 1, 'x');
  EXPECT_FALSE(CategoryFileParser::parse(oversized, QByteArray()));

  QByteArray categories;
  for (int id = 1; id <= CategoryFileParser::MaximumParentDepth + 1; ++id) {
    categories.append(QByteArray::number(id))
        .append("|Category ")
        .append(QByteArray::number(id))
        .append("|")
        .append(QByteArray::number(id - 1))
        .append("\n");
  }
  const auto parsed = CategoryFileParser::parse(categories, QByteArray());
  EXPECT_FALSE(parsed);
  EXPECT_TRUE(parsed.error.contains(QStringLiteral("depth")));
}

TEST(CategoryPersistence, RejectsOversizedCategoryFilesBeforeLoading)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString path =
      temporaryDirectory.filePath(QStringLiteral("categories.dat"));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  const QByteArray oversized(CategoryPersistence::MaximumCategoryFileSize + 1,
                             'x');
  ASSERT_EQ(file.write(oversized), oversized.size());
  file.close();
  EXPECT_FALSE(CategoryPersistence::snapshot(path));
}

TEST(CategoryPersistence, KeepsValidJournalForRetryWhenRestoreFails)
{
  QTemporaryDir temporaryDirectory;
  ASSERT_TRUE(temporaryDirectory.isValid());
  const QString categoriesPath =
      temporaryDirectory.filePath(QStringLiteral("categories.dat"));
  const QString nexusPath =
      temporaryDirectory.filePath(QStringLiteral("nexuscatmap.dat"));
  const QString journalPath =
      CategoryPersistence::journalPath(categoriesPath);
  const CategoryPersistence::Snapshot categories{
      true, QByteArrayLiteral("old categories\n")};
  const CategoryPersistence::Snapshot nexusMap{
      true, QByteArrayLiteral("old mappings\n")};
  ASSERT_TRUE(
      CategoryPersistence::writeJournal(journalPath, categories, nexusMap));
  ASSERT_TRUE(QDir().mkdir(categoriesPath));

  EXPECT_FALSE(
      CategoryPersistence::recoverFiles(categoriesPath, nexusPath));
  EXPECT_TRUE(QFileInfo::exists(journalPath));

  ASSERT_TRUE(QDir().rmdir(categoriesPath));
  EXPECT_TRUE(CategoryPersistence::recoverFiles(categoriesPath, nexusPath));
  EXPECT_FALSE(QFileInfo::exists(journalPath));
  const auto restoredCategories = CategoryPersistence::snapshot(categoriesPath);
  const auto restoredNexusMap = CategoryPersistence::snapshot(nexusPath);
  ASSERT_TRUE(restoredCategories);
  ASSERT_TRUE(restoredNexusMap);
  EXPECT_EQ(restoredCategories->data, categories.data);
  EXPECT_EQ(restoredNexusMap->data, nexusMap.data);
}
