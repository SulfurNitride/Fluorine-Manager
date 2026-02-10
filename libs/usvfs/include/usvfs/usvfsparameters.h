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

#include "dllimport.h"
#include "logging.h"
#include <chrono>

enum class CrashDumpsType : uint8_t
{
  None,
  Mini,
  Data,
  Full
};

extern "C"
{

  // deprecated, use usvfsParameters and usvfsCreateParameters()
  //
  struct USVFSParameters
  {
    char instanceName[65];
    char currentSHMName[65];
    char currentInverseSHMName[65];
    bool debugMode{false};
    LogLevel logLevel{LogLevel::Debug};
    CrashDumpsType crashDumpsType{CrashDumpsType::None};
    char crashDumpsPath[260];
  };

  struct usvfsParameters;

  DLLEXPORT usvfsParameters* usvfsCreateParameters();
  DLLEXPORT usvfsParameters* usvfsDupeParameters(usvfsParameters* p);
  DLLEXPORT void usvfsCopyParameters(const usvfsParameters* source,
                                     usvfsParameters* dest);
  DLLEXPORT void usvfsFreeParameters(usvfsParameters* p);

  DLLEXPORT void usvfsSetInstanceName(usvfsParameters* p, const char* name);
  DLLEXPORT void usvfsSetDebugMode(usvfsParameters* p, BOOL debugMode);
  DLLEXPORT void usvfsSetLogLevel(usvfsParameters* p, LogLevel level);
  DLLEXPORT void usvfsSetCrashDumpType(usvfsParameters* p, CrashDumpsType type);
  DLLEXPORT void usvfsSetCrashDumpPath(usvfsParameters* p, const char* path);
  DLLEXPORT void usvfsSetProcessDelay(usvfsParameters* p, int milliseconds);

  DLLEXPORT const char* usvfsLogLevelToString(LogLevel lv);
  DLLEXPORT const char* usvfsCrashDumpTypeToString(CrashDumpsType t);
}
