#include "gamebryoiniseeder.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace
{
void writeFile(const QString& path, const QByteArray& contents)
{
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write(contents), contents.size());
  file.close();
  ASSERT_EQ(file.error(), QFileDevice::NoError);
}

QByteArray readFile(const QString& path)
{
  QFile file(path);
  EXPECT_TRUE(file.open(QIODevice::ReadOnly));
  return file.readAll();
}
}  // namespace

TEST(GamebryoIniSeeder, PreservesShortAndEmptyExistingInis)
{
  QTemporaryDir root;
  ASSERT_TRUE(root.isValid());
  const QString defaults = QDir(root.path()).filePath("default.ini");
  writeFile(defaults, QByteArray(512, 'D'));

  const QString shortIni = QDir(root.path()).filePath("Game.ini");
  writeFile(shortIni, "[User]\nvalue=kept\n");
  auto result = GamebryoIniSeeder::ensure(root.path(), "Game.ini", defaults);
  ASSERT_TRUE(result.success) << result.error.toStdString();
  EXPECT_FALSE(result.seeded);
  EXPECT_EQ(readFile(shortIni), "[User]\nvalue=kept\n");
  result = GamebryoIniSeeder::ensure(root.path(), "Game.ini", defaults);
  ASSERT_TRUE(result.success) << result.error.toStdString();
  EXPECT_EQ(readFile(shortIni), "[User]\nvalue=kept\n");

  const QString emptyIni = QDir(root.path()).filePath("Prefs.ini");
  writeFile(emptyIni, {});
  result = GamebryoIniSeeder::ensure(root.path(), "Prefs.ini", defaults);
  ASSERT_TRUE(result.success) << result.error.toStdString();
  EXPECT_FALSE(result.seeded);
  EXPECT_TRUE(readFile(emptyIni).isEmpty());
}

TEST(GamebryoIniSeeder, AtomicallySeedsOnlyAMissingFamily)
{
  QTemporaryDir root;
  ASSERT_TRUE(root.isValid());
  const QString source = QDir(root.path()).filePath("source.default");
  writeFile(source, "[Display]\r\nquality=custom\r\n");
  ASSERT_TRUE(QFile::setPermissions(source, QFileDevice::ReadOwner));

  const auto result = GamebryoIniSeeder::ensure(root.path(), "Game.ini", source);
  ASSERT_TRUE(result.success) << result.error.toStdString();
  EXPECT_TRUE(result.seeded);
  EXPECT_EQ(readFile(QDir(root.path()).filePath("Game.ini")),
            "[Display]\r\nquality=custom\r\n");
  EXPECT_TRUE(QFileInfo(QDir(root.path()).filePath("Game.ini"))
                  .permissions()
                  .testFlag(QFileDevice::WriteOwner));

#ifndef _WIN32
  const QFileInfo lower(QDir(root.path()).filePath("game.ini"));
  EXPECT_TRUE(lower.isSymLink());
  EXPECT_EQ(lower.canonicalFilePath(),
            QFileInfo(result.authoritativePath).canonicalFilePath());
#endif
}

TEST(GamebryoIniSeeder, MissingDefaultIsANondestructiveNoOp)
{
  QTemporaryDir root;
  ASSERT_TRUE(root.isValid());

  const auto result = GamebryoIniSeeder::ensure(
      root.path(), "Game.ini", QDir(root.path()).filePath("missing.default"));
  EXPECT_TRUE(result.success) << result.error.toStdString();
  EXPECT_FALSE(result.seeded);
  EXPECT_TRUE(result.authoritativePath.isEmpty());
  EXPECT_FALSE(QFileInfo::exists(QDir(root.path()).filePath("Game.ini")));
}

#ifndef _WIN32
TEST(GamebryoIniSeeder, UsesOneDifferentlyCasedRealFileAsAuthority)
{
  QTemporaryDir root;
  ASSERT_TRUE(root.isValid());
  const QString existing = QDir(root.path()).filePath("GAME.ini");
  writeFile(existing, "unique-user-state");

  const auto result = GamebryoIniSeeder::ensure(root.path(), "Game.ini", {});
  ASSERT_TRUE(result.success) << result.error.toStdString();
  EXPECT_EQ(result.authoritativePath, existing);
  EXPECT_EQ(readFile(existing), "unique-user-state");
  EXPECT_TRUE(QFileInfo(QDir(root.path()).filePath("Game.ini")).isSymLink());
  EXPECT_TRUE(QFileInfo(QDir(root.path()).filePath("game.ini")).isSymLink());
}

TEST(GamebryoIniSeeder, RejectsDistinctRealCaseVariants)
{
  QTemporaryDir root;
  ASSERT_TRUE(root.isValid());
  const QString exact = QDir(root.path()).filePath("Game.ini");
  const QString lower = QDir(root.path()).filePath("game.ini");
  writeFile(exact, "first");
  writeFile(lower, "second");

  const auto result = GamebryoIniSeeder::ensure(root.path(), "Game.ini", {});
  EXPECT_FALSE(result.success);
  EXPECT_EQ(readFile(exact), "first");
  EXPECT_EQ(readFile(lower), "second");
}

TEST(GamebryoIniSeeder, RejectsDanglingForeignAndSpecialLeaves)
{
  QTemporaryDir root;
  QTemporaryDir external;
  ASSERT_TRUE(root.isValid());
  ASSERT_TRUE(external.isValid());

  const QString outside = QDir(external.path()).filePath("outside.ini");
  writeFile(outside, "sentinel");
  const QString exact = QDir(root.path()).filePath("Game.ini");
  ASSERT_TRUE(QFile::link(outside, exact));
  auto result = GamebryoIniSeeder::ensure(root.path(), "Game.ini", {});
  EXPECT_FALSE(result.success);
  EXPECT_EQ(readFile(outside), "sentinel");
  ASSERT_TRUE(QFile::remove(exact));

  ASSERT_TRUE(QFile::link("missing.ini", exact));
  result = GamebryoIniSeeder::ensure(root.path(), "Game.ini", {});
  EXPECT_FALSE(result.success);
  ASSERT_TRUE(QFile::remove(exact));

  const QString fifo = QDir(root.path()).filePath("game.ini");
  ASSERT_EQ(::mkfifo(QFile::encodeName(fifo).constData(), 0600), 0);
  result = GamebryoIniSeeder::ensure(root.path(), "Game.ini", {});
  EXPECT_FALSE(result.success);
}

TEST(GamebryoIniSeeder, RejectsUnsafeDefaultWithoutCreatingResidue)
{
  QTemporaryDir root;
  QTemporaryDir external;
  ASSERT_TRUE(root.isValid());
  ASSERT_TRUE(external.isValid());

  const QString outside = QDir(external.path()).filePath("outside.default");
  writeFile(outside, "sentinel");
  const QString source = QDir(root.path()).filePath("source.default");
  ASSERT_TRUE(QFile::link(outside, source));

  const auto result = GamebryoIniSeeder::ensure(root.path(), "Game.ini", source);
  EXPECT_FALSE(result.success);
  EXPECT_EQ(readFile(outside), "sentinel");
  EXPECT_FALSE(QFileInfo::exists(QDir(root.path()).filePath("Game.ini")));
  EXPECT_FALSE(QFileInfo::exists(QDir(root.path()).filePath("game.ini")));
}
#endif
