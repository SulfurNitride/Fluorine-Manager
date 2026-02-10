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

#ifndef IPLUGIN_H
#define IPLUGIN_H

#include "imoinfo.h"
#include "pluginrequirements.h"
#include "pluginsetting.h"
#include "versioninfo.h"
#include <QList>
#include <QObject>
#include <QString>
#include <vector>

namespace MOBase
{

class IPlugin
{
public:
  // For easier access in child class:
  using Requirements = PluginRequirementFactory;

public:
  virtual ~IPlugin() {}

  /**
   * @brief Initialize the plugin.
   *
   * Note that this function may never be called if no IOrganizer is available
   * at that time, such as when creating the first instance in MO. For proxy
   * plugins, init() is always called, but given a null IOrganizer.
   *
   * Plugins will probably want to store the organizer pointer. It is guaranteed
   * to be valid as long as the plugin is loaded.
   *
   * These functions may be called before init():
   *   - name()
   *   - see IPluginGame for more
   *
   * @return false if the plugin could not be initialized, true otherwise.
   */
  virtual bool init(IOrganizer* organizer) = 0;

  /**
   * this function may be called before init()
   *
   * @return the internal name of this plugin (used for example in the settings menu).
   *
   * @note Please ensure you use a name that will not change. Do NOT include a version
   * number in the name. Do NOT use a localizable string (tr()) here. Settings for
   * example are tied to this name, if you rename your plugin you lose settings users
   * made.
   */
  virtual QString name() const = 0;

  /**
   * @return the localized name for this plugin.
   *
   * @note Unlike name(), this method can (and should!) return a localized name for the
   * plugin.
   * @note This method returns name() by default.
   */
  virtual QString localizedName() const { return name(); }

  /**
   * @brief Retrieve the name of the master plugin of this plugin.
   *
   * It is often easier to implement a functionality as multiple plugins in MO2, but
   * ship the plugins together, e.g. as a Python module or using `createFunctions()`. In
   * this case, having a master plugin (one of the plugin, or a separate one) tells MO2
   * that these plugins are linked and should also be displayed together in the UI. If
   * MO2 ever implements automatic updates for plugins, the `master()` plugin will also
   * be used for this purpose.
   *
   * @return the name of the master plugin of this plugin, or an empty string if this
   * plugin does not have a master.
   */
  virtual QString master() const { return ""; }

  /**
   * @brief Retrieve the requirements for the plugins.
   *
   * This method is called right after init().
   *
   * @return the requirements for this plugin.
   */
  virtual std::vector<std::shared_ptr<const IPluginRequirement>> requirements() const
  {
    return {};
  }

  /**
   * @return the author of this plugin.
   */
  virtual QString author() const = 0;

  /**
   * @return a short description of the plugin to be displayed to the user.
   */
  virtual QString description() const = 0;

  /**
   * @return the version of the plugin. This can be used to detect outdated versions of
   * plugins.
   */
  virtual VersionInfo version() const = 0;

  /**
   * @return the list of configurable settings for this plugin (in the user interface).
   * The list may be empty.
   *
   * @note Plugin can store "hidden" (from the user) settings using
   * IOrganizer::persistent / IOrganizer::setPersistent.
   */
  virtual QList<PluginSetting> settings() const = 0;

  /**
   * @return whether the plugin should be enabled by default
   */
  virtual bool enabledByDefault() const { return true; }
};

}  // namespace MOBase

Q_DECLARE_INTERFACE(MOBase::IPlugin, "com.tannin.ModOrganizer.Plugin/2.0")

#endif  // IPLUGIN_H
