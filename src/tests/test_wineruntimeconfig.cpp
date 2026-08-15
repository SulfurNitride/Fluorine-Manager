#include "wineruntimeconfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <filesystem>

namespace {

QString makePrefix(const QString &root, const QString &user = "steamuser") {
  EXPECT_TRUE(QDir().mkpath(QDir(root).filePath("drive_c/users/" + user)));
  return root;
}

QString makeProton(const QString &root) {
  EXPECT_TRUE(QDir().mkpath(QDir(root).filePath("files/bin")));
  const QString script = QDir(root).filePath("proton");
  QFile file(script);
  EXPECT_TRUE(file.open(QIODevice::WriteOnly));
  EXPECT_EQ(file.write("#!/bin/sh\nexit 0\n"), 17);
  file.close();
  EXPECT_TRUE(file.setPermissions(QFileDevice::ReadOwner |
                                  QFileDevice::WriteOwner |
                                  QFileDevice::ExeOwner));
  QFile wine(QDir(root).filePath("files/bin/wine"));
  EXPECT_TRUE(wine.open(QIODevice::WriteOnly));
  EXPECT_GT(wine.write("#!/bin/sh\nexit 0\n"), 0);
  wine.close();
  EXPECT_TRUE(wine.setPermissions(QFileDevice::ReadOwner |
                                  QFileDevice::WriteOwner |
                                  QFileDevice::ExeOwner));
  return root;
}

WineRuntimeConfig::Inputs baseInputs(const QTemporaryDir &temp) {
  WineRuntimeConfig::Inputs inputs;
  inputs.instanceIniPath =
      QDir(temp.path()).filePath("instance/ModOrganizer.ini");
  inputs.defaultConfigPath = QDir(temp.path()).filePath("config/config.json");
  return inputs;
}

} // namespace

TEST(WineRuntimeConfig, ExplicitInstanceValuesWinIndependently) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString explicitPrefix =
      makePrefix(QDir(temp.path()).filePath("explicit"));
  const QString defaultPrefix =
      makePrefix(QDir(temp.path()).filePath("default"));
  const QString explicitProton =
      makeProton(QDir(temp.path()).filePath("proton-a"));
  const QString defaultProton =
      makeProton(QDir(temp.path()).filePath("proton-b"));

  auto inputs = baseInputs(temp);
  inputs.explicitPrefixPresent = true;
  inputs.explicitPrefix = explicitPrefix;
  inputs.defaultPrefix = defaultPrefix;
  inputs.explicitProtonPresent = true;
  inputs.explicitProton = explicitProton;
  inputs.defaultProton = defaultProton;

  const auto resolved = WineRuntimeConfig::resolve(inputs);
  ASSERT_TRUE(resolved.valid()) << resolved.prefixError.toStdString()
                                << resolved.protonError.toStdString();
  EXPECT_EQ(resolved.prefixPath, QFileInfo(explicitPrefix).canonicalFilePath());
  EXPECT_EQ(resolved.protonPath, QFileInfo(explicitProton).canonicalFilePath());
  EXPECT_EQ(resolved.prefixSource, WineRuntimeConfig::Source::InstanceExplicit);
  EXPECT_EQ(resolved.protonSource, WineRuntimeConfig::Source::InstanceExplicit);
}

TEST(WineRuntimeConfig, FieldsUseTheirOwnHighestAvailableAuthority) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString explicitPrefix =
      makePrefix(QDir(temp.path()).filePath("explicit"));
  const QString defaultProton =
      makeProton(QDir(temp.path()).filePath("proton"));

  auto inputs = baseInputs(temp);
  inputs.explicitPrefixPresent = true;
  inputs.explicitPrefix = explicitPrefix;
  inputs.defaultProton = defaultProton;
  inputs.legacyPrefix = makePrefix(QDir(temp.path()).filePath("legacy"));

  const auto resolved = WineRuntimeConfig::resolve(inputs);
  ASSERT_TRUE(resolved.valid());
  EXPECT_EQ(resolved.prefixSource, WineRuntimeConfig::Source::InstanceExplicit);
  EXPECT_EQ(resolved.protonSource,
            WineRuntimeConfig::Source::ApplicationDefault);
}

TEST(WineRuntimeConfig, PresentDefaultWithEmptyFieldDoesNotFallThrough) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  auto inputs = baseInputs(temp);
  inputs.defaultConfigPresent = true;
  inputs.defaultProton = makeProton(QDir(temp.path()).filePath("proton"));
  inputs.legacyPrefix = makePrefix(QDir(temp.path()).filePath("legacy"));

  const auto resolved = WineRuntimeConfig::resolve(inputs);
  EXPECT_FALSE(resolved.prefixError.isEmpty());
  EXPECT_EQ(resolved.prefixSource,
            WineRuntimeConfig::Source::ApplicationDefault);
  EXPECT_TRUE(resolved.prefixPath.isEmpty());
  EXPECT_TRUE(resolved.protonError.isEmpty());
}

TEST(WineRuntimeConfig, MalformedInstanceNeverFallsThroughToDefault) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  auto inputs = baseInputs(temp);
  inputs.instanceConfigInvalid = true;
  inputs.defaultPrefix = makePrefix(QDir(temp.path()).filePath("default"));
  inputs.defaultProton = makeProton(QDir(temp.path()).filePath("proton"));

  const auto resolved = WineRuntimeConfig::resolve(inputs);
  EXPECT_TRUE(resolved.prefixPath.isEmpty());
  EXPECT_TRUE(resolved.protonPath.isEmpty());
  EXPECT_NE(resolved.prefixError.indexOf("instance configuration"), -1);
}

TEST(WineRuntimeConfig, InvalidExplicitValueNeverFallsThrough) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  auto inputs = baseInputs(temp);
  inputs.explicitPrefixPresent = true;
  inputs.explicitPrefix = QDir(temp.path()).filePath("missing");
  inputs.defaultPrefix = makePrefix(QDir(temp.path()).filePath("default"));

  const auto resolved = WineRuntimeConfig::resolve(inputs);
  EXPECT_FALSE(resolved.valid());
  EXPECT_TRUE(resolved.prefixPath.isEmpty());
  EXPECT_EQ(resolved.prefixSource, WineRuntimeConfig::Source::InstanceExplicit);
  EXPECT_NE(resolved.prefixError.indexOf("neither"), -1);
}

TEST(WineRuntimeConfig, MalformedApplicationDefaultNeverFallsThroughToLegacy) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  auto inputs = baseInputs(temp);
  inputs.defaultConfigInvalid = true;
  inputs.legacyPrefix = makePrefix(QDir(temp.path()).filePath("legacy"));
  inputs.legacyProton = makeProton(QDir(temp.path()).filePath("legacy-proton"));

  const auto resolved = WineRuntimeConfig::resolve(inputs);
  EXPECT_FALSE(resolved.valid());
  EXPECT_TRUE(resolved.prefixPath.isEmpty());
  EXPECT_TRUE(resolved.protonPath.isEmpty());
  EXPECT_EQ(resolved.prefixSource,
            WineRuntimeConfig::Source::ApplicationDefault);
  EXPECT_EQ(resolved.protonSource,
            WineRuntimeConfig::Source::ApplicationDefault);
}

TEST(WineRuntimeConfig, CompatdataRootNormalizesToItsPfx) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString compat = QDir(temp.path()).filePath("compat");
  const QString pfx = makePrefix(QDir(compat).filePath("pfx"));
  auto inputs = baseInputs(temp);
  inputs.defaultPrefix = compat;

  const auto resolved = WineRuntimeConfig::resolve(inputs);
  ASSERT_TRUE(resolved.valid()) << resolved.prefixError.toStdString();
  EXPECT_EQ(resolved.prefixPath, QFileInfo(pfx).canonicalFilePath());
  EXPECT_EQ(resolved.compatDataPath, QFileInfo(compat).canonicalFilePath());
}

TEST(WineRuntimeConfig, DirectPfxPathRetainsItsCompatdataParent) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString compat = QDir(temp.path()).filePath("compat");
  const QString pfx = makePrefix(QDir(compat).filePath("pfx"));
  auto inputs = baseInputs(temp);
  inputs.defaultPrefix = pfx;

  const auto resolved = WineRuntimeConfig::resolve(inputs);
  ASSERT_TRUE(resolved.valid()) << resolved.prefixError.toStdString();
  EXPECT_EQ(resolved.prefixPath, QFileInfo(pfx).canonicalFilePath());
  EXPECT_EQ(resolved.compatDataPath, QFileInfo(compat).canonicalFilePath());
}

TEST(WineRuntimeConfig, UppercasePfxLeafRemainsAPlainDirectPrefix) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString prefix = makePrefix(QDir(temp.path()).filePath("PFX"));
  auto inputs = baseInputs(temp);
  inputs.defaultPrefix = prefix;

  const auto resolved = WineRuntimeConfig::resolve(inputs);
  ASSERT_TRUE(resolved.valid()) << resolved.prefixError.toStdString();
  EXPECT_EQ(resolved.prefixPath, QFileInfo(prefix).canonicalFilePath());
  EXPECT_TRUE(resolved.compatDataPath.isEmpty());
}

#ifdef Q_OS_UNIX
TEST(WineRuntimeConfig, PrefixAliasNormalizesToPhysicalIdentity) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString physical = makePrefix(QDir(temp.path()).filePath("physical"));
  const QString alias = QDir(temp.path()).filePath("alias");
  std::error_code ec;
  std::filesystem::create_directory_symlink(
      QFile::encodeName(physical).constData(), QFile::encodeName(alias).constData(),
      ec);
  ASSERT_FALSE(ec) << ec.message();
  auto inputs = baseInputs(temp);
  inputs.defaultPrefix = alias;

  const auto resolved = WineRuntimeConfig::resolve(inputs);
  ASSERT_TRUE(resolved.valid()) << resolved.prefixError.toStdString();
  EXPECT_EQ(resolved.prefixPath, QFileInfo(physical).canonicalFilePath());
}

TEST(WineRuntimeConfig, StructuralPfxSymlinkIsRejected) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString physical = makePrefix(QDir(temp.path()).filePath("physical"));
  const QString compat = QDir(temp.path()).filePath("compat");
  ASSERT_TRUE(QDir().mkpath(compat));
  std::error_code ec;
  std::filesystem::create_directory_symlink(
      QFile::encodeName(physical).constData(),
      QFile::encodeName(QDir(compat).filePath("pfx")).constData(), ec);
  ASSERT_FALSE(ec) << ec.message();
  auto inputs = baseInputs(temp);
  inputs.defaultPrefix = compat;

  const auto resolved = WineRuntimeConfig::resolve(inputs);
  EXPECT_FALSE(resolved.valid());
  EXPECT_NE(resolved.prefixError.indexOf("symlink"), -1);
}

TEST(WineRuntimeConfig, SymlinkedWineUserIsRejected) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString prefix = makePrefix(QDir(temp.path()).filePath("prefix"));
  const QString external = QDir(temp.path()).filePath("external-user");
  ASSERT_TRUE(QDir().mkpath(external));
  const QString alias = QDir(prefix).filePath("drive_c/users/alice");
  std::error_code ec;
  std::filesystem::create_directory_symlink(
      QFile::encodeName(external).constData(), QFile::encodeName(alias).constData(),
      ec);
  ASSERT_FALSE(ec) << ec.message();
  auto inputs = baseInputs(temp);
  inputs.defaultPrefix = prefix;

  const auto resolved = WineRuntimeConfig::resolve(inputs);
  EXPECT_NE(resolved.prefixError.indexOf("symlinked user profile"), -1);
}
#endif

TEST(WineRuntimeConfig, AmbiguousDirectAndNestedLayoutsAreRejected) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString root = makePrefix(QDir(temp.path()).filePath("ambiguous"));
  makePrefix(QDir(root).filePath("pfx"));
  auto inputs = baseInputs(temp);
  inputs.defaultPrefix = root;

  const auto resolved = WineRuntimeConfig::resolve(inputs);
  EXPECT_FALSE(resolved.valid());
  EXPECT_NE(resolved.prefixError.indexOf("both"), -1);
}

TEST(WineRuntimeConfig, RelativeInstanceOverrideIsAnchoredAtTheInstance) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  ASSERT_TRUE(QDir().mkpath(QDir(temp.path()).filePath("instance")));
  const QString prefix =
      makePrefix(QDir(temp.path()).filePath("instance/runtime"));
  auto inputs = baseInputs(temp);
  inputs.explicitPrefixPresent = true;
  inputs.explicitPrefix = "runtime";

  const auto resolved = WineRuntimeConfig::resolve(inputs);
  ASSERT_TRUE(resolved.valid()) << resolved.prefixError.toStdString();
  EXPECT_EQ(resolved.prefixPath, QFileInfo(prefix).canonicalFilePath());
}

TEST(WineRuntimeConfig, OneNonSystemWineUserIsSelected) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString prefix =
      makePrefix(QDir(temp.path()).filePath("prefix"), "alice");
  ASSERT_TRUE(QDir().mkpath(QDir(prefix).filePath("drive_c/users/Default")));
  auto inputs = baseInputs(temp);
  inputs.defaultPrefix = prefix;

  const auto resolved = WineRuntimeConfig::resolve(inputs);
  ASSERT_TRUE(resolved.valid()) << resolved.prefixError.toStdString();
  EXPECT_EQ(resolved.userProfilePath,
            QFileInfo(QDir(prefix).filePath("drive_c/users/alice"))
                .canonicalFilePath());
}

TEST(WineRuntimeConfig, MultipleWineUsersAreRejected) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString prefix =
      makePrefix(QDir(temp.path()).filePath("prefix"), "alice");
  ASSERT_TRUE(QDir().mkpath(QDir(prefix).filePath("drive_c/users/bob")));
  auto inputs = baseInputs(temp);
  inputs.defaultPrefix = prefix;

  const auto resolved = WineRuntimeConfig::resolve(inputs);
  EXPECT_FALSE(resolved.valid());
  EXPECT_NE(resolved.prefixError.indexOf("multiple"), -1);
}

TEST(WineRuntimeConfig, PrefixWithoutARealWineUserIsRejected) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString prefix = QDir(temp.path()).filePath("prefix");
  ASSERT_TRUE(QDir().mkpath(QDir(prefix).filePath("drive_c/users/Public")));
  auto inputs = baseInputs(temp);
  inputs.defaultPrefix = prefix;

  const auto resolved = WineRuntimeConfig::resolve(inputs);
  EXPECT_NE(resolved.prefixError.indexOf("no usable user"), -1);
}

TEST(WineRuntimeConfig, NonExecutableProtonScriptIsRejected) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString proton = QDir(temp.path()).filePath("proton");
  ASSERT_TRUE(QDir().mkpath(proton));
  QFile script(QDir(proton).filePath("proton"));
  ASSERT_TRUE(script.open(QIODevice::WriteOnly));
  ASSERT_EQ(script.write("#!/bin/sh\n"), 10);
  script.close();
  ASSERT_TRUE(script.setPermissions(QFileDevice::ReadOwner |
                                    QFileDevice::WriteOwner));
  auto inputs = baseInputs(temp);
  inputs.defaultProton = proton;

  const auto resolved = WineRuntimeConfig::resolve(inputs);
  EXPECT_FALSE(resolved.valid());
  EXPECT_NE(resolved.protonError.indexOf("not executable"), -1);
}

TEST(WineRuntimeConfig, ProtonDirectoryWithoutWinePayloadIsRejected) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString proton = QDir(temp.path()).filePath("proton");
  ASSERT_TRUE(QDir().mkpath(proton));
  QFile script(QDir(proton).filePath("proton"));
  ASSERT_TRUE(script.open(QIODevice::WriteOnly));
  ASSERT_GT(script.write("#!/bin/sh\n"), 0);
  script.close();
  ASSERT_TRUE(script.setPermissions(QFileDevice::ReadOwner |
                                    QFileDevice::WriteOwner |
                                    QFileDevice::ExeOwner));
  auto inputs = baseInputs(temp);
  inputs.defaultProton = proton;

  const auto resolved = WineRuntimeConfig::resolve(inputs);
  EXPECT_NE(resolved.protonError.indexOf("Wine runtime"), -1);
}

TEST(WineRuntimeConfig, RevalidationDetectsPrefixAndProtonReplacement) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString prefix = makePrefix(QDir(temp.path()).filePath("prefix"));
  const QString proton = makeProton(QDir(temp.path()).filePath("proton"));
  auto inputs = baseInputs(temp);
  inputs.defaultPrefix = prefix;
  inputs.defaultProton = proton;

  const auto resolved = WineRuntimeConfig::resolve(inputs);
  ASSERT_TRUE(resolved.valid());
  EXPECT_TRUE(WineRuntimeConfig::revalidate(resolved));

  QFile script(QDir(proton).filePath("proton"));
  ASSERT_TRUE(script.open(QIODevice::Append));
  ASSERT_EQ(script.write("# changed\n"), 10);
  script.close();
  QString error;
  EXPECT_FALSE(WineRuntimeConfig::revalidate(resolved, &error));
  EXPECT_NE(error.indexOf("Proton"), -1);
}

TEST(WineRuntimeConfig, RevalidationDetectsPrefixRootReplacement) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString prefix = makePrefix(QDir(temp.path()).filePath("prefix"));
  auto inputs = baseInputs(temp);
  inputs.defaultPrefix = prefix;
  const auto resolved = WineRuntimeConfig::resolve(inputs);
  ASSERT_TRUE(resolved.valid());

  const QString retired = QDir(temp.path()).filePath("retired-prefix");
  ASSERT_TRUE(QDir().rename(prefix, retired));
  makePrefix(prefix);
  QString error;
  EXPECT_FALSE(WineRuntimeConfig::revalidate(resolved, &error));
  EXPECT_NE(error.indexOf("prefix"), -1);
}

TEST(WineRuntimeConfig, PrefixOnlyRevalidationIgnoresAStaleProton) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString prefix = makePrefix(QDir(temp.path()).filePath("prefix"));
  auto inputs = baseInputs(temp);
  inputs.defaultPrefix = prefix;
  inputs.defaultProton = QDir(temp.path()).filePath("missing-proton");
  const auto resolved = WineRuntimeConfig::resolve(inputs);
  ASSERT_TRUE(resolved.prefixError.isEmpty());
  ASSERT_FALSE(resolved.protonError.isEmpty());

  EXPECT_TRUE(WineRuntimeConfig::revalidatePrefix(resolved));
  EXPECT_FALSE(WineRuntimeConfig::revalidate(resolved));
}

TEST(WineRuntimeConfig, RevalidationDetectsWineUserReplacement) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString prefix = makePrefix(QDir(temp.path()).filePath("prefix"));
  auto inputs = baseInputs(temp);
  inputs.defaultPrefix = prefix;
  const auto resolved = WineRuntimeConfig::resolve(inputs);
  ASSERT_TRUE(resolved.prefixError.isEmpty());

  const QString user = QDir(prefix).filePath("drive_c/users/steamuser");
  const QString retired = user + "-retired";
  ASSERT_TRUE(QDir().rename(user, retired));
  ASSERT_TRUE(QDir().mkpath(user));
  QString error;
  EXPECT_FALSE(WineRuntimeConfig::revalidatePrefix(resolved, &error));
  EXPECT_NE(error.indexOf("user profile"), -1);
}

TEST(WineRuntimeConfig, PublishedGenerationIsReplacedAndCleared) {
  WineRuntimeConfig::clear();
  WineRuntimeConfig::Snapshot first;
  ASSERT_TRUE(WineRuntimeConfig::publish(first));
  const auto firstPublished = WineRuntimeConfig::current();
  ASSERT_NE(firstPublished.generation, 0u);

  WineRuntimeConfig::Snapshot second;
  ASSERT_TRUE(WineRuntimeConfig::publish(second));
  EXPECT_GT(WineRuntimeConfig::current().generation, firstPublished.generation);
  WineRuntimeConfig::clear();
  EXPECT_EQ(WineRuntimeConfig::current().generation, 0u);
  EXPECT_FALSE(QCoreApplication::instance()
                   ->property(WineRuntimeConfig::UserProfileProperty)
                   .isValid());
}

int main(int argc, char **argv) {
  QCoreApplication application(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
