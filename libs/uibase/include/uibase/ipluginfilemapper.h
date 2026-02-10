/*
Mod Organizer shared UI functionality

Copyright (C) 2015 Sebastian Herbord. All rights reserved.

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

#ifndef IPLUGINFILEMAPPER_H
#define IPLUGINFILEMAPPER_H

#include "filemapping.h"
#include "iplugin.h"

namespace MOBase
{

/**
 * brief A plugin that adds virtual file links
 * This does not derive from IPlugin to prevent multiple inheritance issues.
 * For stand-alone mapping plugins, derive from IPlugin and IPluginDiagnose
 */
class IPluginFileMapper
{
public:
  /**
   * @return a list of file maps
   */
  virtual MappingType mappings() const = 0;
};

}  // namespace MOBase

Q_DECLARE_INTERFACE(MOBase::IPluginFileMapper,
                    "com.tannin.ModOrganizer.PluginFileMapper/2.0")

#endif  // IPLUGINDIAGNOSE_H
