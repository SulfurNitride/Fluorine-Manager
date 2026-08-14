#include "morrowindpluginlistwriter.h"

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

TEST(MorrowindPluginListWriter, ReplacesOnlyGameFilesSection)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("Morrowind.ini");
  const QByteArray original =
      "; prefix\n[General]\nsetting=value\n[Game Files]\r\n"
      "; old comment\r\nGameFile0=Old.esm\r\n[Archives]\nArchive 0=base.bsa\n";
  ASSERT_TRUE(writeBytes(path, original));

  const auto result = MorrowindPluginListWriter::publish(
      path, {"Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm"});
  ASSERT_EQ(result.status, MorrowindPluginListWriter::Status::Published)
      << qPrintable(result.error);
  EXPECT_EQ(readBytes(path),
            QByteArray("; prefix\n[General]\nsetting=value\n[Game Files]\r\n"
                       "GameFile0=Morrowind.esm\r\n"
                       "GameFile1=Tribunal.esm\r\n"
                       "GameFile2=Bloodmoon.esm\r\n"
                       "[Archives]\nArchive 0=base.bsa\n"));
}

TEST(MorrowindPluginListWriter, AppendsMissingSectionWithoutChangingPrefix)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("Morrowind.ini");
  ASSERT_TRUE(writeBytes(path, "[General]\r\nsetting=value"));

  const auto result = MorrowindPluginListWriter::publish(path, {"Plugin.esp"});
  ASSERT_EQ(result.status, MorrowindPluginListWriter::Status::Published);
  EXPECT_EQ(readBytes(path),
            QByteArray("[General]\r\nsetting=value\r\n[Game Files]\r\n"
                       "GameFile0=Plugin.esp\r\n"));
}

TEST(MorrowindPluginListWriter, EmptyOrAmbiguousInputPreservesExactBytes)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("Morrowind.ini");
  const QByteArray original =
      "[Game Files]\nGameFile0=One.esm\n[General]\nsetting=value\n"
      "[game files]\nGameFile0=Two.esm\n";
  ASSERT_TRUE(writeBytes(path, original));
  const QDateTime originalModificationTime = QFileInfo(path).lastModified();

  EXPECT_EQ(MorrowindPluginListWriter::publish(path, {}).status,
            MorrowindPluginListWriter::Status::NoPlugins);
  EXPECT_EQ(readBytes(path), original);
  EXPECT_EQ(QFileInfo(path).lastModified(), originalModificationTime);
  EXPECT_EQ(MorrowindPluginListWriter::publish(path, {"New.esm"}).status,
            MorrowindPluginListWriter::Status::InvalidFormat);
  EXPECT_EQ(readBytes(path), original);
  EXPECT_EQ(QFileInfo(path).lastModified(), originalModificationTime);
}

#ifdef Q_OS_UNIX
TEST(MorrowindPluginListWriter, UnsafeLeavesAreRejectedWithoutMutation)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString target = temporary.filePath("target.ini");
  const QString link   = temporary.filePath("Morrowind.ini");
  ASSERT_TRUE(writeBytes(target, "[Game Files]\nGameFile0=Old.esm\n"));
  ASSERT_EQ(::symlink("target.ini", QFile::encodeName(link).constData()), 0);

  EXPECT_EQ(MorrowindPluginListWriter::publish(link, {"New.esm"}).status,
            MorrowindPluginListWriter::Status::ReadError);
  EXPECT_EQ(readBytes(target), "[Game Files]\nGameFile0=Old.esm\n");

  ASSERT_TRUE(QFile::remove(link));
  ASSERT_EQ(::mkfifo(QFile::encodeName(link).constData(), 0600), 0);
  EXPECT_EQ(MorrowindPluginListWriter::publish(link, {"New.esm"}).status,
            MorrowindPluginListWriter::Status::ReadError);
  struct stat status;
  ASSERT_EQ(::lstat(QFile::encodeName(link).constData(), &status), 0);
  EXPECT_TRUE(S_ISFIFO(status.st_mode));
}
#endif
