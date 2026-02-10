/*
Mod Organizer BSA handling

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

#include "bsatypes.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <fstream>
#include <string>

using std::fstream;

/*#ifndef MAX_PATH
#define MAX_PATH PATH_MAX
#endif // MAX_PATH
*/

std::string readBString(fstream& file)
{
  unsigned char length = readType<unsigned char>(file);
  char buffer[256];
  if (length > 0) {
    if (!file.read(buffer, length)) {
      throw data_invalid_exception("can't read from bsa");
    }
  }
  buffer[length] = '\0';
  return std::string(buffer);
}

void writeBString(fstream& file, const std::string& string)
{
  unsigned int length =
      std::min<unsigned int>(static_cast<unsigned int>(string.length()), 255);
  writeType<unsigned char>(file, length + 1);
  file.write(string.c_str(), length + 1);
}

std::string readZString(fstream& file)
{
  char buffer[FILENAME_MAX];
  memset(buffer, '\0', FILENAME_MAX);
  if (!file.getline(buffer, FILENAME_MAX, '\0')) {
    throw data_invalid_exception("can't read from bsa");
  }
  return std::string(buffer);
}

void writeZString(fstream& file, const std::string& string)
{
  file.write(string.c_str(), string.length() + 1);
}
