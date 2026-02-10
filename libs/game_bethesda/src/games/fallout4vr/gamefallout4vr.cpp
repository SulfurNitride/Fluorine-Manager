#include "gamefallout4vr.h"

#include "fallout4vrdataarchives.h"
#include "fallout4vrgameplugins.h"
#include "fallout4vrmoddatachecker.h"
#include "fallout4vrmoddatacontent.h"
#include "fallout4vrsavegame.h"
#include "fallout4vrunmanagedmods.h"

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

#include <memory>

#include "scopeguard.h"

using namespace MOBase;

GameFallout4VR::GameFallout4VR() {}

bool GameFallout4VR::init(IOrganizer* moInfo)
{
  if (!GameGamebryo::init(moInfo)) {
    return false;
  }

  registerFeature(std::make_shared<Fallout4VRDataArchives>(this));
  registerFeature(std::make_shared<GamebryoLocalSavegames>(this, "fallout4custom.ini"));
  registerFeature(std::make_shared<Fallout4VRModDataChecker>(this));
  registerFeature(
      std::make_shared<Fallout4VRModDataContent>(m_Organizer->gameFeatures()));
  registerFeature(std::make_shared<GamebryoSaveGameInfo>(this));
  registerFeature(std::make_shared<Fallout4VRGamePlugins>(moInfo));
  registerFeature(std::make_shared<Fallout4VRUnmangedMods>(this));

  return true;
}

QString GameFallout4VR::gameName() const
{
  return "Fallout 4 VR";
}

void GameFallout4VR::detectGame()
{
  m_GamePath    = identifyGamePath();
  m_MyGamesPath = determineMyGamesPath("Fallout4VR");
}

QList<ExecutableInfo> GameFallout4VR::executables() const
{
  return QList<ExecutableInfo>()
         << ExecutableInfo("Fallout 4 VR", findInGameFolder(binaryName()))
         << ExecutableInfo("Creation Kit", findInGameFolder("CreationKit.exe"))
         << ExecutableInfo("LOOT", QFileInfo(getLootPath()))
                .withArgument("--game=\"Fallout4VR\"");
}

QList<ExecutableForcedLoadSetting> GameFallout4VR::executableForcedLoads() const
{
  return QList<ExecutableForcedLoadSetting>();
}

QString GameFallout4VR::name() const
{
  return "Fallout 4 VR Support Plugin";
}

QString GameFallout4VR::localizedName() const
{
  return tr("Fallout 4 VR Support Plugin");
}

QString GameFallout4VR::author() const
{
  return "MO2 Contibutors";
}

QString GameFallout4VR::description() const
{
  return tr("Adds support for the game Fallout 4 VR.\n"
            "Splash by %1")
      .arg("nekoyoubi");
}

MOBase::VersionInfo GameFallout4VR::version() const
{
  return VersionInfo(1, 6, 1, VersionInfo::RELEASE_FINAL);
}

QList<PluginSetting> GameFallout4VR::settings() const
{
  return QList<PluginSetting>();
}

void GameFallout4VR::initializeProfile(const QDir& path, ProfileSettings settings) const
{
  if (settings.testFlag(IPluginGame::MODS)) {
    copyToProfile(localAppFolder() + "/Fallout4VR", path, "plugins.txt");
  }

  if (settings.testFlag(IPluginGame::CONFIGURATION)) {
    if (settings.testFlag(IPluginGame::PREFER_DEFAULTS) ||
        !QFileInfo(myGamesPath() + "/fallout4.ini").exists()) {
      copyToProfile(gameDirectory().absolutePath(), path, "fallout4.ini");
    } else {
      copyToProfile(myGamesPath(), path, "fallout4.ini");
    }

    copyToProfile(myGamesPath(), path, "fallout4prefs.ini");
    copyToProfile(myGamesPath(), path, "fallout4custom.ini");
  }
}

QString GameFallout4VR::savegameExtension() const
{
  return "fos";
}

QString GameFallout4VR::savegameSEExtension() const
{
  return "f4se";
}

std::shared_ptr<const GamebryoSaveGame>
GameFallout4VR::makeSaveGame(QString filePath) const
{
  return std::make_shared<const Fallout4VRSaveGame>(filePath, this);
}

QString GameFallout4VR::steamAPPId() const
{
  return "611660";
}

QStringList GameFallout4VR::primaryPlugins() const
{
  QStringList plugins = {"fallout4.esm", "fallout4_vr.esm"};

  plugins.append(CCPlugins());

  return plugins;
}

QStringList GameFallout4VR::gameVariants() const
{
  return {"Regular"};
}

QString GameFallout4VR::gameShortName() const
{
  return "Fallout4VR";
}

QStringList GameFallout4VR::validShortNames() const
{
  return {"Fallout4"};
}

QString GameFallout4VR::gameNexusName() const
{
  return QString();
}

QStringList GameFallout4VR::iniFiles() const
{
  return {"fallout4.ini", "fallout4custom.ini", "fallout4prefs.ini"};
}

QStringList GameFallout4VR::DLCPlugins() const
{
  return {"dlcrobot.esm",
          "dlcworkshop01.esm",
          "dlccoast.esm",
          "dlcworkshop02.esm",
          "dlcworkshop03.esm",
          "dlcnukaworld.esm",
          "dlcultrahighresolution.esm"};
}

QStringList GameFallout4VR::CCPlugins() const
{
  QStringList plugins = {};
  QFile file(gameDirectory().absoluteFilePath("Fallout4.ccc"));
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

IPluginGame::LoadOrderMechanism GameFallout4VR::loadOrderMechanism() const
{
  return IPluginGame::LoadOrderMechanism::PluginsTxt;
}

int GameFallout4VR::nexusModOrganizerID() const
{
  return 0;  //...
}

int GameFallout4VR::nexusGameID() const
{
  return 0;
}

QString GameFallout4VR::getLauncherName() const
{
  return binaryName();  // Fallout 4 VR has no Launcher, so we just return the name of
                        // the game binary
}

QString GameFallout4VR::identifyGamePath() const
{
#ifdef _WIN32
  QString path = "Software\\Bethesda Softworks\\" + gameName();
  return findInRegistry(HKEY_LOCAL_MACHINE, path.toStdWString().c_str(),
                        L"Installed Path");
#else
  return GameGamebryo::identifyGamePath();
#endif
}
