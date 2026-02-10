/*
Mod Organizer shared UI functionality

Copyright (C) 2024 MO2 Team. All rights reserved.

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

#ifndef IPLUGINGAMEFEATURES_H
#define IPLUGINGAMEFEATURES_H

namespace MOBase
{

class BSAInvalidation;
class DataArchives;
class GamePlugins;
class LocalSavegames;
class ModDataChecker;
class ModDataContent;
class SaveGameInfo;
class ScriptExtender;
class UnmanagedMods;

/**
 * @brief A plugin to implement game features.
 *
 */
class IPluginGameFeautres
{
public:
  /**
   * Retrieve the priority of these game features.
   *
   * For mergeable features, e.g., ModDataChecker, this indicates the order in which the
   * features are merged, otherwise, it indicates which plugin should provide the
   * feature.
   *
   * The managed game always has lowest priority.
   *
   * @return the priority of this set of features.
   */
  virtual int priority() = 0;
};

}  // namespace MOBase

#endif  // IPLUGINDIAGNOSE_H
#pragma once
