#include "gamemorrowind.h"

#include "morrowindbsainvalidation.h"
#include "morrowinddataarchives.h"
#include "morrowindgameplugins.h"
#include "morrowindlocalsavegames.h"
#include "morrowindmoddatachecker.h"
#include "morrowindmoddatacontent.h"
#include "morrowindsavegame.h"
#include "morrowindsavegameinfo.h"

#include "executableinfo.h"
#include "pluginsetting.h"
#include "steamutility.h"

#include <gamebryounmanagedmods.h>

#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>

#include <QtDebug>

#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace MOBase;

GameMorrowind::GameMorrowind() {}

bool GameMorrowind::init(IOrganizer* moInfo)
{
  if (!GameGamebryo::init(moInfo)) {
    return false;
  }

  auto dataArchives = std::make_shared<MorrowindDataArchives>(this);
  registerFeature(dataArchives);
  registerFeature(std::make_shared<MorrowindBSAInvalidation>(dataArchives.get(), this));
  registerFeature(std::make_shared<MorrowindSaveGameInfo>(this));
  registerFeature(std::make_shared<MorrowindLocalSavegames>(this));
  registerFeature(std::make_shared<MorrowindModDataChecker>(this));
  registerFeature(
      std::make_shared<MorrowindModDataContent>(m_Organizer->gameFeatures()));
  registerFeature(std::make_shared<MorrowindGamePlugins>(moInfo));
  registerFeature(std::make_shared<GamebryoUnmangedMods>(this));

  return true;
}

QString GameMorrowind::gameName() const
{
  return "Morrowind";
}

QString GameMorrowind::getLauncherName() const
{
  return "Morrowind Launcher.exe";
}

QDir GameMorrowind::dataDirectory() const
{
  return gameDirectory().absoluteFilePath("Data Files");
}

QDir GameMorrowind::savesDirectory() const
{
  return QDir(gameDirectory().absoluteFilePath("Saves"));
}

QDir GameMorrowind::documentsDirectory() const
{
  return gameDirectory();
}

QList<ExecutableInfo> GameMorrowind::executables() const
{
  return QList<ExecutableInfo>()
         << ExecutableInfo("MWSE (Launcher Method)",
                           findInGameFolder("MWSE Launcher.exe"))
         << ExecutableInfo("Morrowind", findInGameFolder(binaryName()))
         << ExecutableInfo("Morrowind Launcher", findInGameFolder(getLauncherName()))
         << ExecutableInfo("MGE XE", findInGameFolder("MGEXEgui.exe"))
         << ExecutableInfo("LOOT", QFileInfo(getLootPath()))
                .withArgument("--game=\"Morrowind\"");
}

QList<ExecutableForcedLoadSetting> GameMorrowind::executableForcedLoads() const
{
  return QList<ExecutableForcedLoadSetting>();
}

QString GameMorrowind::name() const
{
  return "Morrowind Support Plugin";
}

QString GameMorrowind::localizedName() const
{
  return tr("Morrowind Support Plugin");
}

QString GameMorrowind::author() const
{
  return "Schilduin & MO2 Team";
}

QString GameMorrowind::description() const
{
  return tr("Adds support for the game Morrowind.\n"
            "Splash by %1")
      .arg("AnyOldName3");
}

MOBase::VersionInfo GameMorrowind::version() const
{
  return VersionInfo(1, 5, 1, VersionInfo::RELEASE_FINAL);
}

QList<PluginSetting> GameMorrowind::settings() const
{
  return QList<PluginSetting>();
}

void GameMorrowind::initializeProfile(const QDir& path, ProfileSettings settings) const
{
  if (settings.testFlag(IPluginGame::MODS)) {
    copyToProfile(localAppFolder() + "/Morrowind", path, "plugins.txt");
  }

  if (settings.testFlag(IPluginGame::CONFIGURATION)) {
    copyToProfile(gameDirectory().absolutePath(), path, "Morrowind.ini");
  }
}

QString GameMorrowind::savegameExtension() const
{
  return "ess";
}

QString GameMorrowind::savegameSEExtension() const
{
  return "mwse";
}

std::shared_ptr<const GamebryoSaveGame>
GameMorrowind::makeSaveGame(QString filePath) const
{
  return std::make_shared<const MorrowindSaveGame>(filePath, this);
}

QString GameMorrowind::steamAPPId() const
{
  return "22320";
}

QStringList GameMorrowind::primaryPlugins() const
{
  return {"morrowind.esm"};
}

QString GameMorrowind::binaryName() const
{
  return "Morrowind.exe";
}

QString GameMorrowind::gameShortName() const
{
  return "Morrowind";
}

QString GameMorrowind::gameNexusName() const
{
  return "Morrowind";
}

QStringList GameMorrowind::iniFiles() const
{
  return {"morrowind.ini"};
}

QStringList GameMorrowind::DLCPlugins() const
{
  return {"Tribunal.esm", "Bloodmoon.esm"};
}

MOBase::IPluginGame::SortMechanism GameMorrowind::sortMechanism() const
{
  return SortMechanism::LOOT;
}

int GameMorrowind::nexusModOrganizerID() const
{
  return 1334;
}

int GameMorrowind::nexusGameID() const
{
  return 100;
}

QString GameMorrowind::identifyGamePath() const
{
  QString path = GameGamebryo::identifyGamePath();
  if (path.isEmpty()) {
    // Resolve by Steam app id first (handles non-standard install directory names).
    path = parseSteamLocation(steamAPPId(), "");
  }
  if (path.isEmpty()) {
    path = MOBase::findSteamGame("Morrowind", "Data Files/Morrowind.esm");
  }
  return path;
}
