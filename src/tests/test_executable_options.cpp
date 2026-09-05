#include "executableslist.h"

#include <gtest/gtest.h>

TEST(ExecutableOptions, SteamDefaultsOnForNewAndPluginExecutables)
{
  EXPECT_TRUE(Executable().useSteam());
  const MOBase::ExecutableInfo info(QStringLiteral("Game"), QFileInfo("game.exe"));
  EXPECT_TRUE(Executable(info, Executable::UseProton).useSteam());
  EXPECT_TRUE(Executable(info, {}).useSteam());
}

TEST(ExecutableOptions, SteamChoiceSurvivesCloningMergingAndFlagChanges)
{
  Executable tool(QStringLiteral("xEdit"));
  tool.useSteam(false).steamAppID(QStringLiteral("489830"));
  const Executable clone = tool;
  EXPECT_FALSE(clone.useSteam());

  Executable edited;
  edited.mergeFrom(clone);
  edited.flags(Executable::UseProton | Executable::UseTerminal);
  EXPECT_FALSE(edited.useSteam());
  EXPECT_TRUE(edited.useProton());
  EXPECT_TRUE(edited.useTerminal());
  EXPECT_EQ(QStringLiteral("489830"), edited.steamAppID());

  edited.useSteam(true);
  EXPECT_TRUE(edited.useSteam());
  EXPECT_FALSE(tool.useSteam());
}
