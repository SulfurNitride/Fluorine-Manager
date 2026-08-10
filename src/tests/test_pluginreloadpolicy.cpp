#include "plugincallgate.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

TEST(PluginReloadPolicy, MissingGenerationMayBeLoaded)
{
  EXPECT_EQ(pluginReloadDecision(false), PluginReloadDecision::Load);
}

TEST(PluginReloadPolicy, LoadedGenerationRequiresRestart)
{
  EXPECT_EQ(pluginReloadDecision(true),
            PluginReloadDecision::RestartRequired);
}

TEST(PluginReloadPolicy, ForwardedReloadInsideModalLoopDoesNotCloseCallGate)
{
  auto barrier = std::make_shared<SettingsWriteBarrier>();
  auto gate    = std::make_shared<PluginCallGate>(barrier);

  int pluginCalls = 0;
  ASSERT_TRUE(gate->runIfAllowed([&] { ++pluginCalls; }));

  QEventLoop outerLoop;
  QTimer::singleShot(0, &outerLoop, [&] {
    QEventLoop modalLoop;
    QTimer::singleShot(0, &modalLoop, [&] {
      // A loaded generation is rejected synchronously. No callback gate or
      // plugin object is touched, even though an outer plugin stack is
      // suspended by this nested event loop.
      EXPECT_EQ(pluginReloadDecision(true),
                PluginReloadDecision::RestartRequired);
      modalLoop.quit();
    });
    modalLoop.exec();
    outerLoop.quit();
  });
  outerLoop.exec();

  EXPECT_TRUE(gate->runIfAllowed([&] { ++pluginCalls; }));
  EXPECT_EQ(pluginCalls, 2);
}

TEST(PluginReloadPolicy, GlobalFailStopRejectsQueuedPluginCall)
{
  auto barrier = std::make_shared<SettingsWriteBarrier>();
  auto gate    = std::make_shared<PluginCallGate>(barrier);
  barrier->suppress();

  bool called = false;
  EXPECT_FALSE(gate->runIfAllowed([&] { called = true; }));
  EXPECT_FALSE(called);
}

int main(int argc, char** argv)
{
  QCoreApplication application(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
