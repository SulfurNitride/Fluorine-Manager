#include "prefixsymlinks.h"
#include "gamedetection.h"
#include "wineprefix.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <uibase/log.h>

#include <algorithm>

namespace
{
QString prefixPath(const QTemporaryDir& temporary)
{
  return QDir(temporary.path()).filePath(QStringLiteral("pfx"));
}

QString appDataPath(const QString& prefix)
{
  return QDir(prefix).filePath(
      QStringLiteral("drive_c/users/steamuser/AppData/Local/Skyrim Special Edition"));
}

QString backupPath(const QString& prefix, int index = -1)
{
  const QString name = index < 0
      ? QStringLiteral("skyrim-special-edition-appdata.link")
      : QStringLiteral("skyrim-special-edition-appdata.link.%1").arg(index);
  return QDir(prefix).filePath(QStringLiteral(".fluorine/legacy-links/") + name);
}

bool writeFile(const QString& path, const QByteArray& contents)
{
  if (!QDir().mkpath(QFileInfo(path).absolutePath()))
    return false;
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly))
    return false;
  const qint64 written = file.write(contents);
  file.close();
  return written == contents.size();
}

QByteArray readFile(const QString& path)
{
  QFile file(path);
  EXPECT_TRUE(file.open(QIODevice::ReadOnly));
  return file.readAll();
}

void makePrefix(const QString& prefix)
{
  ASSERT_TRUE(QDir().mkpath(QDir(prefix).filePath(
      QStringLiteral("drive_c/users/steamuser/AppData/Local"))));
  ASSERT_TRUE(QDir().mkpath(QDir(prefix).filePath(
      QStringLiteral("drive_c/users/steamuser/Documents/My Games"))));
}
}  // namespace

class PrefixSymlinks : public ::testing::Test
{
protected:
  static void SetUpTestSuite()
  {
    MOBase::log::LoggerConfiguration configuration;
    configuration.name = "test_prefixsymlinks";
    MOBase::log::createDefault(configuration);
  }
};

TEST_F(PrefixSymlinks, IsolatesSkyrimAppDataWithoutTouchingSteamCatalogOrSaves)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());

  const QString prefix = prefixPath(temporary);
  makePrefix(prefix);

  const QString steamAppData = QDir(temporary.path()).filePath(
      QStringLiteral("steam/pfx/drive_c/users/steamuser/AppData/Local/Skyrim Special Edition"));
  const QByteArray catalog = "{\"CSV2_test\":{\"Version\":\"old\"}}\n";
  const QByteArray steamPlugins = "steam-owned\r\n";
  ASSERT_TRUE(writeFile(QDir(steamAppData).filePath(QStringLiteral("ContentCatalog.txt")), catalog));
  ASSERT_TRUE(writeFile(QDir(steamAppData).filePath(QStringLiteral("Plugins.txt")), steamPlugins));

  const QString appData = appDataPath(prefix);
  ASSERT_TRUE(QFile::link(steamAppData, appData));

  const QString savesTarget = QDir(temporary.path()).filePath(QStringLiteral("profile/saves"));
  ASSERT_TRUE(QDir().mkpath(savesTarget));
  const QString savesLink = QDir(prefix).filePath(
      QStringLiteral("drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition"));
  ASSERT_TRUE(QFile::link(savesTarget, savesLink));

  ASSERT_TRUE(ensureSkyrimSpecialEditionAppDataPrivate(prefix));

  EXPECT_TRUE(QFileInfo(appData).isDir());
  EXPECT_FALSE(QFileInfo(appData).isSymLink());
  EXPECT_FALSE(QFileInfo(QDir(appData).filePath(QStringLiteral("ContentCatalog.txt"))).exists());
  EXPECT_EQ(readFile(QDir(steamAppData).filePath(QStringLiteral("ContentCatalog.txt"))), catalog);
  EXPECT_EQ(readFile(QDir(steamAppData).filePath(QStringLiteral("Plugins.txt"))), steamPlugins);

  ASSERT_TRUE(QFileInfo(backupPath(prefix)).isSymLink());
  EXPECT_EQ(QFileInfo(backupPath(prefix)).symLinkTarget(), steamAppData);

  // AppData isolation must not alter the Documents/My Games save link.
  EXPECT_TRUE(QFileInfo(savesLink).isSymLink());
  EXPECT_EQ(QFileInfo(savesLink).symLinkTarget(), savesTarget);

  // The normal pre-launch deployment remains writable in the private folder.
  WinePrefix prefixObject(prefix);
  ASSERT_TRUE(prefixObject.deployPlugins(
      {QStringLiteral("*Skyrim.esm"), QStringLiteral("*ccbgssse013-dawnfang.esl")},
      QStringLiteral("Skyrim Special Edition"),
      WinePrefix::PluginListMechanism::PluginsTxt));
  EXPECT_EQ(readFile(QDir(appData).filePath(QStringLiteral("Plugins.txt"))),
            QByteArray("*Skyrim.esm\r\n*ccbgssse013-dawnfang.esl\r\n"));
  EXPECT_EQ(readFile(QDir(steamAppData).filePath(QStringLiteral("Plugins.txt"))), steamPlugins);

  // A second setup/pre-launch pass is a no-op and does not create another
  // rollback link.
  ASSERT_TRUE(ensureSkyrimSpecialEditionAppDataPrivate(prefix));
  EXPECT_TRUE(QFileInfo(backupPath(prefix)).isSymLink());
  EXPECT_FALSE(QFileInfo(backupPath(prefix, 1)).exists());
}

TEST_F(PrefixSymlinks, LeavesExistingRealDirectoryAndItsFilesAlone)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString prefix = prefixPath(temporary);
  makePrefix(prefix);

  const QString appData = appDataPath(prefix);
  const QByteArray marker = "prefix-owned\n";
  ASSERT_TRUE(writeFile(QDir(appData).filePath(QStringLiteral("marker.txt")), marker));

  ASSERT_TRUE(ensureSkyrimSpecialEditionAppDataPrivate(prefix));
  EXPECT_TRUE(QFileInfo(appData).isDir());
  EXPECT_FALSE(QFileInfo(appData).isSymLink());
  EXPECT_EQ(readFile(QDir(appData).filePath(QStringLiteral("marker.txt"))), marker);
  EXPECT_FALSE(QFileInfo(backupPath(prefix)).exists());
}

TEST_F(PrefixSymlinks, MigratesDanglingLinkWithoutFollowingOrDeletingItsTarget)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString prefix = prefixPath(temporary);
  makePrefix(prefix);

  const QString missingTarget = QDir(temporary.path()).filePath(QStringLiteral("removed-steam-prefix"));
  const QString appData = appDataPath(prefix);
  ASSERT_TRUE(QFile::link(missingTarget, appData));
  ASSERT_TRUE(QFileInfo(appData).isSymLink());

  ASSERT_TRUE(ensureSkyrimSpecialEditionAppDataPrivate(prefix));
  EXPECT_TRUE(QFileInfo(appData).isDir());
  EXPECT_FALSE(QFileInfo(appData).isSymLink());
  ASSERT_TRUE(QFileInfo(backupPath(prefix)).isSymLink());
  EXPECT_EQ(QFileInfo(backupPath(prefix)).symLinkTarget(), missingTarget);
}

TEST_F(PrefixSymlinks, RetainsRelativeLinkTargetAcrossRollbackBackup)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString prefix = prefixPath(temporary);
  makePrefix(prefix);

  const QString steamAppData = QDir(temporary.path()).filePath(QStringLiteral("steam-appdata"));
  const QByteArray catalog = "relative-catalog\n";
  ASSERT_TRUE(writeFile(QDir(steamAppData).filePath(QStringLiteral("ContentCatalog.txt")), catalog));
  const QString appData = appDataPath(prefix);
  const QString relativeTarget =
      QDir(QFileInfo(appData).absolutePath()).relativeFilePath(steamAppData);
  ASSERT_TRUE(QFile::link(relativeTarget, appData));

  ASSERT_TRUE(ensureSkyrimSpecialEditionAppDataPrivate(prefix));
  EXPECT_TRUE(QFileInfo(appData).isDir());
  ASSERT_TRUE(QFileInfo(backupPath(prefix)).isSymLink());
  EXPECT_EQ(readFile(QDir(backupPath(prefix)).filePath(QStringLiteral("ContentCatalog.txt"))),
            catalog);
  EXPECT_EQ(readFile(backupPath(prefix) + QStringLiteral(".target")),
            relativeTarget.toUtf8());
}

TEST_F(PrefixSymlinks, PreservesLinkAndTargetWhenRollbackDirectoryCannotBeCreated)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString prefix = prefixPath(temporary);
  makePrefix(prefix);

  const QString steamAppData = QDir(temporary.path()).filePath(QStringLiteral("steam-appdata"));
  const QByteArray catalog = "must-survive\n";
  ASSERT_TRUE(writeFile(QDir(steamAppData).filePath(QStringLiteral("ContentCatalog.txt")), catalog));
  const QString appData = appDataPath(prefix);
  ASSERT_TRUE(QFile::link(steamAppData, appData));

  // A regular file blocks the rollback directory. The original link must
  // remain in place and the target must remain untouched.
  ASSERT_TRUE(writeFile(QDir(prefix).filePath(QStringLiteral(".fluorine")), "sentinel"));
  EXPECT_FALSE(ensureSkyrimSpecialEditionAppDataPrivate(prefix));
  EXPECT_TRUE(QFileInfo(appData).isSymLink());
  EXPECT_EQ(QFileInfo(appData).symLinkTarget(), steamAppData);
  EXPECT_EQ(readFile(QDir(steamAppData).filePath(QStringLiteral("ContentCatalog.txt"))), catalog);
  EXPECT_EQ(readFile(QDir(prefix).filePath(QStringLiteral(".fluorine"))), QByteArray("sentinel"));
}

TEST_F(PrefixSymlinks, DoesNotOverwriteExistingRollbackFile)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString prefix = prefixPath(temporary);
  makePrefix(prefix);

  const QString steamAppData = QDir(temporary.path()).filePath(QStringLiteral("steam-appdata"));
  ASSERT_TRUE(writeFile(QDir(steamAppData).filePath(QStringLiteral("ContentCatalog.txt")), "catalog"));
  const QString appData = appDataPath(prefix);
  ASSERT_TRUE(QFile::link(steamAppData, appData));

  ASSERT_TRUE(writeFile(backupPath(prefix), "do-not-overwrite"));
  ASSERT_TRUE(ensureSkyrimSpecialEditionAppDataPrivate(prefix));

  EXPECT_EQ(readFile(backupPath(prefix)), QByteArray("do-not-overwrite"));
  EXPECT_TRUE(QFileInfo(backupPath(prefix, 1)).isSymLink());
  EXPECT_TRUE(QFileInfo(appData).isDir());
}

TEST_F(PrefixSymlinks, SkipsOrphanRollbackMetadata)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString prefix = prefixPath(temporary);
  makePrefix(prefix);

  const QString steamAppData = QDir(temporary.path()).filePath(QStringLiteral("steam-appdata"));
  ASSERT_TRUE(writeFile(QDir(steamAppData).filePath(QStringLiteral("ContentCatalog.txt")), "catalog"));
  const QString appData = appDataPath(prefix);
  ASSERT_TRUE(QFile::link(steamAppData, appData));

  // An interrupted prior migration may have left only its target sidecar.
  // It must reserve that rollback slot rather than overwrite the metadata.
  const QString orphanMetadata = backupPath(prefix) + QStringLiteral(".target");
  ASSERT_TRUE(writeFile(orphanMetadata, "old-target"));
  ASSERT_TRUE(ensureSkyrimSpecialEditionAppDataPrivate(prefix));

  EXPECT_EQ(readFile(orphanMetadata), QByteArray("old-target"));
  EXPECT_TRUE(QFileInfo(backupPath(prefix, 1)).isSymLink());
  EXPECT_TRUE(QFileInfo(appData).isDir());
}

TEST_F(PrefixSymlinks, AutoLinkScanLeavesSkyrimAppDataLinkPrivate)
{
  // Exercise the real automatic scan when this host has a detectable Skyrim
  // installation. The test is safely skipped on build hosts without Steam;
  // all migration behavior remains covered by the deterministic tests above.
  const GameScanResult detected = detectAllGames();
  const bool hasSkyrim = std::any_of(
      detected.games.cbegin(), detected.games.cend(), [](const DetectedGame& game) {
        return game.app_id == QStringLiteral("489830") && !game.prefix_path.isEmpty();
      });
  if (!hasSkyrim)
    GTEST_SKIP() << "No Steam Skyrim SE installation detected";

  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString prefix = prefixPath(temporary);
  makePrefix(prefix);

  const QString externalAppData =
      QDir(temporary.path()).filePath(QStringLiteral("external-appdata"));
  ASSERT_TRUE(writeFile(QDir(externalAppData).filePath(
                            QStringLiteral("ContentCatalog.txt")),
                        "must-not-be-linked"));
  const QString appData = appDataPath(prefix);
  ASSERT_TRUE(QFile::link(externalAppData, appData));

  createGameSymlinksAuto(prefix);

  EXPECT_TRUE(QFileInfo(appData).isSymLink());
  EXPECT_EQ(QFileInfo(appData).symLinkTarget(), externalAppData);
}

TEST_F(PrefixSymlinks, RejectsSymlinkedAncestorBeforeMovingExistingLink)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString prefix = prefixPath(temporary);
  makePrefix(prefix);

  const QString externalAppData = QDir(temporary.path()).filePath(QStringLiteral("external-appdata"));
  const QByteArray catalog = "ancestor-catalog\n";
  ASSERT_TRUE(writeFile(QDir(externalAppData).filePath(QStringLiteral("Skyrim Special Edition/ContentCatalog.txt")), catalog));

  const QString appDataRoot = QDir(prefix).filePath(
      QStringLiteral("drive_c/users/steamuser/AppData/Local"));
  ASSERT_TRUE(QDir(appDataRoot).removeRecursively());
  ASSERT_TRUE(QFile::link(externalAppData, appDataRoot));

  const QString appData = appDataPath(prefix);
  EXPECT_FALSE(ensureSkyrimSpecialEditionAppDataPrivate(prefix));
  EXPECT_TRUE(QFileInfo(appDataRoot).isSymLink());
  EXPECT_TRUE(QFileInfo(appData).isDir());
  EXPECT_EQ(readFile(QDir(externalAppData).filePath(
                QStringLiteral("Skyrim Special Edition/ContentCatalog.txt"))),
            catalog);
  EXPECT_FALSE(QFileInfo(backupPath(prefix)).exists());
}

TEST_F(PrefixSymlinks, AcceptsSymlinkedPrefixRoot)
{
  // umu exposes the prefix as `<dir>/pfx`, which is itself a symlink.
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString realPrefix = QDir(temporary.path()).filePath(QStringLiteral("real-prefix"));
  makePrefix(realPrefix);
  const QString prefix = prefixPath(temporary);
  ASSERT_TRUE(QFile::link(realPrefix, prefix));

  EXPECT_TRUE(ensureSkyrimSpecialEditionAppDataPrivate(prefix));
  EXPECT_TRUE(QFileInfo(appDataPath(prefix)).isDir());
  EXPECT_FALSE(QFileInfo(appDataPath(prefix)).isSymLink());
}

TEST_F(PrefixSymlinks, AcceptsSymlinkedAncestorInsidePrefix)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString prefix = prefixPath(temporary);
  makePrefix(prefix);

  const QString appDataRoot = QDir(prefix).filePath(
      QStringLiteral("drive_c/users/steamuser/AppData/Local"));
  const QString internal = QDir(prefix).filePath(QStringLiteral("drive_c/local-data"));
  ASSERT_TRUE(QDir(appDataRoot).removeRecursively());
  ASSERT_TRUE(QDir().mkpath(internal));
  ASSERT_TRUE(QFile::link(internal, appDataRoot));

  EXPECT_TRUE(ensureSkyrimSpecialEditionAppDataPrivate(prefix));
  EXPECT_TRUE(QFileInfo(QDir(internal).filePath(QStringLiteral("Skyrim Special Edition"))).isDir());
}

TEST_F(PrefixSymlinks, SkippingSaveDataLinksCreatesNoGameLinksAndDropsSharedOnes)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString prefix = prefixPath(temporary);
  makePrefix(prefix);

  const QString local =
      QDir(prefix).filePath(QStringLiteral("drive_c/users/steamuser/AppData/Local"));
  const QString staleTarget = QDir(temporary.path())
      .filePath(QStringLiteral("compatdata/123/pfx/drive_c/users/steamuser/AppData/"
                               "Local/dxvk"));
  ASSERT_TRUE(QDir().mkpath(staleTarget));
  ASSERT_TRUE(QFile::link(staleTarget, QDir(local).filePath(QStringLiteral("dxvk"))));

  createGameSymlinksAuto(prefix, false);

  EXPECT_FALSE(QFileInfo(QDir(local).filePath(QStringLiteral("dxvk"))).isSymLink());
  EXPECT_TRUE(QDir(staleTarget).exists());
  for (const char* dir : {"AppData/Local", "AppData/Roaming", "Documents/My Games"}) {
    const QDir d(QDir(prefix).filePath(QStringLiteral("drive_c/users/steamuser/") +
                                       QLatin1String(dir)));
    for (const QFileInfo& entry :
         d.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System))
      EXPECT_FALSE(entry.isSymLink()) << entry.absoluteFilePath().toStdString();
  }
}
