#include "wineregistryfile.h"

#include <QFile>
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

} // namespace

TEST(WineRegistryFile, ReadsTimestampedSectionsAndPublishesBothViewsOnce)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("system.reg");
  const QByteArray original = "WINE REGISTRY Version 2\n\n"
                              "[Software\\\\Vendor\\\\Game] 1770000000\r\n"
                              "\"Install Path\"=\"C:\\\\Old\"\r\n"
                              "; untouched comment\n"
                              "[Software\\\\Wow6432Node\\\\Vendor\\\\Game]\n"
                              "\"Install Path\"=\"D:\\\\Old\"";
  ASSERT_TRUE(writeBytes(path, original));

  QList<WineRegistryFile::Query> queries{
      {QStringLiteral("Software\\Vendor\\Game"), QStringLiteral("Install Path")},
      {QStringLiteral("Software\\Wow6432Node\\Vendor\\Game"),
       QStringLiteral("Install Path")}};
  ASSERT_TRUE(WineRegistryFile::readValues(path, queries));
  ASSERT_TRUE(queries[0].present);
  ASSERT_TRUE(queries[1].present);
  EXPECT_EQ(queries[0].value, QStringLiteral("C:\\Old"));
  EXPECT_EQ(queries[1].value, QStringLiteral("D:\\Old"));

  const QString replacement = QStringLiteral("Z:\\Games\\A \"Quoted\" Tool\\");
  const QList<WineRegistryFile::Update> updates{
      {queries[0].section, queries[0].name, replacement, true, queries[0].present,
       queries[0].value},
      {queries[1].section, queries[1].name, replacement, true, queries[1].present,
       queries[1].value}};
  const auto result = WineRegistryFile::updateValues(path, updates);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_TRUE(result.changed);
  EXPECT_EQ(readBytes(path), "WINE REGISTRY Version 2\n\n"
                             "[Software\\\\Vendor\\\\Game] 1770000000\r\n"
                             "\"Install Path\"=\"Z:\\\\Games\\\\A \\\"Quoted\\\" "
                             "Tool\\\\\"\r\n"
                             "; untouched comment\n"
                             "[Software\\\\Wow6432Node\\\\Vendor\\\\Game]\n"
                             "\"Install Path\"=\"Z:\\\\Games\\\\A \\\"Quoted\\\" "
                             "Tool\\\\\"");
}

TEST(WineRegistryFile, CompareAndSetRejectsStaleConfirmation)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("system.reg");
  const QByteArray original = "[Software\\\\Vendor\\\\Game]\n\"Path\"=\"newer\"\n";
  ASSERT_TRUE(writeBytes(path, original));

  const auto result = WineRegistryFile::updateValues(
      path, {{QStringLiteral("Software\\Vendor\\Game"), QStringLiteral("Path"),
              QStringLiteral("desired"), true, true, QStringLiteral("old")}});
  EXPECT_EQ(result.status, WineRegistryFile::Status::Conflict);
  EXPECT_EQ(readBytes(path), original);
}

TEST(WineRegistryFile, DecodesWineV2QuotedEscapes)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("system.reg");
  ASSERT_TRUE(writeBytes(
      path,
      QByteArrayLiteral("[Software\\\\Vendor\\\\Game] 1770000000\n") +
          QByteArrayLiteral(
              R"REG("Path"="Z:\\M\x00f3ds\040Folder\t\"quoted\"")REG") +
          '\n'));

  QList<WineRegistryFile::Query> queries{
      {QStringLiteral("Software\\Vendor\\Game"), QStringLiteral("Path")}};
  const auto result = WineRegistryFile::readValues(path, queries);
  ASSERT_TRUE(result) << qPrintable(result.error);
  ASSERT_TRUE(queries[0].present);
  EXPECT_EQ(queries[0].value,
            QString::fromUtf8("Z:\\M\xC3\xB3" "ds Folder\t\"quoted\""));
}

TEST(WineRegistryFile, ParsesEqualsInsideAQuotedValueName)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("system.reg");
  ASSERT_TRUE(writeBytes(
      path,
      "[Software\\\\Vendor\\\\Game]\n\"A=B\"=\"exact-value\"\n"));

  QList<WineRegistryFile::Query> queries{
      {QStringLiteral("Software\\Vendor\\Game"), QStringLiteral("A=B")}};
  const auto result = WineRegistryFile::readValues(path, queries);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_TRUE(queries[0].present);
  EXPECT_EQ(queries[0].value, QStringLiteral("exact-value"));
}

TEST(WineRegistryFile, BatchRepairCreatesTheMissingArchitectureView)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("system.reg");
  ASSERT_TRUE(writeBytes(
      path,
      "[Software\\\\Vendor\\\\Game]\n\"Path\"=\"Z:\\\\Game\"\n"));

  const QString desired = QStringLiteral("Z:\\Game");
  const QList<WineRegistryFile::Update> updates{
      {QStringLiteral("Software\\Vendor\\Game"), QStringLiteral("Path"),
       desired, true, true, desired},
      {QStringLiteral("Software\\Wow6432Node\\Vendor\\Game"),
       QStringLiteral("Path"), desired, true, false, {}}};
  const auto result = WineRegistryFile::updateValues(path, updates);
  ASSERT_TRUE(result) << qPrintable(result.error);

  QList<WineRegistryFile::Query> repaired{
      {updates[0].section, updates[0].name},
      {updates[1].section, updates[1].name}};
  ASSERT_TRUE(WineRegistryFile::readValues(path, repaired));
  EXPECT_TRUE(repaired[0].present);
  EXPECT_TRUE(repaired[1].present);
  EXPECT_EQ(repaired[0].value, desired);
  EXPECT_EQ(repaired[1].value, desired);
}

TEST(WineRegistryFile, InsertsMissingSectionWithoutChangingFinalNewlinePolicy)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("system.reg");
  ASSERT_TRUE(writeBytes(path, "WINE REGISTRY Version 2"));

  const auto result = WineRegistryFile::updateValues(
      path, {{QStringLiteral("Software\\Vendor\\Game"), QStringLiteral("Path"),
              QStringLiteral("Z:\\Game")}});
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_EQ(readBytes(path), "WINE REGISTRY Version 2\n\n"
                             "[Software\\\\Vendor\\\\Game]\n"
                             "\"Path\"=\"Z:\\\\Game\"");
}

TEST(WineRegistryFile, RemovesOnlyExtraDriveValuesByteExactly)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("system.reg");
  const QByteArray original = "\xEF\xBB\xBF[Software\\\\Wine\\\\Drives] 1770000000\r\n"
                              "\"C:\"=\"/\"\r\n"
                              "\"X:\"=\"/games\"\n"
                              "\"Z:\"=\"/\"\r\n"
                              "\"Y:\"=\"/mods\"";
  ASSERT_TRUE(writeBytes(path, original));

  QStringList removed;
  const auto result = WineRegistryFile::removeDriveMappings(path, removed);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_EQ(removed, (QStringList{QStringLiteral("X:"), QStringLiteral("Y:")}));
  EXPECT_EQ(readBytes(path), "\xEF\xBB\xBF[Software\\\\Wine\\\\Drives] 1770000000\r\n"
                             "\"C:\"=\"/\"\r\n"
                             "\"Z:\"=\"/\"");
}

TEST(WineRegistryFile, ReadOnlyChangesPreserveTheOriginal)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("system.reg");
  const QByteArray original =
      "[Software\\\\Vendor\\\\Game]\n"
      "\"Path\"=\"old\"\n"
      "[Software\\\\Wine\\\\Drives]\n"
      "\"X:\"=\"/games\"\n";
  ASSERT_TRUE(writeBytes(path, original));
#ifdef Q_OS_UNIX
  ASSERT_EQ(::chmod(QFile::encodeName(path).constData(), 0400), 0);
#else
  ASSERT_TRUE(QFile::setPermissions(path, QFileDevice::ReadOwner));
#endif

  const auto update = WineRegistryFile::updateValues(
      path, {{QStringLiteral("Software\\Vendor\\Game"),
              QStringLiteral("Path"), QStringLiteral("new")}});
  EXPECT_FALSE(update);
  QStringList removed;
  EXPECT_FALSE(WineRegistryFile::removeDriveMappings(path, removed));
  EXPECT_TRUE(removed.isEmpty());
  EXPECT_EQ(readBytes(path), original);
}

#ifdef Q_OS_UNIX
TEST(WineRegistryFile, RejectsExternalSymlinkAndFifoWithoutFollowing)
{
  QTemporaryDir temporary;
  QTemporaryDir external;
  ASSERT_TRUE(temporary.isValid());
  ASSERT_TRUE(external.isValid());
  const QString outside = external.filePath("system.reg");
  const QString linked = temporary.filePath("linked.reg");
  const QByteArray sentinel = "[Software\\\\Vendor\\\\Game]\n\"Path\"=\"sentinel\"\n";
  ASSERT_TRUE(writeBytes(outside, sentinel));
  ASSERT_EQ(::symlink(QFile::encodeName(outside).constData(),
                      QFile::encodeName(linked).constData()),
            0);
  const auto linkedResult = WineRegistryFile::updateValues(
      linked, {{QStringLiteral("Software\\Vendor\\Game"), QStringLiteral("Path"),
                QStringLiteral("changed")}});
  EXPECT_FALSE(linkedResult);
  EXPECT_EQ(readBytes(outside), sentinel);

  const QString fifo = temporary.filePath("fifo.reg");
  ASSERT_EQ(::mkfifo(QFile::encodeName(fifo).constData(), 0600), 0);
  QStringList removed;
  EXPECT_FALSE(WineRegistryFile::removeDriveMappings(fifo, removed));
  struct stat status;
  ASSERT_EQ(::lstat(QFile::encodeName(fifo).constData(), &status), 0);
  EXPECT_TRUE(S_ISFIFO(status.st_mode));
}
#endif
