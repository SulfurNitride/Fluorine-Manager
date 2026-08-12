#include "stylesheetpath.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

namespace
{
void writeFile(const QString& path, const QByteArray& contents = {})
{
  ASSERT_TRUE(QDir().mkpath(QFileInfo(path).absolutePath()));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write(contents), contents.size());
}
}  // namespace

TEST(StyleSheetPath, ResolvesPortableInstanceStyleWithInstalledPrecedence)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString application = temporary.filePath("application");
  const QString instance    = temporary.filePath("instance");
  const QString installed   = application + "/stylesheets/shared.qss";
  const QString portable    = instance + "/stylesheets/portable.qss";
  writeFile(installed, "installed");
  writeFile(instance + "/stylesheets/shared.qss", "instance");
  writeFile(portable, "portable");

  const auto directories =
      StyleSheetPath::searchDirectories(application, instance);
  EXPECT_EQ(StyleSheetPath::resolve("shared.qss", directories),
            QFileInfo(installed).canonicalFilePath());
  EXPECT_EQ(StyleSheetPath::resolve("portable.qss", directories),
            QFileInfo(portable).canonicalFilePath());
  EXPECT_EQ(StyleSheetPath::available(directories),
            QStringList({"shared.qss", "portable.qss"}));
}

TEST(StyleSheetPath, RejectsPathsAndEscapingSymlinks)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString application = temporary.filePath("application");
  const QString outside     = temporary.filePath("outside.qss");
  const QString styles      = application + "/stylesheets";
  writeFile(outside, "outside");
  ASSERT_TRUE(QDir().mkpath(styles));
  ASSERT_TRUE(QFile::link(outside, styles + "/escape.qss"));

  const auto directories =
      StyleSheetPath::searchDirectories(application, QString());
  EXPECT_TRUE(StyleSheetPath::resolve("../outside.qss", directories).isEmpty());
  EXPECT_TRUE(StyleSheetPath::resolve(outside, directories).isEmpty());
  EXPECT_TRUE(StyleSheetPath::resolve("escape.qss", directories).isEmpty());
  EXPECT_TRUE(StyleSheetPath::available(directories).isEmpty());
}

TEST(StyleSheetPath, PreservesDistinctCaseSensitiveThemeNames)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString application = temporary.filePath("application");
  const QString instance    = temporary.filePath("instance");
  const QString installed   = application + "/stylesheets/Dark.qss";
  const QString portable    = instance + "/stylesheets/dark.qss";
  writeFile(installed, "installed");
  writeFile(portable, "portable");

  const auto directories =
      StyleSheetPath::searchDirectories(application, instance);
  EXPECT_EQ(StyleSheetPath::available(directories),
            QStringList({"Dark.qss", "dark.qss"}));
  EXPECT_EQ(StyleSheetPath::resolve("dark.qss", directories),
            QFileInfo(portable).canonicalFilePath());
}

TEST(StyleSheetPath, RejectsStylesheetDirectoryResolvingToItsRoot)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString application = temporary.filePath("application");
  ASSERT_TRUE(QDir().mkpath(application));
  ASSERT_TRUE(QFile::link(application, application + "/stylesheets"));

  EXPECT_TRUE(
      StyleSheetPath::searchDirectories(application, QString()).isEmpty());
}

TEST(StyleSheetPath, RejectsStylesheetDirectoryOutsideItsRoot)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString application = temporary.filePath("application");
  const QString outside     = temporary.filePath("outside");
  writeFile(outside + "/external.qss", "outside");
  ASSERT_TRUE(QDir().mkpath(application));
  ASSERT_TRUE(QFile::link(outside, application + "/stylesheets"));

  EXPECT_TRUE(
      StyleSheetPath::searchDirectories(application, QString()).isEmpty());
}

TEST(StyleSheetPath, ResolvesAssetsCaseInsensitivelyWithoutChangingTheTheme)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString styles = temporary.filePath("stylesheets");
  const QString asset  = styles + "/Arrows/down.svg";
  writeFile(asset, "svg");

  const QStringList before =
      QDir(styles).entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
  ASSERT_FALSE(QFileInfo::exists(styles + "/Arrows/Down.svg"));
  EXPECT_EQ(StyleSheetPath::resolveAsset("arrows/Down.svg", styles),
            QFileInfo(asset).canonicalFilePath());
  EXPECT_EQ(StyleSheetPath::resolveAsset(":/icons/down.svg", styles),
            ":/icons/down.svg");
  EXPECT_EQ(StyleSheetPath::resolveAsset("https://example.test/a.svg", styles),
            "https://example.test/a.svg");
  EXPECT_TRUE(
      StyleSheetPath::resolveAsset("../outside.svg", styles).isEmpty());
  EXPECT_EQ(QDir(styles).entryList(QDir::AllEntries | QDir::NoDotAndDotDot),
            before);
  EXPECT_FALSE(QFileInfo::exists(styles + "/Arrows/Down.svg"));
}

TEST(StyleSheetPath, PrefersExactAssetAndRejectsAmbiguousCaseMatch)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString styles = temporary.filePath("stylesheets");
  const QString exact  = styles + "/Icons/Marker.svg";
  writeFile(exact, "exact");
  writeFile(styles + "/icons/other.svg", "ambiguous directory");

  EXPECT_EQ(StyleSheetPath::resolveAsset("Icons/Marker.svg", styles),
            QFileInfo(exact).canonicalFilePath());
  EXPECT_EQ(StyleSheetPath::resolveAsset("ICONS/missing.svg", styles),
            QDir(styles).absoluteFilePath("ICONS/missing.svg"));
}

TEST(StyleSheetPath, RewritesAssetsWithSafeFallbackAndQuotedPaths)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString styles = temporary.filePath("NEMAS (copy)/stylesheets");
  const QString asset  = styles + "/Arrows/down.svg";
  writeFile(asset, "svg");

  const QString rewritten = StyleSheetPath::resolveAssets(
      "QWidget { image: url('Arrows/Down.svg'); } "
      "QLabel { image: url(missing.svg); } "
      "QFrame { image: url(../outside.svg); }",
      styles);
  EXPECT_TRUE(rewritten.contains(
      QStringLiteral("url(\"%1\")").arg(QFileInfo(asset).canonicalFilePath())));
  EXPECT_TRUE(rewritten.contains(QStringLiteral("url(\"%1\")")
                                     .arg(QDir(styles).absoluteFilePath(
                                         "missing.svg"))));
  EXPECT_TRUE(rewritten.contains("url()"));
}
