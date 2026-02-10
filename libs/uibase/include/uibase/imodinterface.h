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

#ifndef IMODINTERFACE_H
#define IMODINTERFACE_H

#include <memory>
#include <set>
#include <utility>

#include <QColor>
#include <QDateTime>
#include <QList>
#include <QString>

namespace MOBase
{

class VersionInfo;
class IFileTree;

enum class EndorsedState
{
  ENDORSED_FALSE,
  ENDORSED_TRUE,
  ENDORSED_UNKNOWN,
  ENDORSED_NEVER
};

enum class TrackedState
{
  TRACKED_FALSE,
  TRACKED_TRUE,
  TRACKED_UNKNOWN,
};

class IModInterface
{
public:
  virtual ~IModInterface() {}

public:  // Non-meta related information:
  /**
   * @return the name of the mod.
   */
  virtual QString name() const = 0;

  /**
   * @return the absolute path to the mod to be used in file system operations.
   */
  virtual QString absolutePath() const = 0;

public:  // Meta-related information:
  /**
   * @return the comments for this mod, if any.
   */
  virtual QString comments() const = 0;

  /**
   * @return the notes for this mod, if any.
   */
  virtual QString notes() const = 0;

  /**
   * @brief Retrieve the short name of the game associated with this mod. This may
   * differ from the current game plugin (e.g. you can install a Skyrim LE game in a SSE
   *     installation).
   *
   * @return the name of the game associated with this mod.
   */
  virtual QString gameName() const = 0;

  /**
   * @return the name of the repository from which this mod was installed.
   */
  virtual QString repository() const = 0;

  /**
   * @return the Nexus ID of this mod.
   */
  virtual int nexusId() const = 0;

  /**
   * @return the current version of this mod.
   */
  virtual VersionInfo version() const = 0;

  /**
   * @return the newest version of thid mod (as known by MO2). If this matches
   * version(), then the mod is up-to-date.
   */
  virtual VersionInfo newestVersion() const = 0;

  /**
   * @return the ignored version of this mod (for update), or an invalid version if the
   * user did not ignore version for this mod.
   */
  virtual VersionInfo ignoredVersion() const = 0;

  /**
   * @return the absolute path to the file that was used to install this mod.
   */
  virtual QString installationFile() const = 0;

  virtual std::set<std::pair<int, int>> installedFiles() const = 0;

  /**
   * @return true if this mod was marked as converted by the user.
   *
   * @note When a mod is for a different game, a flag is shown to users to warn them,
   * but they can mark mods as converted to remove this flag.
   */
  virtual bool converted() const = 0;

  /**
   * @return true if th is mod was marked as containing valid game data.
   *
   * @note MO2 uses ModDataChecker to check the content of mods, but sometimes these
   * fail, in which case mods are incorrectly marked as 'not containing valid games
   * data'. Users can choose to mark these mods as valid to hide the warning / flag.
   */
  virtual bool validated() const = 0;

  /**
   * @return the color of the 'Notes' column chosen by the user.
   */
  virtual QColor color() const = 0;

  /**
   * @return the URL of this mod, or an empty QString() if no URL is associated
   *     with this mod.
   */
  virtual QString url() const = 0;

  /**
   * @return the ID of the primary category of this mod.
   */
  virtual int primaryCategory() const = 0;

  /**
   * @return the list of categories this mod belongs to.
   */
  virtual QStringList categories() const = 0;

  /**
   * @return the mod author.
   */
  virtual QString author() const = 0;

  /**
   * @return the mod uploader.
   */
  virtual QString uploader() const = 0;

  /**
   * @return the URL of the uploader.
   */
  virtual QString uploaderUrl() const = 0;

  /**
   * @return the tracked state of this mod.
   */
  virtual TrackedState trackedState() const = 0;

  /**
   * @return the endorsement state of this mod.
   */
  virtual EndorsedState endorsedState() const = 0;

  /**
   * @brief Retrieve a file tree corresponding to the underlying disk content
   *     of this mod.
   *
   * The file tree should not be cached since it is already cached and updated when
   * required.
   *
   * @return a file tree representing the content of this mod.
   */
  virtual std::shared_ptr<const MOBase::IFileTree> fileTree() const = 0;

  /**
   * @return true if this object represents the overwrite mod.
   */
  virtual bool isOverwrite() const = 0;

  /**
   * @return true if this object represents a backup.
   */
  virtual bool isBackup() const = 0;

  /**
   * @return true if this object represents a separator.
   */
  virtual bool isSeparator() const = 0;

  /**
   * @return true if this object represents a foreign mod.
   */
  virtual bool isForeign() const = 0;

public:  // Mutable operations:
  /**
   * @brief set/change the version of this mod
   * @param version new version of the mod
   */
  virtual void setVersion(const VersionInfo& version) = 0;

  /**
   * @brief sets the installation file for this mod
   * @param fileName archive file name
   */
  virtual void setInstallationFile(const QString& fileName) = 0;

  /**
   * @brief set/change the latest known version of this mod
   * @param version newest known version of the mod
   */
  virtual void setNewestVersion(const VersionInfo& version) = 0;

  /**
   * @brief set endorsement state of the mod
   * @param endorsed new endorsement state
   */
  virtual void setIsEndorsed(bool endorsed) = 0;

  /**
   * @brief sets the mod id on nexus for this mod
   * @param the new id to set
   */
  virtual void setNexusID(int nexusID) = 0;

  /**
   * @brief sets the category id from a nexus category id. Conversion to MO id happens
   * internally
   * @param categoryID the nexus category id
   * @note if a mapping is not possible, the category is set to the default value
   */
  virtual void addNexusCategory(int categoryID) = 0;

  /**
   * @brief assign a category to the mod. If the named category doesn't exist it is
   * created
   * @param categoryName name of the new category
   */
  virtual void addCategory(const QString& categoryName) = 0;

  /**
   * @brief unassign a category from this mod.
   * @param categoryName name of the category to be removed
   * @return true if the category was removed successfully, false if no such category
   * was assigned
   */
  virtual bool removeCategory(const QString& categoryName) = 0;

  /**
   * @brief set/change the source game of this mod
   *
   * @param gameName the source game shortName
   */
  virtual void setGameName(const QString& gameName) = 0;

  /**
   * @brief Set a URL for this mod.
   *
   * @param url The URL of this mod.
   */
  virtual void setUrl(const QString& url) = 0;

public:  // Plugin operations:
         /**
          * @brief Retrieve the specified setting in this mod for a plugin.
          *
          * @param pluginName Name of the plugin for which to retrieve a setting. This should
          * always be IPlugin::name()        unless you have a really good reason to access
          * settings        of another plugin.
          * @param key Identifier of the setting.
          * @param defaultValue The default value to return if the setting does not exist.
          *
          * @return the setting, if found, or the default value.
          */
  virtual QVariant pluginSetting(const QString& pluginName, const QString& key,
                                 const QVariant& defaultValue = QVariant()) const = 0;

  /**
   * @brief Retrieve the settings in this mod for a plugin.
   *
   * @param pluginName Name of the plugin for which to retrieve settings. This should
   * always be IPlugin::name() unless you have a really good reason to access settings
   * of another plugin.
   *
   * @return a map from setting key to value. The map is empty if there are not settings
   * for this mod.
   */
  virtual std::map<QString, QVariant>
  pluginSettings(const QString& pluginName) const = 0;

  /**
   * @brief Set the specified setting in this mod for a plugin.
   *
   * @param pluginName Name of the plugin for which to retrieve a setting. This should
   * always be IPlugin::name() unless you have a really good reason to access settings
   * of another plugin.
   * @param key Identifier of the setting.
   * @param value New value for the setting to set.
   *
   * @return true if the setting was set correctly, false otherwise.
   */
  virtual bool setPluginSetting(const QString& pluginName, const QString& key,
                                const QVariant& value) = 0;

  /**
   * @brief Remove all the settings of the specified plugin from th is mod.
   *
   * @param pluginName Name of the plugin for which settings should be removed. This
   * should always be IPlugin::name() unless you have a really good reason to access
   * settings of another plugin.
   *
   * @return the old settings from the given plugin, as returned by `pluginSettings()`.
   */
  virtual std::map<QString, QVariant>
  clearPluginSettings(const QString& pluginName) = 0;
};

}  // namespace MOBase

#endif  // IMODINTERFACE_H
