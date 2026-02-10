#ifndef WINDOWS_COMPAT_H
#define WINDOWS_COMPAT_H

// Compatibility typedefs for Windows types used throughout the organizer codebase.
// On Windows these come from <Windows.h>; on Linux we provide minimal stubs.
//
// This header is included by the organizer PCH and also by uibase/utility.h
// to provide a single source of truth for Windows type compatibility on Linux.

#ifndef _WIN32

#include <cstdint>
#include <cerrno>

// Core Windows types (also used by uibase)
using DWORD = uint32_t;
using HANDLE = void*;
using LPDWORD = DWORD*;
using HRESULT = int32_t;
using HMODULE = void*;

// Additional types used by organizer
using UINT = unsigned int;
using UINT32 = uint32_t;
using BOOL = int;
using BYTE = uint8_t;
using WORD = uint16_t;
using LONG = int32_t;
using ULONG = uint32_t;
using LONGLONG = int64_t;
using ULONGLONG = uint64_t;
using LPCSTR = const char*;
using LPCWSTR = const wchar_t*;
using LPSTR = char*;
using LPWSTR = wchar_t*;
using NTSTATUS = int32_t;
using KNOWNFOLDERID = int;

// INVALID_HANDLE_VALUE
inline HANDLE INVALID_HANDLE_VALUE = reinterpret_cast<HANDLE>(static_cast<intptr_t>(-1));

// FILETIME - 64-bit timestamp, used heavily in the file register
struct FILETIME {
  DWORD dwLowDateTime;
  DWORD dwHighDateTime;
};

// Error helpers
inline DWORD GetLastError() { return static_cast<DWORD>(errno); }

// Windows error code constants
constexpr DWORD ERROR_SUCCESS = 0;
constexpr DWORD ERROR_FILE_NOT_FOUND = 2;
constexpr DWORD ERROR_PATH_NOT_FOUND = 3;
constexpr DWORD ERROR_ACCESS_DENIED = 5;
constexpr DWORD ERROR_INVALID_PARAMETER = 87;
constexpr DWORD ERROR_ALREADY_EXISTS = 183;
constexpr DWORD ERROR_CANCELLED = 1223;
constexpr DWORD ERROR_NOT_SAME_DEVICE = 17;
constexpr DWORD ERROR_BAD_PATHNAME = 161;
constexpr DWORD ERROR_BUFFER_OVERFLOW = 111;
constexpr DWORD ERROR_WRITE_PROTECT = 19;
constexpr DWORD ERROR_DISK_FULL = 112;
constexpr DWORD ERROR_GEN_FAILURE = 31;
constexpr DWORD ERROR_BAD_FORMAT = 11;

// ShellExecute error codes
constexpr int SE_ERR_ACCESSDENIED = 5;
constexpr int SE_ERR_ASSOCINCOMPLETE = 27;
constexpr int SE_ERR_DDEBUSY = 30;
constexpr int SE_ERR_DDEFAIL = 29;
constexpr int SE_ERR_DDETIMEOUT = 28;
constexpr int SE_ERR_DLLNOTFOUND = 32;
constexpr int SE_ERR_NOASSOC = 31;
constexpr int SE_ERR_OOM = 8;
constexpr int SE_ERR_SHARE = 26;

// MAX_PATH
#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif

#endif // !_WIN32
#endif // WINDOWS_COMPAT_H
