#pragma once

#include "ntdll_declarations.h"
#include <dllimport.h>

namespace usvfs
{

DLLEXPORT BOOL WINAPI hook_GetFileAttributesExA(LPCSTR lpFileName,
                                                GET_FILEEX_INFO_LEVELS fInfoLevelId,
                                                LPVOID lpFileInformation);
DLLEXPORT BOOL WINAPI hook_GetFileAttributesExW(LPCWSTR lpFileName,
                                                GET_FILEEX_INFO_LEVELS fInfoLevelId,
                                                LPVOID lpFileInformation);
DLLEXPORT DWORD WINAPI hook_GetFileAttributesA(LPCSTR lpFileName);
DLLEXPORT DWORD WINAPI hook_GetFileAttributesW(LPCWSTR lpFileName);
DLLEXPORT DWORD WINAPI hook_SetFileAttributesW(LPCWSTR lpFileName,
                                               DWORD dwFileAttributes);

DLLEXPORT DWORD WINAPI hook_GetCurrentDirectoryA(DWORD nBufferLength, LPSTR lpBuffer);
DLLEXPORT DWORD WINAPI hook_GetCurrentDirectoryW(DWORD nBufferLength, LPWSTR lpBuffer);
DLLEXPORT BOOL WINAPI hook_SetCurrentDirectoryA(LPCSTR lpPathName);
DLLEXPORT BOOL WINAPI hook_SetCurrentDirectoryW(LPCWSTR lpPathName);

DLLEXPORT DWORD WINAPI hook_GetFullPathNameA(LPCSTR lpFileName, DWORD nBufferLength,
                                             LPSTR lpBuffer, LPSTR* lpFilePart);
DLLEXPORT DWORD WINAPI hook_GetFullPathNameW(LPCWSTR lpFileName, DWORD nBufferLength,
                                             LPWSTR lpBuffer, LPWSTR* lpFilePart);

DLLEXPORT BOOL WINAPI hook_CreateDirectoryW(LPCWSTR lpPathName,
                                            LPSECURITY_ATTRIBUTES lpSecurityAttributes);
DLLEXPORT BOOL WINAPI hook_RemoveDirectoryW(LPCWSTR lpPathName);

DLLEXPORT BOOL WINAPI hook_DeleteFileW(LPCWSTR lpFileName);

DLLEXPORT BOOL WINAPI hook_MoveFileA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName);
DLLEXPORT BOOL WINAPI hook_MoveFileW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName);
DLLEXPORT BOOL WINAPI hook_MoveFileExA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName,
                                       DWORD dwFlags);
DLLEXPORT BOOL WINAPI hook_MoveFileExW(LPCWSTR lpExistingFileName,
                                       LPCWSTR lpNewFileName, DWORD dwFlags);
DLLEXPORT BOOL WINAPI hook_MoveFileWithProgressA(LPCSTR lpExistingFileName,
                                                 LPCSTR lpNewFileName,
                                                 LPPROGRESS_ROUTINE lpProgressRoutine,
                                                 LPVOID lpData, DWORD dwFlags);
DLLEXPORT BOOL WINAPI hook_MoveFileWithProgressW(LPCWSTR lpExistingFileName,
                                                 LPCWSTR lpNewFileName,
                                                 LPPROGRESS_ROUTINE lpProgressRoutine,
                                                 LPVOID lpData, DWORD dwFlags);
;

DLLEXPORT BOOL WINAPI hook_CopyFileExW(LPCWSTR lpExistingFileName,
                                       LPCWSTR lpNewFileName,
                                       LPPROGRESS_ROUTINE lpProgressRoutine,
                                       LPVOID lpData, LPBOOL pbCancel,
                                       DWORD dwCopyFlags);

extern HRESULT(WINAPI* CopyFile2)(PCWSTR pwszExistingFileName, PCWSTR pwszNewFileName,
                                  COPYFILE2_EXTENDED_PARAMETERS* pExtendedParameters);
DLLEXPORT HRESULT WINAPI
hook_CopyFile2(PCWSTR pwszExistingFileName, PCWSTR pwszNewFileName,
               COPYFILE2_EXTENDED_PARAMETERS* pExtendedParameters);

DLLEXPORT HMODULE WINAPI hook_LoadLibraryExA(LPCSTR lpFileName, HANDLE hFile,
                                             DWORD dwFlags);
DLLEXPORT HMODULE WINAPI hook_LoadLibraryExW(LPCWSTR lpFileName, HANDLE hFile,
                                             DWORD dwFlags);

extern BOOL(WINAPI* CreateProcessInternalW)(
    LPVOID token, LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment,
    LPCWSTR lpCurrentDirectory, LPSTARTUPINFOW lpStartupInfo,
    LPPROCESS_INFORMATION lpProcessInformation, LPVOID newToken);
DLLEXPORT BOOL WINAPI hook_CreateProcessInternalW(
    LPVOID token, LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment,
    LPCWSTR lpCurrentDirectory, LPSTARTUPINFOW lpStartupInfo,
    LPPROCESS_INFORMATION lpProcessInformation, LPVOID newToken);

DLLEXPORT DWORD WINAPI hook_GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename,
                                               DWORD nSize);
DLLEXPORT DWORD WINAPI hook_GetModuleFileNameW(HMODULE hModule, LPWSTR lpFilename,
                                               DWORD nSize);

DLLEXPORT HANDLE WINAPI hook_FindFirstFileExW(
    LPCWSTR lpFileName, FINDEX_INFO_LEVELS fInfoLevelId, LPVOID lpFindFileData,
    FINDEX_SEARCH_OPS fSearchOp, LPVOID lpSearchFilter, DWORD dwAdditionalFlags);

DLLEXPORT DWORD WINAPI hook_GetPrivateProfileStringA(LPCSTR lpAppName, LPCSTR lpKeyName,
                                                     LPCSTR lpDefault,
                                                     LPSTR lpReturnedString,
                                                     DWORD nSize, LPCSTR lpFileName);
DLLEXPORT DWORD WINAPI hook_GetPrivateProfileStringW(LPCWSTR lpAppName,
                                                     LPCWSTR lpKeyName,
                                                     LPCWSTR lpDefault,
                                                     LPWSTR lpReturnedString,
                                                     DWORD nSize, LPCWSTR lpFileName);
DLLEXPORT DWORD WINAPI hook_GetPrivateProfileSectionA(LPCSTR lpAppName,
                                                      LPSTR lpReturnedString,
                                                      DWORD nSize, LPCSTR lpFileName);
DLLEXPORT DWORD WINAPI hook_GetPrivateProfileSectionW(LPCWSTR lpAppName,
                                                      LPWSTR lpReturnedString,
                                                      DWORD nSize, LPCWSTR lpFileName);
DLLEXPORT BOOL WINAPI hook_WritePrivateProfileStringA(LPCSTR lpAppName,
                                                      LPCSTR lpKeyName, LPCSTR lpString,
                                                      LPCSTR lpFileName);
DLLEXPORT BOOL WINAPI hook_WritePrivateProfileStringW(LPCWSTR lpAppName,
                                                      LPCWSTR lpKeyName,
                                                      LPCWSTR lpString,
                                                      LPCWSTR lpFileName);

DLLEXPORT VOID WINAPI hook_ExitProcess(UINT exitCode);

}  // namespace usvfs
