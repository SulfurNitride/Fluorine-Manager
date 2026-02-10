#include <Windows.h>
#include <gtest/gtest.h>

HANDLE WINAPI THCreateFileA_1(LPCSTR lpFileName, DWORD dwDesiredAccess,
                              DWORD dwShareMode,
                              LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                              DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes,
                              HANDLE hTemplateFile)
{
  if (strcmp(lpFileName, INVALID_FILENAME.c_str()) == 0) {
    return MARKERHANDLE;
  } else {
    HANDLE res =
        ::CreateFileA(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                      dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
    return res;
  }
}

HANDLE WINAPI THCreateFileW_1(LPCWSTR lpFileName, DWORD dwDesiredAccess,
                              DWORD dwShareMode,
                              LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                              DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes,
                              HANDLE hTemplateFile)
{
  if (wcscmp(lpFileName, INVALID_FILENAME.w_str()) == 0) {
    EXPECT_EQ(0x42, dwDesiredAccess);
    EXPECT_EQ(0x43, dwShareMode);
    EXPECT_EQ(0x44, (int)lpSecurityAttributes);
    EXPECT_EQ(0x45, dwCreationDisposition);
    EXPECT_EQ(0x46, dwFlagsAndAttributes);
    EXPECT_EQ(0x47, (int)hTemplateFile);
    return MARKERHANDLE;
  } else {
    return ::CreateFileW(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                         dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
  }
}
