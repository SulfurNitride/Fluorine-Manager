#include <strings.h>

#include "wineprefix.h"

#include <gtest/gtest.h>
#include <uibase/log.h>

namespace
{
void writeFile(const QString& path, const QString& contents)
{
  ASSERT_TRUE(QDir().mkpath(QFileInfo(path).absolutePath()));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  QTextStream(&file) << contents;
}
}  // namespace

class CaptureNodeTest : public ::testing::Test
{
protected:
  static void SetUpTestSuite()
  {
    MOBase::log::LoggerConfiguration configuration;
    configuration.name = "test_capture_node";
    MOBase::log::createDefault(configuration);
  }

  void SetUp() override
  {
    ASSERT_TRUE(m_tempDir.isValid());
    m_prefix = m_tempDir.path();
    ASSERT_TRUE(QDir().mkpath(QDir(m_prefix).filePath("drive_c")));
  }

  QTemporaryDir m_tempDir;
  QString m_prefix;
};

TEST_F(CaptureNodeTest, NestedDirectoriesAreRelinked)
{
  WinePrefix prefix(m_prefix);
  ASSERT_TRUE(prefix.isValid());

  const QString hostRoot =
      QDir(m_prefix).filePath(
          "drive_c/users/steamuser/AppData/Local/Game/Profiles");
  const QString profileRoot =
      QDir(m_prefix).filePath("profiles/P1/Local/Game/Profiles");

  writeFile(QDir(hostRoot).filePath("Public/Saves/Story/Hero/save01.sav"),
            "save data");
  writeFile(QDir(hostRoot).filePath("Public/Saves/Story/Villain/save02.sav"),
            "other save");

  ASSERT_TRUE(prefix.captureNode(hostRoot, profileRoot));

  const QFileInfo hostInfo(hostRoot);
  ASSERT_TRUE(hostInfo.isSymLink());
  EXPECT_EQ(hostInfo.symLinkTarget(), profileRoot);

  EXPECT_TRUE(
      QFile::exists(QDir(profileRoot).filePath(
          "Public/Saves/Story/Hero/save01.sav")));
  EXPECT_TRUE(
      QFile::exists(QDir(profileRoot).filePath(
          "Public/Saves/Story/Villain/save02.sav")));

  QFile saveFile(
      QDir(hostRoot).filePath("Public/Saves/Story/Hero/save01.sav"));
  ASSERT_TRUE(saveFile.open(QIODevice::ReadOnly));
  EXPECT_EQ(saveFile.readAll(), "save data");
}

TEST_F(CaptureNodeTest, CaptureIsIdempotent)
{
  WinePrefix prefix(m_prefix);
  ASSERT_TRUE(prefix.isValid());

  const QString hostRoot =
      QDir(m_prefix).filePath(
          "drive_c/users/steamuser/AppData/Local/Game/Profiles");
  const QString profileRoot =
      QDir(m_prefix).filePath("profiles/P1/Local/Game/Profiles");

  writeFile(QDir(hostRoot).filePath("Public/Saves/Story/Hero/save01.sav"),
            "save data");

  ASSERT_TRUE(prefix.captureNode(hostRoot, profileRoot));

  ASSERT_TRUE(prefix.captureNode(hostRoot, profileRoot));

  const QFileInfo hostInfo(hostRoot);
  EXPECT_TRUE(hostInfo.isSymLink());
  EXPECT_EQ(hostInfo.symLinkTarget(), profileRoot);

  QFile saveFile(
      QDir(hostRoot).filePath("Public/Saves/Story/Hero/save01.sav"));
  ASSERT_TRUE(saveFile.open(QIODevice::ReadOnly));
  EXPECT_EQ(saveFile.readAll(), "save data");
}

TEST_F(CaptureNodeTest, UncaptureRemovesSymlinkAndPersistsProfile)
{
  WinePrefix prefix(m_prefix);
  ASSERT_TRUE(prefix.isValid());

  const QString hostRoot =
      QDir(m_prefix).filePath(
          "drive_c/users/steamuser/AppData/Local/Game/Profiles");
  const QString profileRoot =
      QDir(m_prefix).filePath("profiles/P1/Local/Game/Profiles");

  writeFile(QDir(hostRoot).filePath("Public/Saves/Story/Hero/save01.sav"),
            "save data");

  ASSERT_TRUE(prefix.captureNode(hostRoot, profileRoot));
  ASSERT_TRUE(QFileInfo(hostRoot).isSymLink());

  prefix.uncaptureNode(hostRoot);

  EXPECT_FALSE(QFileInfo(hostRoot).isSymLink());
  EXPECT_FALSE(QFileInfo(hostRoot).exists());

  EXPECT_TRUE(
      QFile::exists(QDir(profileRoot).filePath(
          "Public/Saves/Story/Hero/save01.sav")));
}

TEST_F(CaptureNodeTest, SecondCaptureSeesOldProfileSymlink)
{
  WinePrefix prefix(m_prefix);
  ASSERT_TRUE(prefix.isValid());

  const QString hostRoot =
      QDir(m_prefix).filePath(
          "drive_c/users/steamuser/AppData/Local/Game/Profiles");
  const QString profileRoot1 =
      QDir(m_prefix).filePath("profiles/P1/Local/Game/Profiles");
  const QString profileRoot2 =
      QDir(m_prefix).filePath("profiles/P2/Local/Game/Profiles");

  writeFile(QDir(hostRoot).filePath("Public/Saves/Story/Hero/save01.sav"),
            "save data");

  ASSERT_TRUE(prefix.captureNode(hostRoot, profileRoot1));
  ASSERT_TRUE(QFileInfo(hostRoot).isSymLink());

  prefix.uncaptureNode(hostRoot);

  ASSERT_TRUE(prefix.captureNode(hostRoot, profileRoot2));
  ASSERT_TRUE(QFileInfo(hostRoot).isSymLink());
  EXPECT_EQ(QFileInfo(hostRoot).symLinkTarget(), profileRoot2);

  EXPECT_TRUE(
      QFile::exists(QDir(profileRoot1).filePath(
          "Public/Saves/Story/Hero/save01.sav")));
  EXPECT_FALSE(
      QFile::exists(QDir(profileRoot2).filePath(
          "Public/Saves/Story/Hero/save01.sav")));
}
