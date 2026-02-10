#include "gamefallout4.h"

#include "fallout4bsainvalidation.h"
#include "fallout4dataarchives.h"
#include "fallout4moddatachecker.h"
#include "fallout4moddatacontent.h"
#include "fallout4savegame.h"
#include "fallout4scriptextender.h"
#include "fallout4unmanagedmods.h"

#include "versioninfo.h"
#include <creationgameplugins.h>
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

const unsigned int GameFallout4::PROBLEM_TEST_FILE;

GameFallout4::GameFallout4() {}

bool GameFallout4::init(IOrganizer* moInfo)
{
  if (!GameGamebryo::init(moInfo)) {
    return false;
  }

  auto dataArchives = std::make_shared<Fallout4DataArchives>(this);

  registerFeature(std::make_shared<Fallout4ScriptExtender>(this));
  registerFeature(dataArchives);
  registerFeature(std::make_shared<GamebryoLocalSavegames>(this, "fallout4custom.ini"));
  registerFeature(std::make_shared<Fallout4ModDataChecker>(this));
  registerFeature(
      std::make_shared<Fallout4ModDataContent>(m_Organizer->gameFeatures()));
  registerFeature(std::make_shared<GamebryoSaveGameInfo>(this));
  registerFeature(std::make_shared<CreationGamePlugins>(moInfo));
  registerFeature(std::make_shared<Fallout4UnmangedMods>(this));
  registerFeature(std::make_shared<Fallout4BSAInvalidation>(dataArchives.get(), this));

  return true;
}

QString GameFallout4::gameName() const
{
  return "Fallout 4";
}

void GameFallout4::detectGame()
{
  m_GamePath    = identifyGamePath();
  m_MyGamesPath = determineMyGamesPath("Fallout4");
}

QList<ExecutableInfo> GameFallout4::executables() const
{
  return QList<ExecutableInfo>()
         << ExecutableInfo("F4SE",
                           findInGameFolder(m_Organizer->gameFeatures()
                                                ->gameFeature<MOBase::ScriptExtender>()
                                                ->loaderName()))
         << ExecutableInfo("Fallout 4", findInGameFolder(binaryName()))
         << ExecutableInfo("Fallout Launcher", findInGameFolder(getLauncherName()))
         << ExecutableInfo("Creation Kit", findInGameFolder("CreationKit.exe"))
                .withSteamAppId("1946160")
         << ExecutableInfo("LOOT", QFileInfo(getLootPath()))
                .withArgument("--game=\"Fallout4\"");
}

QList<ExecutableForcedLoadSetting> GameFallout4::executableForcedLoads() const
{
  return QList<ExecutableForcedLoadSetting>();
}

QString GameFallout4::name() const
{
  return "Fallout 4 Support Plugin";
}

QString GameFallout4::localizedName() const
{
  return tr("Fallout 4 Support Plugin");
}

QString GameFallout4::author() const
{
  return "Tannin & MO2 Team";
}

QString GameFallout4::description() const
{
  return tr("Adds support for the game Fallout 4.\n"
            "Splash by %1")
      .arg("nekoyoubi");
}

MOBase::VersionInfo GameFallout4::version() const
{
  return VersionInfo(1, 8, 0, VersionInfo::RELEASE_FINAL);
}

QList<PluginSetting> GameFallout4::settings() const
{
  return QList<PluginSetting>();
}

MappingType GameFallout4::mappings() const
{
  MappingType result;
  if (testFilePlugins().isEmpty()) {
    for (const QString& profileFile : {"plugins.txt", "loadorder.txt"}) {
      result.push_back({m_Organizer->profilePath() + "/" + profileFile,
                        localAppFolder() + "/" + gameShortName() + "/" + profileFile,
                        false});
    }
  }
  return result;
}

void GameFallout4::initializeProfile(const QDir& path, ProfileSettings settings) const
{
  if (settings.testFlag(IPluginGame::MODS)) {
    copyToProfile(localAppFolder() + "/Fallout4", path, "plugins.txt");
  }

  if (settings.testFlag(IPluginGame::CONFIGURATION)) {
    if (settings.testFlag(IPluginGame::PREFER_DEFAULTS) ||
        !QFileInfo(myGamesPath() + "/fallout4.ini").exists()) {
      copyToProfile(gameDirectory().absolutePath(), path, "fallout4_default.ini",
                    "fallout4.ini");
    } else {
      copyToProfile(myGamesPath(), path, "fallout4.ini");
    }

    copyToProfile(myGamesPath(), path, "fallout4prefs.ini");
    copyToProfile(myGamesPath(), path, "fallout4custom.ini");
  }
}

QString GameFallout4::savegameExtension() const
{
  return "fos";
}

QString GameFallout4::savegameSEExtension() const
{
  return "f4se";
}

std::shared_ptr<const GamebryoSaveGame>
GameFallout4::makeSaveGame(QString filePath) const
{
  return std::make_shared<const Fallout4SaveGame>(filePath, this);
}

QString GameFallout4::steamAPPId() const
{
  return "377160";
}

QStringList GameFallout4::testFilePlugins() const
{
  QStringList plugins;
  if (m_Organizer != nullptr && m_Organizer->profile() != nullptr) {
    QString customIni(
        m_Organizer->profile()->absoluteIniFilePath("Fallout4Custom.ini"));
    if (QFile(customIni).exists()) {
      for (int i = 1; i <= 10; ++i) {
        QString setting("sTestFile");
        setting += std::to_string(i);
        QString plugin = GameGamebryo::readIniValue(customIni, "General", setting, "");
        if (!plugin.isEmpty() && !plugins.contains(plugin, Qt::CaseInsensitive)) {
          plugins.append(plugin);
        }
      }
    }
  }
  return plugins;
}

QStringList GameFallout4::primaryPlugins() const
{
  QStringList plugins = {"fallout4.esm",      "dlcrobot.esm",
                         "dlcworkshop01.esm", "dlccoast.esm",
                         "dlcworkshop02.esm", "dlcworkshop03.esm",
                         "dlcnukaworld.esm",  "dlcultrahighresolution.esm"};

  auto testPlugins = testFilePlugins();
  if (loadOrderMechanism() == LoadOrderMechanism::None) {
    plugins << testPlugins;
  } else {
    plugins << CCPlugins();
  }

  return plugins;
}

QStringList GameFallout4::gameVariants() const
{
  return {"Regular"};
}

QString GameFallout4::gameShortName() const
{
  return "Fallout4";
}

QString GameFallout4::gameNexusName() const
{
  return "fallout4";
}

QStringList GameFallout4::iniFiles() const
{
  return {"fallout4.ini", "fallout4prefs.ini", "fallout4custom.ini"};
}

QStringList GameFallout4::DLCPlugins() const
{
  return {"dlcrobot.esm",
          "dlcworkshop01.esm",
          "dlccoast.esm",
          "dlcworkshop02.esm",
          "dlcworkshop03.esm",
          "dlcnukaworld.esm",
          "dlcultrahighresolution.esm"};
}

QStringList GameFallout4::CCPlugins() const
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

IPluginGame::SortMechanism GameFallout4::sortMechanism() const
{
  if (!testFilePresent())
    return IPluginGame::SortMechanism::LOOT;
  return IPluginGame::SortMechanism::NONE;
}

IPluginGame::LoadOrderMechanism GameFallout4::loadOrderMechanism() const
{
  if (!testFilePresent())
    return IPluginGame::LoadOrderMechanism::PluginsTxt;
  return IPluginGame::LoadOrderMechanism::None;
}

int GameFallout4::nexusModOrganizerID() const
{
  return 28715;
}

int GameFallout4::nexusGameID() const
{
  return 1151;
}

// Start Diagnose
std::vector<unsigned int> GameFallout4::activeProblems() const
{
  std::vector<unsigned int> result;
  if (m_Organizer->managedGame() == this) {
    if (testFilePresent())
      result.push_back(PROBLEM_TEST_FILE);
  }
  return result;
}

bool GameFallout4::testFilePresent() const
{
  if (!testFilePlugins().isEmpty())
    return true;
  return false;
}

QString GameFallout4::shortDescription(unsigned int key) const
{
  switch (key) {
  case PROBLEM_TEST_FILE:
    return tr("sTestFile entries are present");
  }
  return QString();
}

QString GameFallout4::fullDescription(unsigned int key) const
{
  switch (key) {
  case PROBLEM_TEST_FILE: {
    return tr("<p>You have sTestFile settings in your "
              "Fallout4Custom.ini. These must be removed or "
              "the game will not read the plugins.txt file. "
              "Management is disabled.</p>");
  }
  }
  return QString();
}
