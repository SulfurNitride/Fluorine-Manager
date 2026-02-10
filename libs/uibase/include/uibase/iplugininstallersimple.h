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

#ifndef IPLUGININSTALLERSIMPLE_H
#define IPLUGININSTALLERSIMPLE_H

#include "guessedvalue.h"
#include "iplugininstaller.h"

namespace MOBase
{

/**
 * Simple installer for mods. Simple installers only deal with an in-memory structure
 * representing the archive and can modify what to install where by editing this
 * structure. Actually extracting the archive is handled by the caller Plugins
 * implementing this interface have to contain the following line in the class
 * declaration: Q_Interfaces(IPlugin IPluginInstaller IPluginInstallerCustom)
 */
class IPluginInstallerSimple : public QObject, public IPluginInstaller
{

  Q_INTERFACES(IPluginInstaller)

public:
  /**
   * install call for the simple mode. The installer only needs to restructure the tree
   * parameter, the caller does the rest
   * @param modName name of the mod to install. As an input parameter this is the
   * suggested name (i.e. from meta data) The installer may change this parameter to
   * rename the mod)
   * @param tree in-memory representation of the archive content
   * @param version version of the mod. May be empty if the version is not yet known.
   * Can be updated if the plugin can determine the version
   * @param nexusID id of the mod or -1 if unknown. May be updated if the plugin can
   * determine the mod id
   * @return the result of the installation process. If "ERROR_NOTATTEMPTED" is
   * returned, further installers will work with the modified tree. This may be useful
   * when implementing a sort of filter, but usually tree should remain unchanged in
   * that case.
   */
  virtual EInstallResult install(GuessedValue<QString>& modName,
                                 std::shared_ptr<IFileTree>& tree, QString& version,
                                 int& nexusID) = 0;
};

}  // namespace MOBase

Q_DECLARE_INTERFACE(MOBase::IPluginInstallerSimple,
                    "com.tannin.ModOrganizer.PluginInstallerSimple/1.0")

#endif  // IPLUGININSTALLERSIMPLE_H
