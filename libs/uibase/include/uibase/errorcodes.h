#ifndef UIBASE_ERRORCODES_H
#define UIBASE_ERRORCODES_H

#include "dllimport.h"
#include <cstdint>

#ifdef _WIN32
#include <Windows.h>
#else
// POSIX: use uint32_t as DWORD equivalent
using DWORD = uint32_t;
#endif

namespace MOBase
{

QDLLEXPORT const wchar_t* errorCodeName(DWORD code);

}  // namespace MOBase

#endif // UIBASE_ERRORCODES_H
