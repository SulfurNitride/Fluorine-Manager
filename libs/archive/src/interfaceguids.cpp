/*
Mod Organizer archive handling

Copyright (C) 2012 Sebastian Herbord, 2020 MO2 Team. All rights reserved.

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

// This file instantiates the GUIDs needed for linking with the 7zip code
#ifdef _WIN32
#include <initguid.h>
#else
#define INITGUID
#include <Common/MyWindows.h>
#endif

// extractcallback, opencallback
#include <7zip/IPassword.h>

// archive, opencallback
// Note: In a sense, this is unnecessary as IArchive.h includes it
#include <7zip/IStream.h>

// archive, opencallback
#include <7zip/Archive/IArchive.h>
