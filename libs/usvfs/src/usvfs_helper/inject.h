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

#include "usvfsparameters.h"
#include <string>
#include <windows_sane.h>

namespace usvfs
{

/**
 * @brief inject usvfs to a process
 * @param applicationPath
 * @param parameters
 * @param processInfo
 */
void injectProcess(const std::wstring& applicationPath,
                   const usvfsParameters& parameters,
                   const PROCESS_INFORMATION& processInfo);

/**
 * @brief inject usvfs to a process
 * @param applicationPath path to usvfs
 * @param parameters
 * @param process process handle to inject to
 * @param thread main thread inside that process. This can be set to
 * INVALID_HANDLE_VALUE in which case a new thread is created in the process
 */
void injectProcess(const std::wstring& applicationPath,
                   const usvfsParameters& parameters, HANDLE process, HANDLE thread);

}  // namespace usvfs
