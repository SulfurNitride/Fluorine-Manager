#include "../src/curatedguidenxmbroker.h"

#include <gtest/gtest.h>

TEST(Clf3NxmBroker, ClaimsOnlyTheExpectedArtifactAndConsumer)
{
  auto& broker = CuratedGuideNxmBroker::instance();
  broker.clear();
  QString consumer;
  QString requestId;
  const auto connection = QObject::connect(
      &broker, &CuratedGuideNxmBroker::acceptedForConsumer,
      [&consumer, &requestId](const QString& acceptedConsumer,
                             const QString& acceptedRequest,
                             const QString&) {
        consumer  = acceptedConsumer;
        requestId = acceptedRequest;
      });

  broker.expectForConsumer("clf3", "request-1", "skyrimspecialedition", 10, 20,
                           42);
  EXPECT_FALSE(broker.tryConsume(
      "nxm://skyrimspecialedition/mods/10/files/21?key=x&expires=99&user_id=42"));
  EXPECT_TRUE(broker.tryConsume(
      "nxm://skyrimspecialedition/mods/10/files/20?key=x&expires=99&user_id=7"));
  broker.expectForConsumer("clf3", "request-1", "skyrimspecialedition", 10, 20,
                           42);
  EXPECT_TRUE(broker.tryConsume(
      "nxm://skyrimspecialedition/mods/10/files/20?key=x&expires=99&user_id=42"));
  EXPECT_EQ(consumer, "clf3");
  EXPECT_EQ(requestId, "request-1");
  EXPECT_FALSE(broker.tryConsume(
      "nxm://skyrimspecialedition/mods/10/files/20?key=x&expires=99&user_id=42"));

  QObject::disconnect(connection);
  broker.clear();
}

TEST(Clf3NxmBroker, KeepsUnrelatedPendingRequestsQueued)
{
  auto& broker = CuratedGuideNxmBroker::instance();
  broker.clear();
  broker.expectForConsumer("clf3", "first", "fallout4", 1, 2);
  broker.expectForConsumer("curated-guide", "second", "newvegas", 3, 4);

  EXPECT_TRUE(broker.tryConsume(
      "nxm://newvegas/mods/3/files/4?key=x&expires=99&user_id=1"));
  EXPECT_TRUE(broker.isWaiting());
  EXPECT_TRUE(broker.tryConsume(
      "nxm://fallout4/mods/1/files/2?key=x&expires=99&user_id=1"));
  EXPECT_FALSE(broker.isWaiting());
}
