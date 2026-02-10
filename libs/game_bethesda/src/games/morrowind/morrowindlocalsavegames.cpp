/*
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

#include "morrowindlocalsavegames.h"
#include <QtDebug>
#include <iprofile.h>
#include <stddef.h>
#include <string>

MorrowindLocalSavegames::MorrowindLocalSavegames(const MOBase::IPluginGame* game)
    : m_GamePlugin(game)
{}

bool MorrowindLocalSavegames::prepareProfile(MOBase::IProfile* profile)
{
  bool dirty = false;

  if (profile->localSavesEnabled()) {
    if (m_GamePlugin->gameDirectory().exists("Saves")) {
      if (!m_GamePlugin->gameDirectory().rename("Saves", "_Saves")) {
        qCritical("Unable to enable Morrowind local save games!");
      }
      dirty = true;
    }
  } else {
    if (m_GamePlugin->gameDirectory().exists("_Saves")) {
      if (!m_GamePlugin->gameDirectory().rename("_Saves", "Saves")) {
        qCritical("Unable to disable Morrowind local save games!");
      }
      dirty = true;
    }
  }

  return dirty;
}

MappingType MorrowindLocalSavegames::mappings(const QDir& profileSaveDir) const
{
  return {{profileSaveDir.absolutePath(),
           m_GamePlugin->gameDirectory().absoluteFilePath("Saves"), true, true}};
}
