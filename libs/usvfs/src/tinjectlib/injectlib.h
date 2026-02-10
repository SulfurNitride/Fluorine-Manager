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

namespace InjectLib
{

/**
 * @brief inject a dll into the target process
 * @param processHandle handle of the process to inject to
 * @param threadHandle handle of the main thread in the process
 * @param dllname name/path of the dll to inject. The path can't be longer than MAX_PATH
 * characters
 * @param initFunction name of the initialization function. Can't be longer than 20
 * characters. If this is null, no function is called. Important: the init function has
 * to exist, be exported, take the two userdata parameters and must be __cdecl calling
 * convention on 32bit windows! Failing any of these the target process will crash or
 * the dll isn't loaded Why __cdecl? Because otherwise we would need .def files to
 * export the init function with a GetProcAddress-able function name.
 * @param userData data passed to the init function
 * @param userDataSize size of the data to be passed
 * @param skipInit skip the call to the init function if the named function wasn't found
 * in the dll. If false, the target process will crash if the function isn't exported in
 * the dll
 */
void InjectDLL(HANDLE processHandle, HANDLE threadHandle, LPCWSTR dllName,
               LPCSTR initFunction = nullptr, LPCVOID userData = nullptr,
               size_t userDataSize = 0, bool skipInit = false);

}  // namespace InjectLib
