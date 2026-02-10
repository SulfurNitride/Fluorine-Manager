#include "gamefalloutttw.h"

#include "falloutttwbsainvalidation.h"
#include "falloutttwdataarchives.h"
#include "falloutttwmoddatachecker.h"
#include "falloutttwmoddatacontent.h"
#include "falloutttwsavegame.h"
#include "falloutttwscriptextender.h"

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

GameFalloutTTW::GameFalloutTTW() {}

bool GameFalloutTTW::init(IOrganizer* moInfo)
{
  if (!GameGamebryo::init(moInfo)) {
    return false;
  }

  auto dataArchives = std::make_shared<FalloutTTWDataArchives>(this);
  registerFeature(std::make_shared<FalloutTTWScriptExtender>(this));
  registerFeature(dataArchives);
  registerFeature(
      std::make_shared<FalloutTTWBSAInvalidation>(dataArchives.get(), this));
  registerFeature(std::make_shared<GamebryoSaveGameInfo>(this));
  registerFeature(std::make_shared<GamebryoLocalSavegames>(this, "fallout.ini"));
  registerFeature(std::make_shared<FalloutTTWModDataChecker>(this));
  registerFeature(
      std::make_shared<FalloutTTWModDataContent>(m_Organizer->gameFeatures()));
  registerFeature(std::make_shared<GamebryoGamePlugins>(moInfo));
  registerFeature(std::make_shared<GamebryoUnmangedMods>(this));

  return true;
}

void GameFalloutTTW::setVariant(QString variant)
{
  m_GameVariant = variant;
}

void GameFalloutTTW::checkVariants()
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

QDir GameFalloutTTW::documentsDirectory() const
{
  return m_MyGamesPath;
}

QString GameFalloutTTW::identifyGamePath() const
{
  QString result;
#ifdef _WIN32
  QString path = "Software\\Bethesda Softworks\\FalloutNV";
  result       = findInRegistry(HKEY_LOCAL_MACHINE, path.toStdWString().c_str(),
                                L"Installed Path");
#else
  result = GameGamebryo::identifyGamePath();
#endif
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
        result += "/" + subDirs.first();
    }
  }
  return result;
}

void GameFalloutTTW::setGamePath(const QString& path)
{
  m_GamePath = path;
  checkVariants();
  m_MyGamesPath = determineMyGamesPath(gameDirectoryName());
}

QDir GameFalloutTTW::savesDirectory() const
{
  return QDir(m_MyGamesPath + "/Saves");
}

QString GameFalloutTTW::myGamesPath() const
{
  return m_MyGamesPath;
}

bool GameFalloutTTW::isInstalled() const
{
  return !m_GamePath.isEmpty();
}

QString GameFalloutTTW::gameName() const
{
  return "TTW";
}

QString GameFalloutTTW::displayGameName() const
{
  return "Tale of Two Wastelands";
}

QString GameFalloutTTW::gameDirectoryName() const
{
  if (selectedVariant() == "Epic Games")
    return "FalloutNV_Epic";
  else
    return "FalloutNV";
}

void GameFalloutTTW::detectGame()
{
  m_GamePath = identifyGamePath();
  checkVariants();
  m_MyGamesPath = determineMyGamesPath(gameDirectoryName());
}

QList<ExecutableInfo> GameFalloutTTW::executables() const
{
  ExecutableInfo game("Tale of Two Wastelands", findInGameFolder(binaryName()));
  ExecutableInfo launcher("Fallout Launcher", findInGameFolder(getLauncherName()));
  QList<ExecutableInfo> extraExecutables =
      QList<ExecutableInfo>() << ExecutableInfo("GECK", findInGameFolder("geck.exe"))
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

QList<ExecutableForcedLoadSetting> GameFalloutTTW::executableForcedLoads() const
{
  return QList<ExecutableForcedLoadSetting>();
}

QString GameFalloutTTW::name() const
{
  return "Fallout TTW Support Plugin";
}

QString GameFalloutTTW::localizedName() const
{
  return tr("Fallout TTW Support Plugin");
}

QString GameFalloutTTW::author() const
{
  return "SuperSandro2000 & MO2 Team";
}

QString GameFalloutTTW::description() const
{
  return tr("Adds support for the game Fallout TTW");
}

MOBase::VersionInfo GameFalloutTTW::version() const
{
  return VersionInfo(1, 6, 0, VersionInfo::RELEASE_FINAL);
}

QList<PluginSetting> GameFalloutTTW::settings() const
{
  return QList<PluginSetting>()
         << PluginSetting("enable_loot_sorting",
                          tr("While not recommended by the TTW modding community, "
                             "enables LOOT sorting"),
                          false);
}

void GameFalloutTTW::initializeProfile(const QDir& path, ProfileSettings settings) const
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

QString GameFalloutTTW::savegameExtension() const
{
  return "fos";
}

QString GameFalloutTTW::savegameSEExtension() const
{
  return "nvse";
}

std::shared_ptr<const GamebryoSaveGame>
GameFalloutTTW::makeSaveGame(QString filePath) const
{
  return std::make_shared<const FalloutTTWSaveGame>(filePath, this);
}

QString GameFalloutTTW::steamAPPId() const
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

QStringList GameFalloutTTW::primaryPlugins() const
{
  return {"falloutnv.esm",     "deadmoney.esm",           "honesthearts.esm",
          "oldworldblues.esm", "lonesomeroad.esm",        "gunrunnersarsenal.esm",
          "fallout3.esm",      "anchorage.esm",           "thepitt.esm",
          "brokensteel.esm",   "pointlookout.esm",        "zeta.esm",
          "caravanpack.esm",   "classicpack.esm",         "mercenarypack.esm",
          "tribalpack.esm",    "taleoftwowastelands.esm", "YUPTTW.esm"};
}

QStringList GameFalloutTTW::gameVariants() const
{
  return {"Steam", "GOG", "Epic Games"};
}

QString GameFalloutTTW::binaryName() const
{
  return "FalloutNV.exe";
}

QString GameFalloutTTW::gameShortName() const
{
  return "TTW";
}

QStringList GameFalloutTTW::primarySources() const
{
  return {"FalloutNV"};
}

QStringList GameFalloutTTW::validShortNames() const
{
  return {"FalloutNV", "Fallout3"};
}

QString GameFalloutTTW::gameNexusName() const
{
  return "";
}

QStringList GameFalloutTTW::iniFiles() const
{
  return {"fallout.ini", "falloutprefs.ini", "falloutcustom.ini", "GECKCustom.ini",
          "GECKPrefs.ini"};
}

QStringList GameFalloutTTW::DLCPlugins() const
{
  return {};
}

MOBase::IPluginGame::SortMechanism GameFalloutTTW::sortMechanism() const
{
  if (m_Organizer->pluginSetting(name(), "enable_loot_sorting").toBool())
    return IPluginGame::SortMechanism::LOOT;
  return IPluginGame::SortMechanism::NONE;
}

QString GameFalloutTTW::lootGameName() const
{
  return "FalloutNV";
}

int GameFalloutTTW::nexusModOrganizerID() const
{
  return 0;
}

int GameFalloutTTW::nexusGameID() const
{
  return 0;
}

QDir GameFalloutTTW::gameDirectory() const
{
  return QDir(m_GamePath);
}

QString GameFalloutTTW::getLauncherName() const
{
  return "FalloutNVLauncher.exe";
}

MappingType GameFalloutTTW::mappings() const
{
  MappingType result;

  for (const QString& profileFile : {"plugins.txt", "loadorder.txt"}) {
    result.push_back({m_Organizer->profilePath() + "/" + profileFile,
                      localAppFolder() + "/FalloutNV/" + profileFile, false});
    if (selectedVariant() == "Epic Games") {
      result.push_back(
          {m_Organizer->profilePath() + "/" + profileFile,
           localAppFolder() + "/" + gameDirectoryName() + "/" + profileFile, false});
    }
  }
  return result;
}
