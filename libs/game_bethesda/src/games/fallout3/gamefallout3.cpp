#include "gamefallout3.h"

#include "fallout3bsainvalidation.h"
#include "fallout3dataarchives.h"
#include "fallout3moddatachecker.h"
#include "fallout3moddatacontent.h"
#include "fallout3savegame.h"
#include "fallout3scriptextender.h"

#include "executableinfo.h"
#include "pluginsetting.h"
#include "versioninfo.h"
#include <gamebryogameplugins.h>
#include <gamebryolocalsavegames.h>
#include <gamebryosavegameinfo.h>
#include <gamebryounmanagedmods.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

using namespace MOBase;

GameFallout3::GameFallout3() {}

bool GameFallout3::init(IOrganizer* moInfo)
{
  if (!GameGamebryo::init(moInfo)) {
    return false;
  }

  auto dataArchives = std::make_shared<Fallout3DataArchives>(this);
  registerFeature(std::make_shared<Fallout3ScriptExtender>(this));
  registerFeature(dataArchives);
  registerFeature(std::make_shared<Fallout3BSAInvalidation>(dataArchives.get(), this));
  registerFeature(std::make_shared<GamebryoSaveGameInfo>(this));
  registerFeature(std::make_shared<GamebryoLocalSavegames>(this, "fallout.ini"));
  registerFeature(std::make_shared<Fallout3ModDataChecker>(this));
  registerFeature(
      std::make_shared<Fallout3ModDataContent>(m_Organizer->gameFeatures()));
  registerFeature(std::make_shared<GamebryoGamePlugins>(moInfo));
  registerFeature(std::make_shared<GamebryoUnmangedMods>(this));
  return true;
}

QString GameFallout3::identifyVariant() const
{
  if (QFile::exists(m_GamePath + "/Fallout3ng.exe")) {
    return "Low Violence";
  } else if (QFile::exists(m_GamePath + "/Galaxy.dll")) {
    return "GOG";
  } else if (QFile::exists(m_GamePath + "/FalloutLauncherEpic.exe")) {
    return "Epic Games";
  } else if (m_GamePath.endsWith("Fallout 3 goty")) {
    return "Steam (Game of the Year)";
  } else {
    return "Steam (Regular)";
  }
}

QString GameFallout3::identifyGamePath() const
{
  // Steam (Regular)
  auto result = parseSteamLocation("22300", "Fallout 3");

  // Steam (Game of the Year)
  if (result.isEmpty()) {
    result = parseSteamLocation("22370", "Fallout 3 goty");
  }

  // Epic Games
  if (result.isEmpty()) {
    // Fallout 3: Game of the Year Edition: adeae8bbfc94427db57c7dfecce3f1d4
    result = parseEpicGamesLocation({"adeae8bbfc94427db57c7dfecce3f1d4"});
    if (QFileInfo(result).isDir()) {
      QDir startPath = QDir(result);
      auto subDirs =
          startPath.entryList({"Fallout 3 GOTY*"}, QDir::Dirs | QDir::NoDotAndDotDot);
      if (!subDirs.isEmpty())
        result = startPath.absoluteFilePath(subDirs.first());
    }
  }

  // GOG (and Steam)
  if (result.isEmpty()) {
    result = GameGamebryo::identifyGamePath();
  }

  return result;
}

QString GameFallout3::gameName() const
{
  return "Fallout 3";
}

void GameFallout3::detectGame()
{
  m_GamePath = identifyGamePath();
  setGameVariant(identifyVariant());
  m_MyGamesPath = determineMyGamesPath("Fallout3");
}

QList<ExecutableInfo> GameFallout3::executables() const
{
  return QList<ExecutableInfo>()
         << ExecutableInfo("FOSE",
                           findInGameFolder(m_Organizer->gameFeatures()
                                                ->gameFeature<MOBase::ScriptExtender>()
                                                ->loaderName()))
         << ExecutableInfo("Fallout 3", findInGameFolder(binaryName()))
         << ExecutableInfo("Fallout Mod Manager", findInGameFolder("fomm/fomm.exe"))
         << ExecutableInfo("Construction Kit", findInGameFolder("geck.exe"))
         << ExecutableInfo("Fallout Launcher", findInGameFolder(getLauncherName()))
         << ExecutableInfo("BOSS", findInGameFolder("BOSS/BOSS.exe"))
         << ExecutableInfo("LOOT", QFileInfo(getLootPath()))
                .withArgument("--game=\"Fallout3\"");
}

QList<ExecutableForcedLoadSetting> GameFallout3::executableForcedLoads() const
{
  return QList<ExecutableForcedLoadSetting>();
}

QString GameFallout3::name() const
{
  return "Fallout 3 Support Plugin";
}

QString GameFallout3::localizedName() const
{
  return tr("Fallout 3 Support Plugin");
}

QString GameFallout3::author() const
{
  return "Tannin";
}

QString GameFallout3::description() const
{
  return tr("Adds support for the game Fallout 3.");
}

MOBase::VersionInfo GameFallout3::version() const
{
  return VersionInfo(1, 4, 1, VersionInfo::RELEASE_FINAL);
}

QList<PluginSetting> GameFallout3::settings() const
{
  return QList<PluginSetting>();
}

void GameFallout3::initializeProfile(const QDir& path, ProfileSettings settings) const
{
  if (settings.testFlag(IPluginGame::MODS)) {
    copyToProfile(localAppFolder() + "/Fallout3", path, "plugins.txt");
    copyToProfile(localAppFolder() + "/Fallout3", path, "loadorder.txt");
  }

  if (settings.testFlag(IPluginGame::CONFIGURATION)) {
    if (settings.testFlag(IPluginGame::PREFER_DEFAULTS) ||
        !QFileInfo(myGamesPath(), "fallout.ini").exists()) {
      copyToProfile(gameDirectory().absolutePath(), path, "fallout_default.ini",
                    "fallout.ini");
    } else {
      copyToProfile(myGamesPath(), path, "fallout.ini");
    }

    copyToProfile(myGamesPath(), path, "falloutprefs.ini");
    copyToProfile(myGamesPath(), path, "FalloutCustom.ini");
    copyToProfile(myGamesPath(), path, "custom.ini");
    copyToProfile(myGamesPath(), path, "GECKCustom.ini");
    copyToProfile(myGamesPath(), path, "GECKPrefs.ini");
  }
}

QString GameFallout3::savegameExtension() const
{
  return "fos";
}

QString GameFallout3::savegameSEExtension() const
{
  return "";
}

std::shared_ptr<const GamebryoSaveGame>
GameFallout3::makeSaveGame(QString filePath) const
{
  return std::make_shared<const Fallout3SaveGame>(filePath, this);
}

QString GameFallout3::steamAPPId() const
{
  if (selectedVariant() == "Steam (Game Of The Year)") {
    return "22370";
  } else if (selectedVariant() == "Steam (Regular)") {
    return "22300";
  } else {
    return "";
  }
}

QStringList GameFallout3::primaryPlugins() const
{
  return {"fallout3.esm"};
}

QStringList GameFallout3::gameVariants() const
{
  return {"Steam (Regular)", "Steam (Game Of The Year)", "Epic Games", "GOG",
          "Low Violence"};
}

QString GameFallout3::binaryName() const
{
  if (selectedVariant() == "Low Violence") {
    return "Fallout3ng.exe";
  } else {
    return GameGamebryo::binaryName();
  }
}

QString GameFallout3::gameShortName() const
{
  return "Fallout3";
}

QStringList GameFallout3::validShortNames() const
{
  return {"FalloutNV"};
}

QString GameFallout3::gameNexusName() const
{
  return "fallout3";
}

QStringList GameFallout3::iniFiles() const
{
  return {"fallout.ini", "falloutprefs.ini", "FalloutCustom.ini", "GECKCustom.ini",
          "GECKPrefs.ini"};
}

QStringList GameFallout3::DLCPlugins() const
{
  return {"ThePitt.esm", "Anchorage.esm", "BrokenSteel.esm", "PointLookout.esm",
          "Zeta.esm"};
}

int GameFallout3::nexusModOrganizerID() const
{
  return 16348;
}

int GameFallout3::nexusGameID() const
{
  return 120;
}

QString GameFallout3::getLauncherName() const
{
  const QMap<QString, QString> names = {
      {"Steam (Regular)", "Fallout3Launcher.exe"},
      {"Steam (Game of the Year)", "Fallout3Launcher.exe"},
      {"Epic Games", "FalloutLauncherEpic.exe"},
      {"GOG", "FalloutLauncher.exe"},
      {"Low Violence", "Fallout3Launcher.exe"}};

  return names.value(selectedVariant());
}
