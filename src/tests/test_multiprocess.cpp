#include "multiprocess.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QUuid>
#include <gtest/gtest.h>
#include <log.h>

namespace
{
QString uniqueKey()
{
  return "mo-test-" + QUuid::createUuid().toString(QUuid::Id128);
}

TEST(MultiProcess, SecondInstanceIsEphemeral)
{
  const QString key = uniqueKey();
  MOMultiProcess primary(false, nullptr, key);
  ASSERT_FALSE(primary.ephemeral());

  MOMultiProcess second(false, nullptr, key);
  EXPECT_TRUE(second.ephemeral());
}

TEST(MultiProcess, EphemeralSeesHeadlessPrimary)
{
  const QString key = uniqueKey();
  MOMultiProcess primary(false, nullptr, key);
  ASSERT_FALSE(primary.ephemeral());
  primary.setHeadlessPrimary(true);

  MOMultiProcess second(false, nullptr, key);
  ASSERT_TRUE(second.ephemeral());
  EXPECT_TRUE(second.primaryIsHeadless());
}

TEST(MultiProcess, EphemeralSeesNormalPrimary)
{
  const QString key = uniqueKey();
  MOMultiProcess primary(false, nullptr, key);
  ASSERT_FALSE(primary.ephemeral());

  MOMultiProcess second(false, nullptr, key);
  ASSERT_TRUE(second.ephemeral());
  EXPECT_FALSE(second.primaryIsHeadless());
}
}  // namespace

int main(int argc, char** argv)
{
  QTemporaryDir bus;
  qputenv("DBUS_SESSION_BUS_ADDRESS", ("unix:path=" + bus.filePath("absent")).toUtf8());
  QCoreApplication app(argc, argv);
  MOBase::log::LoggerConfiguration configuration;
  configuration.name = "test_multiprocess";
  MOBase::log::createDefault(configuration);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
