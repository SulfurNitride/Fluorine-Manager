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

#ifndef IEXECUTABLE_H
#define IEXECUTABLE_H

#include <QFileInfo>
#include <QString>

namespace MOBase
{
class IExecutable
{
public:  // Information found in ExecutableInfo
  /**
   * @return the title of the executable.
   */
  virtual const QString& title() const = 0;

  /**
   * @return the file info of the executable binary.
   */
  virtual const QFileInfo& binaryInfo() const = 0;

  /**
   * @return the arguments to be passed to the executable.
   *
   * @note This API might be changed in the future to return a QStringList instead.
   */
  virtual const QString& arguments() const = 0;

  /**
   * @return the Steam App ID associated with this executable, or an empty string if
   * there is none.
   */
  virtual const QString& steamAppID() const = 0;

  /**
   * @return the working directory for the executable.
   */
  virtual const QString& workingDirectory() const = 0;

public:  // Information found in flags
  /**
   * @return true if the executable is shown on the toolbar.
   */
  virtual bool isShownOnToolbar() const = 0;

  /**
   * @return true if the executable's application icon is used for desktop shortcuts.
   */
  virtual bool usesOwnIcon() const = 0;

  /**
   * @return true if Mod Organizer should minimize to the system tray while this
   * executable is running.
   */
  virtual bool minimizeToSystemTray() const = 0;

  /**
   * @return true if this executable is hidden in the user interface.
   */
  virtual bool hide() const = 0;
};
}  // namespace MOBase

#endif  // IEXECUTABLE_H
