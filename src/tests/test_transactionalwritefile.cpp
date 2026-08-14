#include <uibase/transactionalwritefile.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gtest/gtest.h>

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
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

QStringList leaves(const QString& directory)
{
  return QDir(directory).entryList(QDir::AllEntries | QDir::Hidden |
                                       QDir::NoDotAndDotDot,
                                   QDir::Name);
}

}  // namespace

static_assert(sizeof(MOBase::TransactionalWriteFile) == sizeof(void*));

TEST(TransactionalWriteFile, CreatesAndReplacesExactContents)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("state.txt");

  MOBase::TransactionalWriteFile create(path);
  ASSERT_TRUE(create.replaceWith("first")) << qPrintable(create.errorString());
  EXPECT_EQ(readBytes(path), "first");

  MOBase::TransactionalWriteFile replace(path);
  ASSERT_TRUE(replace.replaceWith("second\r\n"))
      << qPrintable(replace.errorString());
  EXPECT_EQ(readBytes(path), "second\r\n");

  MOBase::TransactionalWriteFile empty(path);
  ASSERT_TRUE(empty.replaceWith({})) << qPrintable(empty.errorString());
  EXPECT_EQ(readBytes(path), QByteArray{});
  EXPECT_EQ(leaves(temporary.path()), QStringList{"state.txt"});
}

TEST(TransactionalWriteFile, ConstructionAndEarlyReturnPreserveOldFile)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("state.txt");
  ASSERT_TRUE(writeBytes(path, "old"));

  { MOBase::TransactionalWriteFile abandoned(path); }
  EXPECT_EQ(readBytes(path), "old");
  EXPECT_EQ(leaves(temporary.path()), QStringList{"state.txt"});
}

TEST(TransactionalWriteFile, IsSingleUse)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("state.txt");

  MOBase::TransactionalWriteFile file(path);
  ASSERT_TRUE(file.replaceWith("one"));
  EXPECT_FALSE(file.replaceWith("two"));
  EXPECT_FALSE(file.errorString().isEmpty());
  EXPECT_EQ(readBytes(path), "one");
}

TEST(TransactionalWriteFile, RefusesReplacementAfterGenerationChanges)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("state.txt");
  ASSERT_TRUE(writeBytes(path, "old"));

  MOBase::TransactionalWriteFile file(path);
  ASSERT_TRUE(writeBytes(path, "newer-generation"));

  EXPECT_FALSE(file.replaceWith("stale-generation"));
  EXPECT_EQ(readBytes(path), "newer-generation");
  EXPECT_NE(file.errorString().indexOf("changed"), -1);
}

#ifdef Q_OS_UNIX
TEST(TransactionalWriteFile, RefusesSameInodeMutationAfterConstruction)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("state.txt");
  ASSERT_TRUE(writeBytes(path, "aaaa"));

  MOBase::TransactionalWriteFile file(path);
  QFile inPlace(path);
  ASSERT_TRUE(inPlace.open(QIODevice::WriteOnly));
  ASSERT_EQ(inPlace.write("bbbb", 4), 4);
  ASSERT_TRUE(inPlace.flush());
  inPlace.close();

  EXPECT_FALSE(file.replaceWith("stale"));
  EXPECT_EQ(readBytes(path), "bbbb");
}
#endif

TEST(TransactionalWriteFile, PreservesOrdinaryPermissions)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("state.txt");
  ASSERT_TRUE(writeBytes(path, "old"));
  const auto permissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                           QFileDevice::ReadGroup;
  ASSERT_TRUE(QFile::setPermissions(path, permissions));
  const auto storedPermissions = QFileInfo(path).permissions();

  MOBase::TransactionalWriteFile file(path);
  ASSERT_TRUE(file.replaceWith("new")) << qPrintable(file.errorString());
  EXPECT_EQ(QFileInfo(path).permissions(), storedPermissions);
}

#ifdef Q_OS_UNIX
TEST(TransactionalWriteFile, PreservesSupportedCaseAlias)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString target = temporary.filePath("Plugins.txt");
  const QString alias  = temporary.filePath("plugins.txt");
  ASSERT_TRUE(writeBytes(target, "old"));
  ASSERT_EQ(::symlink("Plugins.txt", QFile::encodeName(alias).constData()), 0);

  MOBase::TransactionalWriteFile file(alias);
  ASSERT_TRUE(file.replaceWith("new")) << qPrintable(file.errorString());
  EXPECT_TRUE(QFileInfo(alias).isSymLink());
  EXPECT_EQ(readBytes(target), "new");
  EXPECT_EQ(readBytes(alias), "new");
}

TEST(TransactionalWriteFile, AllowsOwnerControlledParentSymlink)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString realDirectory = temporary.filePath("real");
  const QString aliasDirectory = temporary.filePath("alias");
  ASSERT_TRUE(QDir().mkdir(realDirectory));
  ASSERT_EQ(::symlink("real", QFile::encodeName(aliasDirectory).constData()), 0);

  const QString throughAlias = QDir(aliasDirectory).filePath("state.txt");
  MOBase::TransactionalWriteFile file(throughAlias);
  ASSERT_TRUE(file.replaceWith("new")) << qPrintable(file.errorString());
  EXPECT_EQ(readBytes(QDir(realDirectory).filePath("state.txt")), "new");
  EXPECT_TRUE(QFileInfo(aliasDirectory).isSymLink());
}

TEST(TransactionalWriteFile, RejectsUnsafeSymlinksWithoutFollowingThem)
{
  QTemporaryDir temporary;
  QTemporaryDir external;
  ASSERT_TRUE(temporary.isValid());
  ASSERT_TRUE(external.isValid());
  const QString outside = external.filePath("outside.txt");
  ASSERT_TRUE(writeBytes(outside, "sentinel"));

  const QString different = temporary.filePath("state.txt");
  ASSERT_EQ(::symlink(QFile::encodeName(outside).constData(),
                      QFile::encodeName(different).constData()),
            0);
  MOBase::TransactionalWriteFile externalLink(different);
  EXPECT_FALSE(externalLink.replaceWith("new"));
  EXPECT_TRUE(QFileInfo(different).isSymLink());
  EXPECT_EQ(readBytes(outside), "sentinel");

  const QString dangling = temporary.filePath("dangling.txt");
  ASSERT_EQ(::symlink("Dangling.txt", QFile::encodeName(dangling).constData()), 0);
  MOBase::TransactionalWriteFile danglingLink(dangling);
  EXPECT_FALSE(danglingLink.replaceWith("new"));
  EXPECT_TRUE(QFileInfo(dangling).isSymLink());

  const QString chainTarget = temporary.filePath("Chain.txt");
  const QString chainMiddle = temporary.filePath("chain.txt");
  const QString chainStart  = temporary.filePath("CHAIN.TXT");
  ASSERT_TRUE(writeBytes(chainTarget, "chain"));
  ASSERT_EQ(::symlink("Chain.txt", QFile::encodeName(chainMiddle).constData()), 0);
  ASSERT_EQ(::symlink("chain.txt", QFile::encodeName(chainStart).constData()), 0);
  MOBase::TransactionalWriteFile chained(chainStart);
  EXPECT_FALSE(chained.replaceWith("new"));
  EXPECT_EQ(readBytes(chainTarget), "chain");
}

TEST(TransactionalWriteFile, RejectsDirectoryAndFifoWithoutBlocking)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString directory = temporary.filePath("directory");
  const QString fifo      = temporary.filePath("pipe");
  ASSERT_TRUE(QDir().mkdir(directory));
  ASSERT_EQ(::mkfifo(QFile::encodeName(fifo).constData(), 0600), 0);

  MOBase::TransactionalWriteFile directoryFile(directory);
  EXPECT_FALSE(directoryFile.replaceWith("new"));
  MOBase::TransactionalWriteFile fifoFile(fifo);
  EXPECT_FALSE(fifoFile.replaceWith("new"));

  EXPECT_TRUE(QFileInfo(directory).isDir());
  struct stat status;
  ASSERT_EQ(::lstat(QFile::encodeName(fifo).constData(), &status), 0);
  EXPECT_TRUE(S_ISFIFO(status.st_mode));
}

TEST(TransactionalWriteFile, AtomicallyDetachesSelectedHardlink)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString selected = temporary.filePath("selected.txt");
  const QString sibling  = temporary.filePath("sibling.txt");
  ASSERT_TRUE(writeBytes(selected, "old"));
  ASSERT_EQ(::link(QFile::encodeName(selected).constData(),
                   QFile::encodeName(sibling).constData()),
            0);

  MOBase::TransactionalWriteFile file(selected);
  ASSERT_TRUE(file.replaceWith("new")) << qPrintable(file.errorString());
  EXPECT_EQ(readBytes(selected), "new");
  EXPECT_EQ(readBytes(sibling), "old");
}

TEST(TransactionalWriteFile, ForcedShortWritePreservesOldGeneration)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("state.txt");
  ASSERT_TRUE(writeBytes(path, "old-generation"));

  const pid_t child = ::fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    std::signal(SIGXFSZ, SIG_IGN);
    struct rlimit limit = {4096, 4096};
    if (::setrlimit(RLIMIT_FSIZE, &limit) != 0) {
      _exit(2);
    }
    MOBase::TransactionalWriteFile file(path);
    const QByteArray payload(1024 * 1024, 'x');
    _exit(file.replaceWith(payload) ? 3 : 0);
  }

  int status = 0;
  ASSERT_EQ(::waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(WEXITSTATUS(status), 0);
  EXPECT_EQ(readBytes(path), "old-generation");
  EXPECT_EQ(leaves(temporary.path()), QStringList{"state.txt"});
}
#endif
