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

TEST(ExecutableOptions, CustomEnvVarsDefaultEmpty)
{
  EXPECT_TRUE(Executable().customEnvVars().isEmpty());
  const MOBase::ExecutableInfo info(QStringLiteral("Game"), QFileInfo("game.exe"));
  EXPECT_TRUE(Executable(info, Executable::UseProton).customEnvVars().isEmpty());
}

TEST(ExecutableOptions, CustomEnvVarsSurvivesCloningAndMerging)
{
  Executable tool(QStringLiteral("xEdit"));
  tool.customEnvVars(QStringLiteral("FOO=bar BAZ='lorem ipsum'"));
  const Executable clone = tool;
  EXPECT_EQ(QStringLiteral("FOO=bar BAZ='lorem ipsum'"), clone.customEnvVars());

  Executable edited;
  edited.mergeFrom(clone);
  EXPECT_EQ(QStringLiteral("FOO=bar BAZ='lorem ipsum'"), edited.customEnvVars());

  edited.customEnvVars(QStringLiteral("OTHER=1"));
  EXPECT_EQ(QStringLiteral("OTHER=1"), edited.customEnvVars());
  EXPECT_EQ(QStringLiteral("FOO=bar BAZ='lorem ipsum'"), tool.customEnvVars());

  // mergeFrom overwrites with the source value, including clearing to empty
  Executable cleared;
  edited.mergeFrom(cleared);
  EXPECT_TRUE(edited.customEnvVars().isEmpty());
}
