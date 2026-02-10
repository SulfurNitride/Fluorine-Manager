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

#include <uibase/steamutility.h>

#include <QDir>
#include <QList>
#include <QRegularExpression>
#include <QSettings>
#include <QString>
#include <QTextStream>

namespace MOBase
{

// Lines that contains libraries are in the format:
//    "1" "Path\to\library"
static const QRegularExpression
    kSteamLibraryFilter("^\\s*\"(?<idx>[0-9]+)\"\\s*\"(?<path>.*)\"");

QString findSteam()
{
#ifdef _WIN32
  QSettings steamRegistry("Valve", "Steam");
  return steamRegistry.value("SteamPath").toString();
#else
  // On Linux, check common Steam locations
  QString homePath = QDir::homePath();
  QStringList candidates = {
    homePath + "/.steam/steam",
    homePath + "/.local/share/Steam",
    homePath + "/.steam/debian-installation"
  };
  for (const auto& path : candidates) {
    if (QDir(path).exists()) {
      return path;
    }
  }
  return "";
#endif
}

QString findSteamGame(const QString& appName, const QString& validFile)
{
  QStringList libraryFolders;  // list of Steam libraries to search
  QDir steamDir(findSteam());  // Steam installation directory

  // Can do nothing if Steam doesn't exist
  if (!steamDir.exists())
    return "";

  // The Steam install is always a valid library
  libraryFolders << steamDir.absolutePath();

  // Search libraryfolders.vdf for additional libraries
  QFile libraryFoldersFile(steamDir.absoluteFilePath("steamapps/libraryfolders.vdf"));
  if (libraryFoldersFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream in(&libraryFoldersFile);
    while (!in.atEnd()) {
      QString line                  = in.readLine();
      QRegularExpressionMatch match = kSteamLibraryFilter.match(line);
      if (match.hasMatch()) {
        QString folder = match.captured("path");
#ifdef _WIN32
        folder.replace("/", "\\").replace("\\\\", "\\");
#else
        folder.replace("\\\\", "/").replace("\\", "/");
#endif
        libraryFolders << folder;
      }
    }
  }

  // Search the Steam libraries for the game directory
  for (auto library : libraryFolders) {
    QDir libraryDir(library);
    if (!libraryDir.cd("steamapps/common/" + appName))
      continue;
    QString normalizedValidFile = validFile;
    normalizedValidFile.replace("\\", "/");
    if (normalizedValidFile.startsWith('/')) {
      normalizedValidFile.remove(0, 1);
    }
    if (normalizedValidFile.isEmpty() || libraryDir.exists(normalizedValidFile))
      return libraryDir.absolutePath();
  }

  return "";
}

}  // namespace MOBase
