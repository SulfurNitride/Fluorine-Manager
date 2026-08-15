#include <uibase/registry.h>
#include <uibase/log.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <utility>

#ifdef Q_OS_UNIX
#include <csignal>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{

bool writeBytes(const QString& path, QByteArrayView bytes)
{
  QFile file(path);
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
         file.write(bytes.data(), bytes.size()) == bytes.size() && file.flush();
}

QByteArray readBytes(const QString& path)
{
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

} // namespace

int main(int argc, char** argv)
{
  QCoreApplication application(argc, argv);
  MOBase::log::LoggerConfiguration logging;
  logging.name = "test_registry";
  logging.maxLevel = MOBase::log::Warning;
  MOBase::log::createDefault(std::move(logging));
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(Registry, ReplacesOneValueWithoutNormalizingTheIni)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("Game.ini");
  const QByteArray original = "\xEF\xBB\xBF[General]\r\n  sLocalSavePath =Old\\Path\r\n"
                              "# comment\n[Other]\r\nKeep=1";
  const QByteArray expected = "\xEF\xBB\xBF[General]\r\n  sLocalSavePath =New\\Path\r\n"
                              "# comment\n[Other]\r\nKeep=1";
  ASSERT_TRUE(writeBytes(path, original));

  ASSERT_TRUE(
      MOBase::WriteRegistryValue("general", "SLOCALSAVEPATH", "New\\Path", path));
  EXPECT_EQ(readBytes(path), expected);
}

TEST(Registry, InsertsIntoExistingAndMissingSectionsByteExactly)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString existing = temporary.filePath("existing.ini");
  ASSERT_TRUE(writeBytes(existing, "[General]\r\nAdjacent=1\n[Other]\r\nX=Y\r\n"));
  ASSERT_TRUE(MOBase::WriteRegistryValue("General", "New Key", "value", existing));
  EXPECT_EQ(readBytes(existing),
            "[General]\r\nNew Key=value\r\nAdjacent=1\n[Other]\r\nX=Y\r\n");

  const QString missing = temporary.filePath("missing-section.ini");
  ASSERT_TRUE(writeBytes(missing, "Root=1"));
  ASSERT_TRUE(MOBase::WriteRegistryValue("Section", "Key", "Value", missing));
  EXPECT_EQ(readBytes(missing), "Root=1\n\n[Section]\nKey=Value");
}

TEST(Registry, RemovesOnlyTheFirstMatchingSectionAndKey)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("Game.ini");
  ASSERT_TRUE(writeBytes(path, "[General]\r\nKey=one\r\nAdjacent=keep\n"
                               "[GENERAL]\r\nKey=two"));

  ASSERT_TRUE(MOBase::RemoveRegistryValue("general", "KEY", path));
  EXPECT_EQ(readBytes(path), "[General]\r\nAdjacent=keep\n[GENERAL]\r\nKey=two");
}

TEST(Registry, NoOpRemovalDoesNotRepublishTheFile)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("Game.ini");
  ASSERT_TRUE(writeBytes(path, "[General]\nAdjacent=keep\n"));
  const QFileInfo before(path);

  ASSERT_TRUE(MOBase::RemoveRegistryValue("General", "Missing", path));
  const QFileInfo after(path);
  EXPECT_EQ(readBytes(path), "[General]\nAdjacent=keep\n");
#ifdef Q_OS_UNIX
  struct stat beforeStatus;
  struct stat afterStatus;
  ASSERT_EQ(::stat(QFile::encodeName(path).constData(), &beforeStatus), 0);
  ASSERT_TRUE(MOBase::RemoveRegistryValue("Other", "Missing", path));
  ASSERT_EQ(::stat(QFile::encodeName(path).constData(), &afterStatus), 0);
  EXPECT_EQ(beforeStatus.st_ino, afterStatus.st_ino);
#else
  EXPECT_EQ(before.lastModified(), after.lastModified());
#endif
}

TEST(Registry, RejectsSyntaxInjection)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("Game.ini");
  ASSERT_TRUE(writeBytes(path, "[General]\nKey=old\n"));

  EXPECT_FALSE(MOBase::WriteRegistryValue("General", "Key", "new\nOther=1", path));
  EXPECT_FALSE(MOBase::RemoveRegistryValue("General\n[Other]", "Key", path));
  EXPECT_FALSE(MOBase::WriteRegistryValue("General]", "Key", "new", path));
  EXPECT_FALSE(MOBase::WriteRegistryValue(" General", "Key", "new", path));
  EXPECT_FALSE(MOBase::WriteRegistryValue("General", " Key", "new", path));
  EXPECT_FALSE(MOBase::WriteRegistryValue("General", "Other=Key", "new", path));
  EXPECT_EQ(readBytes(path), "[General]\nKey=old\n");
}

#ifdef Q_OS_UNIX
TEST(Registry, SupportsStrictCaseAliasAndRejectsUnsafeLeaves)
{
  QTemporaryDir temporary;
  QTemporaryDir external;
  ASSERT_TRUE(temporary.isValid());
  ASSERT_TRUE(external.isValid());

  const QString target = temporary.filePath("Game.ini");
  const QString alias = temporary.filePath("game.ini");
  ASSERT_TRUE(writeBytes(target, "[General]\nKey=old\n"));
  ASSERT_EQ(::symlink("Game.ini", QFile::encodeName(alias).constData()), 0);
  ASSERT_TRUE(MOBase::WriteRegistryValue("General", "Key", "new", alias));
  EXPECT_TRUE(QFileInfo(alias).isSymLink());
  EXPECT_EQ(readBytes(target), "[General]\nKey=new\n");

  const QString outside = external.filePath("outside.ini");
  const QString linked = temporary.filePath("linked.ini");
  ASSERT_TRUE(writeBytes(outside, "[General]\nKey=sentinel\n"));
  ASSERT_EQ(::symlink(QFile::encodeName(outside).constData(),
                      QFile::encodeName(linked).constData()),
            0);
  EXPECT_FALSE(MOBase::WriteRegistryValue("General", "Key", "changed", linked));
  EXPECT_EQ(readBytes(outside), "[General]\nKey=sentinel\n");

  const QString fifo = temporary.filePath("pipe.ini");
  ASSERT_EQ(::mkfifo(QFile::encodeName(fifo).constData(), 0600), 0);
  EXPECT_FALSE(MOBase::RemoveRegistryValue("General", "Key", fifo));
  struct stat status;
  ASSERT_EQ(::lstat(QFile::encodeName(fifo).constData(), &status), 0);
  EXPECT_TRUE(S_ISFIFO(status.st_mode));
}

TEST(Registry, ForcedShortRemovalPreservesTheOldGeneration)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("Game.ini");
  const QByteArray original =
      "[General]\nRemove=1\n" + QByteArray(1024 * 1024, 'x') + '\n';
  ASSERT_TRUE(writeBytes(path, original));

  const pid_t child = ::fork();
  ASSERT_GE(child, 0);
  if (child == 0)
  {
    std::signal(SIGXFSZ, SIG_IGN);
    struct rlimit limit = {4096, 4096};
    if (::setrlimit(RLIMIT_FSIZE, &limit) != 0)
    {
      _exit(2);
    }
    _exit(MOBase::RemoveRegistryValue("General", "Remove", path) ? 3 : 0);
  }

  int status = 0;
  ASSERT_EQ(::waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(WEXITSTATUS(status), 0);
  EXPECT_EQ(readBytes(path), original);
}
#endif
