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

#ifndef MORROWINDLOCALSAVEGAMES_H
#define MORROWINDLOCALSAVEGAMES_H

#include <localsavegames.h>

#include "iplugingame.h"
#include <QDir>
#include <QString>

class MorrowindLocalSavegames : public MOBase::LocalSavegames
{

public:
  MorrowindLocalSavegames(const MOBase::IPluginGame* game);

  virtual MappingType mappings(const QDir& profileSaveDir) const override;
  virtual bool prepareProfile(MOBase::IProfile* profile) override;

private:
  const MOBase::IPluginGame* m_GamePlugin;
};

#endif  // MORROWINDLOCALSAVEGAMES_H
