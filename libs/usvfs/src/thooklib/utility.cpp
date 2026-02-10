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
#include "utility.h"
#include "exceptionex.h"
#include <cstdlib>

namespace HookLib
{

FARPROC MyGetProcAddress(HMODULE module, LPCSTR functionName)
{
  // determine position of the exports of the module
  PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)module;
  if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
    return nullptr;
  }

  PIMAGE_NT_HEADERS ntHeaders =
      (PIMAGE_NT_HEADERS)(((LPBYTE)dosHeader) + dosHeader->e_lfanew);
  if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
    return nullptr;
  }

  PIMAGE_OPTIONAL_HEADER optionalHeader = &ntHeaders->OptionalHeader;
  if (optionalHeader->NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT) {
    return nullptr;
  }
  PIMAGE_DATA_DIRECTORY dataDirectory =
      &optionalHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
  PIMAGE_EXPORT_DIRECTORY exportDirectory =
      (PIMAGE_EXPORT_DIRECTORY)((LPBYTE)dosHeader + dataDirectory->VirtualAddress);

  ULONG* addressOfNames = (ULONG*)((LPBYTE)module + exportDirectory->AddressOfNames);
  ULONG* funcAddr = (ULONG*)((LPBYTE)module + exportDirectory->AddressOfFunctions);

  // search exports for the specified name
  for (DWORD i = 0; i < exportDirectory->NumberOfNames; ++i) {
    char* curFunctionName = (char*)((LPBYTE)module + addressOfNames[i]);
    USHORT* nameOrdinals =
        (USHORT*)((LPBYTE)module + exportDirectory->AddressOfNameOrdinals);
    if (strcmp(functionName, curFunctionName) == 0) {
      if (funcAddr[nameOrdinals[i]] >= dataDirectory->VirtualAddress &&
          funcAddr[nameOrdinals[i]] <
              dataDirectory->VirtualAddress + dataDirectory->Size) {
        char* forwardLibName = _strdup((LPSTR)module + funcAddr[nameOrdinals[i]]);
        ON_BLOCK_EXIT([forwardLibName]() {
          free(forwardLibName);
        });
        char* forwardFunctionName = strchr(forwardLibName, '.');
        *forwardFunctionName      = 0;
        ++forwardFunctionName;

        HMODULE forwardLib = LoadLibraryA(forwardLibName);
        FARPROC forward    = nullptr;
        if (forwardLib != nullptr) {
          forward = MyGetProcAddress(forwardLib, forwardFunctionName);
          FreeLibrary(forwardLib);
        }

        return forward;
      }
      return (FARPROC)((LPBYTE)module + funcAddr[nameOrdinals[i]]);
    }
  }
  return nullptr;
}

}  // namespace HookLib
