#include "curatedguideinstallstate.h"
#include "curatedgamemanifest.h"
#include "curatedguiderecipe.h"
#include "curatedfomod.h"
#include "curatedinstancebootstrap.h"
#include "curatedmodlayout.h"

#include <QFile>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QTemporaryDir>
#include <algorithm>
#include <gtest/gtest.h>

namespace
{
void appendLittle32(QByteArray& data, quint32 value)
{
  for (int shift = 0; shift < 32; shift += 8)
    data.append(static_cast<char>((value >> shift) & 0xff));
}

void appendVarint(QByteArray& data, quint64 value)
{
  while (value >= 0x80) {
    data.append(static_cast<char>((value & 0x7f) | 0x80));
    value >>= 7;
  }
  data.append(static_cast<char>(value));
}

void appendProtoBytes(QByteArray& data, int field, const QByteArray& value)
{
  appendVarint(data, quint64(field << 3) | 2);
  appendVarint(data, value.size());
  data += value;
}

QByteArray steamFileMapping(const QString& path, const QByteArray& contents,
                            quint64 flags = 0)
{
  QByteArray mapping;
  appendProtoBytes(mapping, 1, path.toUtf8());
  appendVarint(mapping, 2 << 3);
  appendVarint(mapping, contents.size());
  appendVarint(mapping, 3 << 3);
  appendVarint(mapping, flags);
  appendProtoBytes(mapping, 5,
                   QCryptographicHash::hash(contents, QCryptographicHash::Sha1));
  return mapping;
}

QByteArray steamManifest(const QVector<QPair<QString, QByteArray>>& files)
{
  QByteArray payload;
  appendProtoBytes(payload, 1, steamFileMapping("Data", {}, 64));
  for (const auto& file : files)
    appendProtoBytes(payload, 1, steamFileMapping(file.first, file.second));
  QByteArray data;
  appendLittle32(data, 0x71f617d0);
  appendLittle32(data, payload.size());
  data += payload;
  return data;
}

QString epicBlob(const QByteArray& bytes)
{
  QString result;
  for (const unsigned char byte : bytes)
    result += QString("%1").arg(byte, 3, 10, QLatin1Char('0'));
  return result;
}

QString epicNumber(quint64 value)
{
  QByteArray bytes;
  do {
    bytes.append(static_cast<char>(value & 0xff));
    value >>= 8;
  } while (value);
  return epicBlob(bytes);
}

QByteArray validRecipe()
{
  return R"json({
    "schemaVersion": 1,
    "id": "fixture",
    "displayName": "Fixture",
    "version": "1.0.0",
    "sourceCommit": "0123456789abcdef",
    "pages": [{"path":"guide.html","sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}],
    "artifacts": [{"id":"mod","name":"Mod","source":"nexus","domain":"newvegas","modId":1,"fileId":2,"filename":"mod.7z"}],
    "actions": [
      {"id":"get","type":"acquire","artifact":"mod"},
      {"id":"unpack","type":"extract","artifact":"mod","dependsOn":["get"]}
    ]
  })json";
}
}

TEST(CuratedGuideRecipe, ParsesValidatedRecipe)
{
  QString error;
  const auto recipe = CuratedGuideRecipe::fromJson(validRecipe(), &error);
  EXPECT_TRUE(error.isEmpty());
  ASSERT_TRUE(recipe.isValid());
  EXPECT_EQ(recipe.actions.size(), 2);
  EXPECT_NE(recipe.artifact("mod"), nullptr);
  EXPECT_EQ(recipe.digest().size(), 64);
}

TEST(CuratedGuideRecipe, DigestCoversInstallBehavior)
{
  QString error;
  const QByteArray original = validRecipe();
  QByteArray changed = original;
  changed.replace("\"name\":\"Mod\"", "\"name\":\"Changed Mod\"");
  const auto first = CuratedGuideRecipe::fromJson(original, &error);
  ASSERT_TRUE(error.isEmpty());
  const auto second = CuratedGuideRecipe::fromJson(changed, &error);
  ASSERT_TRUE(error.isEmpty());
  EXPECT_NE(first.digest(), second.digest());
  EXPECT_TRUE(first.matchesDigest(first.legacyDigest()));
}

TEST(CuratedGuideRecipe, BundledCatalogLoadsReviewedLocks)
{
  QStringList errors;
  const auto recipes = CuratedGuideCatalog::bundled(&errors);
  EXPECT_TRUE(errors.isEmpty()) << errors.join("\n").toStdString();
  ASSERT_EQ(recipes.size(), 3);
  const auto recipe = std::find_if(recipes.begin(), recipes.end(), [](const auto& item) {
    return item.id == "vnv-extended";
  });
  ASSERT_NE(recipe, recipes.end());
  const auto* fasterMenu = recipe->artifact("newvegas-67811-faster-main-menu");
  ASSERT_NE(fasterMenu, nullptr);
  EXPECT_EQ(fasterMenu->fileId, 1000115498);
}

TEST(CuratedInstanceBootstrap, RebasesOnlyOriginalGameExecutablePaths)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString iniPath = temporary.filePath("ModOrganizer.ini");
  const QString source = "/home/player/.local/share/Steam/steamapps/common/Fallout New Vegas";
  const QString stock = "/home/player/Games/Viva New Vegas/Stock Game";

  {
    QSettings ini(iniPath, QSettings::IniFormat);
    ini.beginWriteArray("customExecutables", 2);
    ini.setArrayIndex(0);
    ini.setValue("title", "NVSE");
    ini.setValue("binary", R"(Z:\home\player\.local\share\Steam\steamapps\common\Fallout New Vegas\nvse_loader.exe)");
    ini.setValue("workingDirectory", R"(Z:\home\player\.local\share\Steam\steamapps\common\Fallout New Vegas)");
    ini.setArrayIndex(1);
    ini.setValue("title", "Explore Virtual Folder");
    ini.setValue("binary", R"(Z:\home\player\Games\Viva New Vegas\explorer++\Explorer++.exe)");
    ini.setValue("arguments", R"("Z:\home\player\.local\share\Steam\steamapps\common\Fallout New Vegas\data")");
    ini.endArray();

    EXPECT_EQ(rebaseCustomExecutableGamePaths(ini, source, stock), 3);
    ini.sync();
    ASSERT_EQ(ini.status(), QSettings::NoError);
  }

  QSettings ini(iniPath, QSettings::IniFormat);
  ASSERT_EQ(ini.beginReadArray("customExecutables"), 2);
  ini.setArrayIndex(0);
  EXPECT_EQ(ini.value("binary").toString(),
            R"(Z:\home\player\Games\Viva New Vegas\Stock Game\nvse_loader.exe)");
  EXPECT_EQ(ini.value("workingDirectory").toString(),
            R"(Z:\home\player\Games\Viva New Vegas\Stock Game)");
  ini.setArrayIndex(1);
  EXPECT_EQ(ini.value("binary").toString(),
            R"(Z:\home\player\Games\Viva New Vegas\explorer++\Explorer++.exe)");
  EXPECT_EQ(ini.value("arguments").toString(),
            R"("Z:\home\player\Games\Viva New Vegas\Stock Game\data")");
  ini.endArray();
}

TEST(CuratedGuideRecipe, BundledRecipesParseAndContainRequiredAutomation)
{
  for (const QString name : {"vnv-base.json", "vnv-extended.json",
                             "tbot-essentials.json"}) {
    QFile file(QString(FLUORINE_TEST_SOURCE_DIR)
               + "/src/src/resources/curated-guides/" + name);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly)) << name.toStdString();
    QString error;
    const auto recipe = CuratedGuideRecipe::fromJson(file.readAll(), &error);
    ASSERT_TRUE(recipe.isValid()) << error.toStdString();
    EXPECT_EQ(recipe.gamePlugin, name.startsWith("vnv-") ? "New Vegas" : "TTW");
    QStringList separators;
    for (const auto& value : recipe.profile.value("modlist").toArray()) {
      const QString line = value.toString();
      EXPECT_NE(line.compare("*Overwrite", Qt::CaseInsensitive), 0);
      if (line.endsWith("_separator")) separators.push_back(line);
    }
    const QStringList expectedSeparators =
        name == "vnv-extended.json"
            ? QStringList({"+Visuals_separator", "+Content_separator",
                           "+Gameplay_separator", "+User Interface_separator",
                           "+Bug Fixes_separator", "+Utilities_separator"})
            : (name == "vnv-base.json"
                   ? QStringList({"+Bug Fixes_separator", "+Utilities_separator"})
                   : QStringList({"+Utilities_separator"}));
    EXPECT_EQ(separators, expectedSeparators);
    EXPECT_FALSE(recipe.pages.isEmpty());
    EXPECT_FALSE(recipe.updatedAt.isEmpty());
    EXPECT_TRUE(recipe.artworkUrl.startsWith("https://raw.githubusercontent.com/"));
    EXPECT_EQ(recipe.artworkSha256.size(), 64);
    EXPECT_GT(recipe.estimatedDownloadSize, 0);
    EXPECT_GT(recipe.estimatedInstallSize, recipe.estimatedDownloadSize);
    EXPECT_GT(recipe.actions.size(), 20);
    bool hasPatcher = false;
    bool hasProfile = false;
    for (const auto& action : recipe.actions) {
      hasPatcher = hasPatcher || action.type == "run_proton";
      hasProfile = hasProfile || action.type == "write_profile";
    }
    EXPECT_TRUE(hasPatcher);
    EXPECT_TRUE(hasProfile);
    if (name == "vnv-extended.json") {
      const auto profiles = recipe.profile.value("profiles").toArray();
      ASSERT_EQ(profiles.size(), 2);
      EXPECT_EQ(profiles.at(0).toObject().value("name").toString(),
                "Viva New Vegas - Base");
      EXPECT_EQ(profiles.at(1).toObject().value("name").toString(),
                "Viva New Vegas - Extended");
      QStringList baseModlist;
      for (const auto& value : profiles.at(0).toObject().value("modlist").toArray())
        baseModlist.push_back(value.toString());
      QStringList extendedModlist;
      for (const auto& value : profiles.at(1).toObject().value("modlist").toArray())
        extendedModlist.push_back(value.toString());
      EXPECT_TRUE(baseModlist.contains("+Stewie Tweaks - VNV INI"));
      EXPECT_TRUE(extendedModlist.contains("-Stewie Tweaks - VNV INI"));
      const auto* faster = recipe.artifact("newvegas-67811-faster-main-menu");
      ASSERT_NE(faster, nullptr);
      EXPECT_EQ(faster->fileId, 0);
    }
  }
}

TEST(CuratedGuideRecipe, RejectsReservedOverwriteProfileEntry)
{
  const QByteArray json =
      "{\"schemaVersion\":1,\"id\":\"fixture\",\"displayName\":\"Fixture\","
      "\"version\":\"1\",\"profile\":{\"modlist\":[\"*Overwrite\"]},"
      "\"actions\":[{\"id\":\"done\",\"type\":\"write_profile\","
      "\"name\":\"Done\"}]}";
  QString error;
  EXPECT_FALSE(CuratedGuideRecipe::fromJson(json, &error).isValid());
  EXPECT_TRUE(error.contains("profile", Qt::CaseInsensitive));
}

TEST(CuratedGuideRecipe, RejectsUnknownActionsAndCycles)
{
  QByteArray unknown = validRecipe();
  unknown.replace("\"extract\"", "\"shell\"");
  QString error;
  EXPECT_FALSE(CuratedGuideRecipe::fromJson(unknown, &error).isValid());
  EXPECT_TRUE(error.contains("Invalid"));

  QByteArray cycle = validRecipe();
  cycle.replace("\"artifact\":\"mod\"}",
                "\"artifact\":\"mod\",\"dependsOn\":[\"unpack\"]}");
  EXPECT_FALSE(CuratedGuideRecipe::fromJson(cycle, &error).isValid());
  EXPECT_TRUE(error.contains("cycle"));
}

TEST(CuratedGuideRecipe, RejectsTraversal)
{
  QByteArray traversal = validRecipe();
  traversal.replace("guide.html", "../guide.html");
  QString error;
  EXPECT_FALSE(CuratedGuideRecipe::fromJson(traversal, &error).isValid());

  QByteArray actionTraversal = validRecipe();
  actionTraversal.replace("\"type\":\"extract\"",
                          "\"type\":\"install_mod\",\"parameters\":{\"folder\":\"../escape\"}");
  EXPECT_FALSE(CuratedGuideRecipe::fromJson(actionTraversal, &error).isValid());
  EXPECT_TRUE(error.contains("unsafe"));
}

TEST(CuratedGuideRecipe, RequiresCategoryForRuntimeNexusLabelResolution)
{
  QByteArray unresolved = validRecipe();
  unresolved.replace("\"fileId\":2",
                     "\"fileId\":0,\"fileLabel\":\"Current File\"");
  QString error;
  EXPECT_FALSE(CuratedGuideRecipe::fromJson(unresolved, &error).isValid());
  EXPECT_TRUE(error.contains("category"));

  unresolved.replace("\"fileLabel\":\"Current File\"",
                     "\"fileLabel\":\"Current File\",\"fileCategory\":\"main files\"");
  EXPECT_TRUE(CuratedGuideRecipe::fromJson(unresolved, &error).isValid())
      << error.toStdString();
}

TEST(CuratedGuideRecipe, RejectsScrapedSeparatorPlaceholder)
{
  QByteArray recipe = validRecipe();
  recipe.replace("\"actions\": [",
                 "\"profile\":{\"modlist\":[\"+Create separator_separator\"]},"
                 "\"actions\": [");
  QString error;
  EXPECT_FALSE(CuratedGuideRecipe::fromJson(recipe, &error).isValid());
  EXPECT_TRUE(error.contains("separator"));
}

TEST(CuratedGuideState, RoundTripsAtomicallyAndHashesTrees)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString dataDir = temp.filePath("tree");
  ASSERT_TRUE(QDir().mkpath(dataDir));
  QFile data(QDir(dataDir).filePath("file.txt"));
  ASSERT_TRUE(data.open(QIODevice::WriteOnly));
  ASSERT_EQ(data.write("contents"), 8);
  data.close();

  CuratedGuideInstallState state;
  state.jobId = "job";
  state.recipeId = "fixture";
  state.recipeVersion = "1";
  state.recipeDigest = QString(64, 'a');
  state.instanceName = "Fixture";
  state.instancePath = temp.filePath("instance");
  state.downloadsPath = temp.filePath("downloads");
  state.actions.push_back({"get", CuratedActionStatus::Complete, {},
                           curatedBlake3Tree(dataDir), {}, {},
                           QJsonObject{{"store", "Steam"}, {"buildId", "123"}}});
  ASSERT_EQ(state.actions.front().outputDigest.size(), 64);
  const QString statePath = temp.filePath("state.json");
  QString error;
  ASSERT_TRUE(state.save(statePath, &error)) << error.toStdString();
  const auto loaded = CuratedGuideInstallState::load(statePath, &error);
  EXPECT_TRUE(error.isEmpty());
  EXPECT_EQ(loaded.jobId, "job");
  ASSERT_NE(loaded.action("get"), nullptr);
  EXPECT_EQ(loaded.action("get")->status, CuratedActionStatus::Complete);
  EXPECT_EQ(loaded.action("get")->outputDigest, state.actions.front().outputDigest);
  EXPECT_EQ(loaded.action("get")->provenance.value("buildId").toString(), "123");
}

TEST(CuratedGameManifest, ParsesSteamAndCopiesOnlyVerifiedFiles)
{
  const QByteArray executableContents("original executable");
  const QByteArray assetContents("original asset");
  const auto parsed = parseSteamDepotManifestData(
      steamManifest({{"FalloutNV.exe", executableContents},
                     {"Data/FalloutNV.esm", assetContents}}), "22381");
  ASSERT_TRUE(parsed.success) << parsed.error.toStdString();
  ASSERT_EQ(parsed.manifest.files.size(), 2);

  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString source = temp.filePath("source");
  const QString output = temp.filePath("stock.partial");
  ASSERT_TRUE(QDir().mkpath(source + "/Data"));
  for (const auto& item : QVector<QPair<QString, QByteArray>>{
           {"FalloutNV.exe", executableContents},
           {"Data/FalloutNV.esm", assetContents},
           {"nvse_loader.exe", "user mod"}}) {
    QFile file(QDir(source).filePath(item.first));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write(item.second), item.second.size());
  }

  const auto copied = copyCuratedGameFromManifest(source, output, parsed.manifest);
  ASSERT_TRUE(copied.success) << copied.error.toStdString();
  EXPECT_TRUE(QFileInfo(output + "/FalloutNV.exe").isFile());
  EXPECT_TRUE(QFileInfo(output + "/Data/FalloutNV.esm").isFile());
  EXPECT_FALSE(QFileInfo::exists(output + "/nvse_loader.exe"));
  EXPECT_TRUE(copied.unexpectedFiles.contains("nvse_loader.exe"));
  EXPECT_EQ(copied.provenance.value("store").toString(), "Steam");
}

TEST(CuratedGameManifest, ReportsModifiedOriginalWithRepairInstruction)
{
  const auto parsed = parseSteamDepotManifestData(
      steamManifest({{"FalloutNV.exe", "original"}}), "22381");
  ASSERT_TRUE(parsed.success);
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  QFile modified(temp.filePath("FalloutNV.exe"));
  ASSERT_TRUE(modified.open(QIODevice::WriteOnly));
  modified.write("tampered");
  modified.close();

  const auto copied = copyCuratedGameFromManifest(
      temp.path(), temp.filePath("stock"), parsed.manifest);
  EXPECT_FALSE(copied.success);
  EXPECT_TRUE(copied.error.contains("FalloutNV.exe"));
  EXPECT_TRUE(copied.error.contains("Verify integrity"));
}

TEST(CuratedGameManifest, IgnoresNewVegasStoreAndPatcherManagedFiles)
{
  const auto parsed = parseSteamDepotManifestData(
      steamManifest({{"FalloutNV.exe", "original executable"},
                     {"InstallScript.vdf", "original script"},
                     {"libvorbis.dll", "original vorbis"},
                     {"libvorbisfile.dll", "original vorbisfile"}}),
      "22381");
  ASSERT_TRUE(parsed.success) << parsed.error.toStdString();

  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString source = temp.filePath("source");
  ASSERT_TRUE(QDir().mkpath(source));
  for (const auto& item : QVector<QPair<QString, QByteArray>>{
           {"FalloutNV.exe", "original executable"},
           {"InstallScript.vdf", "locally modified script"},
           {"libvorbis.dll", "guide-installed vorbis"},
           {"libvorbisfile.dll", "guide-installed vorbisfile"}}) {
    QFile file(QDir(source).filePath(item.first));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write(item.second), item.second.size());
  }

  const QString output = temp.filePath("stock");
  const auto copied = copyCuratedGameFromManifest(
      source, output, parsed.manifest);
  ASSERT_TRUE(copied.success) << copied.error.toStdString();
  EXPECT_TRUE(QFileInfo(output + "/FalloutNV.exe").isFile());
  EXPECT_FALSE(QFileInfo::exists(output + "/InstallScript.vdf"));
  EXPECT_FALSE(QFileInfo::exists(output + "/libvorbis.dll"));
  EXPECT_FALSE(QFileInfo::exists(output + "/libvorbisfile.dll"));
  EXPECT_TRUE(copied.unexpectedFiles.isEmpty());
}

TEST(CuratedGameManifest, PreservesNewVegasRuntimeForWabbajackStockGame)
{
  const auto parsed = parseSteamDepotManifestData(
      steamManifest({{"FalloutNV.exe", "original executable"},
                     {"InstallScript.vdf", "original script"},
                     {"libvorbis.dll", "original vorbis"},
                     {"libvorbisfile.dll", "original vorbisfile"}}),
      "22381");
  ASSERT_TRUE(parsed.success) << parsed.error.toStdString();

  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString source = temp.filePath("source");
  ASSERT_TRUE(QDir().mkpath(source));
  for (const auto& item : QVector<QPair<QString, QByteArray>>{
           {"FalloutNV.exe", "original executable"},
           {"InstallScript.vdf", "original script"},
           {"libvorbis.dll", "original vorbis"},
           {"libvorbisfile.dll", "original vorbisfile"}}) {
    QFile file(QDir(source).filePath(item.first));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write(item.second), item.second.size());
  }

  const QString output = temp.filePath("stock");
  const auto copied = copyCuratedGameFromManifest(
      source, output, parsed.manifest, false);
  ASSERT_TRUE(copied.success) << copied.error.toStdString();
  EXPECT_TRUE(QFileInfo(output + "/FalloutNV.exe").isFile());
  EXPECT_TRUE(QFileInfo(output + "/InstallScript.vdf").isFile());
  EXPECT_TRUE(QFileInfo(output + "/libvorbis.dll").isFile());
  EXPECT_TRUE(QFileInfo(output + "/libvorbisfile.dll").isFile());
}

TEST(CuratedGameManifest, ResolvesInstalledSteamDepotSet)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString steamRoot = temp.filePath("Steam");
  const QString steamapps = steamRoot + "/steamapps";
  const QString game = steamapps + "/common/Fixture Game";
  ASSERT_TRUE(QDir().mkpath(game));
  ASSERT_TRUE(QDir().mkpath(steamRoot + "/depotcache"));

  QFile appManifest(steamapps + "/appmanifest_42.acf");
  ASSERT_TRUE(appManifest.open(QIODevice::WriteOnly));
  appManifest.write(R"vdf("AppState"
{
  "appid" "42"
  "name" "Fixture Game"
  "StateFlags" "4"
  "installdir" "Fixture Game"
  "buildid" "9001"
  "InstalledDepots"
  {
    "43"
    {
      "manifest" "123456"
      "size" "8"
    }
  }
  "UserConfig" { "language" "english" }
})vdf");
  appManifest.close();
  QFile depot(steamRoot + "/depotcache/43_123456.manifest");
  ASSERT_TRUE(depot.open(QIODevice::WriteOnly));
  depot.write(steamManifest({{"game.exe", "original"}}));
  depot.close();

  const auto resolved = resolveCuratedGameManifest(game, "Steam");
  ASSERT_TRUE(resolved.success) << resolved.error.toStdString();
  EXPECT_EQ(resolved.manifest.gameId, "42");
  EXPECT_EQ(resolved.manifest.buildId, "9001");
  EXPECT_TRUE(resolved.manifest.manifestIds.contains("43:123456"));
  ASSERT_EQ(resolved.manifest.files.size(), 1);
}

TEST(CuratedGameManifest, ParsesEpicJsonFileHashes)
{
  const QByteArray contents("epic original");
  const QByteArray sha1 = QCryptographicHash::hash(contents, QCryptographicHash::Sha1);
  const QJsonObject part{{"Size", epicNumber(contents.size())}};
  const QJsonObject file{{"Filename", "Data/FalloutNV.esm"},
                         {"FileHash", epicBlob(sha1)},
                         {"FileChunkParts", QJsonArray{part}},
                         {"bIsUnixExecutable", false}};
  const QByteArray json = QJsonDocument(
      QJsonObject{{"BuildVersionString", "fixture-build"},
                  {"FileManifestList", QJsonArray{file}}}).toJson();
  const auto parsed = parseEpicBuildPatchManifestData(json, "FNV", "old");
  ASSERT_TRUE(parsed.success) << parsed.error.toStdString();
  ASSERT_EQ(parsed.manifest.files.size(), 1);
  EXPECT_EQ(parsed.manifest.buildId, "fixture-build");
  EXPECT_EQ(parsed.manifest.files.front().variants.front().size, contents.size());
  EXPECT_EQ(parsed.manifest.files.front().variants.front().digest, sha1);
}

TEST(CuratedGuideInstance, BootstrapsGameAndInstancePaths)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString instance = temp.filePath("Viva New Vegas - Base");
  const QString game = QDir(instance).filePath("stock/Fallout New Vegas");
  const QString downloads = temp.filePath("downloads");
  ASSERT_TRUE(QDir().mkpath(instance));
  {
    QSettings legacy(QDir(instance).filePath("ModOrganizer.ini"),
                     QSettings::IniFormat);
    legacy.setValue("General/gameName", "Invisible escaped value");
    legacy.sync();
  }

  const auto result = bootstrapCuratedInstance(
      instance, "Fallout New Vegas", game, downloads);
  ASSERT_TRUE(result.first) << result.second.toStdString();

  QSettings ini(QDir(instance).filePath("ModOrganizer.ini"), QSettings::IniFormat);
  EXPECT_EQ(ini.value("gameName").toString(), "New Vegas");
  EXPECT_EQ(ini.value("gamePath").toString(), QDir::cleanPath(game));
  EXPECT_FALSE(ini.childGroups().contains("General"));
  EXPECT_EQ(ini.value("Settings/base_directory").toString(),
            QDir::cleanPath(instance));
  EXPECT_EQ(ini.value("Settings/download_directory").toString(),
            QDir::cleanPath(downloads));
  EXPECT_EQ(ini.value("Settings/mod_directory").toString(),
            QDir(instance).filePath("mods"));
  EXPECT_TRUE(QDir(downloads).exists());
  EXPECT_TRUE(QDir(instance).exists("mods"));
  EXPECT_TRUE(QDir(instance).exists("profiles"));
  EXPECT_TRUE(QDir(instance).exists("overwrite"));
  QFile rawIni(QDir(instance).filePath("ModOrganizer.ini"));
  ASSERT_TRUE(rawIni.open(QIODevice::ReadOnly));
  const QByteArray contents = rawIni.readAll();
  EXPECT_TRUE(contents.contains("[General]"));
  EXPECT_FALSE(contents.contains("[%General]"));
}

TEST(CuratedModLayout, FlattensAndRejectsTopLevelDataDirectory)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString extracted = temp.filePath("extracted");
  const QString data = QDir(extracted).filePath("Data");
  ASSERT_TRUE(QDir().mkpath(data));
  QFile plugin(QDir(data).filePath("Fixture.esm"));
  ASSERT_TRUE(plugin.open(QIODevice::WriteOnly));
  plugin.write("TES4");
  plugin.close();

  EXPECT_EQ(curatedModPayloadRoot(extracted), data);
  QString error;
  EXPECT_FALSE(validateCuratedModLayout(extracted, &error));
  EXPECT_TRUE(error.contains("top-level Data"));
  EXPECT_TRUE(validateCuratedModLayout(data, &error));
  EXPECT_TRUE(error.isEmpty());
}

TEST(CuratedModLayout, UnwrapsArchiveNameAboveGameData)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString wrapper = temp.filePath("ActorCause Save Bloat Fix");
  const QString plugins = QDir(wrapper).filePath("NVSE/plugins");
  ASSERT_TRUE(QDir().mkpath(plugins));
  QFile dll(QDir(plugins).filePath("ActorCauseSaveBloatFix.dll"));
  ASSERT_TRUE(dll.open(QIODevice::WriteOnly));
  dll.write("fixture");
  dll.close();

  EXPECT_EQ(curatedModPayloadRoot(temp.path()), wrapper);
  QString error;
  EXPECT_FALSE(validateCuratedModLayout(temp.path(), &error));
  EXPECT_TRUE(error.contains("wrapper"));
  EXPECT_TRUE(validateCuratedModLayout(wrapper, &error));
}

TEST(CuratedModLayout, PreservesSingleGameDataDirectory)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  ASSERT_TRUE(QDir().mkpath(temp.filePath("meshes/armor")));
  EXPECT_EQ(curatedModPayloadRoot(temp.path()), temp.path());
  QString error;
  EXPECT_TRUE(validateCuratedModLayout(temp.path(), &error));
}

TEST(CuratedModLayout, DetectsNestedFomodArchive)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  QFile fomod(temp.filePath("The Mod Configuration Menu.fomod"));
  ASSERT_TRUE(fomod.open(QIODevice::WriteOnly));
  fomod.write("fixture");
  fomod.close();

  QString error;
  EXPECT_EQ(curatedNestedFomodArchive(temp.path(), &error), fomod.fileName());
  EXPECT_TRUE(error.isEmpty());
  EXPECT_FALSE(validateCuratedModLayout(temp.path(), &error));
  EXPECT_TRUE(error.contains("unexpanded"));
}

TEST(CuratedModLayout, UnwrapsSingleRootToolDirectory)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString versioned = temp.filePath("nvse_6_4_8");
  ASSERT_TRUE(QDir().mkpath(versioned));
  QFile loader(QDir(versioned).filePath("nvse_loader.exe"));
  ASSERT_TRUE(loader.open(QIODevice::WriteOnly));
  loader.write("fixture");
  loader.close();

  EXPECT_EQ(curatedRootPayloadRoot(temp.path()), versioned);
  EXPECT_EQ(curatedRootPayloadRoot(versioned), versioned);
}

TEST(CuratedGuideFomod, AppliesReviewedChoicesAndConditionalFiles)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString source = temp.filePath("source");
  const QString output = temp.filePath("output");
  ASSERT_TRUE(QDir().mkpath(source + "/fomod"));
  ASSERT_TRUE(QDir().mkpath(source + "/required"));
  ASSERT_TRUE(QDir().mkpath(source + "/choice/sub"));
  ASSERT_TRUE(QDir().mkpath(source + "/conditional/sub"));
  for (const QString path : {"required/base.txt", "choice/sub/chosen.txt",
                             "conditional/sub/flagged.txt"}) {
    QFile file(QDir(source).filePath(path));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(path.toUtf8());
  }
  QFile config(source + "/fomod/ModuleConfig.xml");
  ASSERT_TRUE(config.open(QIODevice::WriteOnly));
  config.write(R"xml(<config>
    <requiredInstallFiles><folder source="required" destination=""/></requiredInstallFiles>
    <installSteps><installStep name="Options"><optionalFileGroups>
      <group name="Choice" type="SelectExactlyOne"><plugins>
        <plugin name="Default"><typeDescriptor><type name="Optional"/></typeDescriptor></plugin>
        <plugin name="Guide Choice"><conditionFlags><flag name="Guide">On</flag></conditionFlags>
          <files><folder source="choice\sub" destination="data\selected" priority="1"/></files>
          <typeDescriptor><type name="Optional"/></typeDescriptor></plugin>
      </plugins></group>
    </optionalFileGroups></installStep></installSteps>
    <conditionalFileInstalls><patterns><pattern>
      <dependencies operator="And"><flagDependency flag="Guide" value="On"/></dependencies>
      <files><folder source="conditional\sub" destination="data\selected" priority="2"/></files>
    </pattern></patterns></conditionalFileInstalls>
  </config>)xml");
  config.close();

  const auto result = installCuratedFomod(source, output, {"Guide Choice"});
  ASSERT_TRUE(result.first) << result.second.toStdString();
  EXPECT_TRUE(QFileInfo::exists(output + "/base.txt"));
  EXPECT_TRUE(QFileInfo::exists(output + "/data/selected/chosen.txt"));
  EXPECT_TRUE(QFileInfo::exists(output + "/data/selected/flagged.txt"));
}

TEST(CuratedGuideFomod, DiscoversSingleWrappedFomodRoot)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString source = temp.filePath("source");
  const QString wrapped = source + "/Goodies";
  const QString output = temp.filePath("output");
  ASSERT_TRUE(QDir().mkpath(wrapped + "/fomod"));
  ASSERT_TRUE(QDir().mkpath(wrapped + "/payload"));
  QFile payload(wrapped + "/payload/Goodies.esp");
  ASSERT_TRUE(payload.open(QIODevice::WriteOnly));
  payload.write("plugin");
  payload.close();
  QFile config(wrapped + "/fomod/ModuleConfig.xml");
  ASSERT_TRUE(config.open(QIODevice::WriteOnly));
  config.write(R"xml(<config><requiredInstallFiles>
    <folder source="payload" destination=""/>
  </requiredInstallFiles></config>)xml");
  config.close();

  const auto result = installCuratedFomod(source, output, {});
  ASSERT_TRUE(result.first) << result.second.toStdString();
  EXPECT_TRUE(QFileInfo::exists(output + "/Goodies.esp"));
}

TEST(CuratedGuideFomod, RejectsAmbiguousNestedFomodRoots)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString source = temp.filePath("source");
  for (const QString wrapper : {"First", "Second"}) {
    ASSERT_TRUE(QDir().mkpath(source + "/" + wrapper + "/fomod"));
    QFile config(source + "/" + wrapper + "/fomod/ModuleConfig.xml");
    ASSERT_TRUE(config.open(QIODevice::WriteOnly));
    config.write("<config/>");
  }
  const auto result = installCuratedFomod(source, temp.filePath("output"), {});
  EXPECT_FALSE(result.first);
  EXPECT_TRUE(result.second.contains("Found 2"));
}
