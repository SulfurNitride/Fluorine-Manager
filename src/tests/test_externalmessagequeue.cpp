#include "externalmessagequeue.h"

#include <gtest/gtest.h>

TEST(ExternalMessageQueue, WaitingAcceptsOnlyCrossGenerationMessages) {
  ExternalMessageQueue queue;
  EXPECT_FALSE(queue.enqueue(QStringLiteral("shortcut"),
                             ExternalMessageQueue::Scope::CurrentGeneration));
  EXPECT_TRUE(queue.enqueue(QStringLiteral("nxm"),
                            ExternalMessageQueue::Scope::AnyGeneration));
  EXPECT_FALSE(queue.takeNext().has_value());

  EXPECT_EQ(queue.resume(), 0);
  EXPECT_TRUE(queue.enqueue(QStringLiteral("refresh"),
                            ExternalMessageQueue::Scope::CurrentGeneration));
  EXPECT_EQ(queue.resume(), 0);
  EXPECT_EQ(queue.takeNext(), QStringLiteral("nxm"));
  EXPECT_EQ(queue.takeNext(), QStringLiteral("refresh"));
  EXPECT_FALSE(queue.takeNext().has_value());
}

TEST(ExternalMessageQueue, RestartDropsOldCommandsButPreservesDownloads) {
  ExternalMessageQueue queue;
  ASSERT_EQ(queue.resume(), 0);
  ASSERT_TRUE(queue.enqueue(QStringLiteral("old generation first"),
                            ExternalMessageQueue::Scope::CurrentGeneration));
  ASSERT_TRUE(queue.enqueue(QStringLiteral("old generation second"),
                            ExternalMessageQueue::Scope::CurrentGeneration));
  EXPECT_EQ(queue.takeNext(), QStringLiteral("old generation first"));

  queue.pause();
  EXPECT_FALSE(queue.enqueue(QStringLiteral("command during restart"),
                             ExternalMessageQueue::Scope::CurrentGeneration));
  ASSERT_TRUE(queue.enqueue(QStringLiteral("nxm during restart"),
                            ExternalMessageQueue::Scope::AnyGeneration));
  EXPECT_FALSE(queue.takeNext().has_value());

  EXPECT_EQ(queue.resume(), 1);
  EXPECT_EQ(queue.takeNext(), QStringLiteral("nxm during restart"));
  EXPECT_FALSE(queue.takeNext().has_value());
}

TEST(ExternalMessageQueue, EnforcesCountAndAggregateByteLimits) {
  ExternalMessageQueue countQueue;
  ASSERT_EQ(countQueue.resume(), 0);
  for (qsizetype i = 0; i < ExternalMessageQueue::MaximumMessages; ++i) {
    EXPECT_TRUE(countQueue.enqueue(
        QStringLiteral("x"), ExternalMessageQueue::Scope::CurrentGeneration));
  }
  EXPECT_FALSE(
      countQueue.enqueue(QStringLiteral("overflow"),
                         ExternalMessageQueue::Scope::CurrentGeneration));

  ExternalMessageQueue byteQueue;
  const QString quarter(ExternalMessageQueue::MaximumBytes / 4,
                        QLatin1Char('x'));
  for (int i = 0; i < 4; ++i) {
    EXPECT_TRUE(
        byteQueue.enqueue(quarter, ExternalMessageQueue::Scope::AnyGeneration));
  }
  EXPECT_EQ(byteQueue.bytes(), ExternalMessageQueue::MaximumBytes);
  EXPECT_FALSE(byteQueue.enqueue(QStringLiteral("x"),
                                 ExternalMessageQueue::Scope::AnyGeneration));
}

TEST(ExternalMessageQueue, StoppingDiscardsPendingAndRejectsNewMessages) {
  ExternalMessageQueue queue;
  ASSERT_TRUE(queue.enqueue(QStringLiteral("pending"),
                            ExternalMessageQueue::Scope::AnyGeneration));
  EXPECT_EQ(queue.stop(), 1);
  EXPECT_EQ(queue.phase(), ExternalMessageQueue::Phase::Stopping);
  EXPECT_EQ(queue.size(), 0);
  EXPECT_EQ(queue.bytes(), 0);
  EXPECT_FALSE(queue.enqueue(QStringLiteral("late"),
                             ExternalMessageQueue::Scope::AnyGeneration));
  EXPECT_EQ(queue.resume(), 0);
  EXPECT_FALSE(queue.takeNext().has_value());
}

TEST(ExternalMessageQueue, TransientExitAttemptDefersWithoutPausing) {
  ExternalMessageQueue queue;
  ASSERT_EQ(queue.resume(), 0);
  ASSERT_TRUE(queue.enqueue(QStringLiteral("during exit prompt"),
                            ExternalMessageQueue::Scope::CurrentGeneration));

  EXPECT_EQ(queue.dispatchAction(true, false),
            ExternalMessageQueue::DispatchAction::Wait);
  EXPECT_TRUE(queue.ready());
  EXPECT_EQ(queue.dispatchAction(false, false),
            ExternalMessageQueue::DispatchAction::Dispatch);
  EXPECT_EQ(queue.takeNext(), QStringLiteral("during exit prompt"));

  ASSERT_TRUE(queue.enqueue(QStringLiteral("after authorization"),
                            ExternalMessageQueue::Scope::CurrentGeneration));
  EXPECT_EQ(queue.dispatchAction(false, true),
            ExternalMessageQueue::DispatchAction::Stop);
}
