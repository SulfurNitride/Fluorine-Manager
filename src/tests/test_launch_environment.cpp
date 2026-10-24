#include "protonlauncher.h"
#include "launchenvironment.h"
#include "nativefileassociation.h"
#include "pluginprocessregistry.h"
#include "processlifetime.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QThread>
#include <boost/program_options.hpp>
#include <gtest/gtest.h>
#include <uibase/log.h>
#include <locale>
#include <csignal>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

// Avoid probing the user's Steam installation in a launch test. The launcher
// and the subprocess/environment handling themselves are the production code.
QString findSteamPath() { return {}; }
static QString slrRunScript;
QString getSlrRunScript() { return slrRunScript; }
QString fluorineDataDir() { return {}; }

TEST(LaunchArguments, UnicodeAndQuotedArgumentsSurviveCommandLineParsing)
{
  struct RestoreLocale {
    std::locale previous = std::locale();
    ~RestoreLocale() { std::locale::global(previous); }
  } restore;
  std::locale::global(std::locale::classic());
  const std::vector<std::string> expected{
      "fluorine-manager", "日本語/中文-한국어.exe", "Русский/العربية/हिन्दी/🚀",
      "café with spaces", R"("Z:\game files\Oblivion.exe" --flag="a b")",
      "an'apostrophe", "trailing\\", "tab\there", "$(literal);$HOME"};
  auto input = expected;
  std::vector<char*> argv;
  for (auto& argument : input) argv.push_back(argument.data());
  const auto commandLine = commandLineFromUtf8Arguments(argv.size(), argv.data());
  const auto actual = boost::program_options::split_unix(
      QString::fromStdWString(commandLine).toStdString());
  EXPECT_EQ(actual, expected);
  namespace po = boost::program_options;
  po::options_description options;
  options.add_options()("arguments", po::value<std::vector<std::string>>());
  po::positional_options_description positional;
  positional.add("arguments", -1);
  po::variables_map values;
  po::store(po::command_line_parser(actual).options(options).positional(positional).run(), values);
  EXPECT_EQ(values["arguments"].as<std::vector<std::string>>(), expected);
}

TEST(ProtonLocale, NeutralFallbacksUseUnicode)
{
  for (const QString& locale : {QString{}, QString{"C"}, QString{"POSIX"}}) {
    SCOPED_TRACE(locale.toStdString());
    QProcessEnvironment env;
    env.insert("LANG", locale);
    prepareProtonLocale(env);
    EXPECT_EQ(env.value("LANG"), "C.UTF-8");
    EXPECT_FALSE(env.contains("LC_ALL"));
    EXPECT_FALSE(env.contains("HOST_LC_ALL"));
    EXPECT_FALSE(env.contains("LC_CTYPE"));
  }
}

TEST(ProtonLocale, PreservesLanguageAndIndependentCategories)
{
  QProcessEnvironment env;
  env.insert("LANG", "ja_JP.UTF-8");
  env.insert("LC_MESSAGES", "fr_FR.UTF-8");
  env.insert("LANGUAGE", "fr:ja:en");
  env.insert("LC_NUMERIC", "C");
  env.insert("LC_TIME", "de_DE.UTF-8");
  const auto original = env;
  prepareProtonLocale(env);
  EXPECT_EQ(env, original);
}

TEST(ProtonLocale, UpgradesEncodingWithoutDiscardingLanguageOrModifier)
{
  const QMap<QString, QString> locales{
      {"ja_JP.SJIS", "ja_JP.UTF-8"},
      {"zh_CN.GB18030", "zh_CN.UTF-8"},
      {"ru_RU.KOI8-R", "ru_RU.UTF-8"},
      {"de_DE.ISO-8859-1", "de_DE.UTF-8"},
      {"sr_RS.ISO-8859-2@latin", "sr_RS.UTF-8@latin"},
      {"ar_SA", "ar_SA.UTF-8"},
      {"hi_IN.utf8", "hi_IN.utf8"},
      {"ko_KR.UTF-8", "ko_KR.UTF-8"},
      {"C", "C.UTF-8"}, {"POSIX", "C.UTF-8"}};
  for (auto it = locales.cbegin(); it != locales.cend(); ++it) {
    SCOPED_TRACE(it.key().toStdString());
    QProcessEnvironment env;
    env.insert("LANG", it.key());
    env.insert("LC_CTYPE", it.key());
    prepareProtonLocale(env);
    EXPECT_EQ(env.value("LANG"), it.value());
    EXPECT_EQ(env.value("LC_CTYPE"), it.value());
    EXPECT_FALSE(env.contains("LC_ALL"));
  }
}

TEST(ProtonLocale, ProtonHostOverrideWinsAndIsPassedThrough)
{
  QProcessEnvironment env;
  env.insert("HOST_LC_ALL", "ja_JP.SJIS");
  env.insert("LC_ALL", "C");
  env.insert("LANG", "de_DE.UTF-8");
  prepareProtonLocale(env);
  EXPECT_EQ(env.value("HOST_LC_ALL"), "ja_JP.UTF-8");
  EXPECT_EQ(env.value("LC_ALL"), "ja_JP.UTF-8");
  EXPECT_EQ(env.value("LANG"), "de_DE.UTF-8");
}

TEST(ProtonLocale, SteamSelectedLanguageReplacesInheritedHostCategories)
{
  QProcessEnvironment env;
  env.insert("LANG", "de_DE.UTF-8");
  env.insert("HOST_LC_ALL", "de_DE.UTF-8");
  env.insert("LC_ADDRESS", "de_DE.UTF-8");
  env.insert("LC_CTYPE", "de_DE.UTF-8");
  env.insert("LC_NUMERIC", "de_DE.UTF-8");
  env.insert("LC_MESSAGES", "de_DE.UTF-8");
  env.insert("LANGUAGE", "de:en");

  prepareProtonLocale(env, gameLanguageToLocale("english"));

  EXPECT_EQ(env.value("LANG"), "en_US.UTF-8");
  EXPECT_FALSE(env.contains("HOST_LC_ALL"));
  EXPECT_FALSE(env.contains("LC_ALL"));
  EXPECT_FALSE(env.contains("LC_ADDRESS"));
  EXPECT_FALSE(env.contains("LC_CTYPE"));
  EXPECT_FALSE(env.contains("LC_NUMERIC"));
  EXPECT_FALSE(env.contains("LC_MESSAGES"));
  EXPECT_FALSE(env.contains("LANGUAGE"));
}

TEST(ProtonLocale, ExplicitExecutableLocaleOverridesSteamLanguage)
{
  QProcessEnvironment env;
  env.insert("LANG", "de_DE.UTF-8");
  env.insert("LC_ADDRESS", "de_DE.UTF-8");
  env.insert("LC_MESSAGES", "fr_FR.UTF-8");
  const QMap<QString, QString> explicitValues{
      {"LANG", "fr_CA"}, {"LC_MESSAGES", "ja_JP.SJIS"}};

  prepareProtonLocale(env, gameLanguageToLocale("english"), explicitValues);

  EXPECT_EQ(env.value("LANG"), "fr_CA.UTF-8");
  EXPECT_FALSE(env.contains("LC_ADDRESS"));
  EXPECT_EQ(env.value("LC_MESSAGES"), "ja_JP.UTF-8");
}

TEST(ProtonLocale, SteamLanguageKeysMapWithoutInventingUnknownLocales)
{
  EXPECT_EQ(gameLanguageToLocale("english"), "en_US.UTF-8");
  EXPECT_EQ(gameLanguageToLocale(" EN "), "en_US.UTF-8");
  EXPECT_EQ(gameLanguageToLocale("de"), "de_DE.UTF-8");
  EXPECT_EQ(gameLanguageToLocale("pt-BR"), "pt_BR.UTF-8");
  EXPECT_EQ(gameLanguageToLocale("German"), "de_DE.UTF-8");
  EXPECT_EQ(gameLanguageToLocale("brazilian"), "pt_BR.UTF-8");
  EXPECT_EQ(gameLanguageToLocale("xx-YY"), "xx_YY.UTF-8");
  EXPECT_EQ(gameLanguageToLocale("ja-jp"), "ja_JP.UTF-8");
  EXPECT_EQ(gameLanguageToLocale("sr-rs@latin"), "sr_RS.UTF-8@latin");
  EXPECT_TRUE(gameLanguageToLocale("not-a-language").isEmpty());
}

TEST(ProtonLocale, ReadsGameLanguageWithoutSteamMetadata)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString iniPath = temp.filePath("skyrim.ini");
  const QList<QPair<QByteArray, QString>> cases{
      {"[General]\nsLanguage=ENGLISH\n", "en_US.UTF-8"},
      {"\xef\xbb\xbf[gEnErAl]\r\n sLaNgUaGe = \"GERMAN\" ; comment\r\n",
       "de_DE.UTF-8"},
      {"[Other]\nsLanguage=english\n[General] ; comment\nsLanguage='fr'\n",
       "fr_FR.UTF-8"},
      {"[General]\nsLanguage=ja ; comment\n", "ja_JP.UTF-8"},
      {QByteArray::fromHex("fffe5b00470065006e006500720061006c005d000a00"
                          "73004c0061006e00670075006100670065003d00720075000a00"),
       "ru_RU.UTF-8"},
      {"[Other]\nsLanguage=english\n", {}},
      {"[General]\n;sLanguage=german\nsLanguage=not-a-language\n", {}},
      {"[General]\nsLanguage=\n", {}},
  };
  for (const auto& [contents, expected] : cases) {
    SCOPED_TRACE(contents.toStdString());
    QFile ini(iniPath);
    ASSERT_TRUE(ini.open(QIODevice::WriteOnly));
    ASSERT_EQ(ini.write(contents), contents.size());
    ini.close();
    EXPECT_EQ(gameLocaleFromIniFiles({iniPath}), expected);
    ASSERT_TRUE(ini.open(QIODevice::ReadOnly));
    EXPECT_EQ(ini.readAll(), contents);
  }
  EXPECT_TRUE(gameLocaleFromIniFiles({}).isEmpty());
  EXPECT_TRUE(gameLocaleFromIniFiles({temp.filePath("missing.ini")}).isEmpty());
}

TEST(ProtonLocale, CustomGameIniOverridesMainButPrefsAndEditorInisDoNot)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  auto writeIni = [&](const QString& name, const QByteArray& body) {
    QFile file(temp.filePath(name));
    return file.open(QIODevice::WriteOnly) && file.write(body) == body.size();
  };
  ASSERT_TRUE(writeIni("fallout4.ini", "[General]\nsLanguage=en\n"));
  ASSERT_TRUE(writeIni("Fallout4Prefs.ini", "[General]\nsLanguage=de\n"));
  ASSERT_TRUE(writeIni("GECKCustom.ini", "[General]\nsLanguage=de\n"));
  const QStringList files{temp.filePath("fallout4.ini"),
                          temp.filePath("Fallout4CUSTOM.ini"),
                          temp.filePath("Fallout4Prefs.ini"),
                          temp.filePath("GECKCustom.ini")};
  EXPECT_EQ(gameLocaleFromIniFiles(files), "en_US.UTF-8");
  ASSERT_TRUE(writeIni("Fallout4CUSTOM.ini", "[General]\nsLanguage=fr\n"));
  EXPECT_EQ(gameLocaleFromIniFiles(files), "fr_FR.UTF-8");
  ASSERT_TRUE(writeIni("Fallout4CUSTOM.ini", "[Display]\nbFullScreen=1\n"));
  EXPECT_EQ(gameLocaleFromIniFiles(files), "en_US.UTF-8");
  ASSERT_TRUE(writeIni("Fallout4CUSTOM.ini", "[General]\nsLanguage=unknown\n"));
  EXPECT_TRUE(gameLocaleFromIniFiles(files).isEmpty());
}

TEST(ProtonLocale, FindsCustomLanguageWhenPluginDeclaresPrefsAsItsPrimaryIni)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString prefsPath = temp.filePath("StarfieldPrefs.ini");
  QFile prefs(prefsPath);
  ASSERT_TRUE(prefs.open(QIODevice::WriteOnly));
  ASSERT_GT(prefs.write("[Display]\nbFullScreen=1\n"), 0);
  prefs.close();
  const QString customPath = temp.filePath("StarfieldCustom.ini");
  QFile custom(customPath);
  ASSERT_TRUE(custom.open(QIODevice::WriteOnly));
  ASSERT_GT(custom.write("[General]\nsLanguage=de\n"), 0);
  custom.close();
  EXPECT_EQ(gameLocaleFromIniFiles({prefsPath, customPath}), "de_DE.UTF-8");
}

TEST(ProtonLocale, EmptyOverridesAllowSteamGameLanguageSelection)
{
  QProcessEnvironment env;
  env.insert("HOST_LC_ALL", "");
  env.insert("LC_ALL", "");
  env.insert("LC_CTYPE", "");
  env.insert("LANG", "ru_RU.UTF-8");
  prepareProtonLocale(env);
  EXPECT_FALSE(env.contains("HOST_LC_ALL"));
  EXPECT_FALSE(env.contains("LC_ALL"));
  EXPECT_FALSE(env.contains("LC_CTYPE"));
  EXPECT_EQ(env.value("LANG"), "ru_RU.UTF-8");
}

TEST(LaunchEnvironment, ExecutableValuesWinForNativeAndProtonLaunches)
{
  for (bool proton : {false, true}) {
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const QString script = temp.path() + "/capture environment";
    const QString output = temp.path() + "/result";
    QFile file(script);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("#!/bin/sh\n"
               "printf '%s\\n' \"$PROTON_ENABLE_WAYLAND\" \"$TEST_KEEP\" "
               "\"$TEST_EMPTY\" \"$TEST_LITERAL\" \"$LC_ALL\" > \"$FLUORINE_TEST_OUTPUT.tmp\"\n"
               "/bin/mv -- \"$FLUORINE_TEST_OUTPUT.tmp\" \"$FLUORINE_TEST_OUTPUT\"\n");
    file.close();
    ASSERT_TRUE(file.setPermissions(file.permissions() | QFile::ExeOwner));
    ProtonLauncher launcher;
    launcher.setBinary(script).setWorkingDir(temp.path())
        .setWrapper("PROTON_ENABLE_WAYLAND=1 TEST_KEEP=global TEST_EMPTY=global",
            wrapperOptionsFromLegacyEnvironment(
                "PROTON_ENABLE_WAYLAND=0\nTEST_EMPTY=\nTEST_LITERAL=a b=$(literal)\nLC_ALL=C\nHOST_LC_ALL="))
        .setSteamDrm(false).setUseSLR(false);
    if (proton) launcher.setProtonPath(script).setPrefix(temp.path());
    launcher.addEnvVar("FLUORINE_TEST_OUTPUT", output);
    ASSERT_TRUE(launcher.launch());
    QElapsedTimer timer;
    timer.start();
    while (!QFile::exists(output) && timer.elapsed() < 5000) {
      QCoreApplication::processEvents();
      QThread::msleep(10);
    }
    QFile result(output);
    ASSERT_TRUE(result.open(QIODevice::ReadOnly));
    const QByteArray expected = QByteArray("0\nglobal\n\na b=$(literal)\n") +
                                (proton ? "C.UTF-8\n" : "C\n");
    EXPECT_EQ(expected, result.readAll());
    QCoreApplication::processEvents();
  }
}

TEST(LaunchEnvironment, SteamBridgeKeepsPreparedEnvironmentWithoutPreloads)
{
  for (const auto& mode : {std::pair{false, false}, std::pair{true, false},
                          std::pair{false, true}, std::pair{true, true}}) {
    const auto [steamDrm, slr] = mode;
    SCOPED_TRACE(testing::Message() << "steam=" << steamDrm << " slr=" << slr);
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const QString script = temp.path() + "/capture";
    const QString output = temp.path() + "/result";
    QFile file(script);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("#!/bin/sh\n"
               "printf '%s\\n' \"$SteamEnv\" \"$LC_ALL\" \"$HOST_LC_ALL\" "
               "\"$LC_MESSAGES\" \"$LANGUAGE\" \"$LD_PRELOAD\" \"$UMU_ID\" "
               "\"$@\" > \"$FLUORINE_TEST_OUTPUT.tmp\"\n"
               "/bin/mv -- \"$FLUORINE_TEST_OUTPUT.tmp\" \"$FLUORINE_TEST_OUTPUT\"\n");
    file.close();
    ASSERT_TRUE(file.setPermissions(file.permissions() | QFile::ExeOwner));
    QFile runtime(temp.path() + "/runtime");
    ASSERT_TRUE(runtime.open(QIODevice::WriteOnly));
    runtime.write("#!/bin/sh\n"
                  "while [ \"$#\" -gt 0 ]; do\n"
                  "  case \"$1\" in\n"
                  "    --ld-preload=*) exit 90 ;;\n"
                  "    --) shift; exec \"$@\" ;;\n"
                  "  esac\n"
                  "  shift\n"
                  "done\nexit 91\n");
    runtime.close();
    ASSERT_TRUE(runtime.setPermissions(runtime.permissions() | QFile::ExeOwner));
    slrRunScript = runtime.fileName();
    ProtonLauncher launcher;
    launcher.setBinary("game.exe").setProtonPath(script).setPrefix(temp.path())
        .setWorkingDir(temp.path()).setSteamDrm(steamDrm).setUseSLR(slr)
        .setWrapper("LC_ALL=C HOST_LC_ALL=C");
    launcher.addEnvVar("HOST_LC_ALL", "C.UTF-8");
    launcher.addEnvVar("LC_MESSAGES", "ja_JP.UTF-8");
    launcher.addEnvVar("LANGUAGE", "ja:en");
    launcher.addEnvVar("SteamEnv", "0");
    launcher.addEnvVar("LD_PRELOAD", "");
    launcher.addEnvVar("FLUORINE_TEST_OUTPUT", output);
    ASSERT_TRUE(launcher.launch());
    QElapsedTimer timer;
    timer.start();
    while (!QFile::exists(output) && timer.elapsed() < 5000) {
      QCoreApplication::processEvents();
      QThread::msleep(10);
    }
    QFile result(output);
    ASSERT_TRUE(result.open(QIODevice::ReadOnly));
    const QByteArray expected = QByteArray(steamDrm ? "1\n" : "0\n") +
        "C.UTF-8\nC.UTF-8\nja_JP.UTF-8\nja:en\n\n\nwaitforexitandrun\ngame.exe\n";
    EXPECT_EQ(result.readAll(), expected);
    QCoreApplication::processEvents();
  }
  slrRunScript.clear();
}

TEST(LaunchEnvironment, UsesLanguageFromSteamAppManifestForChildOnly)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString gameDirectory =
      temp.filePath("steamapps/common/Skyrim Special Edition");
  ASSERT_TRUE(QDir().mkpath(gameDirectory));
  QFile manifest(temp.filePath("steamapps/appmanifest_489830.acf"));
  ASSERT_TRUE(manifest.open(QIODevice::WriteOnly | QIODevice::Text));
  manifest.write("\"AppState\"\n{\n"
                 "  \"appid\" \"489830\"\n"
                 "  \"UserConfig\"\n  {\n"
                 "    \"language\" \"english\"\n  }\n}\n");
  manifest.close();

  const QString proton = temp.filePath("fake-proton");
  const QString output = temp.filePath("locale-result");
  QFile script(proton);
  ASSERT_TRUE(script.open(QIODevice::WriteOnly));
  script.write("#!/bin/sh\nprintf '%s\\n' \"$LANG\" \"$LC_ADDRESS\" \"$LC_CTYPE\" > \"$FLUORINE_TEST_OUTPUT\"\n");
  script.close();
  ASSERT_TRUE(script.setPermissions(script.permissions() | QFile::ExeOwner));

  ProtonLauncher launcher;
  launcher.setBinary("SkyrimSE.exe").setProtonPath(proton).setSteamAppId(489830)
      .setGameDirectory(gameDirectory).setGameLocale("fr_FR.UTF-8")
      .setSteamDrm(false).setUseSLR(false)
      .addEnvVar("FLUORINE_TEST_OUTPUT", output);
  ASSERT_TRUE(launcher.launch());

  QElapsedTimer timer;
  timer.start();
  while (!QFile::exists(output) && timer.elapsed() < 5000) {
    QCoreApplication::processEvents();
    QThread::msleep(10);
  }
  QFile result(output);
  ASSERT_TRUE(result.open(QIODevice::ReadOnly));
  EXPECT_EQ(result.readAll(), QByteArray("en_US.UTF-8\n\n\n"));
  QCoreApplication::processEvents();
}

TEST(LaunchEnvironment, ExistingDxvkConfigIsNotReplaced)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString configPath = temp.filePath("dxvk.conf");
  const QByteArray userConfig = "dxvk.enableGraphicsPipelineLibrary = True\n";
  QFile config(configPath);
  ASSERT_TRUE(config.open(QIODevice::WriteOnly));
  ASSERT_EQ(config.write(userConfig), userConfig.size());
  config.close();

  const QString proton = temp.filePath("fake-proton");
  const QString output = temp.filePath("dxvk-result");
  QFile script(proton);
  ASSERT_TRUE(script.open(QIODevice::WriteOnly));
  script.write("#!/bin/sh\nprintf '%s\\n' \"$DXVK_CONFIG_FILE\" > \"$FLUORINE_TEST_OUTPUT\"\n");
  script.close();
  ASSERT_TRUE(script.setPermissions(script.permissions() | QFile::ExeOwner));

  ProtonLauncher launcher;
  launcher.setBinary("game.exe").setProtonPath(proton).setPrefix(temp.path())
      .setSteamDrm(false).setUseSLR(false).addEnvVar("FLUORINE_TEST_OUTPUT", output);
  ASSERT_TRUE(launcher.launch());

  QElapsedTimer timer;
  timer.start();
  while (!QFile::exists(output) && timer.elapsed() < 5000) {
    QCoreApplication::processEvents();
    QThread::msleep(10);
  }
  QFile result(output);
  ASSERT_TRUE(result.open(QIODevice::ReadOnly));
  EXPECT_EQ(result.readAll().trimmed(), configPath.toUtf8());
  ASSERT_TRUE(config.open(QIODevice::ReadOnly));
  EXPECT_EQ(config.readAll(), userConfig);
  QCoreApplication::processEvents();
}

TEST(LaunchEnvironment, InheritedDxvkConfigTakesPrecedenceOverPrefixDefault)
{
  struct RestoreEnvironment {
    bool wasSet = qEnvironmentVariableIsSet("DXVK_CONFIG_FILE");
    QByteArray value = qgetenv("DXVK_CONFIG_FILE");
    ~RestoreEnvironment()
    {
      if (wasSet) qputenv("DXVK_CONFIG_FILE", value);
      else qunsetenv("DXVK_CONFIG_FILE");
    }
  } restore;

  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString prefix = temp.filePath("prefix");
  ASSERT_TRUE(QDir().mkpath(prefix));
  const QString userConfig = temp.filePath("user.dxvk.conf");
  QFile config(userConfig);
  ASSERT_TRUE(config.open(QIODevice::WriteOnly));
  ASSERT_GT(config.write("dxvk.hud = fps\n"), 0);
  config.close();
  ASSERT_TRUE(qputenv("DXVK_CONFIG_FILE", QFile::encodeName(userConfig)));

  const QString proton = temp.filePath("fake-proton");
  const QString output = temp.filePath("dxvk-result");
  QFile script(proton);
  ASSERT_TRUE(script.open(QIODevice::WriteOnly));
  script.write("#!/bin/sh\nprintf '%s\\n' \"$DXVK_CONFIG_FILE\" > \"$FLUORINE_TEST_OUTPUT\"\n");
  script.close();
  ASSERT_TRUE(script.setPermissions(script.permissions() | QFile::ExeOwner));

  ProtonLauncher launcher;
  launcher.setBinary("game.exe").setProtonPath(proton).setPrefix(prefix)
      .setSteamDrm(false).setUseSLR(false).addEnvVar("FLUORINE_TEST_OUTPUT", output);
  ASSERT_TRUE(launcher.launch());

  QElapsedTimer timer;
  timer.start();
  while (!QFile::exists(output) && timer.elapsed() < 5000) {
    QCoreApplication::processEvents();
    QThread::msleep(10);
  }
  QFile result(output);
  ASSERT_TRUE(result.open(QIODevice::ReadOnly));
  EXPECT_EQ(result.readAll().trimmed(), userConfig.toUtf8());
  EXPECT_FALSE(QFileInfo::exists(QDir(prefix).filePath("dxvk.conf")));
  QCoreApplication::processEvents();
}

TEST(NativeFileAssociation, ExpandsExecFieldsIntoLiteralArgv)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString targetPath =
      temp.filePath("file $(touch should-not-run); 'with spaces'.txt");
  QFile target(targetPath);
  ASSERT_TRUE(target.open(QIODevice::WriteOnly));
  target.close();
  const QString desktopPath = temp.filePath("viewer.desktop");
  QFile desktop(desktopPath);
  ASSERT_TRUE(desktop.open(QIODevice::WriteOnly | QIODevice::Text));
  desktop.write("[Desktop Entry]\n"
                "Type=Application\n"
                "Name=Example Viewer\n"
                "Icon=example-viewer\n"
                "Exec=/usr/bin/true --label %c %i --file=%f --list=a,b \"two words\" %%done\n");
  desktop.close();

  QString error;
  const auto association = nativefileassociation::fromDesktopFile(
      desktopPath, QFileInfo(targetPath), QProcessEnvironment::systemEnvironment(),
      &error);
  ASSERT_TRUE(association) << error.toStdString();
  EXPECT_EQ(association->executable, "/usr/bin/true");
  const QStringList args = QProcess::splitCommand(association->arguments);
  EXPECT_EQ(args, QStringList({"--label", "Example Viewer", "--icon",
                               "example-viewer", "--file=" + targetPath,
                               "--list=a,b", "two words", "%done"}));
  EXPECT_EQ(QProcess::splitCommand(association->commandLine).first(),
            association->executable);
}

TEST(NativeFileAssociation, UnsupportedDesktopLaunchModesFailExplicitly)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString targetPath = temp.filePath("target.txt");
  QFile target(targetPath);
  ASSERT_TRUE(target.open(QIODevice::WriteOnly));
  target.close();

  for (const QByteArray& entry : {
           QByteArray("[Desktop Entry]\nType=Application\nTerminal=true\nExec=/usr/bin/true %f\n"),
           QByteArray("[Desktop Entry]\nType=Application\nExec=/usr/bin/true %x\n"),
           QByteArray("[Desktop Entry]\nType=Application\nExec=/usr/bin/true %f %u\n")}) {
    const QString desktopPath = temp.filePath("unsupported.desktop");
    QFile desktop(desktopPath);
    ASSERT_TRUE(desktop.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(desktop.write(entry), entry.size());
    desktop.close();
    QString error;
    EXPECT_FALSE(nativefileassociation::fromDesktopFile(
        desktopPath, QFileInfo(targetPath), QProcessEnvironment::systemEnvironment(),
        &error));
    EXPECT_FALSE(error.isEmpty());
  }
}

TEST(ProcessEnvironment, RescanRequiresTheMatchingPrefixOrLaunchToken)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString prefix = temp.filePath("pfx");
  ASSERT_TRUE(QDir().mkpath(prefix));
  const QByteArray processEnv = QByteArray("WINEPREFIX=") + prefix.toUtf8() +
      QByteArrayLiteral("\0FLUORINE_LAUNCH_TOKEN=launch-a\0LANG=en_US.UTF-8\0");

  EXPECT_TRUE(processEnvironmentMatchesLaunch(processEnv, prefix, {}));
  EXPECT_TRUE(processEnvironmentMatchesLaunch(processEnv, {}, "launch-a"));
  EXPECT_TRUE(processEnvironmentMatchesLaunch(processEnv, prefix, "launch-a"));
  EXPECT_FALSE(processEnvironmentMatchesLaunch(processEnv, prefix, "launch-b"));
  EXPECT_FALSE(processEnvironmentMatchesLaunch(processEnv, temp.filePath("other"), {}));
  const QByteArray prefixOnly = QByteArray("WINEPREFIX=") + prefix.toUtf8() +
                                QByteArray(1, '\0');
  EXPECT_TRUE(processEnvironmentMatchesLaunch(prefixOnly, prefix, "launch-a"));
  const QByteArray conflicting = QByteArray("WINEPREFIX=") +
      temp.filePath("other").toUtf8() + QByteArray(1, '\0') +
      QByteArrayLiteral("FLUORINE_LAUNCH_TOKEN=launch-a\0");
  EXPECT_FALSE(processEnvironmentMatchesLaunch(conflicting, prefix, "launch-a"));
  EXPECT_FALSE(processEnvironmentMatchesLaunch(processEnv, {}, {}));
  EXPECT_FALSE(processEnvironmentMatchesLaunch(QByteArray{}, prefix, {}));
}

TEST(LaunchWrappers, PerWrapperOptionsComposeWithGlobalsForNativeProtonAndSlr)
{
  for (int mode : {0, 1, 2}) {
    for (int localMode : {0, 1, 2}) { // inherit, variables only, variables + command
      SCOPED_TRACE(testing::Message() << "launch=" << mode << " local=" << localMode);
      QTemporaryDir temp;
      ASSERT_TRUE(temp.isValid());
      const QString output = temp.filePath("result");
      const QString trace = temp.filePath("trace");
      const QString binary = temp.filePath("target program");
      const QString global = temp.filePath("global wrapper");
      const QString local = temp.filePath("local wrapper");
      auto writeScript = [](const QString& path, const QByteArray& body) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;
        if (file.write("#!/bin/sh\n" + body) < 0) return false;
        return file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
      };
      ASSERT_TRUE(writeScript(global,
          "printf 'global:%s:%s\\n' \"$1\" \"$WRAPPER_TEST_SCOPE\" >> \"$WRAPPER_TEST_TRACE\"\n"
          "shift\nexec \"$@\"\n"));
      ASSERT_TRUE(writeScript(local,
          "printf 'local:%s:%s\\n' \"$1\" \"$WRAPPER_TEST_SCOPE\" >> \"$WRAPPER_TEST_TRACE\"\n"
          "shift\nexec \"$@\"\n"));
      ASSERT_TRUE(writeScript(binary,
          "printf '%s\\n' \"$WRAPPER_TEST_SCOPE\" \"$WRAPPER_TEST_KEEP\" "
          "\"$WRAPPER_TEST_EMPTY\" \"$@\" > \"$FLUORINE_TEST_OUTPUT.tmp\"\n"
          "/bin/mv -- \"$FLUORINE_TEST_OUTPUT.tmp\" \"$FLUORINE_TEST_OUTPUT\"\n"));
      slrRunScript = temp.filePath("runtime");
      struct ResetRuntime { ~ResetRuntime() { slrRunScript.clear(); } } resetRuntime;
      ASSERT_TRUE(writeScript(slrRunScript,
          "while [ \"$#\" -gt 0 ]; do\n"
          "  if [ \"$1\" = -- ]; then shift; exec \"$@\"; fi\n"
          "  shift\ndone\nexit 91\n"));

      const QString globalOptions = QString(
          "WRAPPER_TEST_SCOPE=global WRAPPER_TEST_KEEP=global WRAPPER_TEST_EMPTY=global "
          "\"%1\" \"global option\" %command%").arg(global);
      QString localOptions;
      if (localMode > 0) localOptions = "WRAPPER_TEST_SCOPE=local WRAPPER_TEST_EMPTY=";
      if (localMode > 1) localOptions += QString(" \"%1\" \"local option\" %command%").arg(local);
      ProtonLauncher launcher;
      launcher.setBinary(binary).setWorkingDir(temp.path())
          .setArguments({"argument with spaces", "--literal=a=b", "$(literal);$HOME"})
          .setWrapper(globalOptions, localOptions).setSteamDrm(false).setUseSLR(mode == 2)
          .addEnvVar("FLUORINE_TEST_OUTPUT", output).addEnvVar("WRAPPER_TEST_TRACE", trace);
      if (mode > 0) launcher.setProtonPath(binary).setPrefix(temp.path());
      ASSERT_TRUE(launcher.launch());
      QElapsedTimer timer;
      timer.start();
      while (!QFile::exists(output) && timer.elapsed() < 5000) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
      }
      const QByteArray scope = localMode ? "local" : "global";
      QFile result(output), wrapperTrace(trace);
      ASSERT_TRUE(result.open(QIODevice::ReadOnly));
      ASSERT_TRUE(wrapperTrace.open(QIODevice::ReadOnly));
      QByteArray expected = scope + "\nglobal\n" + (localMode ? "\n" : "global\n");
      if (mode > 0) expected += "waitforexitandrun\n" + binary.toUtf8() + '\n';
      expected += "argument with spaces\n--literal=a=b\n$(literal);$HOME\n";
      EXPECT_EQ(result.readAll(), expected);
      QByteArray expectedTrace = "global:global option:" + scope + '\n';
      if (localMode > 1) expectedTrace += "local:local option:local\n";
      EXPECT_EQ(wrapperTrace.readAll(), expectedTrace);
      QCoreApplication::processEvents();
    }
  }
}

TEST(LaunchWrappers, InvalidOptionsRejectLaunchAndCanBeReplaced)
{
  ProtonLauncher launcher;
  launcher.setBinary("/bin/true").setWrapper("", QString("mangohud") + QChar::Null);
  const auto process = launcher.launch();
  EXPECT_FALSE(process);
  EXPECT_EQ(process.pid(), 0);
  EXPECT_EQ(errno, EINVAL);
  launcher.setWrapper("/missing/global/wrapper", "/missing/local/wrapper");
  launcher.setWrapper("");
  EXPECT_TRUE(launcher.launch());
  QCoreApplication::processEvents();
}

namespace
{
bool processFinished(const env::NativeProcess& process)
{
  QElapsedTimer timer;
  timer.start();
  do {
    QCoreApplication::processEvents();
    if (process.status().state != env::ProcessState::Running) return true;
    QThread::msleep(5);
  } while (timer.elapsed() < 3000);
  return false;
}

struct RunningProcessCleanup
{
  env::NativeProcess process;
  ~RunningProcessCleanup()
  {
    if (process && process.status().state == env::ProcessState::Running) {
      process.sendSignal(SIGKILL);
      processFinished(process);
    }
  }
};

env::ProcessCompletion waitWithDeadline(const env::NativeProcess& process,
                                        const QStringList& expected = {})
{
  QElapsedTimer timer;
  timer.start();
  return env::waitForProcessTree(process, expected, false,
      [&](pid_t, const QString&) {
        return timer.elapsed() > 3000 ? env::ProcessWaitAction::Abort
                                      : env::ProcessWaitAction::Continue;
      });
}
}  // namespace

TEST(LaunchEnvironment, NonSteamGameLanguageAndOverridesAffectOnlyTheChild)
{
  struct RestoreEnvironment {
    QMap<QByteArray, std::optional<QByteArray>> values;
    ~RestoreEnvironment()
    {
      for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (it.value()) qputenv(it.key().constData(), *it.value());
        else qunsetenv(it.key().constData());
      }
    }
  } restore;
  for (const auto* key : {"LANG", "LC_ALL", "HOST_LC_ALL", "LC_ADDRESS",
                          "LC_MESSAGES", "LANGUAGE"}) {
    restore.values.insert(key, qEnvironmentVariableIsSet(key)
        ? std::optional<QByteArray>(qgetenv(key)) : std::nullopt);
    ASSERT_TRUE(qputenv(key, QByteArray(key) == "LANGUAGE" ? "de:en" : "de_DE.UTF-8"));
  }

  struct Case { QByteArray language; QString options; QByteArray expected; };
  const QList<Case> cases{
      {"english", {}, "en_US.UTF-8\n\n\n\n\n\n"},
      {"english", "LANG=fr_CA", "fr_CA.UTF-8\n\n\n\n\n\n"},
      {"english", "HOST_LC_ALL=ja_JP", "ja_JP.UTF-8\nja_JP.UTF-8\nja_JP.UTF-8\n\n\n\n"},
      {"unknown", {}, "de_DE.UTF-8\nde_DE.UTF-8\nde_DE.UTF-8\nde_DE.UTF-8\nde_DE.UTF-8\nde:en\n"},
  };
  for (uint32_t appId : {0u, 489830u}) {
    for (const auto& test : cases) {
      SCOPED_TRACE(testing::Message() << "appId=" << appId
                   << " language=" << test.language.toStdString()
                   << " options=" << test.options.toStdString());
      QTemporaryDir temp;
      ASSERT_TRUE(temp.isValid());
      const QString iniPath = temp.filePath("Skyrim.ini");
      QFile ini(iniPath);
      ASSERT_TRUE(ini.open(QIODevice::WriteOnly));
      ASSERT_GT(ini.write("[General]\nsLanguage=" + test.language + '\n'), 0);
      ini.close();
      const QString proton = temp.filePath("fake-proton");
      const QString output = temp.filePath("locale-result");
      QFile script(proton);
      ASSERT_TRUE(script.open(QIODevice::WriteOnly));
      ASSERT_GT(script.write("#!/bin/sh\nprintf '%s\\n' \"$LANG\" \"$LC_ALL\" "
          "\"$HOST_LC_ALL\" \"$LC_ADDRESS\" \"$LC_MESSAGES\" \"$LANGUAGE\" "
          "> \"$FLUORINE_TEST_OUTPUT\"\n"), 0);
      script.close();
      ASSERT_TRUE(script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));

      auto process = ProtonLauncher().setBinary("SkyrimSE.exe")
          .setProtonPath(proton).setGameDirectory(temp.path()).setSteamAppId(appId)
          .setGameLocale(gameLocaleFromIniFiles({iniPath})).setStoreVariant("GOG")
          .setSteamDrm(false).setUseSLR(false).setWrapper({}, test.options)
          .addEnvVar("FLUORINE_TEST_OUTPUT", output).launch();
      ASSERT_TRUE(process);
      RunningProcessCleanup cleanup{process};
      ASSERT_TRUE(processFinished(process));
      EXPECT_EQ(process.status().exitCode, 0);
      QFile result(output);
      ASSERT_TRUE(result.open(QIODevice::ReadOnly));
      EXPECT_EQ(result.readAll(), test.expected);
      EXPECT_EQ(qgetenv("LANG"), "de_DE.UTF-8");
      EXPECT_EQ(qgetenv("LC_ALL"), "de_DE.UTF-8");
      EXPECT_EQ(qgetenv("HOST_LC_ALL"), "de_DE.UTF-8");
      EXPECT_EQ(qgetenv("LC_ADDRESS"), "de_DE.UTF-8");
      EXPECT_EQ(qgetenv("LC_MESSAGES"), "de_DE.UTF-8");
      EXPECT_EQ(qgetenv("LANGUAGE"), "de:en");
    }
  }
}

TEST(NativeLaunchLifecycle, PreservesNonzeroExitThroughImmediateAndDelayedPluginWait)
{
  for (const bool delayed : {false, true}) {
    auto process = ProtonLauncher().setBinary("/bin/sh")
        .setArguments({"-c", "exit 37"}).launch();
    ASSERT_TRUE(process);
    RunningProcessCleanup cleanup{process};
    PluginProcessRegistry registry;
    const auto token = registry.insert(process, {QStringLiteral("sh")});
    if (delayed) ASSERT_TRUE(processFinished(process));
    const auto fromPlugin = registry.take(token);
    ASSERT_TRUE(fromPlugin);
    const auto completion = waitWithDeadline(
        fromPlugin->native, fromPlugin->expectedExecutables);
    EXPECT_EQ(completion.result, env::ProcessWaitResult::Completed);
    EXPECT_EQ(completion.exitCode, 37);
    EXPECT_FALSE(registry.take(token));
  }
}

TEST(NativeLaunchLifecycle, UnlockWithoutVfsLeavesApplicationRunning)
{
  auto process = ProtonLauncher().setBinary("/bin/sleep").setArguments({"30"}).launch();
  ASSERT_TRUE(process);
  RunningProcessCleanup cleanup{process};
  const auto completion = env::waitForProcessTree(process, {}, false,
      [](pid_t, const QString&) { return env::ProcessWaitAction::Unlock; });
  EXPECT_EQ(completion.result, env::ProcessWaitResult::Unlocked);
  EXPECT_EQ(process.status().state, env::ProcessState::Running);
}

TEST(NativeLaunchLifecycle, UnlockWithVfsTerminatesOwnedApplication)
{
  auto process = ProtonLauncher().setBinary("/bin/sleep").setArguments({"30"}).launch();
  ASSERT_TRUE(process);
  RunningProcessCleanup cleanup{process};
  const auto completion = env::waitForProcessTree(process, {}, true,
      [](pid_t, const QString&) { return env::ProcessWaitAction::Unlock; });
  EXPECT_EQ(completion.result, env::ProcessWaitResult::Unlocked);
  EXPECT_NE(completion.exitCode, 0);
  ASSERT_TRUE(processFinished(process));
  EXPECT_TRUE(process.status().crashed);
}

TEST(NativeLaunchLifecycle, CancelDuringPreventExitLeavesApplicationRunning)
{
  auto process = ProtonLauncher().setBinary("/bin/sleep").setArguments({"30"}).launch();
  ASSERT_TRUE(process);
  RunningProcessCleanup cleanup{process};
  const auto completion = env::waitForProcessTree(process, {}, true,
      [](pid_t, const QString&) { return env::ProcessWaitAction::Cancel; });
  EXPECT_EQ(completion.result, env::ProcessWaitResult::Cancelled);
  EXPECT_EQ(process.status().state, env::ProcessState::Running);
}

TEST(NativeLaunchLifecycle, UnlockTerminatesPendingChildAfterLauncherExits)
{
  struct Subreaper {
    int previous = 0;
    bool active = false;
    pid_t child = 0;
    env::NativeProcess observed;
    ~Subreaper()
    {
      if (child > 0) {
        if (observed.status().state == env::ProcessState::Running)
          observed.sendSignal(SIGKILL);
        QElapsedTimer timer;
        timer.start();
        while (::waitpid(child, nullptr, WNOHANG) == 0 && timer.elapsed() < 2000)
          QThread::msleep(5);
      }
      if (active) ::prctl(PR_SET_CHILD_SUBREAPER, previous);
    }
  } subreaper;
  if (::prctl(PR_GET_CHILD_SUBREAPER, &subreaper.previous) != 0 ||
      ::prctl(PR_SET_CHILD_SUBREAPER, 1) != 0) {
    GTEST_SKIP() << "kernel does not support subreaper fixture";
  }
  subreaper.active = true;

  QTemporaryDir fixture;
  ASSERT_TRUE(fixture.isValid());
  const QString scriptPath = fixture.filePath("pending-child.sh");
  const QString gatePath = fixture.filePath("continue.pipe");
  const QString pidPath = fixture.filePath("child.pid");
  QFile script(scriptPath);
  ASSERT_TRUE(script.open(QIODevice::WriteOnly));
  ASSERT_GT(script.write("#!/bin/sh\nread permission < \"$1\"\n"), 0);
  script.close();
  ASSERT_TRUE(script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
  ASSERT_EQ(::mkfifo(QFile::encodeName(gatePath).constData(), 0600), 0);

  auto root = ProtonLauncher().setBinary("/bin/sh").setArguments({
      "-c", "\"$1\" \"$2\" & printf '%s' \"$!\" > \"$3\"; exit 0",
      "launcher", scriptPath, gatePath, pidPath}).launch();
  ASSERT_TRUE(root);
  RunningProcessCleanup cleanup{root};
  ASSERT_TRUE(processFinished(root));
  QFile pidFile(pidPath);
  ASSERT_TRUE(pidFile.open(QIODevice::ReadOnly));
  subreaper.child = pidFile.readAll().toInt();
  ASSERT_GT(subreaper.child, 0);
  subreaper.observed = env::NativeProcess::observe(subreaper.child);
  ASSERT_TRUE(subreaper.observed);
  ASSERT_EQ(subreaper.observed.status().state, env::ProcessState::Running);

  bool unlockedPendingChild = false;
  const auto completion = env::waitForProcessTree(root, {"pendinggame.exe"}, true,
      [&](pid_t pid, const QString&) {
        if (pid == subreaper.child) {
          unlockedPendingChild = true;
          return env::ProcessWaitAction::Unlock;
        }
        return env::ProcessWaitAction::Continue;
      });
  EXPECT_TRUE(unlockedPendingChild);
  EXPECT_EQ(completion.result, env::ProcessWaitResult::Unlocked);
  EXPECT_EQ(subreaper.observed.status().state, env::ProcessState::Exited);
}

TEST(NativeLaunchLifecycle, DoesNotAdoptSameNamedProcessFromAnotherLaunch)
{
  auto unrelated = ProtonLauncher().setBinary("/bin/sleep")
      .setArguments({"30"}).launch();
  ASSERT_TRUE(unrelated);
  RunningProcessCleanup unrelatedCleanup{unrelated};
  auto process = ProtonLauncher().setBinary("/bin/sh")
      .setArguments({"-c", "exit 37"}).launch();
  ASSERT_TRUE(process);
  RunningProcessCleanup cleanup{process};
  const auto completion = waitWithDeadline(process, {"sleep"});
  EXPECT_EQ(completion.result, env::ProcessWaitResult::Completed);
  EXPECT_EQ(completion.exitCode, 37);
  EXPECT_EQ(unrelated.status().state, env::ProcessState::Running);
}

TEST(NativeLaunchLifecycle, PluginTokenFollowsChildAfterDelayedLauncherHandoff)
{
  struct Subreaper {
    int previous = 0;
    bool active = false;
    pid_t child = 0;
    env::NativeProcess observed;
    ~Subreaper()
    {
      if (child > 0) {
        if (observed.status().state == env::ProcessState::Running) {
          observed.sendSignal(SIGKILL);
        }
        QElapsedTimer timer;
        timer.start();
        while (::waitpid(child, nullptr, WNOHANG) == 0 && timer.elapsed() < 2000) {
          QThread::msleep(5);
        }
      }
      if (active) ::prctl(PR_SET_CHILD_SUBREAPER, previous);
    }
  } subreaper;
  if (::prctl(PR_GET_CHILD_SUBREAPER, &subreaper.previous) != 0 ||
      ::prctl(PR_SET_CHILD_SUBREAPER, 1) != 0) {
    GTEST_SKIP() << "kernel does not support subreaper fixture";
  }
  subreaper.active = true;

  QTemporaryDir fixture;
  ASSERT_TRUE(fixture.isValid());
  const QString scriptPath = fixture.filePath("fluorine-lifetime-child.sh");
  const QString gatePath = fixture.filePath("continue.pipe");
  const QString gamePath = fixture.filePath("DelayedGame.exe");
  const QString finishedPath = fixture.filePath("finished");
  const QString pidPath = fixture.filePath("child.pid");
  QFile script(scriptPath);
  ASSERT_TRUE(script.open(QIODevice::WriteOnly));
  ASSERT_GT(script.write(
                "#!/bin/sh\nread permission < \"$1\"\n"
                "\"$FLUORINE_TEST_GAME\" \"$2\"\n"),
            0);
  script.close();
  ASSERT_TRUE(script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
  QFile game(gamePath);
  ASSERT_TRUE(game.open(QIODevice::WriteOnly));
  ASSERT_GT(game.write("#!/bin/sh\nsleep 0.7\nprintf done > \"$1\"\n"), 0);
  game.close();
  ASSERT_TRUE(game.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
  ASSERT_EQ(::mkfifo(QFile::encodeName(gatePath).constData(), 0600), 0);

  auto root = ProtonLauncher().setBinary("/bin/sh").setArguments({
      "-c", "\"$1\" \"$2\" \"$3\" & printf '%s' \"$!\" > \"$4\"; exit 0",
      "launcher", scriptPath, gatePath, finishedPath, pidPath})
      .addEnvVar("FLUORINE_TEST_GAME", gamePath).launch();
  ASSERT_TRUE(root);
  RunningProcessCleanup cleanup{root};
  ASSERT_TRUE(processFinished(root));
  QFile pidFile(pidPath);
  ASSERT_TRUE(pidFile.open(QIODevice::ReadOnly));
  subreaper.child = pidFile.readAll().toInt();
  ASSERT_GT(subreaper.child, 0);
  subreaper.observed = env::NativeProcess::observe(subreaper.child);
  ASSERT_TRUE(subreaper.observed);
  ASSERT_EQ(subreaper.observed.status().state, env::ProcessState::Running);

  PluginProcessRegistry registry;
  const QString expectedGame = QFileInfo(gamePath).fileName().toLower();
  const auto token = registry.insert(root, {expectedGame});
  ASSERT_NE(token, 0u);
  auto pluginProcess = registry.take(token);
  ASSERT_TRUE(pluginProcess);
  ASSERT_EQ(pluginProcess->expectedExecutables, QStringList{expectedGame});

  bool followedGame = false;
  bool released = false;
  QElapsedTimer deadline;
  deadline.start();
  const auto completion = env::waitForProcessTree(pluginProcess->native,
      pluginProcess->expectedExecutables, false,
      [&](pid_t, const QString& name) {
        followedGame = followedGame ||
            name.compare(QFileInfo(gamePath).fileName(), Qt::CaseInsensitive) == 0;
        if (!released && deadline.elapsed() >= 5200) {
          const int gate = ::open(QFile::encodeName(gatePath).constData(),
                                  O_WRONLY | O_NONBLOCK | O_CLOEXEC);
          if (gate >= 0) {
            released = ::write(gate, "continue\n", 9) == 9;
            ::close(gate);
          }
        }
        return deadline.elapsed() > 10000 ? env::ProcessWaitAction::Abort
                                        : env::ProcessWaitAction::Continue;
      });
  EXPECT_TRUE(followedGame);
  EXPECT_TRUE(released);
  EXPECT_EQ(completion.result, env::ProcessWaitResult::Completed);
  EXPECT_TRUE(QFileInfo::exists(finishedPath));
}

TEST(ProtonLauncherSteamDrm, DefaultsOnlyForSteamInstalls)
{
  QTemporaryDir plain;
  ASSERT_TRUE(plain.isValid());

  EXPECT_TRUE(ProtonLauncher::defaultSteamDrmForGame(QString()));
  EXPECT_FALSE(ProtonLauncher::defaultSteamDrmForGame(plain.path()));
  EXPECT_FALSE(ProtonLauncher::defaultSteamDrmForGame(plain.path(), "GOG"));
  EXPECT_TRUE(ProtonLauncher::defaultSteamDrmForGame(
      "/home/u/.local/share/Steam/steamapps/common/Skyrim Special Edition"));

  // A Steam edition instance keeps the bridge even when its files were copied
  // out of the Steam library (Stock Game, Game Root).
  EXPECT_TRUE(ProtonLauncher::defaultSteamDrmForGame(plain.path(), "Steam"));
  EXPECT_TRUE(ProtonLauncher::defaultSteamDrmForGame(plain.path(), "steam"));

  QFile appId(QDir(plain.path()).filePath("steam_appid.txt"));
  ASSERT_TRUE(appId.open(QIODevice::WriteOnly));
  appId.close();
  EXPECT_TRUE(ProtonLauncher::defaultSteamDrmForGame(plain.path()));
}

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  MOBase::log::LoggerConfiguration configuration;
  configuration.name = "test_launch_environment";
  MOBase::log::createDefault(configuration);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
