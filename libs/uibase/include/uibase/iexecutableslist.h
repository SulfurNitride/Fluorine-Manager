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

#ifndef IEXECUTABLESLIST_H
#define IEXECUTABLESLIST_H

#include <QFileInfo>
#include <QString>

#include <generator>

#include "iexecutable.h"

namespace MOBase
{
/**
 * @brief Interface to the list of executables configured in Mod Organizer.
 */
class IExecutablesList
{
public:
  /**
   * @brief Retrieve all configured executables.
   *
   * @return a generator yielding all configured executables.
   */
  virtual std::generator<const IExecutable&> executables() const = 0;

  /**
   * @brief Retrieve an executable by its title.
   *
   * @param title Title of the executable to retrieve.
   *
   * @return the executable with the specified title, or nullptr if not found.
   */
  virtual const IExecutable* getByTitle(const QString& title) const = 0;

  /**
   * @brief Retrieve an executable by its binary file info.
   *
   * @param info File info of the executable binary to retrieve.
   *
   * @return the executable with the specified binary, or nullptr if not found.
   */
  virtual const IExecutable* getByBinary(const QFileInfo& info) const = 0;

  /**
   * @brief Check if an executable with the specified title exists.
   *
   * @param title Title of the executable to check.
   *
   * @return true if an executable with the specified title exists, false otherwise.
   */
  virtual bool contains(const QString& title) const = 0;
};
}  // namespace MOBase

#endif  // IEXECUTABLESLIST_H
