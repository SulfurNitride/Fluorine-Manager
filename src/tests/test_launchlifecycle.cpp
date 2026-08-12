#include "launchlifecycle.h"
#include "processlaunchcontext.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>

#include <gtest/gtest.h>

#include <stdexcept>

TEST(RefreshWaitLatchTest, CompletionBeforeExecSkipsWait) {
  QEventLoop loop;
  RefreshWaitLatch latch(loop);

  // Models a refresh completing inside a nested plugin event loop before
  // ProcessRunner returns from afterRun().
  latch.complete();

  bool enteredWait = false;
  if (!latch.completeBeforeWait()) {
    enteredWait = true;
    loop.exec();
  }

  EXPECT_FALSE(enteredWait);
}

TEST(RefreshWaitLatchTest, CompletionAfterExecWakesWait) {
  QEventLoop loop;
  RefreshWaitLatch latch(loop);
  QTimer::singleShot(0, [&latch]() noexcept { latch.complete(); });

  ASSERT_FALSE(latch.completeBeforeWait());
  loop.exec();
  EXPECT_TRUE(latch.completeBeforeWait());
}

TEST(RefreshWaitLatchTest, CompletionBetweenCheckAndExecLeavesQueuedWake) {
  QEventLoop loop;
  RefreshWaitLatch latch(loop);
  ASSERT_FALSE(latch.completeBeforeWait());

  // Force the exact check-to-exec interleaving. The direct quit is too early;
  // only the queued wake makes the subsequent exec return promptly.
  latch.complete();
  QElapsedTimer elapsed;
  elapsed.start();
  QTimer::singleShot(500, &loop, &QEventLoop::quit);
  loop.exec();

  EXPECT_LT(elapsed.elapsed(), 250);
  EXPECT_TRUE(latch.completeBeforeWait());
}

TEST(RefreshWaitLatchTest, RefreshFailureWakesWaitAndCannotBecomeSuccess) {
  QEventLoop loop;
  RefreshWaitLatch latch(loop);
  QTimer::singleShot(0, [&latch]() noexcept { latch.fail(); });

  ASSERT_FALSE(latch.completeBeforeWait());
  loop.exec();
  EXPECT_TRUE(latch.completeBeforeWait());
  EXPECT_TRUE(latch.failed());

  latch.complete();
  EXPECT_TRUE(latch.failed());
}

TEST(PreparedLaunchRollbackTest, ExceptionInvokesRollbackExactlyOnce) {
  int rollbacks = 0;
  try {
    auto rollback =
        makePreparedLaunchRollback([&rollbacks]() noexcept { ++rollbacks; });
    throw std::runtime_error("injected preparation failure");
  } catch (const std::runtime_error &) {
  }

  EXPECT_EQ(rollbacks, 1);
}

TEST(PreparedLaunchRollbackTest, SuccessfulReceiptDismissesRollback) {
  int rollbacks = 0;
  {
    auto rollback =
        makePreparedLaunchRollback([&rollbacks]() noexcept { ++rollbacks; });
    rollback.dismiss();
  }

  EXPECT_EQ(rollbacks, 0);
}

TEST(PostRefreshStageTest, PostFailureCannotSuppressRefresh) {
  int refreshCalls = 0;
  const auto result = runPostThenRefresh(
      []() { throw std::runtime_error("injected post-run failure"); },
      [&refreshCalls]() {
        ++refreshCalls;
        return true;
      });

  EXPECT_TRUE(result.postFailure);
  EXPECT_FALSE(result.refreshFailure);
  EXPECT_TRUE(result.refreshScheduled);
  EXPECT_EQ(refreshCalls, 1);
}

TEST(PostRefreshStageTest, RefreshFailureIsNotReportedAsCompletion) {
  int refreshCalls = 0;
  const auto result = runPostThenRefresh(
      []() {},
      [&refreshCalls]() -> bool {
        ++refreshCalls;
        throw std::runtime_error("injected refresh startup failure");
      });

  EXPECT_FALSE(result.postFailure);
  EXPECT_TRUE(result.refreshFailure);
  EXPECT_FALSE(result.refreshScheduled);
  EXPECT_EQ(refreshCalls, 1);
}

TEST(PreparedLaunchArtifactTest, ExistingRequestMustActuallyDisappear) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString artifact = temporary.filePath(QStringLiteral("request.json"));
  QFile request(artifact);
  ASSERT_TRUE(request.open(QIODevice::WriteOnly));
  request.close();

  EXPECT_TRUE(removePreparedLaunchArtifact(artifact));
  EXPECT_FALSE(QFileInfo::exists(artifact));
}

TEST(MandatoryLaunchCleanupTest,
     FailureRetainsOwnershipUntilExactRetrySucceeds) {
  ProcessLaunchContextTracker tracker;
  const auto token = QStringLiteral("launch");
  const auto profile = QStringLiteral("Default");
  ASSERT_TRUE(tracker.reserve(token, profile, /*ownsVfs=*/true));

  const auto failedStage = launch_cleanup::beginMandatoryCleanup(
      tracker, token, profile, /*ownsVfs=*/true,
      launch_cleanup::AttemptKind::Initial,
      []() { throw std::runtime_error("injected unmount failure"); });
  const auto &failed = failedStage.attempt();
  EXPECT_EQ(failed.state, launch_cleanup::AttemptState::RetryRequired);
  EXPECT_TRUE(failed.failure);
  EXPECT_FALSE(failed.vfsCleanupPerformed);
  EXPECT_TRUE(tracker.contains(token));
  EXPECT_EQ(tracker.activeLaunches().vfs, 1);
  EXPECT_FALSE(tracker.reserve(QStringLiteral("successor"), profile, true));
  EXPECT_FALSE(tracker.claimCompletion(token, profile, true));
  EXPECT_TRUE(tracker.resumeCompletion(token, profile, true));

  int cleanupCompletions = 0;
  // RetryRequired must not run the finalization/completion stage.
  const auto withheld = failedStage.finalize(
      &tracker, token, []() {},
      [&cleanupCompletions]() { ++cleanupCompletions; });
  EXPECT_FALSE(withheld.trackerFinished);
  EXPECT_FALSE(withheld.completionInvoked);
  EXPECT_EQ(cleanupCompletions, 0);

  int cleanupCalls = 0;
  const auto retriedStage = launch_cleanup::beginMandatoryCleanup(
      tracker, token, profile, /*ownsVfs=*/true,
      launch_cleanup::AttemptKind::Retry,
      [&cleanupCalls]() { ++cleanupCalls; });
  const auto &retried = retriedStage.attempt();
  EXPECT_EQ(retried.state, launch_cleanup::AttemptState::Complete);
  EXPECT_TRUE(retried.vfsCleanupPerformed);
  EXPECT_EQ(cleanupCalls, 1);

  // Physical cleanup released only the VFS reservation. The old tracker entry
  // remains visible through later post-run callbacks until explicitly finished.
  const auto finalized = retriedStage.finalize(
      &tracker, token,
      [&tracker, &profile]() {
        EXPECT_TRUE(
            tracker.reserve(QStringLiteral("successor"), profile, true));
        EXPECT_EQ(tracker.activeLaunches().total, 2);
      },
      [&cleanupCompletions]() { ++cleanupCompletions; });
  EXPECT_TRUE(finalized.trackerFinished);
  EXPECT_TRUE(finalized.completionInvoked);
  EXPECT_EQ(cleanupCompletions, 1);
  tracker.abandon(QStringLiteral("successor"));
  EXPECT_TRUE(tracker.activeLaunches().empty());
}

TEST(MandatoryLaunchCleanupTest,
     NativeLaunchCompletesWithoutVfsCleanupAuthority) {
  ProcessLaunchContextTracker tracker;
  const auto token = QStringLiteral("native-launch");
  const auto profile = QStringLiteral("Default");
  ASSERT_TRUE(tracker.reserve(token, profile, /*ownsVfs=*/false));

  bool cleanupCalled = false;
  const auto result = launch_cleanup::attemptMandatoryCleanup(
      tracker, token, profile, /*ownsVfs=*/false,
      launch_cleanup::AttemptKind::Initial,
      [&cleanupCalled]() { cleanupCalled = true; });

  EXPECT_EQ(result.state, launch_cleanup::AttemptState::Complete);
  EXPECT_FALSE(result.vfsCleanupPerformed);
  EXPECT_FALSE(cleanupCalled);
  tracker.finishCompletion(token);
}

TEST(MandatoryLaunchCleanupTest,
     NonMandatoryAndCompletionExceptionsAreContainedAfterTrackerRelease) {
  ProcessLaunchContextTracker tracker;
  const auto token = QStringLiteral("launch");
  const auto profile = QStringLiteral("Default");
  ASSERT_TRUE(tracker.reserve(token, profile, /*ownsVfs=*/true));
  const auto cleanupStage = launch_cleanup::beginMandatoryCleanup(
      tracker, token, profile, /*ownsVfs=*/true,
      launch_cleanup::AttemptKind::Initial, []() {});
  const auto &cleanup = cleanupStage.attempt();
  ASSERT_EQ(cleanup.state, launch_cleanup::AttemptState::Complete);

  int completions = 0;
  const auto finalized = cleanupStage.finalize(
      &tracker, token,
      []() { throw std::runtime_error("injected plugin failure"); },
      [&completions]() {
        ++completions;
        throw std::runtime_error("injected completion failure");
      });

  EXPECT_TRUE(finalized.postFailure);
  EXPECT_TRUE(finalized.trackerFinished);
  EXPECT_TRUE(finalized.completionInvoked);
  EXPECT_TRUE(finalized.completionFailure);
  EXPECT_EQ(completions, 1);
  EXPECT_FALSE(tracker.contains(token));
}

TEST(MandatoryLaunchCleanupTest, HandedOffOuterLaunchNeverUnmountsNestedOwner) {
  ProcessLaunchContextTracker tracker;
  const auto outer = QStringLiteral("outer");
  const auto profile = QStringLiteral("Default");
  ASSERT_TRUE(tracker.reserve(outer, profile, /*ownsVfs=*/true));
  ASSERT_TRUE(tracker.handoffVfsReservation(outer));

  bool cleanupCalled = false;
  const auto result = launch_cleanup::attemptMandatoryCleanup(
      tracker, outer, profile, /*ownsVfs=*/true,
      launch_cleanup::AttemptKind::Initial,
      [&cleanupCalled]() { cleanupCalled = true; });
  EXPECT_EQ(result.state, launch_cleanup::AttemptState::Complete);
  EXPECT_FALSE(result.vfsCleanupPerformed);
  EXPECT_FALSE(cleanupCalled);

  tracker.finishCompletion(outer);
  EXPECT_TRUE(tracker.reserve(QStringLiteral("nested"), profile, true));
  tracker.abandon(QStringLiteral("nested"));
}

TEST(MandatoryLaunchCleanupTest,
     AdditionalPreparedArtifactIsMandatoryWithoutVfsOwnership) {
  ProcessLaunchContextTracker tracker;
  const auto token = QStringLiteral("native-with-artifact");
  const auto profile = QStringLiteral("Default");
  ASSERT_TRUE(tracker.reserve(token, profile, /*ownsVfs=*/false));

  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString artifact = temporary.filePath(QStringLiteral("request.json"));
  // A directory deterministically models an entry QFile::remove cannot remove.
  ASSERT_TRUE(QDir().mkdir(artifact));

  int removalAttempts = 0;
  const auto failed = launch_cleanup::attemptMandatoryCleanup(
      tracker, token, profile, /*ownsVfs=*/false,
      launch_cleanup::AttemptKind::Initial,
      [&removalAttempts, &artifact]() {
        ++removalAttempts;
        if (!removePreparedLaunchArtifact(artifact)) {
          throw std::runtime_error("injected request removal failure");
        }
      },
      /*additionalCleanupRequired=*/true);
  EXPECT_EQ(failed.state, launch_cleanup::AttemptState::RetryRequired);
  EXPECT_TRUE(tracker.contains(token));

  ASSERT_TRUE(QDir().rmdir(artifact));

  const auto retried = launch_cleanup::attemptMandatoryCleanup(
      tracker, token, profile, /*ownsVfs=*/false,
      launch_cleanup::AttemptKind::Retry,
      [&removalAttempts, &artifact]() {
        ++removalAttempts;
        if (!removePreparedLaunchArtifact(artifact)) {
          throw std::runtime_error("request removal retry failed");
        }
      },
      /*additionalCleanupRequired=*/true);
  EXPECT_EQ(retried.state, launch_cleanup::AttemptState::Complete);
  EXPECT_FALSE(retried.vfsCleanupPerformed);
  EXPECT_EQ(removalAttempts, 2);
  tracker.finishCompletion(token);
  EXPECT_FALSE(tracker.contains(token));
}

TEST(MandatoryLaunchCleanupTest,
     HandedOffLaunchRemovesArtifactWithoutUnmountingNestedOwner) {
  ProcessLaunchContextTracker tracker;
  const auto token = QStringLiteral("outer");
  const auto profile = QStringLiteral("Default");
  ASSERT_TRUE(tracker.reserve(token, profile, /*ownsVfs=*/true));
  ASSERT_TRUE(tracker.handoffVfsReservation(token));

  bool artifactRemoved = false;
  bool nestedUnmounted = false;
  const auto result = launch_cleanup::attemptMandatoryCleanup(
      tracker, token, profile, /*ownsVfs=*/true,
      launch_cleanup::AttemptKind::Initial,
      [&artifactRemoved, &nestedUnmounted](bool cleanupVfs) {
        artifactRemoved = true;
        nestedUnmounted = cleanupVfs;
      },
      /*additionalCleanupRequired=*/true);

  EXPECT_EQ(result.state, launch_cleanup::AttemptState::Complete);
  EXPECT_FALSE(result.vfsCleanupPerformed);
  EXPECT_TRUE(artifactRemoved);
  EXPECT_FALSE(nestedUnmounted);
  tracker.finishCompletion(token);
}

int main(int argc, char **argv) {
  QCoreApplication application(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
