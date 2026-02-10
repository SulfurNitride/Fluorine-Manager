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

#include "filehash.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>

#ifndef MAX_PATH
#define MAX_PATH PATH_MAX
#endif  // MAX_PATH

static unsigned long genHashInt(const unsigned char* pos, const unsigned char* end)
{
  unsigned long hash = 0;
  for (; pos < end; ++pos) {
    hash *= 0x1003f;
    hash += *pos;
  }
  return hash;
}

/**
 * @brief calculateBSAHash
 * @param fileName
 * @return
 * @note the hash calculated for folders seem to be wrong
 */
BSAHash calculateBSAHash(const std::string& fileName)
{
  char fileNameLower[FILENAME_MAX + 1];
  int i = 0;
  for (; i < FILENAME_MAX && fileName[i] != '\0'; ++i) {
    fileNameLower[i] = tolower(fileName[i]);
    if (fileNameLower[i] == '/') {
      fileNameLower[i] = '\\';
    }
  }
  fileNameLower[i] = '\0';

  unsigned char* fileNameLowerU = reinterpret_cast<unsigned char*>(fileNameLower);

  size_t length = strlen(fileNameLower);

  char* ext = strrchr(fileNameLower, '.');
  if (ext == nullptr) {
    ext = fileNameLower + length;
  }

  size_t extLen = strlen(ext);
  length -= extLen;

  unsigned char* extU = reinterpret_cast<unsigned char*>(ext);

  BSAHash hash1 = 0ULL;

  if (length > 0) {
    hash1 = static_cast<BSAHash>(fileNameLowerU[length - 1] |
                                 ((length > 2 ? fileNameLowerU[length - 2] : 0) << 8) |
                                 (length << 16) | (fileNameLowerU[0] << 24));
  }

  if (extLen > 0) {
    if (strcmp(ext + 1, "kf") == 0) {
      hash1 |= 0x80;
    } else if (strcmp(ext + 1, "nif") == 0) {
      hash1 |= 0x8000;
    } else if (strcmp(ext + 1, "dds") == 0) {
      hash1 |= 0x8080;
    } else if (strcmp(ext + 1, "wav") == 0) {
      hash1 |= 0x80000000;
    }

    BSAHash hash2 = static_cast<BSAHash>(genHashInt(fileNameLowerU + 1, extU - 2)) +
                    static_cast<BSAHash>(genHashInt(extU, extU + strlen(ext)));

    hash1 |= (hash2 & 0xFFFFFFFF) << 32;
  }

  return hash1;
}
