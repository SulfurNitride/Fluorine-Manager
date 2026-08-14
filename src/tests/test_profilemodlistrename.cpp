#include "profilemodlistrename.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#ifdef Q_OS_UNIX
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

}  // namespace

TEST(ProfileModlistRename, RenamesExactUnicodeMatchAndKeepsLinesSeparated)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("modlist.txt");
  ASSERT_TRUE(writeBytes(path, "# generated\r\n# second comment\r\n+Old Mod\r\n"
                               "-Other\r\n*Fóreign\r\n"));

  const auto result = ProfileModlistRename::apply(path, "Old Mod", "新しい Mod");
  ASSERT_EQ(result.status, ProfileModlistRename::Status::Changed);
  EXPECT_EQ(result.renamed, 1);
  EXPECT_EQ(readBytes(path),
            QByteArray("# generated\r\n# second comment\r\n+新しい Mod\r\n"
                       "-Other\r\n*Fóreign\r\n"));
}

TEST(ProfileModlistRename, NoMatchPreservesBytesExactly)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("modlist.txt");
  const QByteArray original("# comment with spaces   \n+Existing\n\n");
  ASSERT_TRUE(writeBytes(path, original));

  const auto result = ProfileModlistRename::apply(path, "Missing", "Replacement");
  EXPECT_EQ(result.status, ProfileModlistRename::Status::NoChange);
  EXPECT_EQ(readBytes(path), original);
}

TEST(ProfileModlistRename, ReadFailureDoesNotCreateOrReplaceAnything)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString missing = temporary.filePath("missing.txt");
  const auto missingResult =
      ProfileModlistRename::apply(missing, "Old", "New");
  EXPECT_EQ(missingResult.status, ProfileModlistRename::Status::ReadError);
  EXPECT_FALSE(QFileInfo::exists(missing));

  const QString directory = temporary.filePath("directory");
  ASSERT_TRUE(QDir().mkdir(directory));
  const auto directoryResult =
      ProfileModlistRename::apply(directory, "Old", "New");
  EXPECT_EQ(directoryResult.status, ProfileModlistRename::Status::ReadError);
  EXPECT_TRUE(QFileInfo(directory).isDir());
}

#ifdef Q_OS_UNIX
TEST(ProfileModlistRename, SymlinkIsRejectedAndReferentIsPreserved)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString target = temporary.filePath("target.txt");
  const QString link   = temporary.filePath("modlist.txt");
  ASSERT_TRUE(writeBytes(target, "+Old\r\n"));
  ASSERT_EQ(::symlink("target.txt", QFile::encodeName(link).constData()), 0);

  const auto result = ProfileModlistRename::apply(link, "Old", "New");
  EXPECT_EQ(result.status, ProfileModlistRename::Status::ReadError);
  EXPECT_TRUE(QFileInfo(link).isSymLink());
  EXPECT_EQ(readBytes(target), "+Old\r\n");
}
#endif
