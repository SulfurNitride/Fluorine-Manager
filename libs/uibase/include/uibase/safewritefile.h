/*
Copyright (C) 2014 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef SAFEWRITEFILE_H
#define SAFEWRITEFILE_H

#include "dllimport.h"
#include "utility.h"
#include <QSaveFile>
#include <QString>

namespace MOBase
{

/**
 * @brief a wrapper for QSaveFile that handles errors when opening the file to reduce
 * code duplication
 */
class QDLLEXPORT SafeWriteFile
{
public:
  SafeWriteFile(const QString& fileName);

  QSaveFile* operator->();

private:
  QSaveFile m_SaveFile;
};

}  // namespace MOBase

#endif  // SAFEWRITEFILE_H
