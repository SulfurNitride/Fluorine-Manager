#include "gameskyrimvr.h"

#include "skyrimvrdataarchives.h"
#include "skyrimvrgameplugins.h"
#include "skyrimvrmoddatachecker.h"
#include "skyrimvrmoddatacontent.h"
#include "skyrimvrsavegame.h"
#include "skyrimvrscriptextender.h"
#include "skyrimvrunmanagedmods.h"

#include "versioninfo.h"
#include <executableinfo.h>
#include <gamebryolocalsavegames.h>
#include <gamebryosavegameinfo.h>
#include <pluginsetting.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "scopeguard.h"
#include <memory>

using namespace MOBase;

GameSkyrimVR::GameSkyrimVR() {}

void GameSkyrimVR::setGamePath(const QString& path)
{
  m_GamePath = path;
}

QDir GameSkyrimVR::documentsDirectory() const
{
  return m_MyGamesPath;
}

QString GameSkyrimVR::identifyGamePath() const
{
#ifdef _WIN32
  QString path = "Software\\Bethesda Softworks\\" + gameName();
  return findInRegistry(HKEY_LOCAL_MACHINE, path.toStdWString().c_str(),
                        L"Installed Path");
#else
  // Use the shared Linux detection logic from the base class (NaK + fallbacks).
  return GameGamebryo::identifyGamePath();
#endif
}

QDir GameSkyrimVR::savesDirectory() const
{
  return QDir(m_MyGamesPath + "/Saves");
}

QString GameSkyrimVR::myGamesPath() const
{
  return m_MyGamesPath;
}

bool GameSkyrimVR::isInstalled() const
{
  return !m_GamePath.isEmpty();
}

bool GameSkyrimVR::init(IOrganizer* moInfo)
{
  if (!GameGamebryo::init(moInfo)) {
    return false;
  }

  registerFeature(std::make_shared<SkyrimVRScriptExtender>(this));
  registerFeature(std::make_shared<SkyrimVRDataArchives>(this));
  registerFeature(std::make_shared<GamebryoLocalSavegames>(this, "SkyrimVR.ini"));
  registerFeature(std::make_shared<SkyrimVRModDataChecker>(this));
  registerFeature(std::make_shared<GamebryoSaveGameInfo>(this));
  registerFeature(
      std::make_shared<SkyrimVRModDataContent>(m_Organizer->gameFeatures()));
  registerFeature(std::make_shared<SkyrimVRGamePlugins>(moInfo));
  registerFeature(std::make_shared<SkyrimVRUnmangedMods>(this));

  return true;
}

QString GameSkyrimVR::gameName() const
{
  return "Skyrim VR";
}

QList<ExecutableInfo> GameSkyrimVR::executables() const
{
  return QList<ExecutableInfo>()
         << ExecutableInfo("SKSE",
                           findInGameFolder(m_Organizer->gameFeatures()
                                                ->gameFeature<MOBase::ScriptExtender>()
                                                ->loaderName()))
         << ExecutableInfo("Skyrim VR", findInGameFolder(binaryName()))
         << ExecutableInfo("Creation Kit", findInGameFolder("CreationKit.exe"))
         << ExecutableInfo("LOOT", QFileInfo(getLootPath()))
                .withArgument("--game=\"Skyrim VR\"");
}

QList<ExecutableForcedLoadSetting> GameSkyrimVR::executableForcedLoads() const
{
  return QList<ExecutableForcedLoadSetting>();
}

QFileInfo GameSkyrimVR::findInGameFolder(const QString& relativePath) const
{
  return QFileInfo(m_GamePath + "/" + relativePath);
}

QString GameSkyrimVR::name() const
{
  return "Skyrim VR Support Plugin";
}

QString GameSkyrimVR::localizedName() const
{
  return tr("Skyrim VR Support Plugin");
}

QString GameSkyrimVR::author() const
{
  return "Brixified & MO2 Team";
}

QString GameSkyrimVR::description() const
{
  return tr("Adds support for the game Skyrim VR.");
}

MOBase::VersionInfo GameSkyrimVR::version() const
{
  return VersionInfo(1, 5, 1, VersionInfo::RELEASE_FINAL);
}

QList<PluginSetting> GameSkyrimVR::settings() const
{
  return {PluginSetting("enderal_downloads", "allow Enderal and Enderal SE downloads",
                        QVariant(false))};
}

void GameSkyrimVR::initializeProfile(const QDir& path, ProfileSettings settings) const
{
  if (settings.testFlag(IPluginGame::MODS)) {
    copyToProfile(localAppFolder() + "/Skyrim VR", path, "plugins.txt");
  }

  if (settings.testFlag(IPluginGame::CONFIGURATION)) {
    if (settings.testFlag(IPluginGame::PREFER_DEFAULTS) ||
        !QFileInfo(myGamesPath() + "/skyrimvr.ini").exists()) {
      copyToProfile(gameDirectory().absolutePath(), path, "skyrim.ini", "skyrimvr.ini");
    } else {
      copyToProfile(myGamesPath(), path, "skyrimvr.ini");
    }

    copyToProfile(myGamesPath(), path, "skyrimprefs.ini");
  }
}

QString GameSkyrimVR::savegameExtension() const
{
  return "ess";
}

QString GameSkyrimVR::savegameSEExtension() const
{
  return "skse";
}

std::shared_ptr<const GamebryoSaveGame>
GameSkyrimVR::makeSaveGame(QString filePath) const
{
  return std::make_shared<const SkyrimVRSaveGame>(filePath, this);
}

QString GameSkyrimVR::steamAPPId() const
{
  return "611670";
}

QStringList GameSkyrimVR::primaryPlugins() const
{
  QStringList plugins = {"skyrim.esm",      "update.esm",     "dawnguard.esm",
                         "hearthfires.esm", "dragonborn.esm", "skyrimvr.esm"};

  plugins.append(CCPlugins());

  return plugins;
}

QStringList GameSkyrimVR::gameVariants() const
{
  return {"Regular"};
}

QString GameSkyrimVR::gameShortName() const
{
  return "SkyrimVR";
}

QStringList GameSkyrimVR::primarySources() const
{
  return {"SkyrimSE"};
}

QStringList GameSkyrimVR::validShortNames() const
{
  QStringList shortNames{"Skyrim", "SkyrimSE"};
  if (m_Organizer->pluginSetting(name(), "enderal_downloads").toBool()) {
    shortNames.append({"Enderal", "EnderalSE"});
  }
  return shortNames;
}

QString GameSkyrimVR::gameNexusName() const
{
  return QString();
}

QStringList GameSkyrimVR::iniFiles() const
{
  return {"skyrimvr.ini", "skyrimprefs.ini"};
}

QStringList GameSkyrimVR::DLCPlugins() const
{
  return {"dawnguard.esm", "hearthfires.esm", "dragonborn.esm"};
}

QStringList GameSkyrimVR::CCPlugins() const
{
  QStringList plugins = {};
  QFile file(gameDirectory().filePath("Skyrim.ccc"));
  if (file.open(QIODevice::ReadOnly)) {
    ON_BLOCK_EXIT([&file]() {
      file.close();
    });

    if (file.size() == 0) {
      return plugins;
    }
    while (!file.atEnd()) {
      QByteArray line = file.readLine().trimmed();
      QString modName;
      if ((line.size() > 0) && (line.at(0) != '#')) {
        modName = QString::fromUtf8(line.constData()).toLower();
      }

      if (modName.size() > 0) {
        if (!plugins.contains(modName, Qt::CaseInsensitive)) {
          plugins.append(modName);
        }
      }
    }
  }
  return plugins;
}

IPluginGame::LoadOrderMechanism GameSkyrimVR::loadOrderMechanism() const
{
  return IPluginGame::LoadOrderMechanism::PluginsTxt;
}

MOBase::IPluginGame::SortMechanism GameSkyrimVR::sortMechanism() const
{
  return SortMechanism::LOOT;
}

int GameSkyrimVR::nexusModOrganizerID() const
{
  return 0;
}

int GameSkyrimVR::nexusGameID() const
{
  return 0;
}

QString GameSkyrimVR::getLauncherName() const
{
  return binaryName();  // Skyrim VR has no Launcher, so we just return the name of the
                        // game binary
}

QDir GameSkyrimVR::gameDirectory() const
{
  return QDir(m_GamePath);
}

// Not to delete all the spaces...
MappingType GameSkyrimVR::mappings() const
{
  MappingType result;

  for (const QString& profileFile : {"plugins.txt", "loadorder.txt"}) {
    result.push_back({m_Organizer->profilePath() + "/" + profileFile,
                      localAppFolder() + "/" + gameName() + "/" + profileFile, false});
  }

  return result;
}
