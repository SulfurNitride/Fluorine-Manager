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

#include "gamebryolocalsavegames.h"
#include "registry.h"
#include <QtDebug>
#include <iprofile.h>
#include <stddef.h>
#include <string>

#include "gamegamebryo.h"

GamebryoLocalSavegames::GamebryoLocalSavegames(const GameGamebryo* game,
                                               const QString& iniFileName)
    : m_Game{game}, m_IniFileName(iniFileName)
{}

MappingType GamebryoLocalSavegames::mappings(const QDir& profileSaveDir) const
{
  return {{profileSaveDir.absolutePath(), localSavesDirectory().absolutePath(), true,
           true}};
}

QString GamebryoLocalSavegames::localSavesDummy() const
{
  return "__MO_Saves\\";
}

QDir GamebryoLocalSavegames::localSavesDirectory() const
{
  QString dummy = localSavesDummy();
  dummy.replace("\\", "/");
  return QDir(m_Game->myGamesPath()).absoluteFilePath(dummy);
}

QDir GamebryoLocalSavegames::localGameDirectory() const
{
  return QDir(m_Game->myGamesPath()).absolutePath();
}

QString GamebryoLocalSavegames::routingIniName() const
{
  return m_IniFileName;
}

QByteArray GamebryoLocalSavegames::routingPath() const
{
  return localSavesDummy().toUtf8();
}

bool GamebryoLocalSavegames::prepareProfile(MOBase::IProfile* profile)
{
#ifndef _WIN32
  // Fluorine owns Wine save routing as a launch-scoped transaction. Persisting
  // __MO_Saves here would mutate the prefix during profile selection, outside
  // the prefix lease, and would leave another owner for launch teardown to
  // guess. mappings() remains the authoritative save-directory contract.
  Q_UNUSED(profile);
  return false;
#else
  bool enable = profile->localSavesEnabled();

  QString basePath    = profile->localSettingsEnabled()
                            ? profile->absolutePath()
                            : localGameDirectory().absolutePath();
  QString iniFilePath = basePath + "/" + m_IniFileName;
  QString saveIni     = profile->absolutePath() + "/" + "savepath.ini";

  // Get the current sLocalSavePath
  QString currentPath =
      GameGamebryo::readIniValue(iniFilePath, "General", "sLocalSavePath", "SKIP_ME");
  bool alreadyEnabled = (currentPath == localSavesDummy());

  // Get the current bUseMyGamesDirectory
  QString currentMyGames = GameGamebryo::readIniValue(
      iniFilePath, "General", "bUseMyGamesDirectory", "SKIP_ME");

  // Create the __MO_Saves directory if local saves are enabled and it doesn't exist
  if (enable) {
    QDir saves = localSavesDirectory();
    if (!saves.exists()) {
      saves.mkdir(".");
    }
  }

  // Set the path to __MO_Saves if it's not already
  if (enable && !alreadyEnabled) {
    // If the path is not blank, save it to savepath.ini
    if (currentPath != "SKIP_ME") {
      MOBase::WriteRegistryValue("General", "sLocalSavePath", currentPath, saveIni);
    }
    if (currentMyGames != "SKIP_ME") {
      MOBase::WriteRegistryValue("General", "bUseMyGamesDirectory", currentMyGames,
                                 saveIni);
    }
    MOBase::WriteRegistryValue("General", "sLocalSavePath", localSavesDummy(),
                               iniFilePath);
    MOBase::WriteRegistryValue("General", "bUseMyGamesDirectory", "1", iniFilePath);
  }

  // Get rid of the local saves setting if it's still there
  if (!enable && alreadyEnabled) {
    // If savepath.ini exists, use it and delete it
    if (QFile::exists(saveIni)) {
      QString savedPath =
          GameGamebryo::readIniValue(saveIni, "General", "sLocalSavePath", "DELETE_ME");
      QString savedMyGames = GameGamebryo::readIniValue(
          saveIni, "General", "bUseMyGamesDirectory", "DELETE_ME");
      if (savedPath != "DELETE_ME") {
        MOBase::WriteRegistryValue("General", "sLocalSavePath", savedPath, iniFilePath);
      } else {
        MOBase::RemoveRegistryValue("General", "sLocalSavePath", iniFilePath);
      }
      if (savedMyGames != "DELETE_ME") {
        MOBase::WriteRegistryValue("General", "bUseMyGamesDirectory", savedMyGames,
                                   iniFilePath);
      } else {
        MOBase::RemoveRegistryValue("General", "bUseMyGamesDirectory", iniFilePath);
      }
      QFile::remove(saveIni);
    }
    // Otherwise just delete the setting
    else {
      MOBase::RemoveRegistryValue("General", "sLocalSavePath", iniFilePath);
      MOBase::RemoveRegistryValue("General", "bUseMyGamesDirectory", iniFilePath);
    }
  }

  return enable != alreadyEnabled;
#endif
}
