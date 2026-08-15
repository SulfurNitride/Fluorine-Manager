#include "profiletweakmerge.h"

#include <QDir>
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

TEST(ProfileTweakMerge, LaterTweaksWinAndOneGenerationIsPublished) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString first = temporary.filePath("first.ini");
  const QString second = temporary.filePath("second.ini");
  const QString target = temporary.filePath("initweaks.ini");
  ASSERT_TRUE(writeBytes(first, "[General]\nKey=first\nOther=kept\n"));
  ASSERT_TRUE(
      writeBytes(second, "[general]\nkey=second\n[Extra]\nValue=yes\n"));
  ASSERT_TRUE(writeBytes(target, "old-generation\n"));
#ifdef Q_OS_UNIX
  ASSERT_EQ(::chmod(QFile::encodeName(target).constData(), 0640), 0);
#endif

  QString error;
  ASSERT_TRUE(ProfileTweakMerge::publish({first, second}, target, &error))
      << qPrintable(error);
  EXPECT_EQ(readBytes(target),
            "[General]\nKey=second\nOther=kept\n\n[Extra]\nValue=yes\n\n"
            "[Archive]\nbInvalidateOlderFiles=1\n");
#ifdef Q_OS_UNIX
  struct stat status{};
  ASSERT_EQ(::stat(QFile::encodeName(target).constData(), &status), 0);
  EXPECT_EQ(status.st_mode & 0777, 0640);
#endif
}

TEST(ProfileTweakMerge, MissingInputsRemainOptional) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString target = temporary.filePath("initweaks.ini");
  QString error;
  ASSERT_TRUE(ProfileTweakMerge::publish({temporary.filePath("missing.ini")},
                                         target, &error));
  EXPECT_EQ(readBytes(target), "[Archive]\nbInvalidateOlderFiles=1\n");
}

TEST(ProfileTweakMerge, PresentUnreadableInputPreservesLastGoodGeneration) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString source = temporary.filePath("not-a-file.ini");
  const QString target = temporary.filePath("initweaks.ini");
  ASSERT_TRUE(QDir().mkdir(source));
  ASSERT_TRUE(writeBytes(target, "last-good\n"));

  QString error;
  EXPECT_FALSE(ProfileTweakMerge::publish({source}, target, &error));
  EXPECT_FALSE(error.isEmpty());
  EXPECT_EQ(readBytes(target), "last-good\n");
}

TEST(ProfileTweakMerge, InvalidInputPreservesLastGoodGeneration) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString source = temporary.filePath("invalid.ini");
  const QString target = temporary.filePath("initweaks.ini");
  ASSERT_TRUE(writeBytes(source, "[Invalid]Section]\nKey=value\n"));
  ASSERT_TRUE(writeBytes(target, "last-good\n"));

  QString error;
  EXPECT_FALSE(ProfileTweakMerge::publish({source}, target, &error));
  EXPECT_FALSE(error.isEmpty());
  EXPECT_EQ(readBytes(target), "last-good\n");
}

TEST(ProfileTweakMerge, InvalidUtf8PreservesLastGoodGeneration) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString source = temporary.filePath("invalid-utf8.ini");
  const QString target = temporary.filePath("initweaks.ini");
  ASSERT_TRUE(
      writeBytes(source, QByteArray::fromHex("5b53656374696f6e5d0ac3")));
  ASSERT_TRUE(writeBytes(target, "last-good\n"));

  QString error;
  EXPECT_FALSE(ProfileTweakMerge::publish({source}, target, &error));
  EXPECT_FALSE(error.isEmpty());
  EXPECT_EQ(readBytes(target), "last-good\n");
}

#ifdef Q_OS_UNIX
TEST(ProfileTweakMerge, UnsafeTargetIsNeverFollowed) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString source = temporary.filePath("source.ini");
  const QString outside = temporary.filePath("outside.ini");
  const QString target = temporary.filePath("initweaks.ini");
  ASSERT_TRUE(writeBytes(source, "[General]\nKey=value\n"));
  ASSERT_TRUE(writeBytes(outside, "outside\n"));
  ASSERT_EQ(::symlink(QFile::encodeName(outside).constData(),
                      QFile::encodeName(target).constData()),
            0);

  QString error;
  EXPECT_FALSE(ProfileTweakMerge::publish({source}, target, &error));
  EXPECT_EQ(readBytes(outside), "outside\n");
  EXPECT_TRUE(QFileInfo(target).isSymLink());
}
#endif
