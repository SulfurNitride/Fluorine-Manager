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

#ifndef GAMEBRYOLOCALSAVEGAMES_H
#define GAMEBRYOLOCALSAVEGAMES_H

#include <localsavegames.h>

#include <QDir>
#include <QString>

class GameGamebryo;

class GamebryoLocalSavegames : public MOBase::LocalSavegames
{

public:
  GamebryoLocalSavegames(const GameGamebryo* game, const QString& iniFileName);

  virtual MappingType mappings(const QDir& profileSaveDir) const override;
  virtual bool prepareProfile(MOBase::IProfile* profile) override;

protected:
  // return the path from the local game directory to the local saves folder
  //
  // this is virtual so game plugins for complete game overhauld (Enderal, Nehrim, etc.)
  // can override it properly
  //
  virtual QString localSavesDummy() const;

  QDir localSavesDirectory() const;
  QDir localGameDirectory() const;

private:
  const GameGamebryo* m_Game;
  QString m_IniFileName;
};

#endif  // GAMEBRYOLOCALSAVEGAMES_H
