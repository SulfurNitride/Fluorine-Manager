#include "pandorapreviousoutput.h"

#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

bool writeBytes(const QString &path, QByteArrayView bytes) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
         file.write(bytes.data(), bytes.size()) == bytes.size() && file.flush();
}

QByteArray readBytes(const QString &path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

} // namespace

TEST(PandoraPreviousOutput, NormalizesPathsWithoutChangingLineEndings) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("PreviousOutput.txt");
  ASSERT_TRUE(writeBytes(path, "Prefix\\Meshes\\Characters Female\\A.HKX\r\n"
                               "unchanged-line\n"
                               "Other\\mEsHeS\\Actors\\B.HKX"));
#ifdef Q_OS_UNIX
  ASSERT_EQ(::chmod(QFile::encodeName(path).constData(), 0640), 0);
#endif

  const auto result = PandoraPreviousOutput::normalize(path);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_EQ(result.status, PandoraPreviousOutput::Status::Updated);
  EXPECT_EQ(readBytes(path), "Prefix\\meshes\\characters female\\a.hkx\r\n"
                             "unchanged-line\n"
                             "Other\\meshes\\actors\\b.hkx");
#ifdef Q_OS_UNIX
  struct stat status{};
  ASSERT_EQ(::stat(QFile::encodeName(path).constData(), &status), 0);
  EXPECT_EQ(status.st_mode & 0777, 0640);
#endif
}

TEST(PandoraPreviousOutput, MissingAndUnchangedFilesNeedNoPublication) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString missing = temporary.filePath("missing.txt");
  auto result = PandoraPreviousOutput::normalize(missing);
  ASSERT_TRUE(result);
  EXPECT_EQ(result.status, PandoraPreviousOutput::Status::Missing);

  const QString unchanged = temporary.filePath("PreviousOutput.txt");
  ASSERT_TRUE(writeBytes(unchanged, "already lowercase\\meshes\\file.hkx\n"));
  result = PandoraPreviousOutput::normalize(unchanged);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_EQ(result.status, PandoraPreviousOutput::Status::Unchanged);
  EXPECT_EQ(readBytes(unchanged), "already lowercase\\meshes\\file.hkx\n");
}

TEST(PandoraPreviousOutput, InvalidUtf8PreservesTheOriginal) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("PreviousOutput.txt");
  const QByteArray original = QByteArray::fromHex("5c4d65736865735cc3");
  ASSERT_TRUE(writeBytes(path, original));

  const auto result = PandoraPreviousOutput::normalize(path);
  EXPECT_EQ(result.status, PandoraPreviousOutput::Status::Failed);
  EXPECT_EQ(readBytes(path), original);
}

#ifdef Q_OS_UNIX
TEST(PandoraPreviousOutput, UnsafeSymlinkIsPreserved) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString outside = temporary.filePath("outside.txt");
  const QString path = temporary.filePath("PreviousOutput.txt");
  ASSERT_TRUE(writeBytes(outside, "\\Meshes\\Outside.HKX\n"));
  ASSERT_EQ(::symlink(QFile::encodeName(outside).constData(),
                      QFile::encodeName(path).constData()),
            0);

  const auto result = PandoraPreviousOutput::normalize(path);
  EXPECT_EQ(result.status, PandoraPreviousOutput::Status::Failed);
  EXPECT_EQ(readBytes(outside), "\\Meshes\\Outside.HKX\n");
  EXPECT_TRUE(QFileInfo(path).isSymLink());
}
#endif
