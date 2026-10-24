#include "desktopnotifier.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <uibase/log.h>

namespace
{
class FakeBus : public notify::Bus
{
public:
  bool owner      = true;
  bool notifyOk   = true;
  int notifyCalls = 0;
  QString appName, summary, body;

  bool hasOwner() override { return owner; }

  bool notify(const QString& app, const QString& sum, const QString& text) override
  {
    ++notifyCalls;
    appName = app;
    summary = sum;
    body    = text;
    return notifyOk;
  }
};

TEST(DesktopNotifier, SendSkipsWhenNoOwner)
{
  FakeBus bus;
  bus.owner = false;
  EXPECT_FALSE(notify::send(bus, "Couldn't start Skyrim", "reason"));
  EXPECT_EQ(bus.notifyCalls, 0);
}

TEST(DesktopNotifier, SendPassesFluorineAppName)
{
  FakeBus bus;
  EXPECT_TRUE(notify::send(bus, "Couldn't start Skyrim", "reason"));
  EXPECT_EQ(bus.notifyCalls, 1);
  EXPECT_EQ(bus.appName, "Fluorine");
  EXPECT_EQ(bus.summary, "Couldn't start Skyrim");
  EXPECT_EQ(bus.body, "reason");
}

TEST(DesktopNotifier, SendReturnsFalseWhenNotifyFails)
{
  FakeBus bus;
  bus.notifyOk = false;
  EXPECT_FALSE(notify::send(bus, "s", "b"));
  EXPECT_EQ(bus.notifyCalls, 1);
}

TEST(DesktopNotifier, AddressPrefersDbusSessionBusAddress)
{
  QProcessEnvironment env;
  env.insert("DBUS_SESSION_BUS_ADDRESS", "unix:path=/x");
  env.insert("XDG_RUNTIME_DIR", "/run/user/1000");
  EXPECT_EQ(notify::sessionBusAddress(env), "unix:path=/x");
}

TEST(DesktopNotifier, AddressFallsBackToXdgRuntimeDir)
{
  QProcessEnvironment env;
  env.insert("XDG_RUNTIME_DIR", "/run/user/1000");
  EXPECT_EQ(notify::sessionBusAddress(env), "unix:path=/run/user/1000/bus");
}

TEST(DesktopNotifier, AddressEmptyWithoutEither)
{
  EXPECT_EQ(notify::sessionBusAddress(QProcessEnvironment()), "");
}

TEST(DesktopNotifier, RealBusWithoutDaemonHasNoOwner)
{
  QTemporaryDir dir;
  QProcessEnvironment env;
  env.insert("DBUS_SESSION_BUS_ADDRESS", "unix:path=" + dir.filePath("absent"));

  QElapsedTimer timer;
  timer.start();
  auto bus = notify::sessionBus(env);
  ASSERT_NE(bus, nullptr);
  EXPECT_FALSE(bus->hasOwner());
  EXPECT_FALSE(bus->notify("Fluorine", "s", "b"));
  EXPECT_LT(timer.elapsed(), 5000);
}
}  // namespace

int main(int argc, char** argv)
{
  QTemporaryDir bus;
  qputenv("DBUS_SESSION_BUS_ADDRESS", ("unix:path=" + bus.filePath("absent")).toUtf8());
  QCoreApplication app(argc, argv);
  MOBase::log::LoggerConfiguration configuration;
  configuration.name = "test_desktopnotifier";
  MOBase::log::createDefault(configuration);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
