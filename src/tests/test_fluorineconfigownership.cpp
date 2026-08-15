#include "fluorineconfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <gtest/gtest.h>

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
