#include "gameskyrim.h"

#include "skyrimbsainvalidation.h"
#include "skyrimdataarchives.h"
#include "skyrimgameplugins.h"
#include "skyrimmoddatachecker.h"
#include "skyrimmoddatacontent.h"
#include "skyrimsavegame.h"
#include "skyrimscriptextender.h"

#include "executableinfo.h"
#include "pluginsetting.h"

#include <gamebryogameplugins.h>
#include <gamebryolocalsavegames.h>
#include <gamebryosavegameinfo.h>
#include <gamebryounmanagedmods.h>

#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>

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

GameSkyrim::GameSkyrim() {}

bool GameSkyrim::init(IOrganizer* moInfo)
{
  if (!GameGamebryo::init(moInfo)) {
    return false;
  }

  auto dataArchives = std::make_shared<SkyrimDataArchives>(this);
  registerFeature(std::make_shared<SkyrimScriptExtender>(this));
  registerFeature(dataArchives);
  registerFeature(std::make_shared<SkyrimBSAInvalidation>(dataArchives.get(), this));
  registerFeature(std::make_shared<GamebryoSaveGameInfo>(this));
  registerFeature(std::make_shared<GamebryoLocalSavegames>(this, "skyrim.ini"));
  registerFeature(std::make_shared<SkyrimModDataChecker>(this));
  registerFeature(std::make_shared<SkyrimModDataContent>(m_Organizer->gameFeatures()));
  registerFeature(std::make_shared<SkyrimGamePlugins>(moInfo));
  registerFeature(std::make_shared<GamebryoUnmangedMods>(this));

  return true;
}

QString GameSkyrim::gameName() const
{
  return "Skyrim";
}

QList<ExecutableInfo> GameSkyrim::executables() const
{
  return QList<ExecutableInfo>()
         << ExecutableInfo("SKSE",
                           findInGameFolder(m_Organizer->gameFeatures()
                                                ->gameFeature<MOBase::ScriptExtender>()
                                                ->loaderName()))
         << ExecutableInfo("SBW", findInGameFolder("SBW.exe"))
         << ExecutableInfo("Skyrim", findInGameFolder(binaryName()))
         << ExecutableInfo("Skyrim Launcher", findInGameFolder(getLauncherName()))
         << ExecutableInfo("BOSS", findInGameFolder("BOSS/BOSS.exe"))
         << ExecutableInfo("LOOT", QFileInfo(getLootPath()))
                .withArgument("--game=\"Skyrim\"")
         << ExecutableInfo("Creation Kit", findInGameFolder("CreationKit.exe"))
                .withSteamAppId("202480");
}

QList<ExecutableForcedLoadSetting> GameSkyrim::executableForcedLoads() const
{
  return QList<ExecutableForcedLoadSetting>();
}

QString GameSkyrim::name() const
{
  return "Skyrim Support Plugin";
}

QString GameSkyrim::localizedName() const
{
  return tr("Skyrim Support Plugin");
}

QString GameSkyrim::author() const
{
  return "Tannin & MO2 Team";
}

QString GameSkyrim::description() const
{
  return tr("Adds support for the game Skyrim");
}

MOBase::VersionInfo GameSkyrim::version() const
{
  return VersionInfo(1, 6, 1, VersionInfo::RELEASE_FINAL);
}

QList<PluginSetting> GameSkyrim::settings() const
{
  QList<PluginSetting> results;
  results.push_back(
      PluginSetting("sse_downloads", "allow Skyrim SE downloads", QVariant(false)));
  return results;
}

void GameSkyrim::initializeProfile(const QDir& path, ProfileSettings settings) const
{
  if (settings.testFlag(IPluginGame::MODS)) {
    copyToProfile(localAppFolder() + "/Skyrim", path, "plugins.txt");
  }

  if (settings.testFlag(IPluginGame::CONFIGURATION)) {
    if (settings.testFlag(IPluginGame::PREFER_DEFAULTS) ||
        !QFileInfo(myGamesPath() + "/skyrim.ini").exists()) {
      copyToProfile(gameDirectory().absolutePath(), path, "skyrim_default.ini",
                    "skyrim.ini");
    } else {
      copyToProfile(myGamesPath(), path, "skyrim.ini");
    }

    copyToProfile(myGamesPath(), path, "skyrimprefs.ini");
  }
}

QString GameSkyrim::savegameExtension() const
{
  return "ess";
}

QString GameSkyrim::savegameSEExtension() const
{
  return "skse";
}

std::shared_ptr<const GamebryoSaveGame> GameSkyrim::makeSaveGame(QString filepath) const
{
  return std::make_shared<const SkyrimSaveGame>(filepath, this);
}

QString GameSkyrim::steamAPPId() const
{
  return "72850";
}

QStringList GameSkyrim::primaryPlugins() const
{
  return {"skyrim.esm", "update.esm"};
}

QString GameSkyrim::binaryName() const
{
  return "TESV.exe";
}

QString GameSkyrim::gameShortName() const
{
  return "Skyrim";
}

QString GameSkyrim::gameNexusName() const
{
  return "skyrim";
}

QStringList GameSkyrim::validShortNames() const
{
  QStringList results;
  if (m_Organizer->pluginSetting(name(), "sse_downloads").toBool()) {
    results.push_back("SkyrimSE");
  }
  return results;
}

QStringList GameSkyrim::iniFiles() const
{
  return {"skyrim.ini", "skyrimprefs.ini"};
}

QStringList GameSkyrim::DLCPlugins() const
{
  return {"Dawnguard.esm",
          "Dragonborn.esm",
          "HearthFires.esm",
          "HighResTexturePack01.esp",
          "HighResTexturePack02.esp",
          "HighResTexturePack03.esp"};
}

namespace
{
// Note: This is ripped off from shared/util. And in an upcoming move, the fomod
// installer requires something similar. I suspect I should abstract this out
// into gamebryo (or lower level)

#ifdef _WIN32
VS_FIXEDFILEINFO GetFileVersion(const std::wstring& fileName)
{
  DWORD handle = 0UL;
  DWORD size   = ::GetFileVersionInfoSizeW(fileName.c_str(), &handle);
  if (size == 0) {
    throw std::runtime_error("failed to determine file version info size");
  }

  std::vector<char> buffer(size);
  handle = 0UL;
  if (!::GetFileVersionInfoW(fileName.c_str(), handle, size, buffer.data())) {
    throw std::runtime_error("failed to determine file version info");
  }

  void* versionInfoPtr   = nullptr;
  UINT versionInfoLength = 0;
  if (!::VerQueryValue(buffer.data(), L"\\", &versionInfoPtr, &versionInfoLength)) {
    throw std::runtime_error("failed to determine file version");
  }

  return *static_cast<VS_FIXEDFILEINFO*>(versionInfoPtr);
}
#endif

}  // namespace

IPluginGame::LoadOrderMechanism GameSkyrim::loadOrderMechanism() const
{
#ifdef _WIN32
  try {
    std::wstring fileName =
        gameDirectory().absoluteFilePath(binaryName()).toStdWString().c_str();
    VS_FIXEDFILEINFO versionInfo = ::GetFileVersion(fileName);
    if ((versionInfo.dwFileVersionMS > 0x10004) ||  // version >= 1.5.x?
        ((versionInfo.dwFileVersionMS == 0x10004) &&
         (versionInfo.dwFileVersionLS >= 0x1A0000))) {  // version >= ?.4.26
      return LoadOrderMechanism::PluginsTxt;
    }
  } catch (const std::exception& e) {
    qCritical() << "TESV.exe is invalid: " << e.what();
  }
#endif
  return LoadOrderMechanism::FileTime;
}

int GameSkyrim::nexusModOrganizerID() const
{
  return 0;
}

int GameSkyrim::nexusGameID() const
{
  return 110;
}
