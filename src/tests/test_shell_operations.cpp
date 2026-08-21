#include <uibase/utility.h>

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTemporaryDir>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace
{
void writeFile(const QString& path, const QByteArray& contents)
{
  ASSERT_TRUE(QDir().mkpath(QFileInfo(path).absolutePath()));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write(contents), contents.size());
}

QByteArray readFile(const QString& path)
{
  QFile file(path);
  EXPECT_TRUE(file.open(QIODevice::ReadOnly));
  return file.readAll();
}
}  // namespace

TEST(ShellMove, ReplacesFilesAndRecursivelyMergesDirectories)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString source = QDir(temporary.path()).filePath("overwrite");
  const QString destination = QDir(temporary.path()).filePath("mod");
  writeFile(QDir(source).filePath("nested/replaced.txt"), "new");
  writeFile(QDir(source).filePath("nested/added.txt"), "added");
  writeFile(QDir(destination).filePath("nested/replaced.txt"), "old");
  writeFile(QDir(destination).filePath("keep.txt"), "keep");

  ASSERT_TRUE(MOBase::shellMove(QStringList{source}, QStringList{destination}));
  EXPECT_FALSE(QFileInfo::exists(source));
  EXPECT_EQ(readFile(QDir(destination).filePath("nested/replaced.txt")), "new");
  EXPECT_EQ(readFile(QDir(destination).filePath("nested/added.txt")), "added");
  EXPECT_EQ(readFile(QDir(destination).filePath("keep.txt")), "keep");
}

TEST(ShellMove, MovesDirectoryContentsWithoutRemovingContainer)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString source = QDir(temporary.path()).filePath("overwrite");
  const QString destination = QDir(temporary.path()).filePath("mod");
  writeFile(QDir(source).filePath("nested/file.txt"), "contents");
  writeFile(QDir(destination).filePath("existing.txt"), "keep");

  const QFileInfoList entries =
      QDir(source).entryInfoList(QDir::AllEntries | QDir::Hidden |
                                  QDir::System | QDir::NoDotAndDotDot);
  QStringList sourcePaths;
  QStringList destinationPaths;
  for (const QFileInfo &entry : entries) {
    sourcePaths.append(entry.absoluteFilePath());
    destinationPaths.append(QDir(destination).filePath(entry.fileName()));
  }

  ASSERT_TRUE(MOBase::shellMove(sourcePaths, destinationPaths));
  EXPECT_TRUE(QFileInfo(source).isDir());
  EXPECT_TRUE(QDir(source).entryInfoList(QDir::AllEntries | QDir::Hidden |
                                         QDir::System | QDir::NoDotAndDotDot)
                  .isEmpty());
  EXPECT_EQ(readFile(QDir(destination).filePath("nested/file.txt")),
            "contents");
  EXPECT_EQ(readFile(QDir(destination).filePath("existing.txt")), "keep");
}

TEST(ShellMove, ReplacesSingleExistingFile)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString source = QDir(temporary.path()).filePath("source.txt");
  const QString destination = QDir(temporary.path()).filePath("destination.txt");
  writeFile(source, "new");
  writeFile(destination, "old");

  ASSERT_TRUE(MOBase::shellMove(source, destination));
  EXPECT_FALSE(QFileInfo::exists(source));
  EXPECT_EQ(readFile(destination), "new");
}

TEST(ShellMove, ReportsFailureWithoutClaimingSuccess)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString source = QDir(temporary.path()).filePath("source.txt");
  const QString blockedParent = QDir(temporary.path()).filePath("blocked");
  writeFile(source, "contents");
  writeFile(blockedParent, "not a directory");

  EXPECT_FALSE(MOBase::shellMove(
      source, QDir(blockedParent).filePath("destination.txt")));
  EXPECT_TRUE(QFileInfo::exists(source));
  EXPECT_EQ(readFile(source), "contents");
}

#ifndef _WIN32
TEST(ShellMove, FallsBackAcrossFilesystemsWhenAvailable)
{
  if (!QFileInfo::exists("/dev/shm")) {
    GTEST_SKIP() << "/dev/shm is unavailable";
  }
  QTemporaryDir sourceDir("/dev/shm/fluorine-shell-move-src-XXXXXX");
  QTemporaryDir destinationDir;
  ASSERT_TRUE(sourceDir.isValid());
  ASSERT_TRUE(destinationDir.isValid());

  struct stat sourceStat = {};
  struct stat destinationStat = {};
  ASSERT_EQ(::stat(sourceDir.path().toLocal8Bit().constData(), &sourceStat), 0);
  ASSERT_EQ(::stat(destinationDir.path().toLocal8Bit().constData(), &destinationStat), 0);
  if (sourceStat.st_dev == destinationStat.st_dev) {
    GTEST_SKIP() << "temporary directories share a filesystem";
  }

  const QString source = QDir(sourceDir.path()).filePath("overwrite");
  const QString destination = QDir(destinationDir.path()).filePath("mod");
  writeFile(QDir(source).filePath("nested/replaced.txt"), "new");
  writeFile(QDir(source).filePath("fresh/deep/added.txt"), "added");
  writeFile(QDir(destination).filePath("nested/replaced.txt"), "old");
  writeFile(QDir(destination).filePath("keep.txt"), "keep");

  ASSERT_TRUE(MOBase::shellMove(source, destination));
  EXPECT_FALSE(QFileInfo::exists(source));
  EXPECT_EQ(readFile(QDir(destination).filePath("nested/replaced.txt")),
            "new");
  EXPECT_EQ(readFile(QDir(destination).filePath("fresh/deep/added.txt")),
            "added");
  EXPECT_EQ(readFile(QDir(destination).filePath("keep.txt")), "keep");
}
#endif
