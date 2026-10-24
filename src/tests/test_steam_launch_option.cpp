#include "steamlaunchoption.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <gtest/gtest.h>

using namespace steamlaunch;

namespace
{
QProcessEnvironment envWith(const QString& xdg, const QString& home)
{
  QProcessEnvironment env;
  if (!xdg.isNull()) {
    env.insert("XDG_DATA_HOME", xdg);
  }
  env.insert("HOME", home);
  return env;
}
}  // namespace

TEST(SteamLaunchLine, ExactShape)
{
  const QString launcher = "/h/.local/share/fluorine/bin/fluorine-manager";
  EXPECT_EQ(launchOptionLine(launcher, "Skyrim SE", "", "SKSE"),
            "'/h/.local/share/fluorine/bin/fluorine-manager' -i 'Skyrim SE' run "
            "--headless -e 'SKSE' # %command%");
  EXPECT_EQ(launchOptionLine(launcher, "Skyrim SE", "Main", "SKSE"),
            "'/h/.local/share/fluorine/bin/fluorine-manager' -i 'Skyrim SE' -p "
            "'Main' "
            "run --headless -e 'SKSE' # %command%");
}

TEST(SteamLaunchLine, SurvivesShellWithSteamCommandSubstituted)
{
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString script = dir.filePath("echo-argv");
  {
    QFile f(script);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("#!/bin/sh\nfor a; do printf '%s\\n' \"$a\"; done\n");
    f.setPermissions(QFile::permissions(script) | QFile::ExeOwner);
  }

  const QString instance   = "a b";
  const QStringList titles = {"O'Brien's \"Mod\" #1 %command% $HOME \\x",
                              QString::fromUtf8("Ünïcødé")};
  for (const QString& title : titles) {
    const QString line = launchOptionLine(script, instance, "", title);
    EXPECT_EQ(line.count("%command%"), 1) << qPrintable(line);
    ASSERT_TRUE(line.endsWith("# %command%"));

    // Steam replaces the token as text before handing the line to the shell.
    QString substituted = line;
    substituted.replace("%command%", "'/s/proton' waitforexitandrun '/g/Game.exe'");

    QProcess p;
    p.start("/bin/sh", {"-c", substituted});
    ASSERT_TRUE(p.waitForFinished(10000));
    const QStringList argv =
        QString::fromUtf8(p.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
    EXPECT_EQ(argv, (QStringList{"-i", instance, "run", "--headless", "-e", title}));
  }
}

TEST(SteamLaunchLine, InstalledPathUsesAbsoluteXdgDataHome)
{
  EXPECT_EQ(installedLauncherPath(envWith("/x", "/h")),
            "/x/fluorine/bin/fluorine-manager");
}

TEST(SteamLaunchLine, InstalledPathIgnoresRelativeXdgDataHome)
{
  EXPECT_EQ(installedLauncherPath(envWith("rel", "/h")),
            "/h/.local/share/fluorine/bin/fluorine-manager");
}

TEST(SteamLaunchLine, LauncherFallsBackToRunning)
{
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto env = envWith(dir.path(), "/h");
  EXPECT_EQ(launcherForLine(env, "/run/fm"), "/run/fm");

  ASSERT_TRUE(QDir().mkpath(dir.filePath("fluorine/bin")));
  QFile f(installedLauncherPath(env));
  ASSERT_TRUE(f.open(QIODevice::WriteOnly));
  f.close();
  EXPECT_EQ(launcherForLine(env, "/run/fm"), installedLauncherPath(env));
}

TEST(SteamLaunchLine, InstanceArgumentPortableUsesDirectory)
{
  EXPECT_EQ(instanceArgument(true, "/p/inst", "Portable"), "/p/inst");
  EXPECT_EQ(instanceArgument(false, "/p/inst", "Skyrim SE"), "Skyrim SE");
}

TEST(SteamLaunchLine, AppIdExeWinsAndMustBeDigits)
{
  EXPECT_EQ(steamAppIdFor("", "489830"), std::optional<QString>("489830"));
  EXPECT_EQ(steamAppIdFor(" 22380 ", "489830"), std::optional<QString>("22380"));
  EXPECT_EQ(steamAppIdFor("abc", ""), std::nullopt);
  EXPECT_EQ(steamAppIdFor("", "1,2"), std::nullopt);
  EXPECT_EQ(steamAppIdFor("abc", "489830"), std::nullopt);
}

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
