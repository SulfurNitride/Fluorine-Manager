#include "steamdetection.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <uibase/log.h>

namespace
{
void createFile(const QString& path)
{
  ASSERT_TRUE(QDir().mkpath(QFileInfo(path).absolutePath()));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
}
}  // namespace

class ProtonDetection : public ::testing::Test
{
protected:
  static void SetUpTestSuite()
  {
    MOBase::log::LoggerConfiguration configuration;
    configuration.name = "test_protondetection";
    MOBase::log::createDefault(configuration);
  }
};

TEST_F(ProtonDetection, FindsHeroicRunnerWithoutSteamLibraries)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());

  const QString runner = QDir(temporary.path()).filePath("GE-Proton-latest");
  createFile(QDir(runner).filePath("proton"));
  createFile(QDir(runner).filePath("files/bin/wine"));

  const QVector<SteamProtonInfo> protons =
      findProtonsForPaths({}, {temporary.path()});

  ASSERT_EQ(protons.size(), 1);
  EXPECT_EQ(protons[0].name, QStringLiteral("GE-Proton-latest"));
  EXPECT_EQ(QDir::cleanPath(protons[0].path), QDir::cleanPath(runner));
  EXPECT_FALSE(protons[0].is_steam_proton);
}

TEST_F(ProtonDetection, RejectsRunnerWithoutWineBinary)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());

  const QString runner = QDir(temporary.path()).filePath("GE-Proton-latest");
  createFile(QDir(runner).filePath("proton"));

  EXPECT_TRUE(findProtonsForPaths({}, {temporary.path()}).isEmpty());
}

TEST_F(ProtonDetection, DeduplicatesCanonicalRunnerPaths)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());

  const QString runner = QDir(temporary.path()).filePath("GE-Proton-latest");
  createFile(QDir(runner).filePath("proton"));
  createFile(QDir(runner).filePath("files/bin/wine"));

  const QVector<SteamProtonInfo> protons =
      findProtonsForPaths({}, {temporary.path(), temporary.path()});

  ASSERT_EQ(protons.size(), 1);
}

TEST_F(ProtonDetection, HidesProtonOlderThanElevenUnlessAllowed)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());

  for (const char* name : {"GE-Proton10-34", "GE-Proton11-7"}) {
    const QString runner = QDir(temporary.path()).filePath(name);
    createFile(QDir(runner).filePath("proton"));
    createFile(QDir(runner).filePath("files/bin/wine"));
  }

  const QVector<SteamProtonInfo> supported =
      findProtonsForPaths({}, {temporary.path()});
  ASSERT_EQ(supported.size(), 1);
  EXPECT_EQ(supported[0].name, QStringLiteral("GE-Proton11-7"));

  const QVector<SteamProtonInfo> all =
      findProtonsForPaths({}, {temporary.path()}, true);
  EXPECT_EQ(all.size(), 2);
}

TEST_F(ProtonDetection, IsSupportedProtonChecksMajorVersion)
{
  auto named = [](const char* name) {
    SteamProtonInfo info;
    info.name = QString::fromLatin1(name);
    return info;
  };

  EXPECT_FALSE(isSupportedProton(named("GE-Proton10-34")));
  EXPECT_FALSE(isSupportedProton(named("Proton 10.0")));
  EXPECT_TRUE(isSupportedProton(named("GE-Proton11-7")));
  EXPECT_TRUE(isSupportedProton(named("Proton 11.0")));
  EXPECT_TRUE(isSupportedProton(named("GE-Proton-latest")));
}
