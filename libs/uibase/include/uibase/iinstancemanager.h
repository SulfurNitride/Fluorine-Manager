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

#ifndef IINSTANCEMANAGER_H
#define IINSTANCEMANAGER_H

#include <QDir>
#include <QString>

#include <memory>
#include <vector>

namespace MOBase
{
class IInstance;

/**
 * @brief Interface to the instance manager of Mod Organizer.
 */
class IInstanceManager
{
public:
  /**
   * @return the current instance.
   */
  virtual std::shared_ptr<IInstance> currentInstance() const = 0;

  /**
   * @return The list of absolute paths to all global instances.
   *
   * @note This does not include portable instances.
   */
  virtual std::vector<QDir> globalInstancePaths() const = 0;

  /**
   * @brief Retrieve the global instance corresponding to the given name.
   *
   * @param instanceName Name of the global instance to retrieve. This is the directory
   * name of the instance.
   *
   * @return the global instance corresponding to the given name, or nullptr if no such
   * instance exists.
   */
  virtual std::shared_ptr<const IInstance>
  getGlobalInstance(const QString& instanceName) const = 0;
};
}  // namespace MOBase

#endif  // IINSTANCEMANAGER_H
