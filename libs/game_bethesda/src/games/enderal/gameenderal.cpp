#include "gameenderal.h"

#include "enderalbsainvalidation.h"
#include "enderaldataarchives.h"
#include "enderalgameplugins.h"
#include "enderallocalsavegames.h"
#include "enderalmoddatachecker.h"
#include "enderalmoddatacontent.h"
#include "enderalsavegame.h"
#include "enderalscriptextender.h"

#include "executableinfo.h"
#include "pluginsetting.h"
#include "steamutility.h"
#include "utility.h"

#include <gamebryogameplugins.h>
#include <gamebryosavegameinfo.h>
#include <gamebryounmanagedmods.h>

#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>

#include <QIcon>
#include <QtDebug>

#ifdef _WIN32
#include <Windows.h>
#include <winver.h>
#endif

#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace MOBase;

GameEnderal::GameEnderal() {}

bool GameEnderal::init(IOrganizer* moInfo)
{
  if (!GameGamebryo::init(moInfo)) {
    return false;
  }

  auto dataArchives = std::make_shared<EnderalDataArchives>(this);
  registerFeature(std::make_shared<EnderalScriptExtender>(this));
  registerFeature(dataArchives);
  registerFeature(std::make_shared<EnderalBSAInvalidation>(dataArchives.get(), this));
  registerFeature(std::make_shared<GamebryoSaveGameInfo>(this));
  registerFeature(std::make_shared<EnderalLocalSavegames>(this, "enderal.ini"));
  registerFeature(std::make_shared<EnderalModDataChecker>(this));
  registerFeature(std::make_shared<EnderalModDataContent>(moInfo->gameFeatures()));
  registerFeature(std::make_shared<EnderalGamePlugins>(moInfo));
  registerFeature(std::make_shared<GamebryoUnmangedMods>(this));

  return true;
}

QString GameEnderal::gameName() const
{
  return "Enderal";
}

QList<ExecutableInfo> GameEnderal::executables() const
{
  return QList<ExecutableInfo>()
         //<< ExecutableInfo("SKSE",
         // findInGameFolder(feature<ScriptExtender>()->loaderName()))
         //<< ExecutableInfo("SBW", findInGameFolder("SBW.exe"))
         << ExecutableInfo("Enderal (SKSE)", findInGameFolder(binaryName()))
         << ExecutableInfo("Enderal Launcher", findInGameFolder(getLauncherName()))
         << ExecutableInfo("BOSS", findInGameFolder("BOSS/BOSS.exe"))
         << ExecutableInfo("LOOT", QFileInfo(getLootPath()))
                .withArgument("--game=\"Enderal\"")
         << ExecutableInfo("Creation Kit", findInGameFolder("CreationKit.exe"))
                .withSteamAppId("202480");
}

QList<ExecutableForcedLoadSetting> GameEnderal::executableForcedLoads() const
{
  return QList<ExecutableForcedLoadSetting>();
}

QString GameEnderal::name() const
{
  return "Enderal Support Plugin";
}

QString GameEnderal::localizedName() const
{
  return tr("Enderal Support Plugin");
}

QString GameEnderal::author() const
{
  return "AL12 & MO2 Team";
}

QString GameEnderal::description() const
{
  return tr("Adds support for the game Enderal");
}

MOBase::VersionInfo GameEnderal::version() const
{
  return VersionInfo(1, 3, 1, VersionInfo::RELEASE_FINAL);
}

QList<PluginSetting> GameEnderal::settings() const
{
  QList<PluginSetting> results;
  results.push_back(
      PluginSetting("sse_downloads", "allow Skyrim SE downloads", QVariant(false)));
  return results;
}

void GameEnderal::initializeProfile(const QDir& path, ProfileSettings settings) const
{
  if (settings.testFlag(IPluginGame::MODS)) {
    copyToProfile(localAppFolder() + "/enderal", path, "plugins.txt");
  }

  if (settings.testFlag(IPluginGame::CONFIGURATION)) {
    if (settings.testFlag(IPluginGame::PREFER_DEFAULTS) ||
        !QFileInfo(myGamesPath() + "/enderal.ini").exists()) {

      // there is no default ini, actually they are going to put them in for us!
      copyToProfile(gameDirectory().absolutePath(), path, "enderal_default.ini",
                    "enderal.ini");
      copyToProfile(gameDirectory().absolutePath(), path, "enderalprefs_default.ini",
                    "enderalprefs.ini");
    } else {
      copyToProfile(myGamesPath(), path, "enderal.ini");
      copyToProfile(myGamesPath(), path, "enderalprefs.ini");
    }
  }
}

bool GameEnderal::looksValid(QDir const& path) const
{
  // we need to check both launcher and binary because the binary also exists for
  // Skyrim and the launcher for Enderal SE
  return path.exists(getLauncherName()) && path.exists(binaryName());
}

QIcon GameEnderal::gameIcon() const
{
  return MOBase::iconForExecutable(gameDirectory().absoluteFilePath(getLauncherName()));
}

QString GameEnderal::savegameExtension() const
{
  return "ess";
}

QString GameEnderal::savegameSEExtension() const
{
  return "skse";
}

std::shared_ptr<const GamebryoSaveGame>
GameEnderal::makeSaveGame(QString filepath) const
{
  return std::make_shared<const EnderalSaveGame>(filepath, this);
}

QString GameEnderal::steamAPPId() const
{
  return "933480";
}

QStringList GameEnderal::primaryPlugins() const
{
  return {"Skyrim.esm", "Enderal - Forgotten Stories.esm", "Update.esm"};
}

QString GameEnderal::binaryName() const
{
  return "skse_loader.exe";
}

QString GameEnderal::getLauncherName() const
{
  return "Enderal Launcher.exe";
}

QString GameEnderal::gameShortName() const
{
  return "Enderal";
}

QString GameEnderal::gameNexusName() const
{
  return "enderal";
}

QStringList GameEnderal::primarySources() const
{
  return {"Skyrim"};
}

QStringList GameEnderal::validShortNames() const
{
  QStringList results;
  results.push_back("Skyrim");
  if (m_Organizer->pluginSetting(name(), "sse_downloads").toBool()) {
    results.push_back("SkyrimSE");
  }
  return results;
}

QStringList GameEnderal::iniFiles() const
{
  return {"enderal.ini", "enderalprefs.ini"};
}

QStringList GameEnderal::DLCPlugins() const
{
  return {};
}

IPluginGame::LoadOrderMechanism GameEnderal::loadOrderMechanism() const
{
  return LoadOrderMechanism::PluginsTxt;
}

int GameEnderal::nexusModOrganizerID() const
{
  return 0;
}

int GameEnderal::nexusGameID() const
{
  return 2736;
}

QString GameEnderal::identifyGamePath() const
{
#ifdef _WIN32
  QString path = "Software\\SureAI\\Enderal";
  QString result;
  try {
    result =
        findInRegistry(HKEY_CURRENT_USER, path.toStdWString().c_str(), L"Install_Path");
  } catch (MOBase::MyException) {
    result = MOBase::findSteamGame("Enderal", "Data\\Enderal - Forgotten Stories.esm");
  }
  return result;
#else
  // Prefer exact Steam app-id resolution for Enderal LE.
  QString result = parseSteamLocation(steamAPPId(), "Enderal");
  if (!result.isEmpty() && looksValid(QDir(result))) {
    return result;
  }

  result = MOBase::findSteamGame("Enderal", "Data/Enderal - Forgotten Stories.esm");
  if (!result.isEmpty() && looksValid(QDir(result))) {
    return result;
  }

  return GameGamebryo::identifyGamePath();
#endif
}
