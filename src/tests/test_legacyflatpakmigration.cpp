#include "legacyflatpakmigration.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSettings>
#include <QSharedMemory>
#include <QTemporaryDir>
#include <QUuid>

#include <gtest/gtest.h>

#include <sys/wait.h>
#include <unistd.h>

namespace
{
using LegacyFlatpakMigration::Paths;
using LegacyFlatpakMigration::Status;

class LegacyFlatpakMigrationTest : public testing::Test
{
protected:
  void SetUp() override
  {
    ASSERT_TRUE(temporary.isValid());
    paths.legacyRoot       = temporary.filePath(QStringLiteral("legacy"));
    paths.dataRoot         = temporary.filePath(QStringLiteral("native-data"));
    paths.configRoot       = temporary.filePath(QStringLiteral("native-config"));
    paths.legacyProcessKey = QStringLiteral("fluorine-migration-test-") +
                             QUuid::createUuid().toString(QUuid::WithoutBraces);
    paths.lockTimeoutMs = 0;
  }

  QString legacy(const QString& relative) const
  {
    return QDir(paths.legacyRoot).filePath(relative);
  }

  QString data(const QString& relative) const
  {
    return QDir(paths.dataRoot).filePath(relative);
  }

  QString config(const QString& relative) const
  {
    return QDir(paths.configRoot).filePath(relative);
  }

  void makeFile(const QString& path, const QByteArray& contents = "value")
  {
    ASSERT_TRUE(QDir().mkpath(QFileInfo(path).dir().absolutePath()));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(file.write(contents), contents.size());
    file.close();
  }

  void makeInstance(const QString& parent, const QString& name, bool portable = false)
  {
    const QString path = QDir(parent).filePath(name);
    ASSERT_TRUE(QDir().mkpath(path));
    QSettings settings(QDir(path).filePath(QStringLiteral("ModOrganizer.ini")),
                       QSettings::IniFormat);
    settings.setValue(QStringLiteral("General/portable"), portable);
    settings.setValue(QStringLiteral("General/gameName"), QStringLiteral("Morrowind"));
    settings.sync();
    ASSERT_EQ(settings.status(), QSettings::NoError);
  }

  void makeConfig(const QString& path, const QString& prefix)
  {
    QJsonObject object{
        {QStringLiteral("app_id"), 22320},
        {QStringLiteral("prefix_path"), prefix},
        {QStringLiteral("unknown_future_key"), QStringLiteral("preserved")},
    };
    makeFile(path, QJsonDocument(object).toJson(QJsonDocument::Indented));
  }

  QJsonObject readObject(const QString& path)
  {
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::ReadOnly));
    return QJsonDocument::fromJson(file.readAll()).object();
  }

  QTemporaryDir temporary;
  Paths paths;
};

TEST_F(LegacyFlatpakMigrationTest, MissingLegacyRootIsANoop)
{
  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::NotNeeded);
  EXPECT_FALSE(QFileInfo::exists(paths.dataRoot));

  Paths overlapping      = paths;
  overlapping.configRoot = QDir(paths.dataRoot).filePath(QStringLiteral("config"));
  const auto overlappingResult = LegacyFlatpakMigration::migrate(overlapping);
  EXPECT_EQ(overlappingResult.status, Status::NotNeeded);
  EXPECT_FALSE(QFileInfo::exists(paths.dataRoot));
}

TEST_F(LegacyFlatpakMigrationTest, EmptyConfiguredRootsFailClosed)
{
  Paths invalid = paths;
  invalid.configRoot.clear();

  const auto result = LegacyFlatpakMigration::migrate(invalid);
  EXPECT_EQ(result.status, Status::Failed);
  EXPECT_FALSE(result.diagnostics.isEmpty());
  EXPECT_FALSE(QFileInfo::exists(paths.dataRoot));
}

TEST_F(LegacyFlatpakMigrationTest, OverlappingConfiguredRootsFailBeforeMutation)
{
  const QString root = temporary.filePath(QStringLiteral("overlap"));
  const QList<Paths> cases{
      {QDir(root).filePath(QStringLiteral("one/legacy")),
       QDir(root).filePath(QStringLiteral("one/native")),
       QDir(root).filePath(QStringLiteral("one/legacy/config")), paths.legacyProcessKey,
       0},
      {QDir(root).filePath(QStringLiteral("two/config/legacy")),
       QDir(root).filePath(QStringLiteral("two/native")),
       QDir(root).filePath(QStringLiteral("two/config")), paths.legacyProcessKey, 0},
      {QDir(root).filePath(QStringLiteral("three/legacy")),
       QDir(root).filePath(QStringLiteral("three/native")),
       QDir(root).filePath(QStringLiteral("three/native/config")),
       paths.legacyProcessKey, 0},
      {QDir(root).filePath(QStringLiteral("four/legacy")),
       QDir(root).filePath(QStringLiteral("four/config/native")),
       QDir(root).filePath(QStringLiteral("four/config")), paths.legacyProcessKey, 0},
  };

  for (const Paths& candidate : cases) {
    SCOPED_TRACE(candidate.legacyRoot.toStdString());
    makeInstance(candidate.legacyRoot, QStringLiteral("Legacy"));
    const auto result = LegacyFlatpakMigration::migrate(candidate);
    EXPECT_EQ(result.status, Status::Failed);
    EXPECT_TRUE(
        QFileInfo::exists(QDir(candidate.legacyRoot)
                              .filePath(QStringLiteral("Legacy/ModOrganizer.ini"))));
    EXPECT_FALSE(
        QFileInfo::exists(QDir(candidate.dataRoot).filePath(QStringLiteral("Legacy"))));
  }
}

TEST_F(LegacyFlatpakMigrationTest, ValidCompletionIgnoresLaterConfigRootOverlap)
{
  ASSERT_TRUE(QDir().mkpath(paths.legacyRoot));
  makeFile(legacy(QStringLiteral("MOVED.txt")), QByteArrayLiteral("old\n"));
  ASSERT_EQ(LegacyFlatpakMigration::migrate(paths).status, Status::Complete);

  Paths changed      = paths;
  changed.configRoot = QDir(paths.dataRoot).filePath(QStringLiteral("config"));
  const auto result  = LegacyFlatpakMigration::migrate(changed);
  EXPECT_EQ(result.status, Status::Complete);
}

TEST_F(LegacyFlatpakMigrationTest, MovesPrefixAndGlobalInstancesBeforeRewritingConfig)
{
  ASSERT_TRUE(QDir().mkpath(legacy(QStringLiteral("Prefix/pfx/drive_c"))));
  makeFile(legacy(QStringLiteral("Prefix/pfx/drive_c/sentinel")));
  makeInstance(legacy(QStringLiteral("data/ModOrganizer")), QStringLiteral("Nerevar"));
  makeInstance(legacy(QStringLiteral(".local/share/fluorine")),
               QStringLiteral("Outlander"));
  makeInstance(legacy(QStringLiteral("data/fluorine")), QStringLiteral("DataLayout"));
  makeInstance(
      legacy(QStringLiteral(".var/app/com.fluorine.manager/data/ModOrganizer")),
      QStringLiteral("Nested"));
  makeConfig(legacy(QStringLiteral("config/fluorine/config.json")),
             legacy(QStringLiteral("Prefix/pfx")));

  const QString oldSettings =
      legacy(QStringLiteral("config/Mod Organizer Team/Mod Organizer.conf"));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(oldSettings).dir().absolutePath()));
  {
    QSettings settings(oldSettings, QSettings::IniFormat);
    settings.setValue(QStringLiteral("CurrentInstance"), QStringLiteral("Nerevar"));
    settings.sync();
    ASSERT_EQ(settings.status(), QSettings::NoError);
  }

  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::Complete);
  EXPECT_TRUE(QFileInfo::exists(data(QStringLiteral("Prefix/pfx/drive_c/sentinel"))));
  EXPECT_TRUE(QFileInfo::exists(data(QStringLiteral("Nerevar/ModOrganizer.ini"))));
  EXPECT_TRUE(QFileInfo::exists(data(QStringLiteral("Outlander/ModOrganizer.ini"))));
  EXPECT_TRUE(QFileInfo::exists(data(QStringLiteral("DataLayout/ModOrganizer.ini"))));
  EXPECT_TRUE(QFileInfo::exists(data(QStringLiteral("Nested/ModOrganizer.ini"))));
  EXPECT_FALSE(QFileInfo::exists(legacy(QStringLiteral("Prefix"))));
  EXPECT_FALSE(QFileInfo::exists(legacy(QStringLiteral("data/ModOrganizer/Nerevar"))));

  const QJsonObject migrated =
      readObject(config(QStringLiteral("fluorine/config.json")));
  EXPECT_EQ(migrated.value(QStringLiteral("unknown_future_key")).toString(),
            QStringLiteral("preserved"));
  EXPECT_EQ(migrated.value(QStringLiteral("prefix_path")).toString(),
            data(QStringLiteral("Prefix/pfx")));
  QSettings global(config(QStringLiteral("Mod Organizer Team/Mod Organizer.conf")),
                   QSettings::IniFormat);
  EXPECT_EQ(global.value(QStringLiteral("CurrentInstance")).toString(),
            QStringLiteral("Nerevar"));
  EXPECT_TRUE(QFileInfo::exists(LegacyFlatpakMigration::completionMarkerPath(paths)));
  EXPECT_FALSE(QFileInfo::exists(LegacyFlatpakMigration::prefixReceiptPath(paths)));

  const auto repeated = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(repeated.status, Status::Complete);
}

TEST_F(LegacyFlatpakMigrationTest, LegacyMovedMarkerDoesNotHideAnUnmigratedInstance)
{
  ASSERT_TRUE(QDir().mkpath(paths.legacyRoot));
  makeFile(legacy(QStringLiteral("MOVED.txt")), QByteArrayLiteral("old\n"));
  makeInstance(paths.legacyRoot, QStringLiteral("Recovered"));

  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::Complete);
  EXPECT_TRUE(QFileInfo::exists(data(QStringLiteral("Recovered/ModOrganizer.ini"))));
}

TEST_F(LegacyFlatpakMigrationTest, VersionOneRuntimeResidueIsRetainedAndReported)
{
  ASSERT_TRUE(QDir().mkpath(paths.legacyRoot));
  makeFile(legacy(QStringLiteral("MOVED.txt")), QByteArrayLiteral("old\n"));
  makeFile(data(QStringLiteral("bin/legacy-helper")));
  makeFile(data(QStringLiteral("logs/legacy.log")));

  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::Complete);
  EXPECT_FALSE(result.diagnostics.isEmpty());
  EXPECT_TRUE(QFileInfo::exists(data(QStringLiteral("bin/legacy-helper"))));
  EXPECT_TRUE(QFileInfo::exists(data(QStringLiteral("logs/legacy.log"))));
  EXPECT_TRUE(QFileInfo::exists(result.attentionReport));
}

TEST_F(LegacyFlatpakMigrationTest,
       PrefixCollisionIsRetainedWithoutRebindingOrDeletingEitherSide)
{
  ASSERT_TRUE(QDir().mkpath(legacy(QStringLiteral("Prefix/pfx/drive_c"))));
  ASSERT_TRUE(QDir().mkpath(data(QStringLiteral("Prefix/pfx/drive_c"))));
  makeFile(legacy(QStringLiteral("Prefix/legacy-sentinel")));
  makeFile(data(QStringLiteral("Prefix/native-sentinel")));
  makeConfig(legacy(QStringLiteral("config/fluorine/config.json")),
             legacy(QStringLiteral("Prefix/pfx")));

  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::Attention);
  EXPECT_TRUE(QFileInfo::exists(legacy(QStringLiteral("Prefix/legacy-sentinel"))));
  EXPECT_TRUE(QFileInfo::exists(data(QStringLiteral("Prefix/native-sentinel"))));
  EXPECT_FALSE(QFileInfo::exists(config(QStringLiteral("fluorine/config.json"))));
  EXPECT_TRUE(QFileInfo::exists(result.attentionReport));
  EXPECT_FALSE(QFileInfo::exists(LegacyFlatpakMigration::completionMarkerPath(paths)));
}

TEST_F(LegacyFlatpakMigrationTest,
       UnprovenVersionOnePrefixDoesNotPublishAStaleNativeConfig)
{
  ASSERT_TRUE(QDir().mkpath(data(QStringLiteral("Prefix/native/drive_c"))));
  makeConfig(legacy(QStringLiteral("config/fluorine/config.json")),
             legacy(QStringLiteral("Prefix/legacy")));

  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::Failed);
  EXPECT_FALSE(QFileInfo::exists(config(QStringLiteral("fluorine/config.json"))));
  EXPECT_TRUE(QFileInfo::exists(legacy(QStringLiteral("config/fluorine/config.json"))));
  EXPECT_TRUE(QFileInfo::exists(data(QStringLiteral("Prefix/native/drive_c"))));
}

TEST_F(LegacyFlatpakMigrationTest,
       InstanceNamedPrefixIsNotMovedIntoTheManagedWinePrefixDirectory)
{
  makeInstance(legacy(QStringLiteral("data/ModOrganizer")), QStringLiteral("Prefix"));
  makeFile(legacy(QStringLiteral("data/ModOrganizer/Prefix/sentinel")));

  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::Attention);
  EXPECT_TRUE(
      QFileInfo::exists(legacy(QStringLiteral("data/ModOrganizer/Prefix/sentinel"))));
  EXPECT_FALSE(QFileInfo::exists(data(QStringLiteral("Prefix"))));
  EXPECT_TRUE(QFileInfo::exists(result.attentionReport));
}

TEST_F(LegacyFlatpakMigrationTest,
       InstanceCollisionPreservesBothSidesAndWritesAttentionReport)
{
  makeInstance(legacy(QStringLiteral("data/ModOrganizer")), QStringLiteral("Same"));
  makeInstance(paths.dataRoot, QStringLiteral("Same"));
  makeFile(legacy(QStringLiteral("data/ModOrganizer/Same/legacy")));
  makeFile(data(QStringLiteral("Same/native")));
  const QString oldSettings =
      legacy(QStringLiteral("config/Mod Organizer Team/Mod Organizer.conf"));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(oldSettings).dir().absolutePath()));
  {
    QSettings settings(oldSettings, QSettings::IniFormat);
    settings.setValue(QStringLiteral("CurrentInstance"), QStringLiteral("Same"));
    settings.sync();
  }

  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::Attention);
  EXPECT_TRUE(
      QFileInfo::exists(legacy(QStringLiteral("data/ModOrganizer/Same/legacy"))));
  EXPECT_TRUE(QFileInfo::exists(data(QStringLiteral("Same/native"))));
  EXPECT_TRUE(QFileInfo::exists(result.attentionReport));
  QSettings migrated(config(QStringLiteral("Mod Organizer Team/Mod Organizer.conf")),
                     QSettings::IniFormat);
  EXPECT_FALSE(migrated.contains(QStringLiteral("CurrentInstance")));
  EXPECT_FALSE(QFileInfo::exists(LegacyFlatpakMigration::completionMarkerPath(paths)));
}

TEST_F(LegacyFlatpakMigrationTest,
       RepairsConfigAndSettingsMisplacedByVersionOneMigration)
{
  ASSERT_TRUE(QDir().mkpath(paths.legacyRoot));
  makeFile(legacy(QStringLiteral("MOVED.txt")), QByteArrayLiteral("old\n"));
  makeConfig(data(QStringLiteral("config/fluorine/config.json")),
             QStringLiteral("/unrelated/prefix"));
  const QString misplaced =
      data(QStringLiteral("config/Mod Organizer Team/Mod Organizer.conf"));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(misplaced).dir().absolutePath()));
  {
    QSettings settings(misplaced, QSettings::IniFormat);
    settings.setValue(QStringLiteral("CurrentInstance"), QStringLiteral("Legacy"));
    settings.sync();
  }

  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::Attention);
  EXPECT_EQ(readObject(config(QStringLiteral("fluorine/config.json")))
                .value(QStringLiteral("unknown_future_key"))
                .toString(),
            QStringLiteral("preserved"));
  QSettings migrated(config(QStringLiteral("Mod Organizer Team/Mod Organizer.conf")),
                     QSettings::IniFormat);
  EXPECT_FALSE(migrated.contains(QStringLiteral("CurrentInstance")));
}

TEST_F(LegacyFlatpakMigrationTest,
       ConfigInstanceContentIsNotImportedAsFluorineConfiguration)
{
  makeInstance(paths.legacyRoot, QStringLiteral("config"));
  makeConfig(legacy(QStringLiteral("config/fluorine/config.json")),
             QStringLiteral("/instance-owned/prefix"));

  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::Attention);
  EXPECT_TRUE(QFileInfo::exists(data(QStringLiteral("config/ModOrganizer.ini"))));
  EXPECT_TRUE(QFileInfo::exists(data(QStringLiteral("config/fluorine/config.json"))));
  EXPECT_FALSE(QFileInfo::exists(config(QStringLiteral("fluorine/config.json"))));
}

TEST_F(LegacyFlatpakMigrationTest,
       ConfigInstanceWithoutCompatibilityFilesDoesNotBlockCompletion)
{
  ASSERT_TRUE(QDir().mkpath(paths.legacyRoot));
  makeFile(legacy(QStringLiteral("MOVED.txt")), QByteArrayLiteral("old\n"));
  makeInstance(paths.legacyRoot, QStringLiteral("config"));

  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::Complete);
  EXPECT_TRUE(QFileInfo::exists(data(QStringLiteral("config/ModOrganizer.ini"))));
  EXPECT_TRUE(result.diagnostics.isEmpty());
}

TEST_F(LegacyFlatpakMigrationTest, ConfigInstanceContentIsNotImportedAsGlobalSettings)
{
  makeInstance(paths.legacyRoot, QStringLiteral("config"));
  const QString instanceSettings =
      legacy(QStringLiteral("config/Mod Organizer Team/Mod Organizer.conf"));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(instanceSettings).dir().absolutePath()));
  {
    QSettings settings(instanceSettings, QSettings::IniFormat);
    settings.setValue(QStringLiteral("CurrentInstance"), QStringLiteral("Wrong"));
    settings.sync();
    ASSERT_EQ(settings.status(), QSettings::NoError);
  }

  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::Attention);
  EXPECT_TRUE(QFileInfo::exists(data(QStringLiteral("config/ModOrganizer.ini"))));
  EXPECT_TRUE(QFileInfo::exists(
      data(QStringLiteral("config/Mod Organizer Team/Mod Organizer.conf"))));
  EXPECT_FALSE(QFileInfo::exists(
      config(QStringLiteral("Mod Organizer Team/Mod Organizer.conf"))));
}

TEST_F(LegacyFlatpakMigrationTest,
       PortableConfigInstanceContentIsNotImportedAsApplicationState)
{
  makeInstance(paths.legacyRoot, QStringLiteral("config"), true);
  makeConfig(legacy(QStringLiteral("config/fluorine/config.json")),
             QStringLiteral("/portable-instance/prefix"));
  const QString instanceSettings =
      legacy(QStringLiteral("config/Mod Organizer Team/Mod Organizer.conf"));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(instanceSettings).dir().absolutePath()));
  {
    QSettings settings(instanceSettings, QSettings::IniFormat);
    settings.setValue(QStringLiteral("CurrentInstance"), QStringLiteral("Wrong"));
    settings.sync();
    ASSERT_EQ(settings.status(), QSettings::NoError);
  }

  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::Attention);
  EXPECT_TRUE(QFileInfo::exists(legacy(QStringLiteral("config/ModOrganizer.ini"))));
  EXPECT_FALSE(QFileInfo::exists(config(QStringLiteral("fluorine/config.json"))));
  EXPECT_FALSE(QFileInfo::exists(
      config(QStringLiteral("Mod Organizer Team/Mod Organizer.conf"))));
}

TEST_F(LegacyFlatpakMigrationTest,
       CollidedConfigInstanceContentIsNotImportedAsApplicationState)
{
  makeInstance(paths.legacyRoot, QStringLiteral("config"));
  makeConfig(legacy(QStringLiteral("config/fluorine/config.json")),
             QStringLiteral("/collided-instance/prefix"));
  makeFile(data(QStringLiteral("config/native-owner")));

  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::Attention);
  EXPECT_TRUE(QFileInfo::exists(legacy(QStringLiteral("config/ModOrganizer.ini"))));
  EXPECT_TRUE(QFileInfo::exists(data(QStringLiteral("config/native-owner"))));
  EXPECT_FALSE(QFileInfo::exists(config(QStringLiteral("fluorine/config.json"))));
}

TEST_F(LegacyFlatpakMigrationTest,
       NativeApplicationStateWinsOverInstanceOwnedCompatibilityFiles)
{
  makeInstance(paths.legacyRoot, QStringLiteral("config"), true);
  makeConfig(legacy(QStringLiteral("config/fluorine/config.json")),
             QStringLiteral("/instance-owned/prefix"));
  makeConfig(config(QStringLiteral("fluorine/config.json")),
             QStringLiteral("/native/prefix"));
  const QString instanceSettings =
      legacy(QStringLiteral("config/Mod Organizer Team/Mod Organizer.conf"));
  const QString nativeSettings =
      config(QStringLiteral("Mod Organizer Team/Mod Organizer.conf"));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(instanceSettings).dir().absolutePath()));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(nativeSettings).dir().absolutePath()));
  {
    QSettings settings(instanceSettings, QSettings::IniFormat);
    settings.setValue(QStringLiteral("CurrentInstance"), QStringLiteral("Wrong"));
    settings.sync();
  }
  {
    QSettings settings(nativeSettings, QSettings::IniFormat);
    settings.setValue(QStringLiteral("CurrentInstance"), QStringLiteral("Native"));
    settings.sync();
  }

  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::Complete);
  EXPECT_EQ(readObject(config(QStringLiteral("fluorine/config.json")))
                .value(QStringLiteral("prefix_path"))
                .toString(),
            QStringLiteral("/native/prefix"));
  QSettings migrated(nativeSettings, QSettings::IniFormat);
  EXPECT_EQ(migrated.value(QStringLiteral("CurrentInstance")).toString(),
            QStringLiteral("Native"));
}

TEST_F(LegacyFlatpakMigrationTest,
       InvalidLegacyConfigCandidatePreventsSelectingAnotherCandidate)
{
  makeConfig(legacy(QStringLiteral("config/fluorine/config.json")),
             QStringLiteral("/valid-but-ambiguous/prefix"));
  ASSERT_TRUE(QDir().mkpath(legacy(
      QStringLiteral(".var/app/com.fluorine.manager/config/fluorine/config.json"))));

  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::Attention);
  EXPECT_FALSE(QFileInfo::exists(config(QStringLiteral("fluorine/config.json"))));
  EXPECT_TRUE(QFileInfo::exists(legacy(QStringLiteral("config/fluorine/config.json"))));
}

TEST_F(LegacyFlatpakMigrationTest, RecoversAMovedPrefixAfterConfigPublicationFails)
{
  ASSERT_TRUE(QDir().mkpath(legacy(QStringLiteral("Prefix/pfx/drive_c"))));
  makeFile(legacy(QStringLiteral("Prefix/pfx/drive_c/sentinel")));
  makeConfig(legacy(QStringLiteral("config/fluorine/config.json")),
             legacy(QStringLiteral("Prefix/pfx")));
  makeFile(config(QStringLiteral("fluorine")), QByteArrayLiteral("blocker"));

  const auto interrupted = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(interrupted.status, Status::Failed);
  EXPECT_FALSE(QFileInfo::exists(legacy(QStringLiteral("Prefix"))));
  EXPECT_TRUE(QFileInfo::exists(data(QStringLiteral("Prefix/pfx/drive_c/sentinel"))));
  EXPECT_TRUE(QFileInfo::exists(LegacyFlatpakMigration::prefixReceiptPath(paths)));

  ASSERT_TRUE(QFile::remove(config(QStringLiteral("fluorine"))));
  const auto recovered = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(recovered.status, Status::Complete);
  EXPECT_EQ(readObject(config(QStringLiteral("fluorine/config.json")))
                .value(QStringLiteral("prefix_path"))
                .toString(),
            data(QStringLiteral("Prefix/pfx")));
  EXPECT_FALSE(QFileInfo::exists(LegacyFlatpakMigration::prefixReceiptPath(paths)));
}

TEST_F(LegacyFlatpakMigrationTest,
       RecoversAfterConfigPublicationButBeforeSettingsPublication)
{
  ASSERT_TRUE(QDir().mkpath(legacy(QStringLiteral("Prefix/pfx/drive_c"))));
  makeInstance(legacy(QStringLiteral("data/ModOrganizer")),
               QStringLiteral("Recovered"));
  makeConfig(legacy(QStringLiteral("config/fluorine/config.json")),
             legacy(QStringLiteral("Prefix/pfx")));
  const QString oldSettings =
      legacy(QStringLiteral("config/Mod Organizer Team/Mod Organizer.conf"));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(oldSettings).dir().absolutePath()));
  {
    QSettings settings(oldSettings, QSettings::IniFormat);
    settings.setValue(QStringLiteral("CurrentInstance"), QStringLiteral("Recovered"));
    settings.sync();
    ASSERT_EQ(settings.status(), QSettings::NoError);
  }
  makeFile(config(QStringLiteral("Mod Organizer Team")), QByteArrayLiteral("blocker"));

  const auto interrupted = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(interrupted.status, Status::Failed);
  EXPECT_EQ(readObject(config(QStringLiteral("fluorine/config.json")))
                .value(QStringLiteral("prefix_path"))
                .toString(),
            data(QStringLiteral("Prefix/pfx")));
  EXPECT_TRUE(QFileInfo::exists(LegacyFlatpakMigration::prefixReceiptPath(paths)));

  ASSERT_TRUE(QFile::remove(config(QStringLiteral("Mod Organizer Team"))));
  const auto recovered = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(recovered.status, Status::Complete);
  QSettings migrated(config(QStringLiteral("Mod Organizer Team/Mod Organizer.conf")),
                     QSettings::IniFormat);
  EXPECT_EQ(migrated.value(QStringLiteral("CurrentInstance")).toString(),
            QStringLiteral("Recovered"));
}

TEST_F(LegacyFlatpakMigrationTest, NativeGlobalSettingsRemainUnchanged)
{
  ASSERT_TRUE(QDir().mkpath(paths.legacyRoot));
  const QString oldSettings =
      legacy(QStringLiteral("config/Mod Organizer Team/Mod Organizer.conf"));
  const QString nativeSettings =
      config(QStringLiteral("Mod Organizer Team/Mod Organizer.conf"));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(oldSettings).dir().absolutePath()));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(nativeSettings).dir().absolutePath()));
  {
    QSettings settings(oldSettings, QSettings::IniFormat);
    settings.setValue(QStringLiteral("CurrentInstance"), QStringLiteral("Legacy"));
    settings.setValue(QStringLiteral("LegacyOnly"), 42);
    settings.sync();
  }
  {
    QSettings settings(nativeSettings, QSettings::IniFormat);
    settings.setValue(QStringLiteral("CurrentInstance"), QStringLiteral("Native"));
    settings.sync();
  }

  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::Complete);
  QSettings migrated(nativeSettings, QSettings::IniFormat);
  EXPECT_EQ(migrated.value(QStringLiteral("CurrentInstance")).toString(),
            QStringLiteral("Native"));
  EXPECT_FALSE(migrated.contains(QStringLiteral("LegacyOnly")));
}

TEST_F(LegacyFlatpakMigrationTest,
       ConflictingLegacyGlobalSettingsAreNotArbitrarilySelected)
{
  const QString first =
      legacy(QStringLiteral("config/Mod Organizer Team/Mod Organizer.conf"));
  const QString second = legacy(QStringLiteral(
      ".var/app/com.fluorine.manager/config/Mod Organizer Team/Mod Organizer.conf"));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(first).dir().absolutePath()));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(second).dir().absolutePath()));
  {
    QSettings settings(first, QSettings::IniFormat);
    settings.setValue(QStringLiteral("CurrentInstance"), QStringLiteral("First"));
    settings.sync();
    ASSERT_EQ(settings.status(), QSettings::NoError);
  }
  {
    QSettings settings(second, QSettings::IniFormat);
    settings.setValue(QStringLiteral("CurrentInstance"), QStringLiteral("Second"));
    settings.sync();
    ASSERT_EQ(settings.status(), QSettings::NoError);
  }

  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::Attention);
  EXPECT_FALSE(QFileInfo::exists(
      config(QStringLiteral("Mod Organizer Team/Mod Organizer.conf"))));
  EXPECT_TRUE(QFileInfo::exists(first));
  EXPECT_TRUE(QFileInfo::exists(second));
}

TEST_F(LegacyFlatpakMigrationTest, PrefixRewriteRequiresARealPathComponentBoundary)
{
  ASSERT_TRUE(QDir().mkpath(legacy(QStringLiteral("Prefix/pfx/drive_c"))));
  const QString sibling = paths.legacyRoot + QStringLiteral("-backup/Prefix/pfx");
  makeConfig(legacy(QStringLiteral("config/fluorine/config.json")), sibling);

  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::Complete);
  EXPECT_EQ(readObject(config(QStringLiteral("fluorine/config.json")))
                .value(QStringLiteral("prefix_path"))
                .toString(),
            sibling);
}

TEST_F(LegacyFlatpakMigrationTest, RefusesSymlinkedPrefixWithoutTouchingTarget)
{
  const QString external = temporary.filePath(QStringLiteral("external"));
  ASSERT_TRUE(QDir().mkpath(QDir(external).filePath(QStringLiteral("pfx/drive_c"))));
  ASSERT_TRUE(QDir().mkpath(paths.legacyRoot));
  ASSERT_TRUE(QFile::link(external, legacy(QStringLiteral("Prefix"))));
  makeFile(QDir(external).filePath(QStringLiteral("sentinel")));

  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::Attention);
  EXPECT_TRUE(QFileInfo::exists(QDir(external).filePath(QStringLiteral("sentinel"))));
  EXPECT_TRUE(QFileInfo(legacy(QStringLiteral("Prefix"))).isSymLink());
}

TEST_F(LegacyFlatpakMigrationTest, RefusesAnInstanceBehindASymlinkedLegacyParent)
{
  const QString external = temporary.filePath(QStringLiteral("external-data"));
  makeInstance(QDir(external).filePath(QStringLiteral("ModOrganizer")),
               QStringLiteral("External"));
  ASSERT_TRUE(QDir().mkpath(paths.legacyRoot));
  ASSERT_TRUE(QFile::link(external, legacy(QStringLiteral("data"))));

  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::Attention);
  EXPECT_TRUE(
      QFileInfo::exists(QDir(external).filePath(QStringLiteral("ModOrganizer/External/"
                                                               "ModOrganizer.ini"))));
  EXPECT_FALSE(QFileInfo::exists(data(QStringLiteral("External/ModOrganizer.ini"))));
}

TEST_F(LegacyFlatpakMigrationTest, RefusesASymlinkedLegacyInstanceDirectory)
{
  const QString external = temporary.filePath(QStringLiteral("external-instance"));
  makeInstance(external, QStringLiteral("External"));
  const QString parent = legacy(QStringLiteral("data/ModOrganizer"));
  ASSERT_TRUE(QDir().mkpath(parent));
  ASSERT_TRUE(QFile::link(QDir(external).filePath(QStringLiteral("External")),
                          QDir(parent).filePath(QStringLiteral("External"))));

  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::Attention);
  EXPECT_TRUE(QFileInfo::exists(
      QDir(external).filePath(QStringLiteral("External/ModOrganizer.ini"))));
  EXPECT_FALSE(QFileInfo::exists(data(QStringLiteral("External/ModOrganizer.ini"))));
}

TEST_F(LegacyFlatpakMigrationTest, RefusesASymlinkedInstanceSettingsFile)
{
  const QString external = temporary.filePath(QStringLiteral("external.ini"));
  makeFile(external, QByteArrayLiteral("[General]\ngameName=Morrowind\n"));
  const QString instance = legacy(QStringLiteral("data/ModOrganizer/LinkedIni"));
  ASSERT_TRUE(QDir().mkpath(instance));
  ASSERT_TRUE(QFile::link(external,
                          QDir(instance).filePath(QStringLiteral("ModOrganizer.ini"))));

  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::Attention);
  EXPECT_TRUE(QFileInfo::exists(external));
  EXPECT_TRUE(QFileInfo(QDir(instance).filePath(QStringLiteral("ModOrganizer.ini")))
                  .isSymLink());
  EXPECT_FALSE(QFileInfo::exists(data(QStringLiteral("LinkedIni"))));
}

TEST_F(LegacyFlatpakMigrationTest, OverlappingLegacyLayoutsAreNotMoved)
{
  makeFile(legacy(QStringLiteral("data/ModOrganizer.ini")),
           QByteArrayLiteral("[General]\ngameName=Morrowind\n"));
  makeInstance(legacy(QStringLiteral("data/ModOrganizer")), QStringLiteral("Nested"));

  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::Attention);
  EXPECT_TRUE(QFileInfo::exists(legacy(QStringLiteral("data/ModOrganizer.ini"))));
  EXPECT_TRUE(QFileInfo::exists(
      legacy(QStringLiteral("data/ModOrganizer/Nested/ModOrganizer.ini"))));
  EXPECT_FALSE(QFileInfo::exists(data(QStringLiteral("data"))));
  EXPECT_FALSE(QFileInfo::exists(data(QStringLiteral("Nested"))));
}

TEST_F(LegacyFlatpakMigrationTest, RefusesALegacyRootBehindASymlinkedAncestor)
{
  const QString actual = temporary.filePath(QStringLiteral("actual-home"));
  const QString link   = temporary.filePath(QStringLiteral("linked-home"));
  ASSERT_TRUE(QDir().mkpath(actual));
  ASSERT_TRUE(QFile::link(actual, link));

  Paths linked      = paths;
  linked.legacyRoot = QDir(link).filePath(QStringLiteral("legacy"));
  makeInstance(linked.legacyRoot, QStringLiteral("External"));

  const auto result = LegacyFlatpakMigration::migrate(linked);
  EXPECT_EQ(result.status, Status::Attention);
  EXPECT_TRUE(QFileInfo::exists(
      QDir(linked.legacyRoot).filePath(QStringLiteral("External/ModOrganizer.ini"))));
  EXPECT_FALSE(QFileInfo::exists(data(QStringLiteral("External"))));
}

TEST_F(LegacyFlatpakMigrationTest, RefusesANativeConfigRootBehindASymlinkedAncestor)
{
  const QString actual = temporary.filePath(QStringLiteral("actual-config"));
  const QString link   = temporary.filePath(QStringLiteral("linked-config"));
  ASSERT_TRUE(QDir().mkpath(actual));
  ASSERT_TRUE(QFile::link(actual, link));

  Paths linked      = paths;
  linked.configRoot = link;
  makeConfig(legacy(QStringLiteral("config/fluorine/config.json")),
             QStringLiteral("/legacy/prefix"));

  const auto result = LegacyFlatpakMigration::migrate(linked);
  EXPECT_EQ(result.status, Status::Attention);
  EXPECT_FALSE(
      QFileInfo::exists(QDir(actual).filePath(QStringLiteral("fluorine/config.json"))));
  EXPECT_TRUE(QFileInfo::exists(legacy(QStringLiteral("config/fluorine/config.json"))));
}

TEST_F(LegacyFlatpakMigrationTest, RefusesASymlinkedSettingsDestinationDirectory)
{
  const QString oldSettings =
      legacy(QStringLiteral("config/Mod Organizer Team/Mod Organizer.conf"));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(oldSettings).dir().absolutePath()));
  {
    QSettings settings(oldSettings, QSettings::IniFormat);
    settings.setValue(QStringLiteral("Theme"), QStringLiteral("legacy"));
    settings.sync();
    ASSERT_EQ(settings.status(), QSettings::NoError);
  }

  const QString external = temporary.filePath(QStringLiteral("external-settings"));
  ASSERT_TRUE(QDir().mkpath(paths.configRoot));
  ASSERT_TRUE(QDir().mkpath(external));
  ASSERT_TRUE(QFile::link(external, config(QStringLiteral("Mod Organizer Team"))));

  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::Failed);
  EXPECT_TRUE(QDir(external).entryList(QDir::Files | QDir::Hidden).isEmpty());
  EXPECT_TRUE(QFileInfo::exists(oldSettings));
}

TEST_F(LegacyFlatpakMigrationTest, PortableInstancesRemainAtTheirExplicitPath)
{
  makeInstance(paths.legacyRoot, QStringLiteral("Portable"), true);

  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::Complete);
  EXPECT_TRUE(QFileInfo::exists(legacy(QStringLiteral("Portable/ModOrganizer.ini"))));
  EXPECT_FALSE(QFileInfo::exists(data(QStringLiteral("Portable/ModOrganizer.ini"))));
}

TEST_F(LegacyFlatpakMigrationTest, PhysicalGeneralPortableFlagRemainsPortable)
{
  const QString instance = legacy(QStringLiteral("RawPortable"));
  makeFile(QDir(instance).filePath(QStringLiteral("ModOrganizer.ini")),
           QByteArrayLiteral("[General]\nportable=true\ngameName=Morrowind\n"));

  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::Complete);
  EXPECT_TRUE(
      QFileInfo::exists(QDir(instance).filePath(QStringLiteral("ModOrganizer.ini"))));
  EXPECT_FALSE(QFileInfo::exists(data(QStringLiteral("RawPortable"))));
}

TEST_F(LegacyFlatpakMigrationTest, LockContentionFailsBeforeMutation)
{
  makeInstance(paths.legacyRoot, QStringLiteral("Waiting"));
  QLockFile lock(legacy(QStringLiteral(".fluorine-native-migration-v2.lock")));
  ASSERT_TRUE(lock.tryLock());

  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::Failed);
  EXPECT_TRUE(QFileInfo::exists(legacy(QStringLiteral("Waiting/ModOrganizer.ini"))));
}

TEST_F(LegacyFlatpakMigrationTest, HistoricalProcessBlocksMutation)
{
  makeInstance(paths.legacyRoot, QStringLiteral("Waiting"));
  QSharedMemory oldProcess(paths.legacyProcessKey);
  ASSERT_TRUE(oldProcess.create(1));

  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::Failed);
  EXPECT_TRUE(QFileInfo::exists(legacy(QStringLiteral("Waiting/ModOrganizer.ini"))));
}

TEST_F(LegacyFlatpakMigrationTest, SuccessfulMigrationReleasesHistoricalProcessGuard)
{
  makeInstance(paths.legacyRoot, QStringLiteral("Moved"));

  const auto result = LegacyFlatpakMigration::migrate(paths);
  ASSERT_EQ(result.status, Status::Complete);

  QSharedMemory nextProcess(paths.legacyProcessKey);
  EXPECT_TRUE(nextProcess.create(1)) << nextProcess.errorString().toStdString();
}

TEST_F(LegacyFlatpakMigrationTest, RecoversAStaleHistoricalProcessSegment)
{
  makeInstance(paths.legacyRoot, QStringLiteral("Recovered"));
  const pid_t child = ::fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    QSharedMemory abandoned(paths.legacyProcessKey);
    ::_exit(abandoned.create(1) ? 0 : 2);
  }
  int status = 0;
  ASSERT_EQ(::waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(WEXITSTATUS(status), 0);

  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::Complete);
  EXPECT_TRUE(QFileInfo::exists(data(QStringLiteral("Recovered/ModOrganizer.ini"))));
}

TEST_F(LegacyFlatpakMigrationTest, UnownedAttentionReportIsNeverOverwritten)
{
  ASSERT_TRUE(QDir().mkpath(legacy(QStringLiteral("Prefix/pfx/drive_c"))));
  ASSERT_TRUE(QDir().mkpath(data(QStringLiteral("Prefix/pfx/drive_c"))));
  const QString fixed = data(QStringLiteral("legacy-flatpak-migration-attention.txt"));
  makeFile(fixed, QByteArrayLiteral("user-owned contents\n"));

  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::Attention);
  EXPECT_NE(result.attentionReport, fixed);
  QFile preserved(fixed);
  ASSERT_TRUE(preserved.open(QIODevice::ReadOnly));
  EXPECT_EQ(preserved.readAll(), QByteArrayLiteral("user-owned contents\n"));
  EXPECT_TRUE(QFileInfo::exists(result.attentionReport));
}

TEST_F(LegacyFlatpakMigrationTest, PackageRuntimeAndLogsAreRetainedAndReported)
{
  makeFile(legacy(QStringLiteral("bin/fluorine-manager")));
  makeFile(legacy(QStringLiteral("plugins/custom-plugin.so")));
  makeFile(legacy(QStringLiteral("dlls/custom-runtime.dll")));
  makeFile(legacy(QStringLiteral("lib/custom-runtime.so")));
  makeFile(legacy(QStringLiteral("logs/manager.log")));

  const auto result = LegacyFlatpakMigration::migrate(paths);
  EXPECT_EQ(result.status, Status::Complete);
  EXPECT_FALSE(result.diagnostics.isEmpty());
  EXPECT_TRUE(QFileInfo::exists(legacy(QStringLiteral("bin/fluorine-manager"))));
  EXPECT_TRUE(QFileInfo::exists(legacy(QStringLiteral("plugins/custom-plugin.so"))));
  EXPECT_TRUE(QFileInfo::exists(legacy(QStringLiteral("dlls/custom-runtime.dll"))));
  EXPECT_TRUE(QFileInfo::exists(legacy(QStringLiteral("lib/custom-runtime.so"))));
  EXPECT_TRUE(QFileInfo::exists(legacy(QStringLiteral("logs/manager.log"))));
  EXPECT_TRUE(QFileInfo::exists(result.attentionReport));
  EXPECT_TRUE(QFileInfo::exists(LegacyFlatpakMigration::completionMarkerPath(paths)));
}

}  // namespace
