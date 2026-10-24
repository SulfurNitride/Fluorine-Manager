#include "headless.h"
#include <QApplication>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <uibase/log.h>
#include <uibase/report.h>
#include <utility>
#include <vector>

namespace
{
using Call = std::pair<QString, QString>;

class Headless : public ::testing::Test
{
protected:
  void TearDown() override { headless::deactivateForTesting(); }

  void activateWithFake()
  {
    headless::activate([this](const QString& s, const QString& b) {
      calls.emplace_back(s, b);
    });
  }

  std::vector<Call> calls;
};

TEST_F(Headless, InactiveByDefault)
{
  EXPECT_FALSE(headless::active());
  activateWithFake();
  EXPECT_TRUE(headless::active());
}

TEST_F(Headless, FailNotifiesWithSubject)
{
  activateWithFake();
  headless::setSubject("Skyrim Special Edition");
  headless::fail("x");
  ASSERT_EQ(calls.size(), 1u);
  EXPECT_EQ(calls[0].first, "Couldn't start Skyrim Special Edition");
  EXPECT_EQ(calls[0].second, "x");
}

TEST_F(Headless, FailWithoutSubjectUsesGenericSummary)
{
  activateWithFake();
  headless::fail("x");
  ASSERT_EQ(calls.size(), 1u);
  EXPECT_EQ(calls[0].first, "Fluorine couldn't start the game");
}

TEST_F(Headless, FirstFailureIsKept)
{
  activateWithFake();
  EXPECT_FALSE(headless::firstFailure().has_value());
  headless::fail("a");
  headless::fail("b");
  EXPECT_EQ(headless::firstFailure(), QString("a"));
  EXPECT_EQ(calls.size(), 2u);
}

TEST_F(Headless, ReportErrorRoutesToHandlerWithoutWidgets)
{
  activateWithFake();
  MOBase::reportError("boom");
  ASSERT_EQ(calls.size(), 1u);
  EXPECT_EQ(calls[0].second, "boom");
  EXPECT_TRUE(QApplication::topLevelWidgets().isEmpty());
}

TEST_F(Headless, CriticalOnTopRoutesToHandler)
{
  activateWithFake();
  MOBase::criticalOnTop("bad");
  ASSERT_EQ(calls.size(), 1u);
  EXPECT_EQ(calls[0].second, "bad");
  EXPECT_TRUE(QApplication::topLevelWidgets().isEmpty());
}

TEST_F(Headless, TaskDialogReturnsCancelWhenHeadless)
{
  activateWithFake();
  const auto r = MOBase::TaskDialog(nullptr, "t").main("m").content("c").exec();
  EXPECT_EQ(r, QMessageBox::Cancel);
  ASSERT_EQ(calls.size(), 1u);
  EXPECT_EQ(calls[0].second, "t\nm\nc");
}

TEST_F(Headless, RunExitCodeCompletedPassesGameCode)
{
  EXPECT_EQ(headless::runExitCode(headless::RunOutcome::Completed, 0), 0);
  EXPECT_EQ(headless::runExitCode(headless::RunOutcome::Completed, 3), 3);
}

TEST_F(Headless, RunExitCodeNotWaitedIsLaunchFailure)
{
  EXPECT_EQ(headless::runExitCode(headless::RunOutcome::Completed, -1), 125);
}

TEST_F(Headless, RunExitCodeOutOfRangeBecomesOne)
{
  EXPECT_EQ(headless::runExitCode(headless::RunOutcome::Completed, 259), 1);
}

TEST_F(Headless, RunExitCodeStoppedAndFailed)
{
  EXPECT_EQ(headless::runExitCode(headless::RunOutcome::Stopped, 7), 143);
  EXPECT_EQ(headless::runExitCode(headless::RunOutcome::Failed, 7), 125);
}
}  // namespace

int main(int argc, char** argv)
{
  QTemporaryDir bus;
  qputenv("DBUS_SESSION_BUS_ADDRESS", ("unix:path=" + bus.filePath("absent")).toUtf8());
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  MOBase::log::LoggerConfiguration configuration;
  configuration.name = "test_headless";
  MOBase::log::createDefault(configuration);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
