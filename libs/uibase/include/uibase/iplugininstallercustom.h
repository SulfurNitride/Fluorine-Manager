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

#ifndef IPLUGININSTALLERCUSTOM_H
#define IPLUGININSTALLERCUSTOM_H

#include "guessedvalue.h"
#include "iplugininstaller.h"

namespace MOBase
{

/**
 * Custom installer for mods. Custom installers receive the archive name and have to go
 * from there. They have to be able to extract the archive themself.
 * Plugins implementing this interface have to contain the following line in the class
 * declaration: Q_Interfaces(IPlugin IPluginInstaller IPluginInstallerCustom)
 */
class IPluginInstallerCustom : public QObject, public IPluginInstaller
{

  Q_INTERFACES(IPluginInstaller)

public:
  /**
   * @brief test if the archive represented by the file name can be installed through
   * this installer
   * @param filename of the archive
   * @return true if this installer can handle the archive
   * @note this is only called if the archive couldn't be opened by the caller,
   * otherwise IPluginInstaller::isArchiveSupported(const DirectoryTree &tree) is called
   */
  virtual bool isArchiveSupported(const QString& archiveName) const = 0;

  /**
   * @return returns a list of file extensions that may be supported by this installer
   */
  virtual std::set<QString> supportedExtensions() const = 0;

  /**
   * install call
   * @param modName name of the mod to install. As an input parameter this is the
   * suggested name (i.e. from meta data) The installer may change this parameter to
   * rename the mod)
   * @param filename of the archive
   * @param version version of the mod. May be empty if the version is not yet known.
   * The plugin is responsible for setting the version on the created mod
   * @param nexusID id of the mod or -1 if unknown. The plugin is responsible for
   * setting the mod id for the created mod
   * @return result of the installation process
   */
  virtual EInstallResult install(GuessedValue<QString>& modName, QString gameName,
                                 const QString& archiveName, const QString& version,
                                 int nexusID) = 0;
};

}  // namespace MOBase

Q_DECLARE_INTERFACE(MOBase::IPluginInstallerCustom,
                    "com.tannin.ModOrganizer.PluginInstallerCustom/1.0")

#endif  // IPLUGININSTALLERCUSTOM_H
