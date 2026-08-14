#include "instancegeneralsettings.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QSettings>
#include <QTemporaryDir>

namespace {
QString settingsPath(const QTemporaryDir &directory) {
  return directory.filePath(QStringLiteral("ModOrganizer.ini"));
}

void writeFile(const QString &path, const QByteArray &bytes) {
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write(bytes), bytes.size());
  file.close();
  ASSERT_EQ(file.error(), QFileDevice::NoError);
}
} // namespace

TEST(InstanceGeneralSettings, CanonicalWritesUsePhysicalGeneralSection) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const QString path = settingsPath(directory);

  {
    QSettings settings(path, QSettings::IniFormat);
    InstanceGeneralSettings::write(settings,
                                   InstanceGeneralSettings::Key::GameName,
                                   QStringLiteral("Skyrim Special Edition"));
    InstanceGeneralSettings::write(settings,
                                   InstanceGeneralSettings::Key::GamePath,
                                   QStringLiteral("Z:/Games/Skyrim"));
    InstanceGeneralSettings::write(
        settings, InstanceGeneralSettings::Key::Portable, true);
    settings.sync();
    ASSERT_EQ(settings.status(), QSettings::NoError);

    EXPECT_TRUE(settings.contains(QStringLiteral("gameName")));
    EXPECT_TRUE(settings.contains(QStringLiteral("gamePath")));
    EXPECT_TRUE(settings.contains(QStringLiteral("portable")));
    EXPECT_FALSE(settings.contains(QStringLiteral("General/gameName")));
  }

  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  const QByteArray bytes = file.readAll();
  EXPECT_TRUE(bytes.contains("[General]"));
  EXPECT_FALSE(bytes.contains("[%General]"));
}

TEST(InstanceGeneralSettings, ReadsExactLegacyNestedKeys) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const QString path = settingsPath(directory);
  writeFile(path, "[%General]\n"
                  "gameName=Legacy Game\n"
                  "gamePath=Z:/Legacy/Game\n"
                  "portable=true\n");

  QSettings settings(path, QSettings::IniFormat);
  ASSERT_EQ(settings.status(), QSettings::NoError);
  const auto gameName = InstanceGeneralSettings::read(
      settings, InstanceGeneralSettings::Key::GameName);
  const auto gamePath = InstanceGeneralSettings::read(
      settings, InstanceGeneralSettings::Key::GamePath);
  const auto portable = InstanceGeneralSettings::read(
      settings, InstanceGeneralSettings::Key::Portable);
  ASSERT_TRUE(gameName);
  ASSERT_TRUE(gamePath);
  ASSERT_TRUE(portable);
  EXPECT_EQ(gameName->toString(), QStringLiteral("Legacy Game"));
  EXPECT_EQ(gamePath->toString(), QStringLiteral("Z:/Legacy/Game"));
  EXPECT_TRUE(portable->toBool());
}

TEST(InstanceGeneralSettings, PresentCanonicalValuesWinConflicts) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const QString path = settingsPath(directory);
  writeFile(path, "[General]\n"
                  "gameName=\n"
                  "portable=false\n"
                  "[%General]\n"
                  "gameName=Legacy Game\n"
                  "portable=true\n");

  QSettings settings(path, QSettings::IniFormat);
  const auto gameName = InstanceGeneralSettings::read(
      settings, InstanceGeneralSettings::Key::GameName);
  const auto portable = InstanceGeneralSettings::read(
      settings, InstanceGeneralSettings::Key::Portable);
  ASSERT_TRUE(gameName);
  ASSERT_TRUE(portable);
  EXPECT_TRUE(gameName->toString().isEmpty());
  EXPECT_FALSE(portable->toBool());
}

TEST(InstanceGeneralSettings, WritesPreserveLegacyAndUnrelatedKeys) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const QString path = settingsPath(directory);
  writeFile(path, "[General]\n"
                  "unrelated=keep me\n"
                  "[%General]\n"
                  "gameName=Legacy Game\n");

  {
    QSettings settings(path, QSettings::IniFormat);
    InstanceGeneralSettings::write(settings,
                                   InstanceGeneralSettings::Key::GameName,
                                   QStringLiteral("Canonical Game"));
    settings.sync();
    ASSERT_EQ(settings.status(), QSettings::NoError);
  }

  QSettings settings(path, QSettings::IniFormat);
  EXPECT_EQ(settings.value(QStringLiteral("unrelated")).toString(),
            QStringLiteral("keep me"));
  EXPECT_EQ(settings.value(QStringLiteral("General/gameName")).toString(),
            QStringLiteral("Legacy Game"));
  const auto gameName = InstanceGeneralSettings::read(
      settings, InstanceGeneralSettings::Key::GameName);
  ASSERT_TRUE(gameName);
  EXPECT_EQ(gameName->toString(), QStringLiteral("Canonical Game"));
}

TEST(InstanceGeneralSettings, DoesNotInterpretUnrelatedNestedKeys) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const QString path = settingsPath(directory);
  writeFile(path, "[%General]\n"
                  "game_name=Wrong Name\n"
                  "gameDirectory=Z:/Wrong/Game\n"
                  "isPortable=true\n");

  QSettings settings(path, QSettings::IniFormat);
  EXPECT_FALSE(InstanceGeneralSettings::read(
      settings, InstanceGeneralSettings::Key::GameName));
  EXPECT_FALSE(InstanceGeneralSettings::read(
      settings, InstanceGeneralSettings::Key::GamePath));
  EXPECT_FALSE(InstanceGeneralSettings::read(
      settings, InstanceGeneralSettings::Key::Portable));
}
