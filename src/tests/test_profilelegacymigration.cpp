#include "profilelegacymigration.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <atomic>

#ifdef Q_OS_UNIX
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

constexpr auto IntentKey = "LegacyMigration/Test";
constexpr auto FinalKey = "LocalSaves";
constexpr auto Operation = "test-move";

std::atomic_bool rejectSettingsWrites{false};

bool readCustomSettings(QIODevice &device, QSettings::SettingsMap &values) {
  QDataStream stream(&device);
  stream >> values;
  return stream.status() == QDataStream::Ok;
}

bool writeCustomSettings(QIODevice &device,
                         const QSettings::SettingsMap &values) {
  if (rejectSettingsWrites.load()) {
    return false;
  }
  QDataStream stream(&device);
  stream << values;
  return stream.status() == QDataStream::Ok;
}

QSettings::Format failureInjectingFormat() {
  static const QSettings::Format format = QSettings::registerFormat(
      QStringLiteral("profile-legacy-migration-test"), readCustomSettings,
      writeCustomSettings, Qt::CaseSensitive);
  return format;
}

bool writeBytes(const QString &path, QByteArrayView contents) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
         file.write(contents.data(), contents.size()) == contents.size() &&
         file.flush();
}

QByteArray readBytes(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

ProfileLegacyMigration::Result
migrate(QSettings &settings, const QString &root, const QString &source,
        const QString &destination, ProfileLegacyMigration::EntryKind kind,
        const ProfileLegacyMigration::Hooks *hooks = nullptr) {
  return ProfileLegacyMigration::migrate(settings, root, IntentKey, Operation,
                                         source, destination, kind, FinalKey,
                                         true, hooks);
}

} // namespace

TEST(ProfileLegacyMigration, MovesDirectoryAndCommitsSetting) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString source = temporary.filePath("saves");
  ASSERT_TRUE(QDir().mkdir(source));
  ASSERT_TRUE(writeBytes(QDir(source).filePath("slot.sav"), "save-generation"));

  QSettings settings(temporary.filePath("settings.ini"), QSettings::IniFormat);
  const auto result = migrate(settings, temporary.path(), "saves", "_saves",
                              ProfileLegacyMigration::EntryKind::Directory);
  ASSERT_TRUE(result.succeeded()) << qUtf8Printable(result.error);
  EXPECT_FALSE(QFileInfo::exists(source));
  EXPECT_EQ(readBytes(temporary.filePath("_saves/slot.sav")),
            QByteArray("save-generation"));
  EXPECT_TRUE(settings.value(FinalKey).toBool());
  EXPECT_FALSE(settings.contains(IntentKey));

  QSettings verify(temporary.filePath("settings.ini"), QSettings::IniFormat);
  EXPECT_TRUE(verify.value(FinalKey).toBool());
  EXPECT_FALSE(verify.contains(IntentKey));
}

TEST(ProfileLegacyMigration, MovesFileWithoutChangingBytes) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  ASSERT_TRUE(
      writeBytes(temporary.filePath("Game.ini_"), "[Game]\r\nValue=1\r\n"));

  QSettings settings(temporary.filePath("settings.ini"), QSettings::IniFormat);
  const auto result =
      migrate(settings, temporary.path(), "Game.ini_", "Game.ini",
              ProfileLegacyMigration::EntryKind::File);
  ASSERT_TRUE(result.succeeded()) << qUtf8Printable(result.error);
  EXPECT_FALSE(QFileInfo::exists(temporary.filePath("Game.ini_")));
  EXPECT_EQ(readBytes(temporary.filePath("Game.ini")),
            QByteArray("[Game]\r\nValue=1\r\n"));
}

TEST(ProfileLegacyMigration, CollisionIsPreservedAndRetryable) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString source = temporary.filePath("saves");
  const QString destination = temporary.filePath("_saves");
  ASSERT_TRUE(QDir().mkdir(source));
  ASSERT_TRUE(QDir().mkdir(destination));
  ASSERT_TRUE(writeBytes(QDir(source).filePath("source.sav"), "source"));
  ASSERT_TRUE(writeBytes(QDir(destination).filePath("foreign.sav"), "foreign"));

  QSettings settings(temporary.filePath("settings.ini"), QSettings::IniFormat);
  auto result = migrate(settings, temporary.path(), "saves", "_saves",
                        ProfileLegacyMigration::EntryKind::Directory);
  EXPECT_EQ(result.status, ProfileLegacyMigration::Status::DestinationExists);
  EXPECT_EQ(readBytes(QDir(source).filePath("source.sav")),
            QByteArray("source"));
  EXPECT_EQ(readBytes(QDir(destination).filePath("foreign.sav")),
            QByteArray("foreign"));
  EXPECT_FALSE(settings.contains(FinalKey));
  EXPECT_EQ(ProfileLegacyMigration::pendingOperation(settings, IntentKey),
            QString(Operation));

  ASSERT_TRUE(QFile::remove(QDir(destination).filePath("foreign.sav")));
  ASSERT_TRUE(QDir().rmdir(destination));
  result = migrate(settings, temporary.path(), "saves", "_saves",
                   ProfileLegacyMigration::EntryKind::Directory);
  ASSERT_TRUE(result.succeeded()) << qUtf8Printable(result.error);
  EXPECT_EQ(readBytes(QDir(destination).filePath("source.sav")),
            QByteArray("source"));
  EXPECT_FALSE(settings.contains(IntentKey));
}

TEST(ProfileLegacyMigration, UnsafeEntriesAreNeverMoved) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString outside = temporary.filePath("outside");
  ASSERT_TRUE(QDir().mkdir(outside));
  ASSERT_TRUE(QFile::link(outside, temporary.filePath("saves")));

  QSettings settings(temporary.filePath("settings.ini"), QSettings::IniFormat);
  const auto result = migrate(settings, temporary.path(), "saves", "_saves",
                              ProfileLegacyMigration::EntryKind::Directory);
  EXPECT_EQ(result.status, ProfileLegacyMigration::Status::UnsafeEntry);
  EXPECT_TRUE(QFileInfo(temporary.filePath("saves")).isSymLink());
  EXPECT_TRUE(QFileInfo(outside).isDir());
  EXPECT_FALSE(QFileInfo::exists(temporary.filePath("_saves")));
  EXPECT_FALSE(settings.contains(FinalKey));
}

TEST(ProfileLegacyMigration, FailedIntentPublicationLeavesSourceUntouched) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  ASSERT_TRUE(QDir().mkdir(temporary.filePath("saves")));
  ASSERT_TRUE(writeBytes(temporary.filePath("saves/slot.sav"), "preserve"));

  {
    QSettings settings(temporary.filePath("settings.failure"),
                       failureInjectingFormat());
    rejectSettingsWrites.store(true);
    const auto result = migrate(settings, temporary.path(), "saves", "_saves",
                                ProfileLegacyMigration::EntryKind::Directory);
    EXPECT_EQ(result.status,
              ProfileLegacyMigration::Status::SettingsUnavailable);
    EXPECT_EQ(readBytes(temporary.filePath("saves/slot.sav")),
              QByteArray("preserve"));
    EXPECT_FALSE(QFileInfo::exists(temporary.filePath("_saves")));
    EXPECT_FALSE(settings.contains(FinalKey));
  }
  rejectSettingsWrites.store(false);
}

TEST(ProfileLegacyMigration, MalformedIntentFailsClosed) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  ASSERT_TRUE(QDir().mkdir(temporary.filePath("saves")));
  QSettings settings(temporary.filePath("settings.ini"), QSettings::IniFormat);
  settings.setValue(IntentKey, "not-an-intent");
  settings.sync();

  EXPECT_EQ(ProfileLegacyMigration::pendingOperation(settings, IntentKey),
            QStringLiteral("<invalid>"));
  const auto result = migrate(settings, temporary.path(), "saves", "_saves",
                              ProfileLegacyMigration::EntryKind::Directory);
  EXPECT_EQ(result.status, ProfileLegacyMigration::Status::InvalidRequest);
  EXPECT_TRUE(QFileInfo(temporary.filePath("saves")).isDir());
  EXPECT_FALSE(QFileInfo::exists(temporary.filePath("_saves")));
}

#ifdef Q_OS_UNIX
TEST(ProfileLegacyMigration, ResumesAfterCrashWithPersistedIntent) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  ASSERT_TRUE(QDir().mkdir(temporary.filePath("saves")));
  ASSERT_TRUE(writeBytes(temporary.filePath("saves/slot.sav"), "intent-crash"));

  const pid_t child = ::fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    QSettings settings(temporary.filePath("settings.ini"),
                       QSettings::IniFormat);
    ProfileLegacyMigration::Hooks hooks;
    hooks.afterIntentPersisted = [] { ::_exit(71); };
    (void)migrate(settings, temporary.path(), "saves", "_saves",
                  ProfileLegacyMigration::EntryKind::Directory, &hooks);
    ::_exit(72);
  }
  int status = 0;
  ASSERT_EQ(::waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(WEXITSTATUS(status), 71);

  QSettings settings(temporary.filePath("settings.ini"), QSettings::IniFormat);
  EXPECT_EQ(ProfileLegacyMigration::pendingOperation(settings, IntentKey),
            QString(Operation));
  const auto result = migrate(settings, temporary.path(), "saves", "_saves",
                              ProfileLegacyMigration::EntryKind::Directory);
  ASSERT_TRUE(result.succeeded()) << qUtf8Printable(result.error);
  EXPECT_EQ(readBytes(temporary.filePath("_saves/slot.sav")),
            QByteArray("intent-crash"));
}

TEST(ProfileLegacyMigration, ResumesAfterCrashFollowingDurableRename) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  ASSERT_TRUE(QDir().mkdir(temporary.filePath("saves")));
  ASSERT_TRUE(writeBytes(temporary.filePath("saves/slot.sav"), "rename-crash"));

  const pid_t child = ::fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    QSettings settings(temporary.filePath("settings.ini"),
                       QSettings::IniFormat);
    ProfileLegacyMigration::Hooks hooks;
    hooks.afterRenamePublished = [] { ::_exit(73); };
    (void)migrate(settings, temporary.path(), "saves", "_saves",
                  ProfileLegacyMigration::EntryKind::Directory, &hooks);
    ::_exit(74);
  }
  int status = 0;
  ASSERT_EQ(::waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(WEXITSTATUS(status), 73);

  EXPECT_FALSE(QFileInfo::exists(temporary.filePath("saves")));
  EXPECT_EQ(readBytes(temporary.filePath("_saves/slot.sav")),
            QByteArray("rename-crash"));
  QSettings settings(temporary.filePath("settings.ini"), QSettings::IniFormat);
  EXPECT_EQ(ProfileLegacyMigration::pendingOperation(settings, IntentKey),
            QString(Operation));
  const auto result = migrate(settings, temporary.path(), "saves", "_saves",
                              ProfileLegacyMigration::EntryKind::Directory);
  ASSERT_TRUE(result.succeeded()) << qUtf8Printable(result.error);
  EXPECT_TRUE(settings.value(FinalKey).toBool());
  EXPECT_FALSE(settings.contains(IntentKey));
}
#endif
