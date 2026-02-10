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
#include "ntdll_declarations.h"
#include <cassert>

#define LOAD_EXT(mod, name)                                                            \
  name = reinterpret_cast<name##_type>(::GetProcAddress(mod, #name));                  \
  assert(name != nullptr)

NtQueryDirectoryFile_type NtQueryDirectoryFile;
NtQueryDirectoryFileEx_type NtQueryDirectoryFileEx;
NtQueryFullAttributesFile_type NtQueryFullAttributesFile;
NtQueryAttributesFile_type NtQueryAttributesFile;
NtQueryObject_type NtQueryObject;
NtQueryInformationFile_type NtQueryInformationFile;
NtQueryInformationByName_type NtQueryInformationByName;
NtOpenFile_type NtOpenFile;
NtCreateFile_type NtCreateFile;
NtClose_type NtClose;
RtlDoesFileExists_U_type RtlDoesFileExists_U;
RtlDosPathNameToRelativeNtPathName_U_WithStatus_type
    RtlDosPathNameToRelativeNtPathName_U_WithStatus;
RtlReleaseRelativeName_type RtlReleaseRelativeName;
RtlGetVersion_type RtlGetVersion;
NtTerminateProcess_type NtTerminateProcess;

static bool ntdll_initialized;

void ntdll_declarations_init()
{
  if (!ntdll_initialized) {
    HMODULE ntDLLMod = GetModuleHandleW(L"ntdll.dll");

    LOAD_EXT(ntDLLMod, NtQueryDirectoryFile);
    LOAD_EXT(ntDLLMod, NtQueryDirectoryFileEx);
    LOAD_EXT(ntDLLMod, NtQueryFullAttributesFile);
    LOAD_EXT(ntDLLMod, NtQueryAttributesFile);
    LOAD_EXT(ntDLLMod, NtQueryObject);
    LOAD_EXT(ntDLLMod, NtQueryInformationFile);
    LOAD_EXT(ntDLLMod, NtQueryInformationByName);
    LOAD_EXT(ntDLLMod, NtCreateFile);
    LOAD_EXT(ntDLLMod, NtOpenFile);
    LOAD_EXT(ntDLLMod, NtClose);
    LOAD_EXT(ntDLLMod, RtlDoesFileExists_U);
    LOAD_EXT(ntDLLMod, RtlDosPathNameToRelativeNtPathName_U_WithStatus);
    LOAD_EXT(ntDLLMod, RtlReleaseRelativeName);
    LOAD_EXT(ntDLLMod, RtlGetVersion);
    LOAD_EXT(ntDLLMod, NtTerminateProcess);

    ntdll_initialized = true;
  }
}

static struct __Initializer
{
  __Initializer() { ntdll_declarations_init(); }
} __initializer;
