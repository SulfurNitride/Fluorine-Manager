#include "gamefo4london.h"

#include "fo4londonbsainvalidation.h"
#include "fo4londondataarchives.h"
#include "fo4londonmoddatachecker.h"
#include "fo4londonmoddatacontent.h"
#include "fo4londonsavegame.h"
#include "fo4londonscriptextender.h"
#include "fo4londonunmanagedmods.h"

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

const unsigned int GameFallout4London::PROBLEM_TEST_FILE;

GameFallout4London::GameFallout4London() {}

bool GameFallout4London::init(IOrganizer* moInfo)
{
  if (!GameGamebryo::init(moInfo)) {
    return false;
  }

  auto dataArchives = std::make_shared<Fallout4LondonDataArchives>(this);

  registerFeature(std::make_shared<Fallout4LondonScriptExtender>(this));
  registerFeature(dataArchives);
  registerFeature(
      std::make_shared<GamebryoLocalSavegames>(this, "fo4londoncustom.ini"));
  registerFeature(std::make_shared<Fallout4LondonModDataChecker>(this));
  registerFeature(
      std::make_shared<Fallout4LondonModDataContent>(m_Organizer->gameFeatures()));
  registerFeature(std::make_shared<GamebryoSaveGameInfo>(this));
  registerFeature(std::make_shared<CreationGamePlugins>(moInfo));
  registerFeature(std::make_shared<Fallout4LondonUnmangedMods>(this));
  registerFeature(
      std::make_shared<Fallout4LondonBSAInvalidation>(dataArchives.get(), this));

  return true;
}

QString GameFallout4London::gameName() const
{
  return "Fallout 4 London";
}

void GameFallout4London::detectGame()
{
  m_GamePath    = identifyGamePath();
  m_MyGamesPath = determineMyGamesPath("Fallout4");
}

QString GameFallout4London::identifyGamePath() const
{
#ifdef _WIN32
  // TODO: Add GOG support
  QString path = "Software\\Bethesda Softworks\\Fallout4";
  return findInRegistry(HKEY_LOCAL_MACHINE, path.toStdWString().c_str(),
                        L"Installed Path");
#else
  return GameGamebryo::identifyGamePath();
#endif
}

QList<ExecutableInfo> GameFallout4London::executables() const
{
  return QList<ExecutableInfo>()
         << ExecutableInfo("F4SE",
                           findInGameFolder(m_Organizer->gameFeatures()
                                                ->gameFeature<MOBase::ScriptExtender>()
                                                ->loaderName()))
         << ExecutableInfo("Fallout 4 London", findInGameFolder(binaryName()))
         << ExecutableInfo("Fallout Launcher", findInGameFolder(getLauncherName()))
         << ExecutableInfo("Creation Kit", findInGameFolder("CreationKit.exe"))
                .withSteamAppId("1946160")
         << ExecutableInfo("LOOT", QFileInfo(getLootPath()))
                .withArgument("--game=\"Fallout4\"");
}

QList<ExecutableForcedLoadSetting> GameFallout4London::executableForcedLoads() const
{
  return QList<ExecutableForcedLoadSetting>();
}

QString GameFallout4London::name() const
{
  return "Fallout 4 London Support Plugin";
}

QString GameFallout4London::localizedName() const
{
  return tr("Fallout 4 London Support Plugin");
}

QString GameFallout4London::author() const
{
  return "MO2 Team";
}

QString GameFallout4London::description() const
{
  return tr("Adds support for the game Fallout 4 London.");
}

MOBase::VersionInfo GameFallout4London::version() const
{
  return VersionInfo(0, 0, 1, VersionInfo::RELEASE_PREALPHA);
}

QList<PluginSetting> GameFallout4London::settings() const
{
  return QList<PluginSetting>();
}

MappingType GameFallout4London::mappings() const
{
  MappingType result;
  if (testFilePlugins().isEmpty()) {
    for (const QString& profileFile : {"plugins.txt", "loadorder.txt"}) {
      result.push_back({m_Organizer->profilePath() + "/" + profileFile,
                        localAppFolder() + "/Fallout4/" + profileFile, false});
    }
  }
  return result;
}

void GameFallout4London::initializeProfile(const QDir& path,
                                           ProfileSettings settings) const
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

QString GameFallout4London::savegameExtension() const
{
  return "fos";
}

QString GameFallout4London::savegameSEExtension() const
{
  return "f4se";
}

std::shared_ptr<const GamebryoSaveGame>
GameFallout4London::makeSaveGame(QString filePath) const
{
  return std::make_shared<const Fallout4LondonSaveGame>(filePath, this);
}

QString GameFallout4London::steamAPPId() const
{
  return "377160";
}

QStringList GameFallout4London::testFilePlugins() const
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

QStringList GameFallout4London::primaryPlugins() const
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

QStringList GameFallout4London::enabledPlugins() const
{
  return {"bakaframework.esm", "londonworldspace.esm", "londonworldspace-dlcblock.esp"};
}

QStringList GameFallout4London::gameVariants() const
{
  return {"Regular"};
}

QString GameFallout4London::gameShortName() const
{
  return "Fallout4London";
}

QStringList GameFallout4London::validShortNames() const
{
  return {"Fallout4"};
}

QString GameFallout4London::gameNexusName() const
{
  return "fallout4london";
}

QString GameFallout4London::binaryName() const
{
  return "Fallout4.exe";
}

QString GameFallout4London::getLauncherName() const
{
  return "Fallout4Launcher.exe";
}

QStringList GameFallout4London::iniFiles() const
{
  return {"fallout4.ini", "fallout4prefs.ini", "fallout4custom.ini"};
}

QStringList GameFallout4London::DLCPlugins() const
{
  return {"dlcrobot.esm",
          "dlcworkshop01.esm",
          "dlccoast.esm",
          "dlcworkshop02.esm",
          "dlcworkshop03.esm",
          "dlcnukaworld.esm",
          "dlcultrahighresolution.esm"};
}

QStringList GameFallout4London::CCPlugins() const
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

IPluginGame::SortMechanism GameFallout4London::sortMechanism() const
{
  if (!testFilePresent())
    return IPluginGame::SortMechanism::LOOT;
  return IPluginGame::SortMechanism::NONE;
}

IPluginGame::LoadOrderMechanism GameFallout4London::loadOrderMechanism() const
{
  if (!testFilePresent())
    return IPluginGame::LoadOrderMechanism::PluginsTxt;
  return IPluginGame::LoadOrderMechanism::None;
}

int GameFallout4London::nexusModOrganizerID() const
{
  return 28715;
}

int GameFallout4London::nexusGameID() const
{
  return 1151;
}

// Start Diagnose
std::vector<unsigned int> GameFallout4London::activeProblems() const
{
  std::vector<unsigned int> result;
  if (m_Organizer->managedGame() == this) {
    if (testFilePresent())
      result.push_back(PROBLEM_TEST_FILE);
  }
  return result;
}

bool GameFallout4London::testFilePresent() const
{
  if (!testFilePlugins().isEmpty())
    return true;
  return false;
}

QString GameFallout4London::shortDescription(unsigned int key) const
{
  switch (key) {
  case PROBLEM_TEST_FILE:
    return tr("sTestFile entries are present");
  }
  return QString();
}

QString GameFallout4London::fullDescription(unsigned int key) const
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
