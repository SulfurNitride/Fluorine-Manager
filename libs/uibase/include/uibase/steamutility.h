/*
Mod Organizer shared UI functionality

Copyright (C) 2019 MO2 Contributors. All rights reserved.

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

#ifndef STEAMUTILITY_H
#define STEAMUTILITY_H

#include "dllimport.h"
#include <QList>
#include <QString>

namespace MOBase
{

/**
 * @brief Gets the installation path to Steam according to the registy
 **/
QDLLEXPORT QString findSteam();

/**
 * @brief Gets the installation path to a Steam game
 *
 * @param appName The Steam application name, i.e., the expected steamapps folder name
 * @param validFile If the given file exists in the found installation path, the game is
          consider to be valid.  May be blank.
 **/
QDLLEXPORT QString findSteamGame(const QString& appName, const QString& validFile);

}  // namespace MOBase

#endif  // STEAMUTILITY_H
