/*
Userspace Virtual Filesystem

Copyright (C) 2015 Sebastian Herbord. All rights reserved.

This file is part of usvfs.

usvfs is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

usvfs is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with usvfs. If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once

#include <windows_sane.h>

namespace HookLib
{

/// \brief reimplementation of GetProcAddress to circumvent foreign hooks of
/// GetProcAddress like AcLayer
/// \param module handle to the module that contains the function or variable
/// \param functionName function to retrieve the address of
/// \return address of the exported function
FARPROC MyGetProcAddress(HMODULE module, LPCSTR functionName);

}  // namespace HookLib
