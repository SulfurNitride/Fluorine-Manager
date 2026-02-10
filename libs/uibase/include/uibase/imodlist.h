/*
Mod Organizer shared UI functionality

Copyright (C) 2013 Sebastian Herbord. All rights reserved.

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

#ifndef IMODLIST_H
#define IMODLIST_H

#include <functional>
#include <map>

#include <QList>
#include <QString>

#include "imodinterface.h"
#include "iprofile.h"

namespace MOBase
{

/**
 * @brief interface to the mod-list
 * @note all api functions in this interface work need the internal name of a mod to
 * find a mod. For regular mods (mods the user installed) the display name (as shown to
 * the user) and internal name are identical. For other mods (non-MO mods) there is
 * currently no way to translate from display name to internal name because the display
 *   name might not me un-ambiguous.
 */
class IModList
{

public:
  enum ModState
  {
    STATE_EXISTS    = 0x00000001,
    STATE_ACTIVE    = 0x00000002,
    STATE_ESSENTIAL = 0x00000004,
    STATE_EMPTY     = 0x00000008,
    STATE_ENDORSED  = 0x00000010,
    STATE_VALID     = 0x00000020,
    STATE_ALTERNATE = 0x00000040
  };

  Q_DECLARE_FLAGS(ModStates, ModState)

public:
  virtual ~IModList() {}

  /**
   * @brief retrieves the display name of a mod from it's internal name
   * @param internalName the internal name
   * @return a string intended to identify the name to the user
   * @note If you received an internal name from an api (i.e. IPluginList::origin) then
   * you should use that name to identify the mod to all other api calls but use this
   * function to retrieve the name to show to the user.
   */
  virtual QString displayName(const QString& internalName) const = 0;

  /**
   * @brief Retrieve a list of all installed mod names.
   *
   * @return list of mods (internal names).
   */
  virtual QStringList allMods() const = 0;

  /**
   * @brief Retrieve the list of installed mod names, sorted by current profile
   * priority.
   *
   * @param profile The profile to use for the priority. If nullptr, the current
   *     profile is used.
   *
   * @return list of mods (internal names), sorted by priority.
   */
  virtual QStringList
  allModsByProfilePriority(MOBase::IProfile* profile = nullptr) const = 0;

  /**
   * @brief Retrieve the mod with the given name.
   *
   * @param name Name of the mod to retrieve.
   *
   * @return the mod with the given name, or a null pointer if there is no mod with this
   * name.
   */
  virtual IModInterface* getMod(const QString& name) const = 0;

  /**
   * @brief Remove a mod (from disc and from the ui).
   *
   * @param mod The mod to remove.
   *
   * @return true on success, false on error.
   */
  virtual bool removeMod(MOBase::IModInterface* mod) = 0;

  /**
   * @brief Rename the given mod.
   *
   * This invalidate the mod so you should use the returned value afterwards.
   *
   * @param mod The mod to rename.
   *
   * @return the new mod (after renaming) on success, a null pointer on error.
   */
  virtual MOBase::IModInterface* renameMod(MOBase::IModInterface* mod,
                                           const QString& name) = 0;

  /**
   * @brief retrieve the state of a mod
   * @param name name of the mod
   * @return a bitset of information about the mod
   */
  virtual ModStates state(const QString& name) const = 0;

  /**
   * @brief enable or disable a mod
   *
   * @param name name of the mod
   * @param active if true the mod is enabled, otherwise it's disabled
   *
   * @return true on success, false if the mod name is not valid
   *
   * @note calling this will cause MO to re-evaluate its virtual file system so this is
   * fairly expensive
   */
  virtual bool setActive(const QString& name, bool active) = 0;

  /**
   * @brief enable or disable a list of mod
   *
   * @param names names of the mod
   * @param active if true mods are enabled, otherwise they are disabled
   *
   * @return the number of mods successfully enabled or disabled
   *
   * @note calling this will cause MO to re-evaluate its virtual file system so this is
   * fairly expensive
   */
  virtual int setActive(const QStringList& names, bool active) = 0;

  /**
   * @brief retrieve the priority of a mod
   * @param name name of the mod
   * @return the priority (the higher the more important). Returns -1 if the mod doesn't
   * exist
   */
  virtual int priority(const QString& name) const = 0;

  /**
   * @brief change the priority of a mod
   * @param name name of the mod
   * @param newPriority new priority of the mod
   * @return true on success, false if the priority change was not possible. This is
   * usually because one of the parameters is invalid
   * @note Very important: newPriority is the new priority after the move. Keep in mind
   * that the mod disappears from it's old location and all mods with higher priority
   * than the moved mod decrease in priority by one.
   */
  virtual bool setPriority(const QString& name, int newPriority) = 0;

  /**
   * @brief Add a new callback to be called when a new mod is installed.
   *
   * Parameters of the callback:
   *   - The installed mod.
   *
   * @param func Function to called when a mod has been installed.
   */
  virtual bool onModInstalled(const std::function<void(IModInterface*)>& func) = 0;

  /**
   * @brief Add a new callback to be called when a mod is removed.
   *
   * Parameters of the callback:
   *   - The name of the removed mod.
   *
   * @param func Function to called when a mod has been removed.
   */
  virtual bool onModRemoved(const std::function<void(QString const&)>& func) = 0;

  /**
   * @brief Installs a handler for the event that the state of mods changed
   * (enabled/disabled, endorsed, ...).
   *
   * Parameters of the callback:
   *   - Map containing the mods whose states have changed. Keys are mod names and
   * values are mod states.
   *
   * @param func The signal to be called when the state of any mod changes.
   *
   * @return true if the handler was successfully installed, false otherwise (there is
   * as of now no known reason this should fail).
   */
  virtual bool onModStateChanged(
      const std::function<void(const std::map<QString, ModStates>&)>& func) = 0;

  /**
   * installs a handler for the event that a mod changes priority
   * @param func the signal to be called when the priority of a mod changes
   *        (first parameter for the handler is the name of the mod, second is the old
   * priority, third the new one)
   * @return true if the handler was successfully installed (there is as of now no known
   * reason this should fail)
   */
  virtual bool
  onModMoved(const std::function<void(const QString&, int, int)>& func) = 0;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(IModList::ModStates)

}  // namespace MOBase

#endif  // IMODLIST_H
