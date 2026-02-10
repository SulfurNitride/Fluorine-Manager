#ifndef IPLUGINGAME_H
#define IPLUGINGAME_H

#include "executableinfo.h"
#include "iplugin.h"
#include "isavegame.h"

class QIcon;
class QUrl;
class QString;

#include <any>
#include <cstdint>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace MOBase
{

// Game plugins can be loaded without an IOrganizer being available, in which
// case detectGame() is called, but not init().
//
// These functions may be called before init() (after detectGame()):
//   - gameName()
//   - isInstalled()
//   - gameIcon()
//   - gameDirectory()
//   - dataDirectory()
//   - gameVariants()
//   - looksValid()
//   - see IPlugin::init() for more
//
//
class IPluginGame : public QObject, public IPlugin
{
  Q_INTERFACES(IPlugin)

public:
  enum class LoadOrderMechanism
  {
    None,
    FileTime,
    PluginsTxt
  };

  enum class SortMechanism
  {
    NONE,
    MLOX,
    BOSS,
    LOOT
  };

  enum ProfileSetting
  {
    MODS            = 0x01,
    CONFIGURATION   = 0x02,
    SAVEGAMES       = 0x04,
    PREFER_DEFAULTS = 0x08
  };

  Q_DECLARE_FLAGS(ProfileSettings, ProfileSetting)

public:
  // Game plugin should not have requirements:
  std::vector<std::shared_ptr<const IPluginRequirement>>
  requirements() const final override
  {
    return {};
  }

  // Game plugin can not be disabled
  bool enabledByDefault() const final override { return true; }

  /**
   * this function may be called before init()
   *
   * @return name of the game
   */
  virtual QString gameName() const = 0;

  /**
   * used to override display-specific text in place of the gameName
   * for example the text for the main window or the initial instance creation game
   * plugin selection/list
   *
   * added for future translation purposes or to make plugins visually more accurate in
   * the ui
   *
   * @return display-specific name of the game
   */
  virtual QString displayGameName() const { return gameName(); }

  /**
   * @brief Detect the game.
   *
   * This method is the first method called for game plugins (before init()). The
   * following methods can be called after detectGame() but before init():
   * - gameName()
   * - isInstalled()
   * - gameIcon()
   * - gameDirectory()
   * - dataDirectory()
   * - gameVariants()
   * - looksValid()
   * - see IPlugin::init() for more
   */
  virtual void detectGame() = 0;

  /**
   * @brief initialize a profile for this game
   * @param directory the directory where the profile is to be initialized
   * @param settings parameters for how the profile should be initialized
   * @note the MO app does not yet support virtualizing only specific aspects but
   * plugins should be written with this future functionality in mind
   * @note this function will be used to initially create a profile, potentially to
   * repair it or upgrade/downgrade it so the implementations have to gracefully handle
   * the case that the directory already contains files!
   */
  virtual void initializeProfile(const QDir& directory,
                                 ProfileSettings settings) const = 0;

  /**
   * @brief List save games in the specified folder.
   *
   * @param folder The folder containing the saves.
   *
   * @return the list of saves in the specified folder.
   */
  /**
   * @return file extension of save games for this game
   */
  virtual std::vector<std::shared_ptr<const ISaveGame>>
  listSaves(QDir folder) const = 0;

  /**
   * this function may be called before init()
   *
   * @return true if this game has been discovered as installed, false otherwise
   */
  virtual bool isInstalled() const = 0;

  /**
   * this function may be called before init()
   *
   * @return an icon for this game
   */
  virtual QIcon gameIcon() const = 0;

  /**
   * this function may be called before init()
   *
   * @return directory to the game installation
   */
  virtual QDir gameDirectory() const = 0;

  /**
   * this function may be called before init()
   *
   * @return directory where the game expects to find its data files
   */
  virtual QDir dataDirectory() const = 0;

  virtual QString modDataDirectory() const { return ""; }

  /**
   * this function may be called before init()
   *
   * @return directories where we may find data files outside the main location
   */
  virtual QMap<QString, QDir> secondaryDataDirectories() const { return {}; }

  /**
   * @brief set the path to the managed game
   * @param path to the game
   * @note this will be called by by MO to set the concrete path of the game. This is
   * particularly relevant if the path wasn't auto-detected but had to be set manually
   * by the user
   */
  virtual void setGamePath(const QString& path) = 0;

  /**
   * @return directory of the documents folder where configuration files and such for
   * this game reside
   */
  virtual QDir documentsDirectory() const = 0;

  /**
   * @return path to where save games are stored.
   */
  virtual QDir savesDirectory() const = 0;

  /**
   * @return list of automatically discovered executables of the game itself and tools
   * surrounding it
   */
  virtual QList<ExecutableInfo> executables() const { return {}; }

  /**
   * @brief Get the default list of libraries that can be force loaded with executables
   */
  virtual QList<ExecutableForcedLoadSetting> executableForcedLoads() const = 0;

  /**
   * @return steam app id for this game. Should be empty for games not available on
   * steam
   * @note if a game is available in multiple versions those might have different app
   * ids. the plugin should try to return the right one
   */
  virtual QString steamAPPId() const { return ""; }

  /**
   * @return list of plugins that are part of the game and not considered optional
   */
  virtual QStringList primaryPlugins() const { return {}; }

  /**
   * @return list of plugins enabled by the game but not in a strict load order
   */
  virtual QStringList enabledPlugins() const { return {}; }

  /**
   * this function may be called before init()
   *
   * @return list of game variants
   * @note if there are multiple variants of a game (and the variants make a difference
   * to the plugin) like a regular one and a GOTY-edition the plugin can return a list
   * of them and the user gets to chose which one he owns.
   */
  virtual QStringList gameVariants() const { return {}; }

  /**
   * @brief if there are multiple game variants (returned by gameVariants) this will get
   * called on start with the user-selected game edition
   * @param variant the game edition selected by the user
   */
  virtual void setGameVariant(const QString& variant) = 0;

  /**
   * @brief Get the name of the executable that gets run
   */
  virtual QString binaryName() const = 0;

  /**
   * @brief Get the 'short' name of the game
   *
   * the short name of the game is used for - save ames, registry entries and
   * nexus mod pages as far as I can see.
   */
  virtual QString gameShortName() const = 0;

  /**
   * @brief game name that's passed to the LOOT cli --game argument
   *
   * only applicable for games using LOOT based sorting
   * defaults to gameShortName()
   */
  virtual QString lootGameName() const { return gameShortName(); }

  /**
   * @brief Get any primary alternative 'short' name for the game
   *
   * this is used to determine if a Nexus (or other) download source should be
   * considered a 'primary' source for the game so that it isn't flagged as an
   * alternative source
   */
  virtual QStringList primarySources() const { return {}; }

  /**
   * @brief Get any valid 'short' name for the game
   *
   * this is used to determine if a Nexus download is valid for the current game
   * not all game variants have their own nexus pages and others can handle downloads
   * from other nexus game pages and should be allowed
   *
   * the short name should be considered the primary handler for a directly supported
   * game for puroses of auto-launching an instance
   */
  virtual QStringList validShortNames() const { return {}; }

  /**
   * @brief Get the 'short' name of the game
   *
   * the short name of the game is used for - save ames, registry entries and
   * nexus mod pages as far as I can see.
   */
  virtual QString gameNexusName() const { return ""; }

  /**
   * @brief Get the list of .ini files this game uses
   *
   * @note It is important that the 'main' .ini file comes first in this list
   */
  virtual QStringList iniFiles() const { return {}; }

  /**
   * @brief Get a list of esp/esm files that are part of known dlcs
   */
  virtual QStringList DLCPlugins() const { return {}; }

  /**
   * @brief Get the current list of active Creation Club plugins
   */
  virtual QStringList CCPlugins() const { return {}; }

  /*
   * @brief determine the load order mechanism used by this game.
   *
   * @note this may throw an exception if the mechanism can't be determined
   */
  virtual LoadOrderMechanism loadOrderMechanism() const
  {
    return LoadOrderMechanism::None;
  }

  /**
   * @brief determine the sorting mech
   */
  virtual SortMechanism sortMechanism() const { return SortMechanism::NONE; }

  /**
   * @brief Get the Nexus ID of Mod Organizer
   */
  virtual int nexusModOrganizerID() const { return 0; }

  /**
   * @brief Get the Nexus Game ID
   */
  virtual int nexusGameID() const = 0;

  /**
   * this function may be called before init()
   *
   * @brief See if the supplied directory looks like a valid game
   */
  virtual bool looksValid(QDir const&) const = 0;

  /**
   * @brief Get version of program
   */
  virtual QString gameVersion() const = 0;

  /**
   * @brief Get the name of the game launcher
   */
  virtual QString getLauncherName() const = 0;

  /**
   * @brief Get a URL for the support page for the game
   */
  virtual QString getSupportURL() const { return ""; }

  /**
   * @brief Gets a virtualization mapping for mod directories
   *
   * @note Maps internal mod directories to a list of paths
   * @default Root directory maps to game data path(s)
   */
  virtual QMap<QString, QStringList> getModMappings() const
  {
    QMap<QString, QStringList> map;
    QStringList dataDirs = {dataDirectory().absolutePath()};
    for (auto path : secondaryDataDirectories()) {
      dataDirs.append(path.absolutePath());
    }
    map[""] = dataDirs;
    return map;
  }
};

Q_DECLARE_OPERATORS_FOR_FLAGS(IPluginGame::ProfileSettings)

}  // namespace MOBase

Q_DECLARE_INTERFACE(MOBase::IPluginGame, "com.tannin.ModOrganizer.PluginGame/2.0")
Q_DECLARE_METATYPE(MOBase::IPluginGame const*)

#endif  // IPLUGINGAME_H
