#include "fluorineconfig.h"

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
FluorineConfig makePrefix(const QString& root)
{
  FluorineConfig config;
  config.prefix_path = QDir(root).filePath(QStringLiteral("pfx"));
  EXPECT_TRUE(QDir().mkpath(QDir(config.prefix_path).filePath("drive_c")));
  return config;
}
}  // namespace

TEST(FluorineConfigOwnership, RefusesUnmarkedCustomPrefix)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const FluorineConfig config = makePrefix(QDir(temporary.path()).filePath("external"));

  EXPECT_FALSE(config.canDestroyPrefix());
}

TEST(FluorineConfigOwnership, TreatsDirectCompatibilityRootAsDeletionBoundary)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());

  const QString root = QDir(temporary.path()).filePath("direct-root");
  ASSERT_TRUE(QDir().mkpath(QDir(root).filePath("pfx/drive_c")));

  FluorineConfig config;
  config.prefix_path = root;

  EXPECT_EQ(config.compatDataPath(), QDir::cleanPath(root));
  EXPECT_FALSE(config.canDestroyPrefix());

  ASSERT_TRUE(config.markPrefixOwned());
  EXPECT_TRUE(config.canDestroyPrefix());
}

TEST(FluorineConfigOwnership, AcceptsMarkedManagedPrefix)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const FluorineConfig config = makePrefix(QDir(temporary.path()).filePath("managed"));

  ASSERT_TRUE(config.markPrefixOwned());
  EXPECT_TRUE(config.canDestroyPrefix());
  EXPECT_TRUE(QFile::exists(
      QDir(config.compatDataPath()).filePath(".fluorine-managed-prefix")));
  QFile marker(QDir(config.compatDataPath()).filePath(".fluorine-managed-prefix"));
  ASSERT_TRUE(marker.open(QIODevice::ReadOnly));
  EXPECT_EQ(marker.readAll(), "Fluorine Manager managed prefix\n");
#ifdef Q_OS_UNIX
  struct stat status{};
  ASSERT_EQ(::stat(QFile::encodeName(marker.fileName()).constData(), &status), 0);
  EXPECT_EQ(status.st_mode & 0777, 0600);
#endif
}

TEST(FluorineConfigOwnership, RefusesAndPreservesCollidingMarker)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const FluorineConfig config = makePrefix(QDir(temporary.path()).filePath("managed"));
  const QString markerPath    = QDir(config.compatDataPath()).filePath(".fluorine-managed-prefix");
  QFile marker(markerPath);
  ASSERT_TRUE(marker.open(QIODevice::WriteOnly));
  ASSERT_EQ(marker.write("unrelated marker\n"), 17);
  marker.close();

  EXPECT_FALSE(config.canDestroyPrefix());
  EXPECT_FALSE(config.markPrefixOwned());
  ASSERT_TRUE(marker.open(QIODevice::ReadOnly));
  EXPECT_EQ(marker.readAll(), "unrelated marker\n");
}

TEST(FluorineConfigOwnership, RefusesShortOwnershipMarker)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const FluorineConfig config = makePrefix(QDir(temporary.path()).filePath("managed"));
  QFile marker(QDir(config.compatDataPath()).filePath(".fluorine-managed-prefix"));
  ASSERT_TRUE(marker.open(QIODevice::WriteOnly));
  ASSERT_EQ(marker.write("Fluorine"), 8);
  marker.close();

  EXPECT_FALSE(config.canDestroyPrefix());
  EXPECT_FALSE(config.markPrefixOwned());
}

TEST(FluorineConfigOwnership, DirectRootRecreationRestoresOwnershipMarker)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());

  const QString root = QDir(temporary.path()).filePath("direct-root");
  ASSERT_TRUE(QDir().mkpath(QDir(root).filePath("pfx/drive_c")));

  FluorineConfig config;
  config.prefix_path = root;
  ASSERT_TRUE(config.markPrefixOwned());

  ASSERT_TRUE(config.resetPrefixForRecreation());
  EXPECT_FALSE(config.prefixExists());
  EXPECT_TRUE(config.canDestroyPrefix());
  EXPECT_TRUE(
      QFile::exists(QDir(root).filePath(".fluorine-managed-prefix")));
}

TEST(FluorineConfigOwnership, FailedConventionalRecreationRemainsDestroyable)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());

  const QByteArray oldConfigHome = qgetenv("XDG_CONFIG_HOME");
  const QString configHome = QDir(temporary.path()).filePath("config");
  ASSERT_TRUE(qputenv("XDG_CONFIG_HOME", configHome.toUtf8()));

  const QString compatData = QDir(temporary.path()).filePath("compatdata/1234");
  FluorineConfig config = makePrefix(compatData);
  ASSERT_TRUE(config.markPrefixOwned());
  ASSERT_TRUE(config.save());
  ASSERT_TRUE(config.resetPrefixForRecreation());
  EXPECT_FALSE(config.prefixExists());
  EXPECT_TRUE(config.canDestroyPrefix());
  EXPECT_TRUE(config.destroyPrefix());
  EXPECT_FALSE(QFileInfo::exists(compatData));

  if (oldConfigHome.isNull()) {
    qunsetenv("XDG_CONFIG_HOME");
  } else {
    qputenv("XDG_CONFIG_HOME", oldConfigHome);
  }
}

TEST(FluorineConfigOwnership, DeletesOnlyMarkedCompatibilityRoot)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());

  const QByteArray oldConfigHome = qgetenv("XDG_CONFIG_HOME");
  const QString configHome = QDir(temporary.path()).filePath("config");
  ASSERT_TRUE(qputenv("XDG_CONFIG_HOME", configHome.toUtf8()));

  const QString root = QDir(temporary.path()).filePath("managed");
  const FluorineConfig config = makePrefix(root);
  ASSERT_TRUE(config.markPrefixOwned());
  ASSERT_TRUE(config.save());

  EXPECT_TRUE(config.destroyPrefix());
  EXPECT_FALSE(QFile::exists(root));
  EXPECT_FALSE(QFile::exists(QDir(configHome).filePath("fluorine/config.json")));

  if (oldConfigHome.isNull()) {
    qunsetenv("XDG_CONFIG_HOME");
  } else {
    qputenv("XDG_CONFIG_HOME", oldConfigHome);
  }
}

TEST(FluorineConfigOwnership, AcceptsLegacyDefaultPrefixWithoutMarker)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());

  const QByteArray oldHome = qgetenv("HOME");
  ASSERT_TRUE(qputenv("HOME", temporary.path().toUtf8()));

  const QString root =
      QDir(temporary.path()).filePath(".local/share/fluorine/Prefix");
  const FluorineConfig config = makePrefix(root);
  EXPECT_TRUE(config.canDestroyPrefix());

  if (oldHome.isNull()) {
    qunsetenv("HOME");
  } else {
    qputenv("HOME", oldHome);
  }
}

TEST(FluorineConfigOwnership, RefusesSymlinkedCompatibilityRoot)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());

  const QString target = QDir(temporary.path()).filePath("target");
  ASSERT_TRUE(QDir().mkpath(QDir(target).filePath("pfx/drive_c")));

  const QString link = QDir(temporary.path()).filePath("linked-root");
  ASSERT_TRUE(QFile::link(target, link));

  FluorineConfig config;
  config.prefix_path = QDir(link).filePath("pfx");
  EXPECT_FALSE(config.markPrefixOwned());
  EXPECT_FALSE(config.canDestroyPrefix());
}

#ifdef Q_OS_UNIX
TEST(FluorineConfigOwnership, RefusesSymlinkedMarker)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const FluorineConfig config = makePrefix(QDir(temporary.path()).filePath("managed"));
  const QString outside       = temporary.filePath("outside-marker");
  QFile marker(outside);
  ASSERT_TRUE(marker.open(QIODevice::WriteOnly));
  ASSERT_EQ(marker.write("Fluorine Manager managed prefix\n"), 32);
  marker.close();
  const QString markerPath = QDir(config.compatDataPath()).filePath(".fluorine-managed-prefix");
  ASSERT_EQ(
      ::symlink(QFile::encodeName(outside).constData(), QFile::encodeName(markerPath).constData()),
      0);

  EXPECT_FALSE(config.canDestroyPrefix());
  EXPECT_FALSE(config.markPrefixOwned());
  EXPECT_TRUE(QFileInfo(markerPath).isSymLink());
}

TEST(FluorineConfigOwnership, RefusesSymlinkedAncestorWithoutCreatingResidue)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString outside = temporary.filePath("outside");
  ASSERT_TRUE(QDir().mkpath(outside));
  const QString alias = temporary.filePath("alias");
  ASSERT_EQ(::symlink(QFile::encodeName(outside).constData(), QFile::encodeName(alias).constData()),
            0);

  FluorineConfig config;
  config.prefix_path = QDir(alias).filePath("managed/pfx");
  EXPECT_FALSE(config.markPrefixOwned());
  EXPECT_FALSE(config.canDestroyPrefix());
  EXPECT_FALSE(QFileInfo::exists(QDir(outside).filePath("managed")));
}
#endif

TEST(FluorineConfigOwnership, RefusesSymlinkedPfxInsideMarkedRoot)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());

  const QString target = QDir(temporary.path()).filePath("external-pfx");
  ASSERT_TRUE(QDir().mkpath(QDir(target).filePath("drive_c")));

  const QString root = QDir(temporary.path()).filePath("managed-root");
  ASSERT_TRUE(QDir().mkpath(root));
  ASSERT_TRUE(QFile::link(target, QDir(root).filePath("pfx")));

  QFile marker(QDir(root).filePath(".fluorine-managed-prefix"));
  ASSERT_TRUE(marker.open(QIODevice::WriteOnly));
  marker.close();

  FluorineConfig config;
  config.prefix_path = QDir(root).filePath("pfx");
  EXPECT_FALSE(config.canDestroyPrefix());
}
