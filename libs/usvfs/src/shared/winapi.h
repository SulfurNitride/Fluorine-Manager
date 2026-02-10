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

#include "logging.h"
#include "stringcast.h"
#include "windows_sane.h"
#include <ShlObj.h>

#define ALIAS(alias, original)                                                         \
  template <typename... Args>                                                          \
  auto alias(Args&&... args) -> decltype(original(std::forward<Args>(args)...))        \
  {                                                                                    \
    return original(std::forward<Args>(args)...);                                      \
  }

#define ALIAST(alias, original)                                                        \
  template <typename T, typename... Args>                                              \
  auto alias<T>(Args && ... args)                                                      \
      -> decltype(original<T>(std::forward<Args>(args)...))                            \
  {                                                                                    \
    return original<T>(std::forward<Args>(args)...);                                   \
  }

namespace winapi
{

struct parameter_error : public std::runtime_error
{
  parameter_error(const std::string& msg) : runtime_error(msg) {}
};

}  // namespace winapi

namespace winapi::process
{

/**
 * @brief result of process creation
 */
struct Result
{
  Result()
  {
    ::ZeroMemory(&processInfo, sizeof(PROCESS_INFORMATION));
    ::ZeroMemory(&startupInfo, sizeof(STARTUPINFO));
    startupInfo.cb = sizeof(STARTUPINFO);
  }

  Result(Result&& reference)
      : valid(reference.valid), startupInfo(reference.startupInfo),
        processInfo(reference.processInfo), errorCode(reference.errorCode)
  {
    reference.valid = false;
  }

  ~Result()
  {
    if (valid) {
      CloseHandle(processInfo.hProcess);
      CloseHandle(processInfo.hThread);
    }

    if (stdoutPipe != INVALID_HANDLE_VALUE) {
      CloseHandle(stdoutPipe);
    }
  }

  Result(const Result&) = delete;

  size_t readStdout(std::vector<uint8_t>& buffer, bool& eof)
  {
    if (stdoutPipe != INVALID_HANDLE_VALUE) {
      DWORD read;
      BOOL res = ReadFile(stdoutPipe, &buffer[0], static_cast<DWORD>(buffer.size()),
                          &read, nullptr);
      eof      = (res == TRUE) && (read == 0);
      return static_cast<size_t>(read);
    } else {
      eof = true;
      return 0;
    }
  }

  bool valid{false};
  STARTUPINFO startupInfo;
  PROCESS_INFORMATION processInfo;
  DWORD errorCode{0UL};

  HANDLE stdoutPipe{INVALID_HANDLE_VALUE};
};

/**
 * @brief internal class to handle process creation with named parameters.
 */
template <typename CharT>
class _Create
{
public:
  _Create(const std::basic_string<CharT>& binaryName);
  _Create(const _Create<CharT>& reference)                   = delete;
  _Create<CharT>& operator=(const _Create<CharT>& reference) = delete;

  _Create(_Create<CharT>&& reference)
      : m_CurrentDirectory(std::move(reference.m_CurrentDirectory)),
        m_ProcessAttributes(reference.m_ProcessAttributes),
        m_ThreadAttributes(reference.m_ThreadAttributes),
        m_InheritHandles(reference.m_InheritHandles),
        m_CreationFlags(std::move(reference.m_CreationFlags)),
        m_Executed(reference.m_Executed)
  {
    // stringstream should be moveable but it seems it isn't on mingw
    m_CommandLine << reference.m_CommandLine.rdbuf();
  }

  /// named parameter "argument". May be called repeatedly. This is
  /// directly appended to the command line with a separating space. No
  /// quoting happens
  template <typename ArgT>
  _Create& argument(const ArgT& argin)
  {
    m_CommandLine << " " << argin;
    return *this;
  }

  template <typename ArgT>
  _Create& arg(const ArgT& argin)
  {
    return this->argument(argin);
  }

  template <typename IterT>
  _Create& arguments(IterT begin, IterT end)
  {
    for (; begin != end; ++begin) {
      m_CommandLine << " " << *begin;
    }

    return *this;
  }

  /// @brief set the working directory for the process
  _Create& workingDirectory(const std::basic_string<CharT>& path);

  /// @brief set process attributes
  _Create& processAttributes(SECURITY_ATTRIBUTES* attributes);

  /// @brief set thread attributes
  _Create& threadAttributes(SECURITY_ATTRIBUTES* attributes);

  /// @brief activate inheriting handles
  _Create& inheritHandles();

  /// @brief have the process start suspended
  _Create& suspended();

  /// @brief set the process up to output stout to a pipe which can be
  /// retrieved through the result object
  _Create& stdoutPipe();

  /// @brief end the named parameter cascade and create the process
  Result operator()()
  {
    m_CommandLine.seekp(0, std::ios::end);
    unsigned int length = static_cast<unsigned int>(m_CommandLine.tellp());
    std::unique_ptr<CharT[]> clBuffer(new CharT[length + 1]);
    memset(clBuffer.get(), 0, (length + 1) * sizeof(CharT));
    memcpy(clBuffer.get(), m_CommandLine.str().c_str(), length * sizeof(CharT));
    Result result;

    if (m_StdoutPipe) {
      result.stdoutPipe = setupPipe(result.startupInfo.hStdOutput);
      result.startupInfo.dwFlags |= STARTF_USESTDHANDLES;
    }

    result.valid =
        createProcessInt(nullptr, clBuffer.get(), m_ProcessAttributes,
                         m_ThreadAttributes, m_InheritHandles, m_CreationFlags, nullptr,
                         m_CurrentDirectory.length() > 0 ? m_CurrentDirectory.c_str()
                                                         : nullptr,
                         &result.startupInfo, &result.processInfo) == TRUE;

    if (m_Stdout != INVALID_HANDLE_VALUE) {
      // got to close the write end of pipes
      CloseHandle(result.startupInfo.hStdOutput);
    }

    if (result.valid) {
      result.errorCode = NOERROR;
    } else {
      result.errorCode = GetLastError();
    }
    return result;
  }

private:
  static BOOL createProcessInt(LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
                               SECURITY_ATTRIBUTES* lpProcessAttributes,
                               SECURITY_ATTRIBUTES* lpThreadAttributes,
                               BOOL bInheritHandles, DWORD dwCreationFlags,
                               LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
                               LPSTARTUPINFOW lpStartupInfo,
                               LPPROCESS_INFORMATION lpProcessInformation)
  {
    return ::CreateProcessW(lpApplicationName, lpCommandLine, lpProcessAttributes,
                            lpThreadAttributes, bInheritHandles, dwCreationFlags,
                            lpEnvironment, lpCurrentDirectory, lpStartupInfo,
                            lpProcessInformation);
  }

  static BOOL createProcessInt(LPCSTR lpApplicationName, LPSTR lpCommandLine,
                               SECURITY_ATTRIBUTES* lpProcessAttributes,
                               SECURITY_ATTRIBUTES* lpThreadAttributes,
                               BOOL bInheritHandles, DWORD dwCreationFlags,
                               LPVOID lpEnvironment, LPCSTR lpCurrentDirectory,
                               LPSTARTUPINFOW lpStartupInfo,
                               LPPROCESS_INFORMATION lpProcessInformation)
  {
    std::wstring executable;
    if (lpApplicationName != nullptr) {
      executable = usvfs::shared::string_cast<std::wstring>(lpApplicationName);
    }

    std::wstring cmdline;
    if (lpCommandLine != nullptr) {
      cmdline = usvfs::shared::string_cast<std::wstring>(lpCommandLine);
    }

    std::wstring cwd;
    if (lpCurrentDirectory != nullptr) {
      cwd = usvfs::shared::string_cast<std::wstring>(lpCurrentDirectory);
    }

    return ::CreateProcessW(lpApplicationName != nullptr ? executable.c_str() : nullptr,
                            lpCommandLine != nullptr ? &cmdline[0] : nullptr,
                            lpProcessAttributes, lpThreadAttributes, bInheritHandles,
                            dwCreationFlags, lpEnvironment,
                            lpCurrentDirectory != nullptr ? cwd.c_str() : nullptr,
                            lpStartupInfo, lpProcessInformation);
  }

  HANDLE setupPipe(HANDLE& childHandle)
  {
    SECURITY_ATTRIBUTES attr;
    attr.nLength              = sizeof(SECURITY_ATTRIBUTES);
    attr.bInheritHandle       = TRUE;
    attr.lpSecurityDescriptor = nullptr;

    HANDLE pipe[2];

    CreatePipe(&pipe[0], &pipe[1], &attr, 0);
    SetHandleInformation(pipe[0], HANDLE_FLAG_INHERIT, 0);

    childHandle = pipe[1];

    return pipe[0];
  }

private:
  std::basic_stringstream<CharT> m_CommandLine;
  std::basic_string<CharT> m_CurrentDirectory{};
  SECURITY_ATTRIBUTES* m_ProcessAttributes{nullptr};
  SECURITY_ATTRIBUTES* m_ThreadAttributes{nullptr};
  BOOL m_InheritHandles{false};
  DWORD m_CreationFlags{0UL};
  bool m_Executed{false};
  bool m_StdoutPipe{false};

  HANDLE m_Stdout{INVALID_HANDLE_VALUE};
};

}  // namespace winapi::process

namespace winapi::file
{
/**
 * @brief internal class to handle file creation (opening) with named
 * parameters.
 */
template <typename CharT, DWORD DefaultDisposition>
class _Create
{
public:
  _Create(const std::basic_string<CharT>& fileName) : m_FileName(fileName) {}

  _Create& access(DWORD desiredAccess)
  {
    m_DesiredAccess = desiredAccess;
    return *this;
  }

  _Create& share(DWORD shareMode)
  {
    m_ShareMode = shareMode;
    return *this;
  }

  _Create& createAlways()
  {
    m_CreationDisposition = CREATE_ALWAYS;
    return *this;
  }

  _Create& openAlways()
  {
    m_CreationDisposition = OPEN_ALWAYS;
    return *this;
  }

  _Create& security(SECURITY_ATTRIBUTES* attributes)
  {
    m_SecurityAttributes = attributes;
    return *this;
  }

  _Create& templateFile(HANDLE templateFile)
  {
    m_Template = templateFile;
    return *this;
  }

  /// @brief end the named parameter cascade and open the file
  HANDLE operator()()
  {
    return callDelegate(
        std::integral_constant<bool, sizeof(CharT) == sizeof(wchar_t)>());
  }

private:
  HANDLE callDelegate(std::true_type)
  {
    return ::CreateFileW(m_FileName.c_str(), m_DesiredAccess, m_ShareMode,
                         m_SecurityAttributes, m_CreationDisposition, m_Flags,
                         m_Template);
  }

  HANDLE callDelegate(std::false_type)
  {
    return ::CreateFileA(m_FileName.c_str(), m_DesiredAccess, m_ShareMode,
                         m_SecurityAttributes, m_CreationDisposition, m_Flags,
                         m_Template);
  }

private:
  std::basic_string<CharT> m_FileName;
  DWORD m_DesiredAccess{GENERIC_ALL};
  DWORD m_ShareMode{0UL};
  DWORD m_CreationDisposition{DefaultDisposition};
  DWORD m_Flags{FILE_ATTRIBUTE_NORMAL};
  HANDLE m_Template{nullptr};
  SECURITY_ATTRIBUTES* m_SecurityAttributes{nullptr};
};

}  // namespace winapi::file

namespace winapi::ansi
{

std::string getModuleFileName(HMODULE module, HANDLE process = INVALID_HANDLE_VALUE);
std::pair<std::string, std::string> getFullPathName(LPCSTR fileName);
std::string getCurrentDirectory();
typedef process::_Create<char> createProcess;
typedef file::_Create<char, CREATE_NEW> createFile;
typedef file::_Create<char, OPEN_EXISTING> openFile;

}  // namespace winapi::ansi

namespace winapi::wide
{

std::wstring getModuleFileName(HMODULE module, HANDLE process = INVALID_HANDLE_VALUE);
std::pair<std::wstring, std::wstring> getFullPathName(LPCWSTR fileName);
std::wstring getCurrentDirectory();
std::wstring getKnownFolderPath(REFKNOWNFOLDERID folderID);

typedef process::_Create<wchar_t> createProcess;
typedef file::_Create<wchar_t, CREATE_NEW> createFile;
typedef file::_Create<wchar_t, OPEN_EXISTING> openFile;

}  // namespace winapi::wide

/**
 * useful convenience functions close to the api
 */
namespace winapi::ex
{

/**
 * @brief retrieve the address range covering the code section of a module
 * @param moduleHandle handle to the module
 * @return start and end address of the code section
 * @note the code section can only be identified if it has the standardized section name
 * ".text" Otherwise the whole address range of all sections in the module is returned.
 *       This happens for compressed exectuables for example
 */
std::pair<uintptr_t, uintptr_t> getSectionRange(HANDLE moduleHandle);

struct OSVersion
{
  DWORD major;
  DWORD minor;
  DWORD build;
  DWORD platformid;
  DWORD servicpack;
};

OSVersion getOSVersion();

}  // namespace winapi::ex

namespace winapi::ex::ansi
{

/**
 * @brief retrieve an error string for a windows error message
 * @param errorCode the error code to look up. If this is left at the default,
 * ::GetLastError is used
 * @return string representation of the error. Currently this is localized
 */
std::string errorString(DWORD errorCode = std::numeric_limits<DWORD>::max());

/**
 * @brief convert filetime to string
 * @param time time to convert
 * @return a string representation (currently only supports utc and iso format with
 * second precision)
 */
std::string toString(const FILETIME& time);

/**
 * @brief find file name in a windows file path
 * @param path the path to search in
 * @return the file name of the path or an empty string if the path ends on
 *         a slash
 * @note this function doesn't access the file system so it doesn't depend
 *       on whether the file actually exists. This also means it can't
 *       determine if a path that doesn't end on a slash refers to a file or
 *       directory
 * @note the return value is a pointer into the same buffer, no copy is
 *       created
 */
LPCSTR GetBaseName(LPCSTR string);

}  // namespace winapi::ex::ansi

namespace winapi::ex::wide
{
/**
 * retrieve the name of the binary section containing the specified address
 * @param address the address to test
 * @param process the process for which to retrieve the section. If this is
 *        nullptr, the current process is analized.
 * @return name of the section or "unknown" if no matching section was found
 */
std::wstring getSectionName(PVOID address, HANDLE process = nullptr);

/**
 * @brief test if a file exists
 * @param path path to check
 * @param isDirectory (optional) if this isn't null, it will be set to true if the path
 * specifies a directory, false otherwise
 * @return true if the file (or directory) exists.
 */
bool fileExists(LPCWSTR fileName, bool* isDirectory = nullptr);

/**
 * @brief retrieve an error string for a windows error message
 * @param errorCode the error code to look up. If this is left at the default,
 * ::GetLastError is used
 * @return string representation of the error. Currently this is localized
 */
std::wstring errorString(DWORD errorCode = std::numeric_limits<DWORD>::max());

/**
 * @brief convert filetime to string
 * @param time time to convert
 * @return a string representation (currently only supports utc and iso format with
 * second precision)
 */
std::wstring toString(const FILETIME& time);

/**
 * @brief find file name in a windows file path
 * @param path the path to search in
 * @return the file name of the path or an empty string if the path ends on
 *         a slash
 * @note this function doesn't access the file system so it doesn't depend
 *       on whether the file actually exists. This also means it can't
 *       determine if a path that doesn't end on a slash refers to a file or
 *       directory
 * @note the return value is a pointer into the same buffer, no copy is
 *       created
 */
LPCWSTR GetBaseName(LPCWSTR path);

/**
 * @see const-variant of this function
 */
LPWSTR GetBaseName(LPWSTR path);

struct FileResult
{
  std::wstring fileName;
  ULONG attributes;
};

/**
 * @brief a quick function to find all files in a directory or files following a
 * pattern. This uses NtQueryDirectoryFile api internally so it should be faster than
 * the usual FindFirstFile/FindNextFile pattern
 * @param directoryName name of the directory to search in
 * @param pattern name pattern that needs to match
 * @return the list of files found
 */
std::vector<FileResult> quickFindFiles(LPCWSTR directoryName, LPCWSTR pattern);

/**
 * @brief create the specified directory including all intermediate
 * directories
 * @param path the path to create
 * @param securityAttributes the security attributes to use for all created
 *        directories. if this is null (default), the standard attributes
 *        are used
 * @return true if the directory (and possibly parent directories) were actually created
 *        and false if the directory already existed. Throws exceptions on failure.
 */
bool createPath(boost::filesystem::path path,
                LPSECURITY_ATTRIBUTES securityAttributes = nullptr);
inline bool createPath(LPCWSTR path, LPSECURITY_ATTRIBUTES securityAttributes = nullptr)
{
  return createPath(boost::filesystem::path(path), securityAttributes);
}

std::wstring getWindowsBuildLab(bool ex = false);

}  // namespace winapi::ex::wide

namespace winapi::process
{

template <typename CharT>
_Create<CharT>::_Create(const std::basic_string<CharT>& binaryName)
{
  if (binaryName.length() > MAX_PATH) {
    throw parameter_error("executable filename can't be longer than 260 characters");
  }
  m_CommandLine << "\"" << binaryName << "\"";
}

template <typename CharT>
_Create<CharT>& _Create<CharT>::workingDirectory(const std::basic_string<CharT>& path)
{
  m_CurrentDirectory = path;
  return *this;
}

template <typename CharT>
_Create<CharT>& _Create<CharT>::processAttributes(SECURITY_ATTRIBUTES* attributes)
{
  m_ProcessAttributes = attributes;
  return *this;
}

template <typename CharT>
_Create<CharT>& _Create<CharT>::threadAttributes(SECURITY_ATTRIBUTES* attributes)
{
  m_ThreadAttributes = attributes;
  return *this;
}

template <typename CharT>
_Create<CharT>& _Create<CharT>::inheritHandles()
{
  m_InheritHandles = true;
  return *this;
}

template <typename CharT>
_Create<CharT>& _Create<CharT>::suspended()
{
  m_CreationFlags |= CREATE_SUSPENDED;
  return *this;
}

template <typename CharT>
_Create<CharT>& _Create<CharT>::stdoutPipe()
{
  m_StdoutPipe = true;
  return *this;
}

}  // namespace winapi::process
