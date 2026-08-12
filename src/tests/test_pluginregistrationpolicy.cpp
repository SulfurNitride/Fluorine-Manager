#include "pluginregistrationpolicy.h"
#include "pluginregistrationtransaction.h"

#include <stdexcept>

#include <gtest/gtest.h>

using PluginRegistration::BatchDecision;
using PluginRegistration::ProxiedBatchLedger;
using PluginRegistration::Transaction;

TEST(PluginRegistrationPolicy, FailedLogicalPluginIsNotRetried)
{
  QObject firstInterface;
  QObject secondInterface;
  ProxiedBatchLedger ledger;
  const std::optional<QString> name(QStringLiteral("Configurator"));

  EXPECT_EQ(ledger.begin(&firstInterface, name), BatchDecision::Attempt);
  ledger.reject(name);
  EXPECT_EQ(ledger.begin(&secondInterface, name),
            BatchDecision::FailedLogicalPlugin);
}

TEST(PluginRegistrationPolicy, UnrelatedLogicalPluginRemainsAdmissible)
{
  QObject failed;
  QObject unrelated;
  ProxiedBatchLedger ledger;

  ledger.reject(QStringLiteral("Configurator"));
  EXPECT_EQ(ledger.begin(&failed, QStringLiteral("Configurator")),
            BatchDecision::FailedLogicalPlugin);
  EXPECT_EQ(ledger.begin(&unrelated, QStringLiteral("AnotherPlugin")),
            BatchDecision::Attempt);
}

TEST(PluginRegistrationPolicy, DuplicateQObjectIsProcessedOnce)
{
  QObject object;
  ProxiedBatchLedger ledger;

  EXPECT_EQ(ledger.begin(&object, QStringLiteral("Plugin")),
            BatchDecision::Attempt);
  EXPECT_EQ(ledger.begin(&object, QStringLiteral("Plugin")),
            BatchDecision::DuplicateObject);
}

TEST(PluginRegistrationPolicy, MissingNameDoesNotConflateCandidates)
{
  QObject first;
  QObject second;
  ProxiedBatchLedger ledger;

  EXPECT_EQ(ledger.begin(&first, std::nullopt), BatchDecision::Attempt);
  ledger.reject(std::nullopt);
  EXPECT_EQ(ledger.begin(&second, std::nullopt), BatchDecision::Attempt);
}

TEST(PluginRegistrationTransaction, FailedInitializationRunsOnceAndRollsBack)
{
  int published = 0;
  int initCalls = 0;
  {
    Transaction transaction;
    transaction.stage([&] { ++published; }, [&] { --published; });
    EXPECT_FALSE(transaction.initializeOnce([&] {
      ++initCalls;
      return false;
    }));
    EXPECT_FALSE(transaction.initializeOnce([&] {
      ++initCalls;
      return true;
    }));
  }
  EXPECT_EQ(initCalls, 1);
  EXPECT_EQ(published, 0);
}

TEST(PluginRegistrationTransaction, RollsBackPartialThrowInReverseOrder)
{
  QString state;
  EXPECT_THROW(
      {
        Transaction transaction;
        transaction.stage([&] { state += 'a'; }, [&] { state += 'A'; });
        transaction.stage(
            [&] {
              state += 'b';
              throw std::runtime_error("stage failed");
            },
            [&] { state += 'B'; });
      },
      std::runtime_error);
  EXPECT_EQ(state, QStringLiteral("abBA"));
}

TEST(PluginRegistrationTransaction, InitializationExceptionRollsBackStages)
{
  int published = 0;
  EXPECT_THROW(
      {
        Transaction transaction;
        transaction.stage([&] { ++published; }, [&] { --published; });
        transaction.initializeOnce(
            []() -> bool { throw std::runtime_error("init failed"); });
      },
      std::runtime_error);
  EXPECT_EQ(published, 0);
}

TEST(PluginRegistrationTransaction, CommitRetainsPublishedState)
{
  int published = 0;
  {
    Transaction transaction;
    transaction.stage([&] { ++published; }, [&] { --published; });
    EXPECT_TRUE(transaction.initializeOnce([] { return true; }));
    transaction.commit();
  }
  EXPECT_EQ(published, 1);
}

TEST(PluginRegistrationTransaction, RollbackExceptionDoesNotSkipEarlierStages)
{
  QString state;
  {
    Transaction transaction;
    transaction.stage([&] { state += 'a'; }, [&] { state += 'A'; });
    transaction.stage(
        [&] { state += 'b'; },
        [&] {
          state += 'B';
          throw std::runtime_error("rollback failed");
        });
  }
  EXPECT_EQ(state, QStringLiteral("abBA"));
}
