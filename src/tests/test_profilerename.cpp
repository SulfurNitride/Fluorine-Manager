#include "profilerename.h"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

int main(int argc, char** argv)
{
  QCoreApplication application(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

namespace
{

bool writeBytes(const QString& path, QByteArrayView contents)
{
  QFile file(path);
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
         file.write(contents.data(), contents.size()) == contents.size() && file.flush();
}

QByteArray readBytes(const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
  {
    return {};
  }
  return file.readAll();
}

struct ProfileFixture
{
  QTemporaryDir temporary;
  QString source;
  QString settingsPath;
  std::unique_ptr<QSettings> settings;

  ProfileFixture()
  {
    source = temporary.filePath("Alpha");
    EXPECT_TRUE(QDir().mkdir(source));
    settingsPath = QDir(source).filePath("settings.ini");
    settings = std::make_unique<QSettings>(settingsPath, QSettings::IniFormat);
  }
};

}  // namespace

TEST(ProfileRename, RenamesAndRebindsPendingSettings)
{
  ProfileFixture fixture;
  ASSERT_TRUE(fixture.temporary.isValid());
  fixture.settings->setValue("LocalSaves", true);

  auto result = ProfileRename::apply(QDir(fixture.source), *fixture.settings, "Renamed");
  ASSERT_EQ(result.status, ProfileRename::Status::Renamed) << qUtf8Printable(result.error);
  ASSERT_NE(result.replacementSettings, nullptr);
  EXPECT_FALSE(QFileInfo::exists(fixture.source));
  EXPECT_TRUE(QFileInfo(result.targetPath).isDir());
  EXPECT_TRUE(result.replacementSettings->value("LocalSaves").toBool());

  result.replacementSettings->setValue("LocalSettings", true);
  result.replacementSettings->sync();
  ASSERT_EQ(result.replacementSettings->status(), QSettings::NoError);
  QSettings verify(QDir(result.targetPath).filePath("settings.ini"), QSettings::IniFormat);
  EXPECT_TRUE(verify.value("LocalSaves").toBool());
  EXPECT_TRUE(verify.value("LocalSettings").toBool());
  fixture.settings.reset();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);
  QCoreApplication::processEvents();
  EXPECT_FALSE(QFileInfo::exists(fixture.settingsPath));
}

TEST(ProfileRename, ExistingDestinationPreservesBothProfiles)
{
  ProfileFixture fixture;
  ASSERT_TRUE(fixture.temporary.isValid());
  fixture.settings->setValue("Owner", "Alpha");
  fixture.settings->sync();

  const QString destination = fixture.temporary.filePath("Beta");
  ASSERT_TRUE(QDir().mkdir(destination));
  const QString sentinel = QDir(destination).filePath("sentinel.txt");
  ASSERT_TRUE(writeBytes(sentinel, "beta"));

  const auto result = ProfileRename::apply(QDir(fixture.source), *fixture.settings, "Beta");
  EXPECT_EQ(result.status, ProfileRename::Status::DestinationExists);
  EXPECT_TRUE(QFileInfo(fixture.source).isDir());
  EXPECT_EQ(readBytes(sentinel), QByteArray("beta"));
  EXPECT_EQ(fixture.settings->value("Owner").toString(), QStringLiteral("Alpha"));
}

TEST(ProfileRename, CaseOnlyRenameSucceedsButSiblingAliasIsRejected)
{
  ProfileFixture ownCase;
  ASSERT_TRUE(ownCase.temporary.isValid());

  auto ownAlias = ProfileRename::apply(QDir(ownCase.source), *ownCase.settings, "alpha");
  EXPECT_EQ(ownAlias.status, ProfileRename::Status::Renamed)
      << qUtf8Printable(ownAlias.error);
  EXPECT_TRUE(QFileInfo(ownCase.temporary.filePath("alpha")).isDir());
  EXPECT_FALSE(QFileInfo::exists(ownCase.source));

  ProfileFixture siblingCase;
  ASSERT_TRUE(siblingCase.temporary.isValid());
  const QString sibling = siblingCase.temporary.filePath("BETA");
  ASSERT_TRUE(QDir().mkdir(sibling));
  auto siblingAlias =
      ProfileRename::apply(QDir(siblingCase.source), *siblingCase.settings, "beta");
  EXPECT_EQ(siblingAlias.status, ProfileRename::Status::DestinationExists);
  EXPECT_TRUE(QFileInfo(sibling).isDir());
}

TEST(ProfileRename, UnicodeNameIsPreserved)
{
  ProfileFixture fixture;
  ASSERT_TRUE(fixture.temporary.isValid());

  auto result = ProfileRename::apply(QDir(fixture.source), *fixture.settings,
                                     QStringLiteral("Prófil 新"));
  ASSERT_EQ(result.status, ProfileRename::Status::Renamed) << qUtf8Printable(result.error);
  EXPECT_EQ(QFileInfo(result.targetPath).fileName(), QStringLiteral("Prófil 新"));
}

TEST(ProfileRename, InvalidNameAndMismatchedSettingsDoNotMoveSource)
{
  ProfileFixture fixture;
  ASSERT_TRUE(fixture.temporary.isValid());

  auto invalid = ProfileRename::apply(QDir(fixture.source), *fixture.settings, "../Beta");
  EXPECT_EQ(invalid.status, ProfileRename::Status::InvalidName);

  QSettings other(fixture.temporary.filePath("other.ini"), QSettings::IniFormat);
  auto mismatch = ProfileRename::apply(QDir(fixture.source), other, "Beta");
  EXPECT_EQ(mismatch.status, ProfileRename::Status::SettingsMismatch);
  EXPECT_TRUE(QFileInfo(fixture.source).isDir());
  EXPECT_FALSE(QFileInfo::exists(fixture.temporary.filePath("Beta")));
}

TEST(ProfileRename, SettingsSyncFailurePreservesSource)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString source = temporary.filePath("Alpha");
  ASSERT_TRUE(QDir().mkdir(source));
  const QString settingsPath = QDir(source).filePath("settings.ini");
  ASSERT_TRUE(QDir().mkdir(settingsPath));
  QSettings settings(settingsPath, QSettings::IniFormat);
  settings.setValue("Pending", true);

  const auto result = ProfileRename::apply(QDir(source), settings, "Beta");
  EXPECT_EQ(result.status, ProfileRename::Status::SettingsSyncFailed);
  EXPECT_TRUE(QFileInfo(source).isDir());
  EXPECT_FALSE(QFileInfo::exists(temporary.filePath("Beta")));
}

#ifdef Q_OS_UNIX
TEST(ProfileRename, SymlinkSourceAndDanglingDestinationAreRejected)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString real = temporary.filePath("real");
  const QString source = temporary.filePath("Alpha");
  ASSERT_TRUE(QDir().mkdir(real));
  ASSERT_EQ(::symlink("real", QFile::encodeName(source).constData()), 0);
  QSettings settings(QDir(source).filePath("settings.ini"), QSettings::IniFormat);

  auto sourceResult = ProfileRename::apply(QDir(source), settings, "Beta");
  EXPECT_EQ(sourceResult.status, ProfileRename::Status::SourceUnavailable);
  EXPECT_TRUE(QFileInfo(source).isSymLink());

  ASSERT_TRUE(QFile::remove(source));
  ASSERT_TRUE(QDir().mkdir(source));
  QSettings regularSettings(QDir(source).filePath("settings.ini"), QSettings::IniFormat);
  const QString destination = temporary.filePath("Beta");
  ASSERT_EQ(::symlink("missing", QFile::encodeName(destination).constData()), 0);

  auto destinationResult = ProfileRename::apply(QDir(source), regularSettings, "Beta");
  EXPECT_EQ(destinationResult.status, ProfileRename::Status::DestinationExists);
  EXPECT_TRUE(QFileInfo(destination).isSymLink());
  EXPECT_TRUE(QFileInfo(source).isDir());
}
#endif
