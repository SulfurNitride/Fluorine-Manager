#include "nexuscachedirectory.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkCacheMetaData>
#include <QNetworkDiskCache>
#include <QTemporaryDir>

#include <gtest/gtest.h>

namespace
{
void writeFile(const QString& path, const QByteArray& bytes)
{
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write(bytes), bytes.size());
  file.close();
}

QByteArray readFile(const QString& path)
{
  QFile file(path);
  EXPECT_TRUE(file.open(QIODevice::ReadOnly));
  return file.readAll();
}

QStringList filesBelow(const QString& path)
{
  QStringList files;
  QDirIterator entries(path, QDir::Files | QDir::Hidden | QDir::System,
                       QDirIterator::Subdirectories);
  const QDir root(path);
  while (entries.hasNext()) {
    files.push_back(root.relativeFilePath(entries.next()));
  }
  files.sort();
  return files;
}
}

TEST(NexusCacheDirectory, UsesDedicatedNestedDirectoryAndPreservesRoot)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());

  const QString sentinel = QDir(temporary.path()).filePath("foreign-cache.d");
  const QString unrelated = QDir(temporary.path()).filePath("unrelated/cache.bin");
  ASSERT_TRUE(QDir().mkpath(QFileInfo(unrelated).absolutePath()));
  writeFile(sentinel, "root sentinel");
  writeFile(unrelated, "nested sentinel");

  const auto result = NexusCacheDirectory::prepare(temporary.path());
  ASSERT_TRUE(result);
  EXPECT_EQ(result.path,
            QDir(temporary.path())
                .filePath(".fluorine-manager/nexus-network-v1"));
  EXPECT_NE(QDir::cleanPath(result.path), QDir::cleanPath(temporary.path()));

  QNetworkAccessManager manager;
  const auto configured =
      NexusCacheDirectory::configure(manager, temporary.path());
  ASSERT_TRUE(configured);
  auto* diskCache = qobject_cast<QNetworkDiskCache*>(manager.cache());
  ASSERT_NE(diskCache, nullptr);
  EXPECT_EQ(QDir::cleanPath(diskCache->cacheDirectory()),
            QDir::cleanPath(result.path));
  EXPECT_TRUE(NexusCacheDirectory::clear(manager));

  EXPECT_EQ(readFile(sentinel), "root sentinel");
  EXPECT_EQ(readFile(unrelated), "nested sentinel");
  EXPECT_TRUE(QFileInfo(QDir(result.path).filePath(".fluorine-cache-owner"))
                  .isFile());
}

TEST(NexusCacheDirectory, RejectedConfigurationRetainsLiveOwnedCache)
{
  QTemporaryDir first;
  QTemporaryDir unsafe;
  QTemporaryDir corrected;
  ASSERT_TRUE(first.isValid());
  ASSERT_TRUE(unsafe.isValid());
  ASSERT_TRUE(corrected.isValid());

  QNetworkAccessManager manager;
  ASSERT_TRUE(NexusCacheDirectory::configure(manager, first.path()));
  auto* firstCache = manager.cache();
  ASSERT_NE(firstCache, nullptr);

  writeFile(QDir(unsafe.path()).filePath(".fluorine-manager"), "foreign");
  const auto rejected = NexusCacheDirectory::configure(manager, unsafe.path());
  EXPECT_EQ(rejected.status, NexusCacheDirectory::Status::Collision);
  EXPECT_EQ(manager.cache(), firstCache);

  const auto recovered =
      NexusCacheDirectory::configure(manager, corrected.path());
  ASSERT_TRUE(recovered);
  const auto* diskCache = qobject_cast<QNetworkDiskCache*>(manager.cache());
  ASSERT_NE(diskCache, nullptr);
  EXPECT_EQ(manager.cache(), firstCache);
  EXPECT_EQ(QDir::cleanPath(diskCache->cacheDirectory()),
            QDir::cleanPath(recovered.path));
  EXPECT_TRUE(NexusCacheDirectory::clear(manager));
}

TEST(NexusCacheDirectory, QuarantineFailureDoesNotAttachOrReplaceCache)
{
  QTemporaryDir first;
  QTemporaryDir corrected;
  QTemporaryDir missingParent;
  ASSERT_TRUE(first.isValid());
  ASSERT_TRUE(corrected.isValid());
  ASSERT_TRUE(missingParent.isValid());

  const QString missing = missingParent.path();
  ASSERT_TRUE(missingParent.remove());
  const QString invalidTemplate =
      QDir(missing).filePath("quarantine/cache-XXXXXX");

  QNetworkAccessManager initiallyEmpty;
  const auto initialFailure = NexusCacheDirectory::configure(
      initiallyEmpty, first.path(), invalidTemplate);
  EXPECT_EQ(initialFailure.status, NexusCacheDirectory::Status::IoError);
  EXPECT_EQ(initiallyEmpty.cache(), nullptr);

  QNetworkAccessManager configured;
  const auto accepted = NexusCacheDirectory::configure(configured, first.path());
  ASSERT_TRUE(accepted);
  auto* cache = qobject_cast<QNetworkDiskCache*>(configured.cache());
  ASSERT_NE(cache, nullptr);
  const QString acceptedPath = cache->cacheDirectory();

  const auto reconfigurationFailure = NexusCacheDirectory::configure(
      configured, corrected.path(), invalidTemplate);
  EXPECT_EQ(reconfigurationFailure.status,
            NexusCacheDirectory::Status::IoError);
  EXPECT_EQ(configured.cache(), cache);
  EXPECT_EQ(cache->cacheDirectory(), acceptedPath);
  EXPECT_TRUE(NexusCacheDirectory::clear(configured));
}

#ifdef Q_OS_UNIX
TEST(NexusCacheDirectory, RejectsSymlinkInsideQtCacheLayout)
{
  QTemporaryDir temporary;
  QTemporaryDir external;
  ASSERT_TRUE(temporary.isValid());
  ASSERT_TRUE(external.isValid());

  const auto prepared = NexusCacheDirectory::prepare(temporary.path());
  ASSERT_TRUE(prepared);
  const QString sentinel = QDir(external.path()).filePath("foreign.d");
  writeFile(sentinel, "external");
  const QString dataLink = QDir(prepared.path).filePath("data8");
  ASSERT_TRUE(QFile::link(external.path(), dataLink));

  QNetworkAccessManager manager;
  const auto result = NexusCacheDirectory::configure(manager, temporary.path());
  EXPECT_EQ(result.status, NexusCacheDirectory::Status::Collision);
  EXPECT_EQ(result.errorPath, dataLink);
  EXPECT_EQ(manager.cache(), nullptr);
  EXPECT_EQ(readFile(sentinel), "external");
}

TEST(NexusCacheDirectory, ClearRejectsReplacedCacheLeaf)
{
  QTemporaryDir temporary;
  QTemporaryDir external;
  ASSERT_TRUE(temporary.isValid());
  ASSERT_TRUE(external.isValid());

  QNetworkAccessManager manager;
  const auto configured =
      NexusCacheDirectory::configure(manager, temporary.path());
  ASSERT_TRUE(configured);
  auto* diskCache = qobject_cast<QNetworkDiskCache*>(manager.cache());
  ASSERT_NE(diskCache, nullptr);

  const QString sentinel = QDir(external.path()).filePath("foreign.d");
  writeFile(sentinel, "external");
  const QFileInfo cacheInfo(configured.path);
  QDir parent(cacheInfo.absolutePath());
  ASSERT_TRUE(parent.rename(cacheInfo.fileName(), "saved-cache"));
  ASSERT_TRUE(QFile::link(external.path(), configured.path));

  EXPECT_FALSE(NexusCacheDirectory::clear(manager));
  EXPECT_EQ(manager.cache(), diskCache);
  EXPECT_NE(QDir::cleanPath(diskCache->cacheDirectory()),
            QDir::cleanPath(configured.path));
  EXPECT_FALSE(diskCache->cacheDirectory().isEmpty());
  EXPECT_TRUE(QFileInfo(diskCache->cacheDirectory()).isDir());
  EXPECT_EQ(readFile(sentinel), "external");

  QNetworkCacheMetaData metadata;
  metadata.setUrl(QUrl("https://example.invalid/cache-probe"));
  metadata.setExpirationDate(QDateTime::currentDateTimeUtc().addDays(1));
  QIODevice* cacheData = diskCache->prepare(metadata);
  ASSERT_NE(cacheData, nullptr);
  ASSERT_EQ(cacheData->write("cache payload"), 13);
  diskCache->insert(cacheData);
  EXPECT_EQ(readFile(sentinel), "external");
  EXPECT_EQ(filesBelow(external.path()), QStringList{"foreign.d"});

  ASSERT_TRUE(QFile::remove(configured.path));
  ASSERT_TRUE(parent.rename("saved-cache", cacheInfo.fileName()));
}
#endif

TEST(NexusCacheDirectory, ExistingSafeDirectoryIsIdempotent)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());

  const auto first  = NexusCacheDirectory::prepare(temporary.path());
  const auto second = NexusCacheDirectory::prepare(temporary.path());

  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_EQ(second.path, first.path);
}

TEST(NexusCacheDirectory, CreatesMissingConfiguredRootOnFirstRun)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());

  const QString root = QDir(temporary.path()).filePath("webcache");
  const QString sentinel = QDir(temporary.path()).filePath("keep.txt");
  writeFile(sentinel, "parent sentinel");
  ASSERT_FALSE(QFileInfo::exists(root));

  QNetworkAccessManager manager;
  const auto first = NexusCacheDirectory::configure(manager, root);
  ASSERT_TRUE(first);
  EXPECT_TRUE(QFileInfo(root).isDir());
  EXPECT_FALSE(QFileInfo(root).isSymLink());
  EXPECT_EQ(first.path,
            QDir(root).filePath(".fluorine-manager/nexus-network-v1"));
  ASSERT_NE(manager.cache(), nullptr);
  EXPECT_EQ(readFile(sentinel), "parent sentinel");

  const auto second = NexusCacheDirectory::prepare(root);
  ASSERT_TRUE(second);
  EXPECT_EQ(second.path, first.path);
}

TEST(NexusCacheDirectory, RejectsInvalidRoots)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());

  EXPECT_EQ(NexusCacheDirectory::prepare({}).status,
            NexusCacheDirectory::Status::InvalidRoot);
  EXPECT_EQ(NexusCacheDirectory::prepare("relative/cache").status,
            NexusCacheDirectory::Status::InvalidRoot);
  EXPECT_EQ(NexusCacheDirectory::prepare(
                QDir(temporary.path()).filePath("missing/webcache"))
                .status,
            NexusCacheDirectory::Status::InvalidRoot);
}

TEST(NexusCacheDirectory, RejectsNamespaceFileCollision)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());

  const QString collision =
      QDir(temporary.path()).filePath(".fluorine-manager");
  writeFile(collision, "foreign");

  const auto result = NexusCacheDirectory::prepare(temporary.path());
  EXPECT_EQ(result.status, NexusCacheDirectory::Status::Collision);
  EXPECT_EQ(result.errorPath, collision);
  EXPECT_EQ(readFile(collision), "foreign");
}

TEST(NexusCacheDirectory, RejectsCacheFileCollision)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());

  const QString nameSpace =
      QDir(temporary.path()).filePath(".fluorine-manager");
  ASSERT_TRUE(QDir().mkpath(nameSpace));
  const QString collision = QDir(nameSpace).filePath("nexus-network-v1");
  writeFile(collision, "foreign");

  const auto result = NexusCacheDirectory::prepare(temporary.path());
  EXPECT_EQ(result.status, NexusCacheDirectory::Status::Collision);
  EXPECT_EQ(result.errorPath, collision);
  EXPECT_EQ(readFile(collision), "foreign");
}

#ifdef Q_OS_UNIX
TEST(NexusCacheDirectory, RejectsDanglingConfiguredRoot)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());

  const QString root = QDir(temporary.path()).filePath("webcache");
  const QString missingTarget = QDir(temporary.path()).filePath("missing");
  ASSERT_TRUE(QFile::link(missingTarget, root));

  const auto result = NexusCacheDirectory::prepare(root);
  EXPECT_EQ(result.status, NexusCacheDirectory::Status::Collision);
  EXPECT_EQ(result.errorPath, root);
  EXPECT_TRUE(QFileInfo(root).isSymLink());
  EXPECT_FALSE(QFileInfo::exists(missingTarget));
}

TEST(NexusCacheDirectory, RejectsNamespaceSymlinkAndPreservesTarget)
{
  QTemporaryDir temporary;
  QTemporaryDir external;
  ASSERT_TRUE(temporary.isValid());
  ASSERT_TRUE(external.isValid());

  const QString sentinel = QDir(external.path()).filePath("keep.txt");
  writeFile(sentinel, "external");
  const QString link = QDir(temporary.path()).filePath(".fluorine-manager");
  ASSERT_TRUE(QFile::link(external.path(), link));

  const auto result = NexusCacheDirectory::prepare(temporary.path());
  EXPECT_EQ(result.status, NexusCacheDirectory::Status::Collision);
  EXPECT_EQ(result.errorPath, link);
  EXPECT_EQ(readFile(sentinel), "external");
}

TEST(NexusCacheDirectory, RejectsCacheSymlinkAndPreservesTarget)
{
  QTemporaryDir temporary;
  QTemporaryDir external;
  ASSERT_TRUE(temporary.isValid());
  ASSERT_TRUE(external.isValid());

  const QString nameSpace =
      QDir(temporary.path()).filePath(".fluorine-manager");
  ASSERT_TRUE(QDir().mkpath(nameSpace));
  const QString sentinel = QDir(external.path()).filePath("keep.txt");
  writeFile(sentinel, "external");
  const QString link = QDir(nameSpace).filePath("nexus-network-v1");
  ASSERT_TRUE(QFile::link(external.path(), link));

  const auto result = NexusCacheDirectory::prepare(temporary.path());
  EXPECT_EQ(result.status, NexusCacheDirectory::Status::Collision);
  EXPECT_EQ(result.errorPath, link);
  EXPECT_EQ(readFile(sentinel), "external");
}
#endif

int main(int argc, char** argv)
{
  QCoreApplication application(argc, argv);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
