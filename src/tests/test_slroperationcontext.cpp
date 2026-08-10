#include "slrmanager.h"

#include <gtest/gtest.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

TEST(SlrOperationTrackerTest, SuppressionRejectsAdmissionAndWaitsForActiveLease)
{
  SlrOperationTracker tracker;
  auto operation = tracker.tryBegin();
  ASSERT_TRUE(operation.has_value());
  EXPECT_EQ(tracker.activeOperations(), 1U);
  EXPECT_FALSE(tracker.drained());

  tracker.suppressAndCancel();

  EXPECT_TRUE(tracker.admissionSuppressed());
  EXPECT_FALSE(tracker.tryBegin().has_value());
  EXPECT_TRUE(operation->isCancellationRequested());
  EXPECT_FALSE(tracker.drained());

  operation.reset();
  EXPECT_TRUE(tracker.drained());
}

TEST(SlrOperationTrackerTest, ActiveDrainTracksWorkerLifetime)
{
  SlrOperationTracker tracker;
  std::mutex mutex;
  std::condition_variable cv;
  bool acquired = false;
  bool release = false;

  std::thread worker([&] {
    auto operation = tracker.tryBegin();
    ASSERT_TRUE(operation.has_value());
    {
      std::lock_guard lock(mutex);
      acquired = true;
    }
    cv.notify_all();

    std::unique_lock lock(mutex);
    cv.wait(lock, [&] { return release; });
  });

  {
    std::unique_lock lock(mutex);
    cv.wait(lock, [&] { return acquired; });
  }
  EXPECT_FALSE(tracker.drained());

  tracker.suppressAndCancel();
  EXPECT_FALSE(tracker.drained());

  {
    std::lock_guard lock(mutex);
    release = true;
  }
  cv.notify_all();
  worker.join();
  EXPECT_TRUE(tracker.drained());
}

TEST(SlrOperationTrackerTest, CancellationRejectsEveryDurableInstallStage)
{
  constexpr SlrDurableStage durableStages[] = {
      SlrDurableStage::RuntimeSwap,
      SlrDurableStage::XrandrCommit,
      SlrDurableStage::BuildIdCommit,
  };

  for (const auto stage : durableStages) {
    SlrOperationTracker tracker;
    SlrCancellationSource cancellation;
    auto operation = tracker.tryBegin(cancellation.token());
    ASSERT_TRUE(operation.has_value());

    cancellation.cancel();
    bool mutated = false;
    EXPECT_FALSE(operation->runDurableStepIfAllowed(stage, [&] { mutated = true; }));
    EXPECT_FALSE(mutated) << "durable stage " << static_cast<int>(stage);
  }
}

TEST(SlrOperationTrackerTest, CancelAfterExtractionRejectsRuntimeSwap)
{
  SlrOperationTracker tracker;
  SlrCancellationSource cancellation;
  auto operation = tracker.tryBegin(cancellation.token());
  ASSERT_TRUE(operation.has_value());

  const bool stagedExtractionComplete = true;
  cancellation.cancel();

  bool runtimeReplaced = false;
  ASSERT_TRUE(stagedExtractionComplete);
  EXPECT_FALSE(operation->runDurableStepIfAllowed(SlrDurableStage::RuntimeSwap,
                                                  [&] { runtimeReplaced = true; }));
  EXPECT_FALSE(runtimeReplaced);
}

TEST(SlrOperationTrackerTest, SuppressionBeforeXrandrCommitRejectsWrite)
{
  SlrOperationTracker tracker;
  auto operation = tracker.tryBegin();
  ASSERT_TRUE(operation.has_value());
  tracker.suppressAndCancel();

  bool xrandrCommitted = false;
  EXPECT_FALSE(operation->runDurableStepIfAllowed(SlrDurableStage::XrandrCommit,
                                                  [&] { xrandrCommitted = true; }));
  EXPECT_FALSE(xrandrCommitted);
}

TEST(SlrOperationTrackerTest, CancellationBeforeBuildIdCommitRejectsWrite)
{
  SlrOperationTracker tracker;
  SlrCancellationSource cancellation;
  auto operation = tracker.tryBegin(cancellation.token());
  ASSERT_TRUE(operation.has_value());
  cancellation.cancel();

  bool buildIdCommitted = false;
  EXPECT_FALSE(operation->runDurableStepIfAllowed(SlrDurableStage::BuildIdCommit,
                                                  [&] { buildIdCommitted = true; }));
  EXPECT_FALSE(buildIdCommitted);
}

TEST(SlrOperationTrackerTest, LinkedCancellationStopsBlockingWorkerStages)
{
  std::atomic_bool ownerCancelled{false};
  SlrCancellationSource cancellation(
      [&] { return ownerCancelled.load(std::memory_order_acquire); });
  SlrOperationTracker tracker;
  auto operation = tracker.tryBegin(cancellation.token());
  ASSERT_TRUE(operation.has_value());

  ownerCancelled.store(true, std::memory_order_release);
  EXPECT_TRUE(operation->isCancellationRequested());

  bool runtimeReplaced = false;
  EXPECT_FALSE(operation->runDurableStepIfAllowed(SlrDurableStage::RuntimeSwap,
                                                  [&] { runtimeReplaced = true; }));
  EXPECT_FALSE(runtimeReplaced);
}

TEST(SlrOperationTrackerTest, SuppressionLinearizesWithDurableCommit)
{
  SlrOperationTracker tracker;
  auto operation = tracker.tryBegin();
  ASSERT_TRUE(operation.has_value());

  std::mutex mutex;
  std::condition_variable cv;
  bool commitStarted = false;
  bool finishCommit = false;
  bool suppressionReturned = false;

  std::thread commitThread([&] {
    EXPECT_TRUE(operation->runDurableStepIfAllowed(SlrDurableStage::RuntimeSwap, [&] {
      {
        std::lock_guard lock(mutex);
        commitStarted = true;
      }
      cv.notify_all();
      std::unique_lock lock(mutex);
      cv.wait(lock, [&] { return finishCommit; });
    }));
  });

  {
    std::unique_lock lock(mutex);
    cv.wait(lock, [&] { return commitStarted; });
  }

  std::thread suppressThread([&] {
    tracker.suppressAndCancel();
    {
      std::lock_guard lock(mutex);
      suppressionReturned = true;
    }
    cv.notify_all();
  });

  suppressThread.join();
  {
    std::lock_guard lock(mutex);
    EXPECT_TRUE(suppressionReturned);
    finishCommit = true;
  }
  cv.notify_all();

  commitThread.join();
  EXPECT_TRUE(suppressionReturned);

  bool lateMutation = false;
  EXPECT_FALSE(operation->runDurableStepIfAllowed(SlrDurableStage::BuildIdCommit,
                                                  [&] { lateMutation = true; }));
  EXPECT_FALSE(lateMutation);
}

// Keep this last: process-wide fail-stop suppression is intentionally
// irreversible. It verifies that every public network/install entry point
// rejects synchronously without touching the network or filesystem.
TEST(SlrOperationTrackerTest, ProcessWideSuppressionRejectsAllPublicCallers)
{
  suppressSlrOperationsForFailedRollback();

  EXPECT_TRUE(slrOperationAdmissionSuppressed());
  EXPECT_FALSE(ensureXrandrInstalled({}, nullptr));
  EXPECT_FALSE(checkSlrUpdate().error.isEmpty());
  EXPECT_FALSE(downloadSlr(nullptr, nullptr).isEmpty());
  EXPECT_TRUE(slrOperationsDrainedForFailedRollback());
}
