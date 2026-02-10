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
#include "winapi.h"
#include "exceptionex.h"
#include "logging.h"
#include "ntdll_declarations.h"
#include "stringcast.h"
#include "stringutils.h"
#include "unicodestring.h"

namespace winapi::ansi
{

std::string getModuleFileName(HMODULE module, HANDLE process)
{
  std::wstring result = wide::getModuleFileName(module, process);
  return usvfs::shared::string_cast<std::string>(result);
}

std::string getCurrentDirectory()
{
  std::string result;
  DWORD required = GetCurrentDirectoryA(0, nullptr);
  if (required == 0UL) {
    throw usvfs::shared::windows_error("failed to determine current working directory");
  }
  result.resize(required);
  GetCurrentDirectoryA(required, &result[0]);
  result.resize(required - 1);
  return result;
}

std::pair<std::string, std::string> getFullPathName(LPCSTR fileName)
{
  static const int INIT_SIZE = 128;
  std::string result;
  result.resize(INIT_SIZE);
  LPSTR filePart     = nullptr;
  DWORD requiredSize = GetFullPathNameA(fileName, INIT_SIZE, &result[0], &filePart);
  if (requiredSize >= INIT_SIZE) {
    result.resize(requiredSize);
    GetFullPathNameA(fileName, requiredSize, &result[0], &filePart);
  }
  if (requiredSize != 0UL) {
    return std::make_pair(result, std::string(filePart != nullptr ? filePart : ""));
  } else {
    return make_pair(result, std::string());
  }
}

}  // namespace winapi::ansi

namespace winapi::wide
{

std::wstring getModuleFileName(HMODULE module, HANDLE process)
{
  std::wstring result;
  result.resize(64);
  DWORD rc = 0UL;

  while ((rc = (process == INVALID_HANDLE_VALUE)
                   ? ::GetModuleFileNameW(module, &result[0],
                                          static_cast<DWORD>(result.size()))
                   : ::GetModuleFileNameExW(process, module, &result[0],
                                            static_cast<DWORD>(result.size()))) ==
         result.size()) {
    result.resize(result.size() * 2);
  }

  if (rc == 0UL) {
    if (::GetLastError() == ERROR_PARTIAL_COPY) {
#if BOOST_ARCH_X86_64
      return L"unknown (32-bit process)";
#else
      return L"unknown (64-bit process)";
#endif
    } else {
      throw usvfs::shared::windows_error("failed to retrieve module file name");
    }
  }

  result.resize(rc);

  return result;
}

std::pair<std::wstring, std::wstring> getFullPathName(LPCWSTR fileName)
{
  wchar_t buf1[MAX_PATH];
  std::vector<wchar_t> buf2;
  wchar_t* result    = buf1;
  LPWSTR filePart    = nullptr;
  DWORD requiredSize = GetFullPathNameW(fileName, MAX_PATH, result, &filePart);
  if (requiredSize >= MAX_PATH) {
    buf2.resize(requiredSize);
    result       = &buf2[0];
    requiredSize = GetFullPathNameW(fileName, requiredSize, result, &filePart);
  }
  return make_pair(std::wstring(result, requiredSize),
                   std::wstring((requiredSize && filePart) ? filePart : L""));
}

std::wstring getCurrentDirectory()
{
  // really great api this (::GetCurrentDirectoryW)
  //  - if it succeeds, returns size in characters WITHOUT zero termination
  //  - if it fails due to buffer too small, returns size in characters WITH zero
  //  termination
  //  - if it fails for other reasons, returns 0
  std::wstring result;
  DWORD required = GetCurrentDirectoryW(0, nullptr);
  if (required == 0UL) {
    throw usvfs::shared::windows_error("failed to determine current working directory");
  }
  result.resize(required);
  GetCurrentDirectoryW(required, &result[0]);
  result.resize(required - 1);
  return result;
}

std::wstring getKnownFolderPath(REFKNOWNFOLDERID folderID)
{
  PWSTR writablePath;

  ::SHGetKnownFolderPath(folderID, 0, nullptr, &writablePath);

  ON_BLOCK_EXIT([writablePath]() {
    ::CoTaskMemFree(writablePath);
  });

  return std::wstring(writablePath);
}

}  // namespace winapi::wide

namespace winapi::ex
{

std::pair<uintptr_t, uintptr_t> getSectionRange(HANDLE moduleHandle)
{
  std::pair<uintptr_t, uintptr_t> result;
  bool found          = false;
  uintptr_t exeModule = reinterpret_cast<uintptr_t>(moduleHandle);
  if (exeModule == 0) {
    throw std::runtime_error("failed to determine address range of executable");
  }

  std::pair<uintptr_t, uintptr_t> totalRange{UINT_MAX, 0};

  PIMAGE_DOS_HEADER dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(exeModule);
  PIMAGE_NT_HEADERS ntHeader =
      reinterpret_cast<PIMAGE_NT_HEADERS>(exeModule + dosHeader->e_lfanew);
  PIMAGE_SECTION_HEADER sectionHeader =
      reinterpret_cast<PIMAGE_SECTION_HEADER>(ntHeader + 1);
  for (int i = 0; i < ntHeader->FileHeader.NumberOfSections && !found; ++i) {
    if (memcmp(sectionHeader->Name, ".text", 5) == 0) {
      result.first  = exeModule + sectionHeader->VirtualAddress;
      result.second = result.first + sectionHeader->Misc.VirtualSize;
      found         = true;
    } else {
      uintptr_t start   = exeModule + sectionHeader->VirtualAddress;
      totalRange.first  = std::min(totalRange.first, start);
      totalRange.second = std::max<uintptr_t>(totalRange.second,
                                              start + sectionHeader->Misc.VirtualSize);
    }
    ++sectionHeader;
  }

  if (!found) {
    return totalRange;
  }

  return result;
}

OSVersion getOSVersion()
{
  RTL_OSVERSIONINFOEXW versionInfo;
  ZeroMemory(&versionInfo, sizeof(RTL_OSVERSIONINFOEXW));
  versionInfo.dwOSVersionInfoSize = sizeof(RTL_OSVERSIONINFOEXW);
  RtlGetVersion((PRTL_OSVERSIONINFOW)&versionInfo);

  OSVersion result;
  result.major      = versionInfo.dwMajorVersion;
  result.minor      = versionInfo.dwMinorVersion;
  result.build      = versionInfo.dwBuildNumber;
  result.platformid = versionInfo.dwPlatformId;
  result.servicpack =
      versionInfo.wServicePackMajor << 16 | versionInfo.wServicePackMinor;
  return result;
}

}  // namespace winapi::ex

namespace winapi::ex::ansi
{

std::string errorString(DWORD errorCode)
{
  std::ostringstream finalMessage;

  LPSTR buffer = nullptr;

  DWORD currentErrorCode = GetLastError();

  errorCode =
      errorCode != std::numeric_limits<DWORD>::max() ? errorCode : currentErrorCode;

  // TODO: the message is not english?
  if (FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                     nullptr, errorCode,
                     0  //, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT)
                     ,
                     (LPSTR)&buffer, 0, nullptr) == 0) {
    finalMessage << "(unknown error [" << errorCode << "])";
  } else {
    if (buffer != nullptr) {
      size_t end = strlen(buffer) - 1;
      while ((buffer[end] == '\n') || (buffer[end] == '\r')) {
        buffer[end--] = '\0';
      }
      finalMessage << "(" << buffer << " [" << errorCode << "])";
      LocalFree(buffer);  // allocated by FormatMessage
    }
  }

  SetLastError(currentErrorCode);  // restore error code because FormatMessage might
                                   // have modified it
  return finalMessage.str();
}

std::string toString(const FILETIME& time)
{
  SYSTEMTIME temp;
  FileTimeToSystemTime(&time, &temp);
  std::ostringstream stream;
  stream << temp.wYear << "-" << temp.wMonth << "-" << temp.wDay << temp.wHour << ":"
         << temp.wMinute << ":" << temp.wSecond;
  return stream.str();
}

LPCSTR GetBaseName(LPCSTR string)
{
  LPCSTR result = string + strlen(string) - 1;
  while (result > string) {
    if ((*result == '\\') || (*result == '/')) {
      ++result;
      break;
    } else {
      --result;
    }
  }
  return result;
}

}  // namespace winapi::ex::ansi

namespace winapi::ex::wide
{

bool fileExists(LPCWSTR fileName, bool* isDirectory)
{
  DWORD attrib = GetFileAttributesW(fileName);

  if (attrib == INVALID_FILE_ATTRIBUTES) {
    return false;
  } else {
    if (isDirectory != nullptr) {
      *isDirectory = (attrib & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
    return true;
  }
}

std::wstring errorString(DWORD errorCode)
{
  std::wostringstream finalMessage;

  LPWSTR buffer = nullptr;

  DWORD currentErrorCode = GetLastError();

  errorCode =
      errorCode != std::numeric_limits<DWORD>::max() ? errorCode : currentErrorCode;

  // TODO: the message is not english?
  if (FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                     nullptr, errorCode,
                     0  //, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT)
                     ,
                     (LPWSTR)&buffer, 0, nullptr) == 0) {
    finalMessage << L"(unknown error [" << errorCode << "])";
  } else {
    if (buffer != nullptr) {
      size_t end = wcslen(buffer) - 1;
      while ((buffer[end] == L'\n') || (buffer[end] == L'\r')) {
        buffer[end--] = L'\0';
      }
      finalMessage << L"(" << buffer << L" [" << errorCode << L"])";
      LocalFree(buffer);  // allocated by FormatMessage
    }
  }

  SetLastError(currentErrorCode);  // restore error code because FormatMessage might
                                   // have modified it
  return finalMessage.str();
}

std::wstring toString(const FILETIME& time)
{
  SYSTEMTIME temp;
  FileTimeToSystemTime(&time, &temp);
  std::wostringstream stream;
  stream << temp.wYear << "-" << temp.wMonth << "-" << temp.wDay << temp.wHour << ":"
         << temp.wMinute << ":" << temp.wSecond;
  return stream.str();
}

LPCWSTR GetBaseName(LPCWSTR string)
{
  LPCWSTR result;
  if ((string == nullptr) || (string[0] == L'\0')) {
    result = string;
  } else {
    result = string + wcslen(string) - 1;
  }

  while (result > string) {
    if ((*result == L'\\') || (*result == L'/')) {
      ++result;
      break;
    } else {
      --result;
    }
  }
  return result;
}

LPWSTR GetBaseName(LPWSTR path)
{
  LPCWSTR result = GetBaseName(static_cast<LPCWSTR>(path));
  return const_cast<LPWSTR>(result);
}

std::wstring getSectionName(PVOID addressIn, HANDLE process)
{
  if (process == nullptr) {
    process = GetCurrentProcess();
  }
  HMODULE modules[1024];
  intptr_t address = reinterpret_cast<intptr_t>(addressIn);
  DWORD required;
  if (::EnumProcessModules(process, modules, sizeof(modules), &required)) {
    for (DWORD i = 0; i < (std::min<DWORD>(1024UL, required) / sizeof(HMODULE)); ++i) {
      std::pair<intptr_t, intptr_t> range = getSectionRange(modules[i]);
      if ((address > range.first) && (address < range.second)) {
        try {
          return winapi::wide::getModuleFileName(modules[i], process);
        } catch (const std::exception&) {
          return std::wstring(L"unknown");
        }
      }
    }
  }
  return std::wstring(L"unknown");
}

std::vector<FileResult> quickFindFiles(LPCWSTR directoryName, LPCWSTR pattern)
{
  std::vector<FileResult> result;

  static const unsigned int BUFFER_SIZE = 1024;

  HANDLE hdl = CreateFileW(directoryName, GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);

  ON_BLOCK_EXIT([hdl]() {
    CloseHandle(hdl);
  });

  uint8_t buffer[BUFFER_SIZE];

  NTSTATUS res = STATUS_SUCCESS;  // status success
  while (res == STATUS_SUCCESS) {
    IO_STATUS_BLOCK status;

    res = NtQueryDirectoryFile(
        hdl, nullptr, nullptr, nullptr, &status, buffer, BUFFER_SIZE,
        FileFullDirectoryInformation, FALSE,
        static_cast<PUNICODE_STRING>(usvfs::UnicodeString(pattern)), FALSE);
    if (res == STATUS_SUCCESS) {
      FILE_FULL_DIR_INFORMATION* info =
          reinterpret_cast<FILE_FULL_DIR_INFORMATION*>(buffer);
      void* endPos = buffer + status.Information;
      while (info < endPos) {
        FileResult file;
        file.fileName =
            std::wstring(info->FileName, info->FileNameLength / sizeof(wchar_t));
        file.attributes = info->FileAttributes;

        result.push_back(file);
        if (info->NextEntryOffset == 0) {
          break;
        } else {
          info = reinterpret_cast<FILE_FULL_DIR_INFORMATION*>(
              reinterpret_cast<uint8_t*>(info) + info->NextEntryOffset);
        }
      }
    }
  }

  return result;
}

bool createPath(boost::filesystem::path path, LPSECURITY_ATTRIBUTES securityAttributes)
{
  // sanity and guaranteed recursion end:
  if (!path.has_relative_path())
    throw usvfs::shared::windows_error(
        "createPath() refusing to create non-existing top level path: " +
        path.string());

  DWORD attr = GetFileAttributesW(path.c_str());
  DWORD err  = GetLastError();
  if (attr != INVALID_FILE_ATTRIBUTES) {
    if (attr & FILE_ATTRIBUTE_DIRECTORY)
      return false;  // if directory already exists all is good
    else
      throw usvfs::shared::windows_error("createPath() called on a file: " +
                                         path.string());
  }
  if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND)
    throw usvfs::shared::windows_error(
        "createPath() GetFileAttributesW failed on: " + path.string(), err);

  if (err != ERROR_FILE_NOT_FOUND)  // ERROR_FILE_NOT_FOUND means parent directory
                                    // already exists
    createPath(path.parent_path(),
               securityAttributes);  // otherwise create parent directory (recursively)

  BOOL res = CreateDirectoryW(path.c_str(), securityAttributes);
  if (!res) {
    err = GetLastError();
    throw usvfs::shared::windows_error(
        "createPath() CreateDirectoryW failed on: " + path.string(), err);
  }
  return true;
}

std::wstring getWindowsBuildLab(bool ex)
{
  HKEY hKey = nullptr;
  auto res  = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                            LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion)", 0,
                            KEY_READ, &hKey);
  if (res != ERROR_SUCCESS || !hKey)
    return L"Opening HKLM Windows NT\\CurrentVersion failed?!";
  WCHAR buf[200];
  DWORD size = static_cast<DWORD>(sizeof(buf));
  res        = RegQueryValueExW(hKey, ex ? L"BuildLabEx" : L"BuildLab", NULL, NULL,
                                reinterpret_cast<LPBYTE>(buf), &size);
  if (res != ERROR_SUCCESS || size > sizeof(buf))
    return ex ? L"BuildLabEx reg value not found?!" : L"BuildLab reg value not found?!";
  size /= sizeof(buf[0]);
  if (size && !buf[size - 1])
    --size;
  return std::wstring(buf, size);
}

}  // namespace winapi::ex::wide
