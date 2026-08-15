#include <uibase/utility.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gtest/gtest.h>

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
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

}  // namespace

TEST(CopyDir, CopiesCompleteTreeIncludingHiddenEntries)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString source      = temporary.filePath("source");
  const QString destination = temporary.filePath("destination");
  ASSERT_TRUE(QDir().mkdir(source));
  ASSERT_TRUE(QDir(source).mkdir("nested"));
  ASSERT_TRUE(QDir(source).mkdir(".hidden-dir"));
  ASSERT_TRUE(writeBytes(QDir(source).filePath("visible.txt"), "visible"));
  ASSERT_TRUE(writeBytes(QDir(source).filePath(".hidden"), "hidden"));
  ASSERT_TRUE(writeBytes(QDir(source).filePath("nested/child.txt"), "child"));
  ASSERT_TRUE(writeBytes(QDir(source).filePath(".hidden-dir/value"), "secret"));

  ASSERT_TRUE(MOBase::copyDir(source, destination, false));
  EXPECT_EQ(readBytes(QDir(destination).filePath("visible.txt")),
            QByteArray("visible"));
  EXPECT_EQ(readBytes(QDir(destination).filePath(".hidden")),
            QByteArray("hidden"));
  EXPECT_EQ(readBytes(QDir(destination).filePath("nested/child.txt")),
            QByteArray("child"));
  EXPECT_EQ(readBytes(QDir(destination).filePath(".hidden-dir/value")),
            QByteArray("secret"));
}

TEST(CopyDir, NestedFailureRemovesNewDestination)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString source      = temporary.filePath("source");
  const QString destination = temporary.filePath("destination");
  ASSERT_TRUE(QDir().mkdir(source));
  ASSERT_TRUE(QDir(source).mkdir("nested"));
  ASSERT_TRUE(writeBytes(QDir(source).filePath("first.txt"), "first"));
  ASSERT_TRUE(QFile::link(temporary.path(), QDir(source).filePath("nested/unsafe")));

  EXPECT_FALSE(MOBase::copyDir(source, destination, false));
  EXPECT_FALSE(QFileInfo::exists(destination));
  EXPECT_TRUE(QFileInfo(QDir(source).filePath("nested/unsafe")).isSymLink());
}

TEST(CopyDir, MergeFailurePreservesExistingDestination)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString source      = temporary.filePath("source");
  const QString destination = temporary.filePath("destination");
  ASSERT_TRUE(QDir().mkdir(source));
  ASSERT_TRUE(QDir().mkdir(destination));
  ASSERT_TRUE(writeBytes(QDir(destination).filePath("sentinel"), "keep"));
  ASSERT_TRUE(writeBytes(QDir(source).filePath("a-created"), "rollback"));
  ASSERT_TRUE(writeBytes(temporary.filePath("outside"), "outside"));
  ASSERT_TRUE(
      QFile::link(temporary.filePath("outside"), QDir(source).filePath("z-unsafe")));

  EXPECT_FALSE(MOBase::copyDir(source, destination, true));
  EXPECT_EQ(readBytes(QDir(destination).filePath("sentinel")), QByteArray("keep"));
  EXPECT_FALSE(QFileInfo::exists(QDir(destination).filePath("a-created")));
}

TEST(CopyDir, ExistingDestinationAndMissingParentFailWithoutMutation)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString source      = temporary.filePath("source");
  const QString destination = temporary.filePath("destination");
  ASSERT_TRUE(QDir().mkdir(source));
  ASSERT_TRUE(QDir().mkdir(destination));
  ASSERT_TRUE(writeBytes(QDir(destination).filePath("sentinel"), "keep"));

  EXPECT_FALSE(MOBase::copyDir(source, destination, false));
  EXPECT_EQ(readBytes(QDir(destination).filePath("sentinel")), QByteArray("keep"));

  const QString unreachable = temporary.filePath("missing/child");
  EXPECT_FALSE(MOBase::copyDir(source, unreachable, false));
  EXPECT_FALSE(QFileInfo::exists(temporary.filePath("missing")));
}

TEST(CopyDir, DestinationInsideSourceIsRejected)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString source = temporary.filePath("source");
  ASSERT_TRUE(QDir().mkdir(source));
  ASSERT_TRUE(writeBytes(QDir(source).filePath("value"), "keep"));

  const QString nestedDestination = QDir(source).filePath("copy");
  EXPECT_FALSE(MOBase::copyDir(source, nestedDestination, false));
  EXPECT_FALSE(QFileInfo::exists(nestedDestination));
  EXPECT_EQ(readBytes(QDir(source).filePath("value")), QByteArray("keep"));
}
