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

#ifndef BSATYPES_H
#define BSATYPES_H

#include <fstream>
#include <string>

#include <dxgiformat.h>

#include <DDS.h>

#include "bsaexception.h"

#ifdef WIN32
#include <Windows.h>

typedef unsigned char BSAUChar;
typedef unsigned short BSAUShort;
typedef unsigned int BSAUInt;
typedef unsigned long BSAULong;
typedef unsigned long long BSAHash;

#else  // WIN32
#include <stdint.h>

typedef unsigned char BSAUChar;
typedef unsigned short BSAUShort;
typedef unsigned int BSAUInt;
typedef unsigned long BSAULong;
typedef unsigned long long BSAHash;

#endif  // WIN32

enum ArchiveType
{
  TYPE_MORROWIND,
  TYPE_OBLIVION,
  TYPE_FALLOUT3,
  TYPE_FALLOUTNV = TYPE_FALLOUT3,
  TYPE_SKYRIM    = TYPE_FALLOUT3,
  TYPE_SKYRIMSE,
  TYPE_FALLOUT4,
  TYPE_STARFIELD,
  TYPE_STARFIELD_LZ4_TEXTURE,
  TYPE_FALLOUT4NG_7,
  TYPE_FALLOUT4NG_8
};

struct MorrowindFileOffset
{
  BSAUInt size;
  BSAUInt offset;
};

struct FO4TextureHeader
{
  BSAUInt nameHash;
  char extension[4];
  BSAUInt dirHash;
  BSAUChar unknown1;
  BSAUChar chunkNumber;
  BSAUShort chunkHeaderSize;
  BSAUShort height;
  BSAUShort width;
  BSAUChar mipCount;
  DXGI_FORMAT format;
  bool isCubemap;
  BSAUChar unknown2;
};

struct FO4TextureChunk
{
  BSAHash offset;
  BSAUInt packedSize;
  BSAUInt unpackedSize;
  BSAUShort startMip;
  BSAUShort endMip;
  BSAUInt unknown;
};

template <typename T>
static T readType(std::fstream& file)
{
  union
  {
    char buffer[sizeof(T)];
    T value;
  };
  if (!file.read(buffer, sizeof(T))) {
    throw data_invalid_exception("can't read from bsa");
  }
  return value;
}

template <typename T>
static void writeType(std::fstream& file, const T& value)
{
  union
  {
    char buffer[sizeof(T)];
    T valueTemp;
  };
  valueTemp = value;

  file.write(buffer, sizeof(T));
}

std::string readBString(std::fstream& file);
void writeBString(std::fstream& file, const std::string& string);

std::string readZString(std::fstream& file);
void writeZString(std::fstream& file, const std::string& string);

#endif  // BSATYPES_H
