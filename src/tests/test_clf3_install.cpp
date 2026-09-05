#include "../src/clf3installutils.h"
#include "../src/clf3processcontroller.h"

#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <gtest/gtest.h>
#include <limits>

using namespace Clf3InstallUtils;

TEST(Clf3Preflight, CombinesSpaceOnSharedDrivesButKeepsOtherDrivesSeparate)
{
  const auto result = combineSpaceRequirements({
      {"disk-a", "Installed", 150, 100}, {"disk-a", "Downloads", 145, 60},
      {"disk-b", "Temporary", 80, 20}});
  ASSERT_EQ(result.size(), 2);
  EXPECT_EQ(result[0].required, 160);
  EXPECT_EQ(result[0].available, 145);
  EXPECT_GT(result[0].required, result[0].available);
  EXPECT_EQ(result[1].required, 20);
  EXPECT_EQ(result[1].available, 80);
}

TEST(Clf3Preflight, UnknownSpaceAndOverflowCannotLookSafe)
{
  const auto maximum = std::numeric_limits<qint64>::max();
  const auto result = combineSpaceRequirements({
      {"disk", "Installed", -1, maximum}, {"disk", "Downloads", 100, 10}});
  ASSERT_EQ(result.size(), 1);
  EXPECT_EQ(result[0].required, maximum);
  EXPECT_EQ(result[0].available, -1);
  EXPECT_EQ(temporarySpaceEstimate(0, 0), 2LL * 1024 * 1024 * 1024);
  EXPECT_EQ(temporarySpaceEstimate(100LL * 1024 * 1024 * 1024, 0), 20LL * 1024 * 1024 * 1024);
}

TEST(Clf3Preflight, CreatesNestedFoldersAndLeavesNoWriteProbe)
{
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const auto path = directory.filePath("nested/downloads");
  EXPECT_TRUE(prepareWritableDirectory(path).isEmpty());
  EXPECT_TRUE(QDir(path).isEmpty());
  QFile file(directory.filePath("file"));
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  EXPECT_FALSE(prepareWritableDirectory(file.fileName()).isEmpty());
  EXPECT_FALSE(prepareWritableDirectory(file.fileName() + "/child").isEmpty());
  EXPECT_FALSE(prepareWritableDirectory("relative/path").isEmpty());
}

TEST(Clf3Preflight, DetectsGameFolderOverlapThroughSymlinksAndMissingChildren)
{
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const auto game = directory.filePath("game");
  ASSERT_TRUE(QDir().mkpath(game));
  ASSERT_TRUE(QFile::link(game, directory.filePath("alias")));
  EXPECT_TRUE(pathsOverlap(game, directory.filePath("alias/not-created-yet/mods")));
  EXPECT_TRUE(pathsOverlap(directory.path(), game));
  EXPECT_TRUE(pathsOverlap(game, game));
  EXPECT_FALSE(pathsOverlap(game, directory.filePath("game-backup")));
  EXPECT_FALSE(pathsOverlap(game, QString()));
}

TEST(Clf3Logs, RemovesSignedLinksHeadersAndJsonCredentials)
{
  const QString input = QStringLiteral(
      "Download failed: https://name:password@example.org/archive.7z?token=cdnsecret#fragment\n"
      "nxm://skyrim/mods/1/files/2?key=nxmsecret&expires=99\n"
      "Authorization: Bearer headersecret\n"
      "{\"api_key\":\"jsonsecret\"}\n"
      "access_token=tokensecret\n"
      "Using Bearer baresecret\n"
      "Failed archive: example.7z\n");
  const auto result = redactLog(input);
  for (const auto& secret : {"password", "cdnsecret", "fragment", "nxmsecret", "headersecret",
                             "jsonsecret", "tokensecret", "baresecret"})
    EXPECT_FALSE(result.contains(secret)) << secret << ": " << result.toStdString();
  EXPECT_TRUE(result.contains("https://example.org/archive.7z"));
  EXPECT_TRUE(result.contains("nxm://skyrim/mods/1/files/2"));
  EXPECT_TRUE(result.contains("Failed archive: example.7z"));
}

TEST(Clf3Setup, DeploymentCheckpointSurvivesRestartButDoesNotMatchAnotherJobOrGame)
{
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const auto source = directory.filePath("source");
  const auto destination = directory.filePath("game");
  const auto other = directory.filePath("other-game");
  ASSERT_TRUE(QDir().mkpath(source));
  ASSERT_TRUE(QDir().mkpath(destination));
  ASSERT_TRUE(QDir().mkpath(other));
  const auto checkpoint = directory.filePath("checkpoint.ini");
  EXPECT_FALSE(rootDeploymentMatches(checkpoint, "job-1", source, destination));
  ASSERT_TRUE(saveRootDeployment(checkpoint, "job-1", source, destination));
  EXPECT_TRUE(rootDeploymentMatches(checkpoint, "job-1", source, destination));
  EXPECT_FALSE(rootDeploymentMatches(checkpoint, "job-2", source, destination));
  EXPECT_FALSE(rootDeploymentMatches(checkpoint, "job-1", source, other));
  EXPECT_FALSE(rootDeploymentMatches(checkpoint, "", source, destination));
  ASSERT_TRUE(QDir(destination).removeRecursively());
  EXPECT_FALSE(rootDeploymentMatches(checkpoint, "job-1", source, destination));
}

class Clf3Settings : public ::testing::Test
{
protected:
  QTemporaryDir directory;
  QString previousOrganization;
  void SetUp() override
  {
    ASSERT_TRUE(directory.isValid());
    previousOrganization = QCoreApplication::organizationName();
    QCoreApplication::setOrganizationName({});
  }
  void TearDown() override
  {
    QCoreApplication::setOrganizationName(previousOrganization);
  }
};

TEST_F(Clf3Settings, SavesAndRestoresResumeDataWithoutAnOrganizationName)
{
  // Reproduce Fluorine's application metadata, which the earlier tests missed.
  QSettings brokenDefault;
  EXPECT_EQ(brokenDefault.status(), QSettings::AccessError);
  auto settings = openSettings(directory.path());
  EXPECT_EQ(settings->fileName(), directory.filePath("fluorine/wabbajack.ini"));
  settings->setValue("clf3/pending/source", "https://example.org/list.wabbajack");
  settings->setValue("clf3/pending/output", directory.filePath("Viva New Vegas"));
  settings->setValue("clf3/pending/stage", "post-install");
  settings->setValue("clf3/pending/stats", QByteArray("{\"archives_downloaded\":4}"));
  settings->sync();
  ASSERT_EQ(settings->status(), QSettings::NoError);
  settings.reset();
  const auto restored = openSettings(directory.path());
  EXPECT_EQ(restored->status(), QSettings::NoError);
  EXPECT_EQ(restored->value("clf3/pending/source").toString(), "https://example.org/list.wabbajack");
  EXPECT_EQ(restored->value("clf3/pending/output").toString(), directory.filePath("Viva New Vegas"));
  EXPECT_EQ(restored->value("clf3/pending/stage").toString(), "post-install");
  EXPECT_EQ(restored->value("clf3/pending/stats").toByteArray(), QByteArray("{\"archives_downloaded\":4}"));
}

TEST_F(Clf3Settings, ImportsOnlyDownloaderKeysAndDoesNotResurrectClearedJobs)
{
  QSettings legacy(directory.filePath("Unknown Organization/ModOrganizer.conf"), QSettings::IniFormat);
  legacy.setValue("clf3/pending/source", "legacy.wabbajack");
  legacy.setValue("clf3/gallery/showNsfw", true);
  legacy.setValue("unrelated/preference", "leave alone");
  legacy.sync();
  ASSERT_EQ(legacy.status(), QSettings::NoError);
  auto settings = openSettings(directory.path());
  ASSERT_EQ(settings->status(), QSettings::NoError);
  EXPECT_EQ(settings->value("clf3/pending/source").toString(), "legacy.wabbajack");
  EXPECT_TRUE(settings->value("clf3/gallery/showNsfw").toBool());
  EXPECT_FALSE(settings->contains("unrelated/preference"));
  settings->remove("clf3/pending");
  settings->sync();
  ASSERT_EQ(settings->status(), QSettings::NoError);
  settings.reset();
  settings = openSettings(directory.path());
  EXPECT_FALSE(settings->contains("clf3/pending/source"));
  EXPECT_TRUE(settings->value("clf3/gallery/showNsfw").toBool());
  EXPECT_TRUE(legacy.contains("clf3/pending/source"));
  EXPECT_EQ(legacy.value("unrelated/preference").toString(), "leave alone");
}

TEST_F(Clf3Settings, MigrationPreservesNewerDownloaderSettings)
{
  QSettings existing(directory.filePath("fluorine/wabbajack.ini"), QSettings::IniFormat);
  existing.setValue("clf3/pending/source", "new.wabbajack");
  existing.sync();
  QSettings legacy(directory.filePath("Unknown Organization/ModOrganizer.conf"), QSettings::IniFormat);
  legacy.setValue("clf3/pending/source", "old.wabbajack");
  legacy.sync();
  const auto settings = openSettings(directory.path());
  EXPECT_EQ(settings->status(), QSettings::NoError);
  EXPECT_EQ(settings->value("clf3/pending/source").toString(), "new.wabbajack");
}

TEST_F(Clf3Settings, StillReportsRealWriteFailures)
{
  QFile obstruction(directory.filePath("fluorine"));
  ASSERT_TRUE(obstruction.open(QIODevice::WriteOnly));
  obstruction.close();
  auto settings = openSettings(directory.path());
  settings->setValue("clf3/pending/source", "test.wabbajack");
  settings->sync();
  EXPECT_EQ(settings->status(), QSettings::AccessError);
  EXPECT_EQ(settings->fileName(), directory.filePath("fluorine/wabbajack.ini"));
}

class Clf3Process : public ::testing::Test
{
protected:
  QTemporaryDir directory;
  QByteArray previousOverride;
  bool hadOverride{};
  Clf3ProcessController controller;

  void SetUp() override
  {
    ASSERT_TRUE(directory.isValid());
    hadOverride = qEnvironmentVariableIsSet("FLUORINE_CLF3_PATH");
    previousOverride = qgetenv("FLUORINE_CLF3_PATH");
  }
  void TearDown() override
  {
    if (controller.isRunning()) {
      QSignalSpy stopped(&controller, &Clf3ProcessController::cancelled);
      controller.cancel();
      if (stopped.isEmpty()) stopped.wait(9000);
    }
    if (hadOverride) qputenv("FLUORINE_CLF3_PATH", previousOverride);
    else qunsetenv("FLUORINE_CLF3_PATH");
  }
  void engine(const QByteArray& body, bool handshake = true)
  {
    QFile file(directory.filePath("clf3"));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("#!/bin/bash\nulimit -c 0\n");
    if (handshake) file.write(
        "printf '%s\\n' '{\"type\":\"hello\",\"protocol_version\":1,\"engine_version\":\"test\"}'\n"
        "read -r acknowledgement\n");
    file.write(body);
    file.close();
    ASSERT_TRUE(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    qputenv("FLUORINE_CLF3_PATH", file.fileName().toUtf8());
  }
  void start() { controller.startInstall("test.wabbajack", directory.path(), directory.path(), {}); }
};

TEST_F(Clf3Process, DoesNotReportSuccessUntilProcessExitsAndHandlesUnterminatedFinalLine)
{
  engine(R"(printf '%s' '{"type":"install_completed","stats":{"archives_downloaded":4}}'
sleep 0.2
)");
  QSignalSpy success(&controller, &Clf3ProcessController::completed);
  QSignalSpy failure(&controller, &Clf3ProcessController::failed);
  bool runningAtCompletion = true;
  QObject::connect(&controller, &Clf3ProcessController::completed, &controller,
                   [&] { runningAtCompletion = controller.isRunning(); });
  start();
  ASSERT_TRUE(success.wait(3000));
  EXPECT_FALSE(runningAtCompletion);
  EXPECT_TRUE(failure.isEmpty());
  EXPECT_EQ(success.count(), 1);
  EXPECT_EQ(success.first().first().toJsonObject().value("archives_downloaded").toInt(), 4);
}

TEST_F(Clf3Process, CrashReportsExactlyOneFailure)
{
  engine("kill -ABRT $$\n");
  QSignalSpy failure(&controller, &Clf3ProcessController::failed);
  QSignalSpy success(&controller, &Clf3ProcessController::completed);
  start();
  ASSERT_TRUE(failure.wait(3000));
  QTest::qWait(50);
  EXPECT_EQ(failure.count(), 1);
  EXPECT_TRUE(success.isEmpty());
  EXPECT_FALSE(controller.isRunning());
}

TEST_F(Clf3Process, ProtocolMismatchReportsOneFailureAfterExit)
{
  engine(R"(printf '%s\n' '{"type":"hello","protocol_version":999}'
read -r ignored
)", false);
  QSignalSpy failure(&controller, &Clf3ProcessController::failed);
  start();
  ASSERT_TRUE(failure.wait(3000));
  QTest::qWait(50);
  EXPECT_EQ(failure.count(), 1);
  EXPECT_TRUE(failure.first().first().toString().contains("999"));
  EXPECT_FALSE(controller.isRunning());
}

TEST_F(Clf3Process, SuccessEventFollowedByNonzeroExitIsFailure)
{
  engine(R"(printf '%s\n' '{"type":"install_completed","stats":{}}'
exit 2
)");
  QSignalSpy failure(&controller, &Clf3ProcessController::failed);
  QSignalSpy success(&controller, &Clf3ProcessController::completed);
  start();
  ASSERT_TRUE(failure.wait(3000));
  EXPECT_TRUE(success.isEmpty());
}

TEST_F(Clf3Process, RepeatedCancellationDoesNotKillNextInstallation)
{
  engine(R"(read -r cancel
printf '%s\n' '{"type":"install_completed","stats":{}}'
)");
  QSignalSpy ready(&controller, &Clf3ProcessController::engineReady);
  QSignalSpy cancelled(&controller, &Clf3ProcessController::cancelled);
  QSignalSpy failure(&controller, &Clf3ProcessController::failed);
  QSignalSpy success(&controller, &Clf3ProcessController::completed);
  start();
  ASSERT_TRUE(ready.wait(3000));
  controller.cancel();
  controller.cancel();
  ASSERT_TRUE(cancelled.wait(3000));
  EXPECT_EQ(cancelled.count(), 1);
  EXPECT_TRUE(success.isEmpty());
  EXPECT_TRUE(failure.isEmpty());
  // Outlive the previous cancellation's five-second terminate timer.
  engine(R"(sleep 6
printf '%s\n' '{"type":"install_completed","stats":{}}'
)");
  start();
  ASSERT_TRUE(success.wait(9000));
  EXPECT_EQ(success.count(), 1);
  EXPECT_TRUE(failure.isEmpty());
}

TEST_F(Clf3Process, CancellationBeforeStartupDoesNotReportFailure)
{
  engine("read -r cancel\n");
  QSignalSpy cancelled(&controller, &Clf3ProcessController::cancelled);
  QSignalSpy failure(&controller, &Clf3ProcessController::failed);
  start();
  controller.cancel();
  ASSERT_TRUE(cancelled.wait(9000));
  EXPECT_TRUE(failure.isEmpty());
  EXPECT_EQ(cancelled.count(), 1);
}

TEST_F(Clf3Process, FailedStartupReportsExactlyOnce)
{
  engine("exit 0\n");
  QFile file(directory.filePath("clf3"));
  ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  file.write("#!/nonexistent/fluorine-interpreter\n");
  file.close();
  QSignalSpy failure(&controller, &Clf3ProcessController::failed);
  start();
  ASSERT_TRUE(failure.wait(3000));
  QTest::qWait(50);
  EXPECT_EQ(failure.count(), 1);
  EXPECT_FALSE(controller.isRunning());
}

TEST_F(Clf3Process, PreservesEngineFailureReasonAndIgnoresLaterSuccess)
{
  engine(R"(printf '%s\n' '{"type":"install_failed","message":"Archive hash mismatch"}'
printf '%s\n' '{"type":"install_completed","stats":{}}'
exit 1
)");
  QSignalSpy failure(&controller, &Clf3ProcessController::failed);
  QSignalSpy success(&controller, &Clf3ProcessController::completed);
  start();
  ASSERT_TRUE(failure.wait(3000));
  EXPECT_EQ(failure.count(), 1);
  EXPECT_EQ(failure.first().first().toString(), QStringLiteral("Archive hash mismatch"));
  EXPECT_TRUE(success.isEmpty());
}

TEST_F(Clf3Process, ReassemblesStderrBeforeExportRedaction)
{
  engine(R"(printf '%s' 'Authorization: Bearer ' >&2
sleep 0.1
printf '%s\n' 'split-secret' >&2
printf '%s\n' '{"type":"install_completed","stats":{}}'
)");
  QSignalSpy logs(&controller, &Clf3ProcessController::logLine);
  QSignalSpy success(&controller, &Clf3ProcessController::completed);
  start();
  ASSERT_TRUE(success.wait(3000));
  ASSERT_EQ(logs.count(), 1);
  EXPECT_FALSE(redactLog(logs.first().first().toString()).contains("split-secret"));
}

TEST_F(Clf3Process, EscalatesCancellationWhenEngineIgnoresCancelAndTerminate)
{
  engine(R"(trap '' TERM
while :; do read -r ignored; done
)");
  QSignalSpy ready(&controller, &Clf3ProcessController::engineReady);
  QSignalSpy cancelled(&controller, &Clf3ProcessController::cancelled);
  QSignalSpy failure(&controller, &Clf3ProcessController::failed);
  start();
  ASSERT_TRUE(ready.wait(3000));
  controller.cancel();
  ASSERT_TRUE(cancelled.wait(10000));
  EXPECT_EQ(cancelled.count(), 1);
  EXPECT_TRUE(failure.isEmpty());
  EXPECT_FALSE(controller.isRunning());
}

TEST_F(Clf3Process, FailsStartupHandshakeInsteadOfWaitingForever)
{
  engine("read -r ignored\n", false);
  QSignalSpy failure(&controller, &Clf3ProcessController::failed);
  start();
  ASSERT_TRUE(failure.wait(35000));
  EXPECT_EQ(failure.count(), 1);
  EXPECT_TRUE(failure.first().first().toString().contains("handshake"));
  EXPECT_FALSE(controller.isRunning());
}

int main(int argc, char** argv)
{
  QCoreApplication application(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
