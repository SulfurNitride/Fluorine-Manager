#include "plugindatapath.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

namespace
{
void writeFile(const QString& path)
{
  ASSERT_TRUE(QDir().mkpath(QFileInfo(path).absolutePath()));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write("plugin"), 6);
}
}  // namespace

TEST(PluginDataPath, UsesLegacyDataForDirectInstancePluginFile)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString base     = temporary.filePath("instance");
  const QString plugin   = base + "/plugins/pyCfg.py";
  const QString data     = base + "/plugins/data";
  const QString fallback = temporary.filePath("writable-default");
  writeFile(plugin);
  ASSERT_TRUE(QDir().mkpath(data));

  EXPECT_EQ(PluginDataPath::select(fallback, base + "/plugins", plugin),
            QDir::cleanPath(data));
}

TEST(PluginDataPath, UsesLegacyDataForDirectInstancePluginPackage)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString base     = temporary.filePath("instance");
  const QString package  = base + "/plugins/configurator";
  const QString data     = base + "/plugins/data";
  const QString fallback = temporary.filePath("writable-default");
  ASSERT_TRUE(QDir().mkpath(package));
  ASSERT_TRUE(QDir().mkpath(data));

  EXPECT_EQ(PluginDataPath::select(fallback, base + "/plugins", package),
            QDir::cleanPath(data));
}

TEST(PluginDataPath, KeepsWritableDefaultForNonInstancePlugins)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString base     = temporary.filePath("instance");
  const QString data     = base + "/plugins/data";
  const QString fallback = temporary.filePath("writable-default");
  const QString bundled  = temporary.filePath("installation/plugins/bundled.py");
  const QString collision =
      temporary.filePath("instance-extra/plugins/collision.py");
  ASSERT_TRUE(QDir().mkpath(data));
  writeFile(bundled);
  writeFile(collision);

  EXPECT_EQ(PluginDataPath::select(fallback, base + "/plugins", bundled),
            fallback);
  EXPECT_EQ(PluginDataPath::select(fallback, base + "/plugins", collision),
            fallback);
}

TEST(PluginDataPath, KeepsWritableDefaultWithoutDistinctInstancePluginDirectory)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString plugin   = temporary.filePath("plugins/bundled.py");
  const QString fallback = temporary.filePath("writable-default");
  writeFile(plugin);
  ASSERT_TRUE(QDir().mkpath(temporary.filePath("plugins/data")));

  EXPECT_EQ(PluginDataPath::select(fallback, QString(), plugin), fallback);
}

TEST(PluginDataPath, KeepsWritableDefaultForNestedPluginLibrary)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString plugins  = temporary.filePath("instance/plugins");
  const QString library  = plugins + "/package/plugin.so";
  const QString fallback = temporary.filePath("writable-default");
  writeFile(library);
  ASSERT_TRUE(QDir().mkpath(plugins + "/data"));

  EXPECT_EQ(PluginDataPath::select(fallback, plugins, library), fallback);
}

TEST(PluginDataPath, KeepsWritableDefaultWhenLegacyDataIsNotUsable)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString fallback = temporary.filePath("writable-default");

  const QString missingBase   = temporary.filePath("missing-instance");
  const QString missingPlugin = missingBase + "/plugins/plugin.py";
  writeFile(missingPlugin);
  EXPECT_EQ(PluginDataPath::select(fallback, missingBase + "/plugins",
                                   missingPlugin),
            fallback);

  const QString fileBase   = temporary.filePath("file-instance");
  const QString filePlugin = fileBase + "/plugins/plugin.py";
  writeFile(filePlugin);
  writeFile(fileBase + "/plugins/data");
  EXPECT_EQ(PluginDataPath::select(fallback, fileBase + "/plugins", filePlugin),
            fallback);

  const QString lockedBase   = temporary.filePath("locked-instance");
  const QString lockedPlugin = lockedBase + "/plugins/plugin.py";
  const QString lockedData   = lockedBase + "/plugins/data";
  writeFile(lockedPlugin);
  ASSERT_TRUE(QDir().mkpath(lockedData));
  ASSERT_TRUE(QFile::setPermissions(
      lockedData, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
  EXPECT_EQ(PluginDataPath::select(fallback, lockedBase + "/plugins",
                                   lockedPlugin),
            fallback);
}

TEST(PluginDataPath, SupportsSymlinkedInstancePluginDirectory)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString base        = temporary.filePath("instance");
  const QString realPlugins = temporary.filePath("real-plugins");
  const QString plugin      = realPlugins + "/plugin.py";
  const QString data        = realPlugins + "/data";
  const QString fallback    = temporary.filePath("writable-default");
  ASSERT_TRUE(QDir().mkpath(base));
  ASSERT_TRUE(QDir().mkpath(data));
  writeFile(plugin);
  ASSERT_TRUE(QFile::link(realPlugins, base + "/plugins"));

  EXPECT_EQ(PluginDataPath::select(fallback, base + "/plugins", plugin),
            QDir::cleanPath(base + "/plugins/data"));
}
