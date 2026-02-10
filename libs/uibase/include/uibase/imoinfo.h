/*
Mod Organizer shared UI functionality

Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 3 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef IMOINFO_H
#define IMOINFO_H

#include <QDir>
#include <QList>
#include <QMainWindow>
#include <QString>
#include <QVariant>
#include <cstdint>
#include <any>
#include <functional>
#include <memory>

// Include utility.h for HANDLE/DWORD typedefs on Linux
#include "utility.h"

#include "game_features/game_feature.h"
#include "guessedvalue.h"
#include "imodlist.h"
#include "iprofile.h"
#include "versioninfo.h"
#include "versioning.h"

namespace MOBase
{

class IExecutablesList;
class IFileTree;
class IModInterface;
class IModRepositoryBridge;
class IDownloadManager;
class IInstanceManager;
class IPluginList;
class IPlugin;
class IPluginGame;
class IGameFeatures;

/**
 * @brief Interface to class that provides information about the running session
 *        of Mod Organizer to be used by plugins
 *
 * When MO requires plugins but does not have a valid instance loaded (such as
 * on first start in the instance creation dialog), init() will not be called at
 * all, except for proxy plugins.
 *
 * In the case of proxy plugins, init() is called with a null IOrganizer.
 */
class QDLLEXPORT IOrganizer : public QObject
{
  Q_OBJECT

public:
  /**
   * @brief information about a virtualised file
   */
  struct FileInfo
  {
    QString filePath;  /// full path to the file
    QString archive;   /// name of the archive if this file is in a BSA, otherwise this
                       /// is empty.
    QStringList origins;  /// list of origins containing this file. the first one is the
                          /// highest priority one
  };

public:
  virtual ~IOrganizer() {}

  // the directory for plugin data, typically plugins/data
  //
  static QString getPluginDataPath();

  /**
   * @return create a new nexus interface class
   */
  virtual IModRepositoryBridge* createNexusBridge() const = 0;

  /**
   * @return the name of the current instance (the directory name or "Portable" for
   * portable instances)
   */
  virtual QString instanceName() const = 0;

  /**
   * @return name of the active profile or an empty string if no profile is loaded (yet)
   */
  virtual QString profileName() const = 0;

  /**
   * @return the (absolute) path to the active profile or an empty string if no profile
   * is loaded (yet)
   */
  virtual QString profilePath() const = 0;

  /**
   * @return the (absolute) path to the download directory
   */
  virtual QString downloadsPath() const = 0;

  /**
   * @return the (absolute) path to the overwrite directory
   */
  virtual QString overwritePath() const = 0;

  /**
   * @return the (absolute) path to the base directory
   */
  virtual QString basePath() const = 0;

  /**
   * @return the (absolute) path to the mods directory
   */
  virtual QString modsPath() const = 0;

  /**
   * @return the running version of Mod Organizer
   */
  [[deprecated]] virtual VersionInfo appVersion() const = 0;

  /**
   * @return the running version of Mod Organizer
   */
  virtual Version version() const = 0;

  /**
   * @brief create a new mod with the specified name
   * @param name name of the new mod
   * @return an interface that can be used to modify the mod. nullptr if the user
   * canceled
   * @note a popup asking the user to merge, rename or replace the mod is displayed if
   * the mod already exists. That has to happen on the main thread and MO2 will deadlock
   * if it happens on any other. If this needs to be called from another thread, use
   * IModList::getMod() to verify the mod-name is unused first
   */
  virtual IModInterface* createMod(GuessedValue<QString>& name) = 0;

  /**
   * @brief get the game plugin matching the specified game
   * @param gameName name of the game short name
   * @return a game plugin, or nullptr if there is no match
   */
  virtual IPluginGame* getGame(const QString& gameName) const = 0;

  /**
   * @brief let the organizer know that a mod has changed
   * @param the mod that has changed
   */
  virtual void modDataChanged(IModInterface* mod) = 0;

  /**
   * @brief Check if a plugin is enabled.
   *
   * @param pluginName Plugin to check.
   *
   * @return true if the plugin is enabled, false otherwise.
   */
  virtual bool isPluginEnabled(IPlugin* plugin) const = 0;

  /**
   * @brief Check if a plugin is enabled.
   *
   * @param pluginName Name of the plugin to check.
   *
   * @return true if the plugin is enabled, false otherwise.
   */
  virtual bool isPluginEnabled(QString const& pluginName) const = 0;

  /**
   * @brief Retrieve the specified setting for a plugin.
   *
   * @param pluginName Name of the plugin for which to retrieve a setting. This should
   * always be IPlugin::name() unless you have a really good reason to access settings
   * of another mod. You can not access settings for a plugin that isn't installed.
   * @param key identifier of the setting
   * @return the setting
   * @note an invalid qvariant is returned if the setting has not been declared
   */
  virtual QVariant pluginSetting(const QString& pluginName,
                                 const QString& key) const = 0;

  /**
   * @brief Set the specified setting for a plugin.
   *
   * This automatically emit pluginSettingChanged(), so you do not have to do it
   * yourself.
   *
   * @param pluginName Name of the plugin for which to change a value. This should
   * always be IPlugin::name() unless you have a really good reason to access data of
   * another mod AND if you can verify that plugin is actually installed.
   * @param key Identifier of the setting.
   * @param value Value to set.
   *
   * @throw an exception is thrown if pluginName doesn't refer to an installed plugin.
   */
  virtual void setPluginSetting(const QString& pluginName, const QString& key,
                                const QVariant& value) = 0;

  /**
   * @brief retrieve the specified persistent value for a plugin
   * @param pluginName name of the plugin for which to retrieve a value. This should
   * always be IPlugin::name() unless you have a really good reason to access data of
   * another mod.
   * @param key identifier of the value
   * @param def default value to return if the key is not (yet) set
   * @return the value
   * @note A persistent is an arbitrary value that the plugin can set and retrieve that
   * is persistently stored by the main application. There is no UI for the user to
   * change this value but (s)he can directly access the storage
   */
  virtual QVariant persistent(const QString& pluginName, const QString& key,
                              const QVariant& def = QVariant()) const = 0;

  /**
   * @brief set the specified persistent value for a plugin
   * @param pluginName name of the plugin for which to change a value. This should
   * always be IPlugin::name() unless you have a really good reason to access data of
   * another mod AND if you can verify that plugin is actually installed
   * @param key identifier of the value
   * @param value value to set
   * @param sync if true the storage is immediately written to disc. This costs
   * performance but is safer against data loss
   * @throw an exception is thrown if pluginName doesn't refer to an installed plugin
   */
  virtual void setPersistent(const QString& pluginName, const QString& key,
                             const QVariant& value, bool sync = true) = 0;

  /**
   * @return path to a directory where plugin data should be stored.
   */
  virtual QString pluginDataPath() const = 0;

  /**
   * @brief install a mod archive at the specified location
   * @param fileName absolute file name of the mod to install
   * @param nameSuggestion suggested name for this mod. This can still be changed by the
   * user
   * @return interface to the newly installed mod or nullptr if no installation took
   * place (failure or use canceled
   */
  virtual IModInterface* installMod(const QString& fileName,
                                    const QString& nameSuggestion = QString()) = 0;

  /**
   * @brief resolves a path relative to the virtual data directory to its absolute real
   * path
   * @param fileName path to resolve
   * @return the absolute real path or an empty string
   */
  virtual QString resolvePath(const QString& fileName) const = 0;

  /**
   * @brief retrieves a list of (virtual) subdirectories for a path (relative to the
   * data directory)
   * @param directoryName relative path to the directory to list
   * @return a list of directory names
   */
  virtual QStringList listDirectories(const QString& directoryName) const = 0;

  /**
   * @brief find files in the virtual directory matching the filename filter
   * @param path the path to search in
   * @param filter filter function to match against
   * @return a list of matching files
   */
  virtual QStringList
  findFiles(const QString& path,
            const std::function<bool(const QString&)>& filter) const = 0;

  /**
   * @brief find files in the virtual directory matching the filename filter
   * @param path the path to search in
   * @param filters list of glob filters to match against
   * @return a list of matching files
   */
  virtual QStringList findFiles(const QString& path,
                                const QStringList& filters) const = 0;

  /**
   * @brief retrieve the file origins for the speicified file. The origins are listed
   * with their internal name
   * @return list of origins that contain the specified file, sorted by their priority
   * @note the internal name of a mod can differ from the display name for
   * disambiguation
   */
  virtual QStringList getFileOrigins(const QString& fileName) const = 0;

  /**
   * @brief find files in the virtual directory matching the specified complex filter
   * @param path the path to search in
   * @param filter filter function to match against
   * @return a list of matching files
   * @note this function is more expensive than the one filtering by name so use the
   * other one if it suffices
   */
  virtual QList<FileInfo>
  findFileInfos(const QString& path,
                const std::function<bool(const FileInfo&)>& filter) const = 0;

  /**
   * @return a IFileTree representing the virtual file tree.
   */
  virtual std::shared_ptr<const MOBase::IFileTree> virtualFileTree() const = 0;

  /**
   * @return interface to the instance manager
   */
  virtual IInstanceManager* instanceManager() const = 0;

  /**
   * @return interface to the download manager
   */
  virtual IDownloadManager* downloadManager() const = 0;

  /**
   * @return interface to the list of plugins (esps, esms, and esls)
   */
  virtual IPluginList* pluginList() const = 0;

  /**
   * @return interface to the list of mods
   */
  virtual IModList* modList() const = 0;

  /**
   * @return interface to the list of executables
   */
  virtual IExecutablesList* executablesList() const = 0;

  /**
   * @return interface to the active profile
   */
  virtual std::shared_ptr<IProfile> profile() const = 0;

  /**
   * @return list of names of all profiles
   */
  virtual QStringList profileNames() const = 0;

  /**
   * @brief retrieve a profile by name
   *
   * @param name name of the profile
   *
   * @return the profile with the specified name or nullptr if not found
   */
  virtual std::shared_ptr<const IProfile> getProfile(const QString& name) const = 0;

  /**
   * @return interface to game features.
   */
  virtual IGameFeatures* gameFeatures() const = 0;

  /**
   * @brief runs a program using the virtual filesystem
   *
   * @param executable  either the name of an executable configured in MO, or
   *                    a path to an executable; if relative, it is resolved
   *                    against the game directory
   *
   * @param args        arguments to pass to the executable; if this is empty
   *                    and `executable` refers to a configured executable,
   *                    its arguments are used
   *
   * @param cwd         working directory for the executable; if this is empty,
   *                    it is set to either the cwd set by the user in the
   *                    configured executable (if any) or the directory of the
   *                    executable
   *
   * @param profile     name of the profile to use; defaults to the active
   *                    profile
   *
   * @param forcedCustomOverwrite  the name of the mod to set as the custom
   *                               overwrite directory, regardless of what the
   *                               profile has configured
   *
   * @param ignoreCustomOverwrite  if `executable` is the name of a configured
   *                               executable, ignores the executable's custom
   *                               overwrite
   *
   * @return a handle to the process that was started or INVALID_HANDLE_VALUE
   *         if the application failed to start.
   */
  virtual HANDLE startApplication(const QString& executable,
                                  const QStringList& args = QStringList(),
                                  const QString& cwd = "", const QString& profile = "",
                                  const QString& forcedCustomOverwrite = "",
                                  bool ignoreCustomOverwrite           = false) = 0;

  /**
   * @brief blocks until the given process has completed
   *
   * @param handle     the process to wait for
   * @param refresh    whether MO should refresh after the process completed
   * @param exitCode   the exit code of the process after it ended
   *
   * @return true if the process completed successfully
   *
   * @note this will always show the lock overlay, regardless of whether the
   *       user has disabled locking in the setting, so use this with care;
   *       note that the lock overlay will always allow the user to unlock, in
   *       which case this will return false
   */
  virtual bool waitForApplication(HANDLE handle, bool refresh = true,
                                  LPDWORD exitCode = nullptr) const = 0;

  /**
   * @brief Refresh the internal mods file structure from disk. This includes the mod
   * list, the plugin list, data tab and other smaller things like problems button (same
   * as pressing F5).
   *
   * @note The main part of the refresh of the mods file strcuture, modlist and
   * pluginlist is done asynchronously, so you should not expect them to be up-to-date
   * when this function returns.
   *
   * @param saveChanges If true, the relevant profile information is saved first
   * (enabled mods and their order).
   */
  virtual void refresh(bool saveChanges = true) = 0;

  /**
   * @brief get the currently managed game info
   */
  virtual MOBase::IPluginGame const* managedGame() const = 0;

  /**
   * @brief Add a new callback to be called when an application is about to be run.
   *
   * Parameters of the callback:
   *   - Path (absolute) to the application to be run.
   *   - [Optional] Working directory for the run.
   *   - [Optional] Argument for the binary.
   *
   * The callback can return false to prevent the application from being launched.
   *
   * @param func Function to be called when an application is run.
   */
  virtual bool onAboutToRun(const std::function<bool(const QString&)>& func) = 0;
  virtual bool onAboutToRun(
      const std::function<bool(const QString&, const QDir&, const QString&)>& func) = 0;

  /**
   * @brief Add a new callback to be called when an has finished running.
   *
   * Parameters of the callback:
   *   - Path (absolute) to the application that has finished running.
   *   - Exit code of the application.
   *
   *
   * @param func Function to be called when an application is run.
   */
  virtual bool
  onFinishedRun(const std::function<void(const QString&, unsigned int)>& func) = 0;

  /**
   * @brief Add a new callback to be called when the user interface has been
   * initialized.
   *
   * Parameters of the callback:
   *   - The  main window of the application.
   *
   * @param func Function to be called when the user interface has been initialized.
   */
  virtual bool
  onUserInterfaceInitialized(std::function<void(QMainWindow*)> const& func) = 0;

  /**
   * @brief Add a new callback to be called when the next refresh finishes (or
   * immediately if possible).
   *
   * @param func Callback.
   * @param immediateIfPossible If true and no refresh is in progress, the callback will
   * be called immediately.
   */
  virtual bool onNextRefresh(std::function<void()> const& func,
                             bool immediateIfPossible = true) = 0;

  /**
   * @brief Add a new callback to be called when a new profile is created.
   *
   * Parameters of the callback:
   *   - The created profile (can be a temporary object, so it should not be stored).
   *
   * @param func Function to be called when a profile is created.
   *
   */
  virtual bool onProfileCreated(std::function<void(IProfile*)> const& func) = 0;

  /**
   * @brief Add a new callback to be called when a profile is renamed.
   *
   * Parameters of the callback:
   *   - The renamed profile.
   *   - The old name of the profile.
   *   - The new name of the profile.
   *
   * @param func Function to be called when a profile is renamed.
   *
   */
  virtual bool onProfileRenamed(
      std::function<void(IProfile*, QString const&, QString const&)> const& func) = 0;

  /**
   * @brief Add a new callback to be called when a profile is removed.
   *
   * Parameters of the callback:
   *   - The name of the removed profile.
   *
   * The function is called after the profile has been removed, so the profile is not
   * accessible anymore.
   *
   * @param func Function to be called when a profile is removed.
   *
   */
  virtual bool onProfileRemoved(std::function<void(QString const&)> const& func) = 0;

  /**
   * @brief Add a new callback to be called when the current profile is changed.
   *
   * Parameters of the callback:
   *   - The old profile. Can be a null pointer if no profile was set (e.g. at startup).
   *   - The new profile, cannot be null.
   *
   * The function is called when the profile is changed but some operations related to
   * the profile might not be finished when this is called (e.g., the virtual file
   * system might not be up-to-date).
   *
   * @param func Function to be called when the current profile change.
   *
   */
  virtual bool
  onProfileChanged(std::function<void(IProfile*, IProfile*)> const& func) = 0;

  /**
   * @brief Add a new callback to be called when a plugin setting is changed.
   *
   * Parameters of the callback:
   *   - Name of the plugin.
   *   - Name of the setting.
   *   - Old value of the setting. Can be a default-constructed (invalid) QVariant if
   * the setting did not exist before.
   *   - New value of the setting. Can be a default-constructed (invalid) QVariant if
   * the setting has been removed.
   *
   * @param func Function to be called when a plugin setting is changed.
   */
  virtual bool onPluginSettingChanged(
      std::function<void(QString const&, const QString& key, const QVariant&,
                         const QVariant&)> const& func) = 0;

  /**
   * @brief Add a new callback to be called when a plugin is enabled.
   *
   * Parameters of the callback:
   *   - The enabled plugin.
   *
   * @param func Function to be called when a plugin is enabled.
   */
  virtual bool onPluginEnabled(std::function<void(const IPlugin*)> const& func) = 0;

  /**
   * @brief Add a new callback to be called when the specified plugin is enabled.
   *
   * @param name Name of the plugin to watch.
   * @param func Function to be called when a plugin is enabled.
   */
  virtual bool onPluginEnabled(const QString& pluginName,
                               std::function<void()> const& func) = 0;

  /**
   * @brief Add a new callback to be called when a plugin is disabled.
   *
   * Parameters of the callback:
   *   - The disabled plugin.
   *
   * @param func Function to be called when a plugin is disabled.
   */
  virtual bool onPluginDisabled(std::function<void(const IPlugin*)> const& func) = 0;

  /**
   * @brief Add a new callback to be called when the specified plugin is disabled.
   *
   * @param name Name of the plugin to watch.
   * @param func Function to be called when a plugin is disabled.
   */
  virtual bool onPluginDisabled(const QString& pluginName,
                                std::function<void()> const& func) = 0;
};

}  // namespace MOBase

namespace MOBase::details
{
// called from MO
QDLLEXPORT void setPluginDataPath(const QString& s);
}  // namespace MOBase::details

#endif  // IMOINFO_H
