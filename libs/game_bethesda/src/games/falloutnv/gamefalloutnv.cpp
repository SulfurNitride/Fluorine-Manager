#include "gamefalloutnv.h"

#include "falloutnvbsainvalidation.h"
#include "falloutnvdataarchives.h"
#include "falloutnvmoddatachecker.h"
#include "falloutnvmoddatacontent.h"
#include "falloutnvsavegame.h"
#include "falloutnvscriptextender.h"

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

GameFalloutNV::GameFalloutNV() {}

bool GameFalloutNV::init(IOrganizer* moInfo)
{
  if (!GameGamebryo::init(moInfo)) {
    return false;
  }

  auto dataArchives = std::make_shared<FalloutNVDataArchives>(this);
  registerFeature(std::make_shared<FalloutNVScriptExtender>(this));
  registerFeature(dataArchives);
  registerFeature(std::make_shared<FalloutNVBSAInvalidation>(dataArchives.get(), this));
  registerFeature(std::make_shared<GamebryoSaveGameInfo>(this));
  registerFeature(std::make_shared<GamebryoLocalSavegames>(this, "fallout.ini"));
  registerFeature(std::make_shared<FalloutNVModDataChecker>(this));
  registerFeature(
      std::make_shared<FalloutNVModDataContent>(m_Organizer->gameFeatures()));
  registerFeature(std::make_shared<GamebryoGamePlugins>(moInfo));
  registerFeature(std::make_shared<GamebryoUnmangedMods>(this));

  return true;
}

void GameFalloutNV::setVariant(QString variant)
{
  m_GameVariant = variant;
}

void GameFalloutNV::checkVariants()
{
  QFileInfo gog_dll(m_GamePath + "\\Galaxy.dll");
  QFileInfo epic_dll(m_GamePath + "\\EOSSDK-Win32-Shipping.dll");
  if (gog_dll.exists())
    setVariant("GOG");
  else if (epic_dll.exists())
    setVariant("Epic Games");
  else
    setVariant("Steam");
}

QDir GameFalloutNV::documentsDirectory() const
{
  return m_MyGamesPath;
}

QString GameFalloutNV::identifyGamePath() const
{
  auto result = GameGamebryo::identifyGamePath();  // Default registry path
  // EPIC Game Store
  if (result.isEmpty()) {
    /**
     * Basegame: 5daeb974a22a435988892319b3a4f476
     * Dead Money: b290229eb58045cbab9501640f3278f3
     * Honest Hearts: 562d4a2c1b3147b089a7c453e3ddbcbe
     * Old World Blues: c8dae1ab0570475a8b38a9041e614840
     * Lonesome Road: 4fa3d8d9b2cb4714a19a38d1a598be8f
     * Gun Runners' Arsenal: 7dcfb9cd9d134728b2646466c34c7b3b
     * Courier's Stash: ee9a44b4530942499ef1c8c390731fce
     */
    result = parseEpicGamesLocation({"5daeb974a22a435988892319b3a4f476"});
    if (QFileInfo(result).isDir()) {
      QDir startPath = QDir(result);
      auto subDirs   = startPath.entryList({"Fallout New Vegas*"},
                                           QDir::Dirs | QDir::NoDotAndDotDot);
      if (!subDirs.isEmpty())
        result = startPath.absoluteFilePath(subDirs.first());
    }
  }
  return result;
}

void GameFalloutNV::setGamePath(const QString& path)
{
  m_GamePath = path;
  checkVariants();
  m_MyGamesPath = determineMyGamesPath(gameDirectoryName());
}

QDir GameFalloutNV::savesDirectory() const
{
  return QDir(m_MyGamesPath + "/Saves");
}

QString GameFalloutNV::myGamesPath() const
{
  return m_MyGamesPath;
}

bool GameFalloutNV::isInstalled() const
{
  return !m_GamePath.isEmpty();
}

QString GameFalloutNV::gameName() const
{
  return "New Vegas";
}

QString GameFalloutNV::gameDirectoryName() const
{
  if (selectedVariant() == "Epic Games")
    return "FalloutNV_Epic";
  else
    return "FalloutNV";
}

void GameFalloutNV::detectGame()
{
  m_GamePath = identifyGamePath();
  checkVariants();
  m_MyGamesPath = determineMyGamesPath(gameDirectoryName());
}

QList<ExecutableInfo> GameFalloutNV::executables() const
{
  ExecutableInfo game("New Vegas", findInGameFolder(binaryName()));
  ExecutableInfo launcher("Fallout Launcher", findInGameFolder(getLauncherName()));
  QList<ExecutableInfo> extraExecutables =
      QList<ExecutableInfo>() << ExecutableInfo("Fallout Mod Manager",
                                                findInGameFolder("fomm/fomm.exe"))
                              << ExecutableInfo("BOSS",
                                                findInGameFolder("BOSS/BOSS.exe"))
                              << ExecutableInfo("GECK", findInGameFolder("geck.exe"))
                              << ExecutableInfo("LOOT", QFileInfo(getLootPath()))
                                     .withArgument("--game=\"FalloutNV\"");
  if (selectedVariant() != "Epic Games") {
    extraExecutables.prepend(ExecutableInfo(
        "NVSE", findInGameFolder(m_Organizer->gameFeatures()
                                     ->gameFeature<MOBase::ScriptExtender>()
                                     ->loaderName())));
  } else {
    game.withArgument("-EpicPortal");
    launcher.withArgument("-EpicPortal");
  }
  QList<ExecutableInfo> executables = {game, launcher};
  executables += extraExecutables;
  return executables;
}

QList<ExecutableForcedLoadSetting> GameFalloutNV::executableForcedLoads() const
{
  return QList<ExecutableForcedLoadSetting>();
}

QString GameFalloutNV::name() const
{
  return "Fallout NV Support Plugin";
}

QString GameFalloutNV::localizedName() const
{
  return tr("Fallout NV Support Plugin");
}

QString GameFalloutNV::author() const
{
  return "Tannin & MO2 Team";
}

QString GameFalloutNV::description() const
{
  return tr("Adds support for the game Fallout New Vegas");
}

MOBase::VersionInfo GameFalloutNV::version() const
{
  return VersionInfo(1, 6, 0, VersionInfo::RELEASE_FINAL);
}

QList<PluginSetting> GameFalloutNV::settings() const
{
  return QList<PluginSetting>()
         << PluginSetting("enable_loot_sorting",
                          tr("While not recommended by the FNV modding community, "
                             "enables LOOT sorting"),
                          false);
}

void GameFalloutNV::initializeProfile(const QDir& path, ProfileSettings settings) const
{
  if (settings.testFlag(IPluginGame::MODS)) {
    copyToProfile(localAppFolder() + "/" + gameDirectoryName(), path, "plugins.txt");
  }

  if (settings.testFlag(IPluginGame::CONFIGURATION)) {
    if (settings.testFlag(IPluginGame::PREFER_DEFAULTS) ||
        !QFileInfo(myGamesPath() + "/fallout.ini").exists()) {
      copyToProfile(gameDirectory().absolutePath(), path, "fallout_default.ini",
                    "fallout.ini");
    } else {
      copyToProfile(myGamesPath(), path, "fallout.ini");
    }

    copyToProfile(myGamesPath(), path, "falloutprefs.ini");
    copyToProfile(myGamesPath(), path, "falloutcustom.ini");
    copyToProfile(myGamesPath(), path, "GECKCustom.ini");
    copyToProfile(myGamesPath(), path, "GECKPrefs.ini");
  }
}

QString GameFalloutNV::savegameExtension() const
{
  return "fos";
}

QString GameFalloutNV::savegameSEExtension() const
{
  return "nvse";
}

std::shared_ptr<const GamebryoSaveGame>
GameFalloutNV::makeSaveGame(QString filePath) const
{
  return std::make_shared<const FalloutNVSaveGame>(filePath, this);
}

QString GameFalloutNV::steamAPPId() const
{
  if (selectedVariant() == "Steam") {
    if (m_GamePath.endsWith("enplczru")) {
      return "22490";
    } else {
      return "22380";
    }
  }
  return QString();
}

QStringList GameFalloutNV::primaryPlugins() const
{
  return {"falloutnv.esm"};
}

QStringList GameFalloutNV::gameVariants() const
{
  return {"Steam", "GOG", "Epic Games"};
}

QString GameFalloutNV::gameShortName() const
{
  return "FalloutNV";
}

QStringList GameFalloutNV::validShortNames() const
{
  return {"Fallout3"};
}

QString GameFalloutNV::gameNexusName() const
{
  return "newvegas";
}

QStringList GameFalloutNV::iniFiles() const
{
  return {"fallout.ini", "falloutprefs.ini", "falloutcustom.ini", "GECKCustom.ini",
          "GECKPrefs.ini"};
}

QStringList GameFalloutNV::DLCPlugins() const
{
  return {"DeadMoney.esm",    "HonestHearts.esm",      "OldWorldBlues.esm",
          "LonesomeRoad.esm", "GunRunnersArsenal.esm", "CaravanPack.esm",
          "ClassicPack.esm",  "MercenaryPack.esm",     "TribalPack.esm"};
}

MOBase::IPluginGame::SortMechanism GameFalloutNV::sortMechanism() const
{
  if (m_Organizer->pluginSetting(name(), "enable_loot_sorting").toBool())
    return IPluginGame::SortMechanism::LOOT;
  return IPluginGame::SortMechanism::NONE;
}

int GameFalloutNV::nexusModOrganizerID() const
{
  return 42572;
}

int GameFalloutNV::nexusGameID() const
{
  return 130;
}

QDir GameFalloutNV::gameDirectory() const
{
  return QDir(m_GamePath);
}

MappingType GameFalloutNV::mappings() const
{
  MappingType result;

  for (const QString& profileFile : {"plugins.txt", "loadorder.txt"}) {
    result.push_back({m_Organizer->profilePath() + "/" + profileFile,
                      localAppFolder() + "/" + gameShortName() + "/" + profileFile,
                      false});
    if (selectedVariant() == "Epic Games") {
      result.push_back(
          {m_Organizer->profilePath() + "/" + profileFile,
           localAppFolder() + "/" + gameDirectoryName() + "/" + profileFile, false});
    }
  }

  return result;
}
