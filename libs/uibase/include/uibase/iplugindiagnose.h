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

#ifndef IPLUGINDIAGNOSE_H
#define IPLUGINDIAGNOSE_H

#include <QList>
#include <QObject>
#include <QString>
#include <functional>
#include <vector>

namespace MOBase
{

/**
 * @brief A plugin that creates problem reports to be displayed in the UI.
 * This can be used to report problems related to the same plugin (which implements
 * further interfaces) or as a stand-alone diagnosis tool. This does not derive from
 * IPlugin to prevent multiple inheritance issues. For stand-alone diagnosis plugins,
 * derive from IPlugin and IPluginDiagnose
 */
class IPluginDiagnose
{
public:
  /**
   * @return a list of keys of active problems
   * @note this is not expected to be called if isActive returns false
   */
  virtual std::vector<unsigned int> activeProblems() const = 0;

  /**
   * @brief retrieve a short description for the specified problem for the overview
   * page. HTML syntax is supported.
   * @param key the identifier of the problem as reported by activeProblems()
   * @return a (short) description text
   * @throw should throw an exception if the key is not valid
   */
  virtual QString shortDescription(unsigned int key) const = 0;

  /**
   * @brief retrieve the full description for the specified problem. HTML syntax is
   * supported.
   * @param key the identifier of the problem as reported by activeProblems()
   * @return a (long) description text
   * @throw should throw an exception if the key is not valid
   */
  virtual QString fullDescription(unsigned int key) const = 0;

  /**
   * @param key the identifier of the problem as reported by activeProblems()
   * @return true if this plugin provides a guide to fix the issue
   * @throw should throw an exception if the key is not valid
   */
  virtual bool hasGuidedFix(unsigned int key) const = 0;

  /**
   * @brief start the guided fix for the specified problem
   * @param key the identifier of the problem as reported by activeProblems()
   * @throw should throw an exception if the key is not valid or if there is no guided
   * fix for the issue
   */
  virtual void startGuidedFix(unsigned int key) const = 0;

  /**
   * @brief Register the callback to be called when this plugin is invalidated.
   *
   * Only one callback can be activate at a time.
   */
  void onInvalidated(std::function<void()> callback) { m_OnInvalidated = callback; }

  virtual ~IPluginDiagnose() {}

protected:
  void invalidate() { m_OnInvalidated(); }

private:
  std::function<void()> m_OnInvalidated;
};

}  // namespace MOBase

Q_DECLARE_INTERFACE(MOBase::IPluginDiagnose,
                    "com.tannin.ModOrganizer.PluginDiagnose/1.1")

#endif  // IPLUGINDIAGNOSE_H
