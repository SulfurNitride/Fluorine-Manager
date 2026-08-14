#include "winepluginlistsync.h"

#include <uibase/transactionalwritefile.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
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

}  // namespace

TEST(WinePluginListSync, ReadsAndCountsARegularSnapshot)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("Plugins.txt");
  ASSERT_TRUE(writeBytes(path, "# header\r\n *One.esm\r\nTwo.esp\n\t*Three.esm\n"));

  const auto result = WinePluginListSync::read(path);
  ASSERT_TRUE(result.snapshot) << qPrintable(result.error);
  EXPECT_EQ(result.snapshot->contents,
            "# header\r\n *One.esm\r\nTwo.esp\n\t*Three.esm\n");
  EXPECT_EQ(WinePluginListSync::countStarred(result.snapshot->contents), 2);
  EXPECT_TRUE(result.snapshot->modificationTime.isValid());
}

TEST(WinePluginListSync, SuspiciousActiveDropPolicyIsBounded)
{
  EXPECT_FALSE(WinePluginListSync::isSuspiciousActiveDrop(10, 0));
  EXPECT_FALSE(WinePluginListSync::isSuspiciousActiveDrop(100, -1));
  EXPECT_FALSE(WinePluginListSync::isSuspiciousActiveDrop(100, 90));
  EXPECT_FALSE(WinePluginListSync::isSuspiciousActiveDrop(100, 70));
  EXPECT_TRUE(WinePluginListSync::isSuspiciousActiveDrop(100, 69));
  EXPECT_TRUE(WinePluginListSync::isSuspiciousActiveDrop(40, 20));
}

#ifdef Q_OS_UNIX
TEST(WinePluginListSync, AcceptsStrictCaseAliasAndRecognizesSameLeaf)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString target = temporary.filePath("Plugins.txt");
  const QString alias  = temporary.filePath("plugins.txt");
  const QString sibling = temporary.filePath("PLUGINS.TXT");
  ASSERT_TRUE(writeBytes(target, "*One.esm\n"));
  ASSERT_TRUE(writeBytes(sibling, "stale\n"));
  ASSERT_EQ(::symlink("Plugins.txt", QFile::encodeName(alias).constData()), 0);

  const auto result = WinePluginListSync::read(alias);
  ASSERT_TRUE(result.snapshot) << qPrintable(result.error);
  EXPECT_TRUE(WinePluginListSync::isSameFile(target, *result.snapshot));
  EXPECT_TRUE(WinePluginListSync::isSameFile(alias, *result.snapshot));
  EXPECT_FALSE(WinePluginListSync::isSameFile(sibling, *result.snapshot));

  MOBase::TransactionalWriteFile siblingTransaction(sibling);
  QString error;
  ASSERT_TRUE(WinePluginListSync::publish(siblingTransaction, *result.snapshot,
                                          error))
      << qPrintable(error);
  EXPECT_EQ(readBytes(target), "*One.esm\n");
  EXPECT_EQ(readBytes(alias), "*One.esm\n");
  EXPECT_EQ(readBytes(sibling), "*One.esm\n");
}

TEST(WinePluginListSync, RefusesExternalSymlinkAndFifoWithoutBlocking)
{
  QTemporaryDir temporary;
  QTemporaryDir external;
  ASSERT_TRUE(temporary.isValid());
  ASSERT_TRUE(external.isValid());
  const QString target = external.filePath("outside.txt");
  const QString link   = temporary.filePath("plugins.txt");
  ASSERT_TRUE(writeBytes(target, "secret"));
  ASSERT_EQ(::symlink(QFile::encodeName(target).constData(),
                      QFile::encodeName(link).constData()),
            0);
  EXPECT_FALSE(WinePluginListSync::read(link).snapshot);
  EXPECT_EQ(readBytes(target), "secret");

  ASSERT_TRUE(QFile::remove(link));
  ASSERT_EQ(::mkfifo(QFile::encodeName(link).constData(), 0600), 0);
  EXPECT_FALSE(WinePluginListSync::read(link).snapshot);
  struct stat status;
  ASSERT_EQ(::lstat(QFile::encodeName(link).constData(), &status), 0);
  EXPECT_TRUE(S_ISFIFO(status.st_mode));
}
#endif

TEST(WinePluginListSync, PublishesContentsAndTimestampBeforeCommit)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString source = temporary.filePath("source.txt");
  const QString target = temporary.filePath("target.txt");
  ASSERT_TRUE(writeBytes(source, "*New.esm\n"));
  ASSERT_TRUE(writeBytes(target, "old"));
  const QDateTime wanted =
      QDateTime::fromMSecsSinceEpoch(1'700'000'000'000LL);
  QFile sourceFile(source);
  ASSERT_TRUE(sourceFile.open(QIODevice::ReadWrite));
  ASSERT_TRUE(sourceFile.setFileTime(wanted, QFileDevice::FileModificationTime));
  sourceFile.close();

  const auto snapshot = WinePluginListSync::read(source);
  ASSERT_TRUE(snapshot.snapshot) << qPrintable(snapshot.error);
  MOBase::TransactionalWriteFile transaction(target);
  QString error;
  ASSERT_TRUE(WinePluginListSync::publish(transaction, *snapshot.snapshot, error))
      << qPrintable(error);

  EXPECT_EQ(readBytes(target), "*New.esm\n");
  EXPECT_EQ(QFileInfo(target).lastModified().toMSecsSinceEpoch(),
            snapshot.snapshot->modificationTime.toMSecsSinceEpoch());
}

TEST(WinePluginListSync, FailedDestinationPreservesExistingLeaf)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString source = temporary.filePath("source.txt");
  const QString target = temporary.filePath("target");
  ASSERT_TRUE(writeBytes(source, "*New.esm\n"));
  ASSERT_TRUE(QDir().mkdir(target));
  const auto snapshot = WinePluginListSync::read(source);
  ASSERT_TRUE(snapshot.snapshot);

  MOBase::TransactionalWriteFile transaction(target);
  QString error;
  EXPECT_FALSE(WinePluginListSync::publish(transaction, *snapshot.snapshot, error));
  EXPECT_TRUE(QFileInfo(target).isDir());
}
