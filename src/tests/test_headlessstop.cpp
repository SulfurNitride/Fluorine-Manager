#include "headlessstop.h"

#include <gtest/gtest.h>

#include <csignal>
#include <unistd.h>

namespace
{
void exitWithSignalCode(int sig)
{
  _exit(128 + sig);
}

class HeadlessStop : public ::testing::Test
{
protected:
  void SetUp() override { headless::resetStopForTesting(); }
  void TearDown() override { headless::resetStopForTesting(); }
};
}  // namespace

TEST_F(HeadlessStop, SignalDuringWaitSetsFlag)
{
  headless::installStopHandlers(exitWithSignalCode);
  headless::setWaitActive(true);
  raise(SIGTERM);
  EXPECT_TRUE(headless::stopRequested());
}

TEST_F(HeadlessStop, SighupIsHandled)
{
  headless::installStopHandlers(exitWithSignalCode);
  headless::setWaitActive(true);
  raise(SIGHUP);
  EXPECT_TRUE(headless::stopRequested());
}

TEST_F(HeadlessStop, SigintIsHandled)
{
  headless::installStopHandlers(exitWithSignalCode);
  headless::setWaitActive(true);
  raise(SIGINT);
  EXPECT_TRUE(headless::stopRequested());
}

TEST_F(HeadlessStop, SignalAfterWaitSetsFlag)
{
  headless::installStopHandlers(exitWithSignalCode);
  headless::setWaitActive(true);
  headless::setWaitActive(false);
  raise(SIGTERM);
  EXPECT_TRUE(headless::stopRequested());
}

TEST_F(HeadlessStop, SignalBeforeWaitUsesFallback)
{
  EXPECT_EXIT(
      {
        headless::installStopHandlers(exitWithSignalCode);
        raise(SIGTERM);
      },
      ::testing::ExitedWithCode(143), "");
}

TEST_F(HeadlessStop, SecondSignalEscalates)
{
  EXPECT_EXIT(
      {
        headless::installStopHandlers(exitWithSignalCode);
        headless::setWaitActive(true);
        raise(SIGTERM);
        raise(SIGTERM);
      },
      ::testing::ExitedWithCode(143), "");
}

TEST_F(HeadlessStop, WaitActionUnlocksOnlyAfterStop)
{
  headless::installStopHandlers(exitWithSignalCode);
  EXPECT_EQ(headless::waitAction(), env::ProcessWaitAction::Continue);
  headless::setWaitActive(true);
  raise(SIGTERM);
  EXPECT_EQ(headless::waitAction(), env::ProcessWaitAction::Unlock);
}
