#include "gamefallout76.h"

#include "fallout76dataarchives.h"
#include "fallout76moddatachecker.h"
#include "fallout76moddatacontent.h"
#include "fallout76unmanagedmods.h"

#include "versioninfo.h"
#include <creationgameplugins.h>
#include <executableinfo.h>
#include <pluginsetting.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

#include "scopeguard.h"

using namespace MOBase;

GameFallout76::GameFallout76() {}

bool GameFallout76::init(IOrganizer* moInfo)
{
  if (!GameGamebryo::init(moInfo)) {
    return false;
  }

  registerFeature(std::make_shared<Fallout76DataArchives>(this));
  registerFeature(std::make_shared<Fallout76ModDataChecker>(this));
  registerFeature(
      std::make_shared<Fallout76ModDataContent>(m_Organizer->gameFeatures()));
  registerFeature(std::make_shared<CreationGamePlugins>(moInfo));
  registerFeature(std::make_shared<Fallout76UnmangedMods>(this));

  return true;
}

QString GameFallout76::gameName() const
{
  return "Fallout 76";
}

void GameFallout76::detectGame()
{
  m_GamePath    = identifyGamePath();
  m_MyGamesPath = determineMyGamesPath(gameName());
}

QList<ExecutableInfo> GameFallout76::executables() const
{
  return QList<ExecutableInfo>()
         << ExecutableInfo("Fallout 76", findInGameFolder(binaryName()));
}

QList<ExecutableForcedLoadSetting> GameFallout76::executableForcedLoads() const
{
  return {};
}

QString GameFallout76::name() const
{
  return "Fallout 76 Support Plugin";
}

QString GameFallout76::author() const
{
  return "Mod Organizer Team; EntranceJew";
}

QString GameFallout76::description() const
{
  return tr("Adds support for the game Fallout 76.\n"
            "Splash by %1")
      .arg("nekoyoubi");
}

MOBase::VersionInfo GameFallout76::version() const
{
  return VersionInfo(1, 0, 0, VersionInfo::RELEASE_FINAL);
}

QList<PluginSetting> GameFallout76::settings() const
{
  return QList<PluginSetting>();
}

void GameFallout76::initializeProfile(const QDir& path, ProfileSettings settings) const
{
  if (settings.testFlag(IPluginGame::MODS)) {
    copyToProfile(localAppFolder() + "/Fallout76", path, "plugins.txt");
    copyToProfile(localAppFolder() + "/Fallout76", path, "loadorder.txt");
  }

  if (settings.testFlag(IPluginGame::CONFIGURATION)) {
    if (settings.testFlag(IPluginGame::PREFER_DEFAULTS) ||
        !QFileInfo(myGamesPath() + "/Fallout76.ini").exists()) {
      copyToProfile(gameDirectory().absolutePath(), path, "Fallout76_default.ini",
                    "Fallout76.ini");
    } else {
      copyToProfile(myGamesPath(), path, "Fallout76.ini");
    }

    copyToProfile(myGamesPath(), path, "Fallout76Prefs.ini");
    copyToProfile(myGamesPath(), path, "Fallout76Custom.ini");
  }
}

QString GameFallout76::savegameExtension() const
{
  return "";
}

QString GameFallout76::savegameSEExtension() const
{
  return "";
}

std::vector<std::shared_ptr<const MOBase::ISaveGame>>
GameFallout76::listSaves(QDir folder) const
{
  return {};
}

QString GameFallout76::identifyGamePath() const
{
  return parseSteamLocation(steamAPPId(), gameShortName());
}

std::shared_ptr<const GamebryoSaveGame> GameFallout76::makeSaveGame(QString) const
{
  return nullptr;
}

QString GameFallout76::steamAPPId() const
{
  return "1151340";
}

QStringList GameFallout76::primaryPlugins() const
{
  QStringList plugins = {"SeventySix.esm"};

  plugins.append(CCPlugins());

  return plugins;
}

QStringList GameFallout76::gameVariants() const
{
  return {"Regular"};
}

QString GameFallout76::gameShortName() const
{
  return "Fallout76";
}

QString GameFallout76::gameNexusName() const
{
  return "Fallout76";
}

QStringList GameFallout76::iniFiles() const
{
  return {"Fallout76.ini", "Fallout76Prefs.ini", "Fallout76Custom.ini"};
}

QStringList GameFallout76::DLCPlugins() const
{
  return {};
}

QStringList GameFallout76::CCPlugins() const
{
  QStringList plugins = {};
  QFile file(gameDirectory().absoluteFilePath("Fallout76.ccc"));
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

IPluginGame::SortMechanism GameFallout76::sortMechanism() const
{
  return IPluginGame::SortMechanism::NONE;
}

IPluginGame::LoadOrderMechanism GameFallout76::loadOrderMechanism() const
{
  return IPluginGame::LoadOrderMechanism::PluginsTxt;
}

int GameFallout76::nexusModOrganizerID() const
{
  return -1;
}

int GameFallout76::nexusGameID() const
{
  return 2590;
}
