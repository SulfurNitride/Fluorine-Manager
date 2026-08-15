#include "steamdetection.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <uibase/log.h>

namespace {
void createFile(const QString &path) {
  ASSERT_TRUE(QDir().mkpath(QFileInfo(path).absolutePath()));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
}
} // namespace

class ProtonDetection : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    MOBase::log::LoggerConfiguration configuration;
    configuration.name = "test_protondetection";
    MOBase::log::createDefault(configuration);
  }
};

TEST_F(ProtonDetection, FindsHeroicRunnerWithoutSteamLibraries) {
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

TEST_F(ProtonDetection, RejectsRunnerWithoutWineBinary) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());

  const QString runner = QDir(temporary.path()).filePath("GE-Proton-latest");
  createFile(QDir(runner).filePath("proton"));

  EXPECT_TRUE(findProtonsForPaths({}, {temporary.path()}).isEmpty());
}

TEST_F(ProtonDetection, DeduplicatesCanonicalRunnerPaths) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());

  const QString runner = QDir(temporary.path()).filePath("GE-Proton-latest");
  createFile(QDir(runner).filePath("proton"));
  createFile(QDir(runner).filePath("files/bin/wine"));

  const QVector<SteamProtonInfo> protons =
      findProtonsForPaths({}, {temporary.path(), temporary.path()});

  ASSERT_EQ(protons.size(), 1);
}

TEST_F(ProtonDetection, PreservesSameNamedRunnersAtDifferentPaths) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());

  const QString first = QDir(temporary.path()).filePath("one/GE-Proton-latest");
  const QString second =
      QDir(temporary.path()).filePath("two/GE-Proton-latest");
  for (const QString &runner : {first, second}) {
    createFile(QDir(runner).filePath("proton"));
    createFile(QDir(runner).filePath("files/bin/wine"));
  }

  const QVector<SteamProtonInfo> protons =
      findProtonsForPaths({}, {QDir(temporary.path()).filePath("one"),
                               QDir(temporary.path()).filePath("two")});

  ASSERT_EQ(protons.size(), 2);
  EXPECT_NE(QFileInfo(protons[0].path).canonicalFilePath(),
            QFileInfo(protons[1].path).canonicalFilePath());
}
