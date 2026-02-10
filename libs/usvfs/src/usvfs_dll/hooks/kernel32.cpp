#include "kernel32.h"
#include "sharedids.h"

#include "../hookcallcontext.h"
#include "../hookcontext.h"
#include "../hookmanager.h"
#include "../maptracker.h"
#include <formatters.h>
#include <inject.h>
#include <loghelpers.h>
#include <stringcast.h>
#include <stringutils.h>
#include <usvfs.h>
#include <winapi.h>
#include <winbase.h>

namespace ush = usvfs::shared;
using ush::CodePage;
using ush::string_cast;

namespace usvfs
{
MapTracker k32DeleteTracker;
MapTracker k32FakeDirTracker;
}  // namespace usvfs

class CurrentDirectoryTracker
{
public:
  using wstring = std::wstring;

  bool get(wstring& currentDir, const wchar_t* forRelativePath = nullptr)
  {
    int index = m_currentDrive;
    if (forRelativePath && *forRelativePath && forRelativePath[1] == ':')
      if (!getDriveIndex(forRelativePath, index))
        spdlog::get("usvfs")->warn(
            "CurrentDirectoryTracker::get() invalid drive letter: {}, will use current "
            "drive {}",
            string_cast<std::string>(forRelativePath),
            static_cast<char>('A' + index));  // prints '@' for m_currentDrive == -1
    if (index < 0)
      return false;

    std::shared_lock<std::shared_mutex> lock(m_mutex);
    if (m_perDrive[index].empty())
      return false;
    else {
      currentDir = m_perDrive[index];
      return true;
    }
  }

  bool set(const wstring& currentDir)
  {
    int index = -1;
    bool good = !currentDir.empty() && getDriveIndex(currentDir.c_str(), index) &&
                currentDir[1] == ':';
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_currentDrive = good ? index : -1;
    if (good)
      m_perDrive[index] = currentDir;
    return good;
  }

private:
  static bool getDriveIndex(const wchar_t* path, int& index)
  {
    if (*path >= 'a' && *path <= 'z')
      index = *path - 'a';
    else if (*path >= 'A' && *path <= 'Z')
      index = *path - 'A';
    else
      return false;
    return true;
  }

  mutable std::shared_mutex m_mutex;
  wstring m_perDrive['z' - 'a' + 1];
  int m_currentDrive{-1};
};

CurrentDirectoryTracker k32CurrentDirectoryTracker;

// attempts to copy source to destination and return the error code
static inline DWORD copyFileDirect(LPCWSTR source, LPCWSTR destination, bool overwrite)
{
  usvfs::FunctionGroupLock lock(usvfs::MutExHookGroup::SHELL_FILEOP);
  return CopyFileExW(source, destination, NULL, NULL, NULL,
                     overwrite ? 0 : COPY_FILE_FAIL_IF_EXISTS)
             ? ERROR_SUCCESS
             : GetLastError();
}

static inline WCHAR pathNameDriveLetter(LPCWSTR path)
{
  if (!path || !path[0])
    return 0;
  if (path[1] == ':')
    return path[0];
  // if path is not ?: or \* then we need to get absolute path:
  std::wstring buf;
  if (path[0] != '\\') {
    buf  = winapi::wide::getFullPathName(path).first;
    path = buf.c_str();
    if (!path[0] || path[1] == ':')
      return path[0];
  }
  // check for \??\C:
  if (path[1] && path[2] && path[3] && path[4] && path[0] == '\\' && path[3] == '\\' &&
      path[5] == ':')
    return path[4];
  // give up
  return 0;
}

// returns false also in case we fail to determine the drive letter of the path
static inline bool pathsOnDifferentDrives(LPCWSTR path1, LPCWSTR path2)
{
  WCHAR drive1 = pathNameDriveLetter(path1);
  WCHAR drive2 = pathNameDriveLetter(path2);
  return drive1 && drive2 && towupper(drive1) != towupper(drive2);
}

HMODULE WINAPI usvfs::hook_LoadLibraryExA(LPCSTR lpFileName, HANDLE hFile,
                                          DWORD dwFlags)
{
  HMODULE res = nullptr;

  HOOK_START_GROUP(MutExHookGroup::LOAD_LIBRARY)
  const std::wstring fileName = ush::string_cast<std::wstring>(lpFileName);

  PRE_REALCALL
  res = LoadLibraryExW(fileName.c_str(), hFile, dwFlags);
  POST_REALCALL

  HOOK_END
  return res;
}

HMODULE WINAPI usvfs::hook_LoadLibraryExW(LPCWSTR lpFileName, HANDLE hFile,
                                          DWORD dwFlags)
{
  HMODULE res = nullptr;

  HOOK_START_GROUP(MutExHookGroup::LOAD_LIBRARY)
  // Why is the usual if (!callContext.active()... check missing?

  RerouteW reroute = RerouteW::create(READ_CONTEXT(), callContext, lpFileName);
  PRE_REALCALL
  res = ::LoadLibraryExW(reroute.fileName(), hFile, dwFlags);
  POST_REALCALL

  if (reroute.wasRerouted()) {
    LOG_CALL()
        .PARAM(lpFileName)
        .PARAM(reroute.fileName())
        .PARAM(res)
        .PARAM(callContext.lastError());
  }

  HOOK_END

  return res;
}

/// determine name of the binary to run based on parameters for createprocess
std::wstring getBinaryName(LPCWSTR applicationName, LPCWSTR lpCommandLine)
{
  if (applicationName != nullptr) {
    std::pair<std::wstring, std::wstring> fullPath =
        winapi::wide::getFullPathName(applicationName);
    return fullPath.second;
  } else {
    if (lpCommandLine[0] == '"') {
      const wchar_t* endQuote = wcschr(lpCommandLine, '"');
      if (endQuote != nullptr) {
        return std::wstring(lpCommandLine + 1, endQuote - 1);
      }
    }

    // according to the documentation, if the commandline is unquoted and has
    // spaces, it will be interpreted in multiple ways, i.e.
    // c:\program.exe files\sub dir\program name
    // c:\program files\sub.exe dir\program name
    // c:\program files\sub dir\program.exe name
    // c:\program files\sub dir\program name.exe
    LPCWSTR space = wcschr(lpCommandLine, L' ');
    while (space != nullptr) {
      std::wstring subString(lpCommandLine, space);
      bool isDirectory = true;
      if (winapi::ex::wide::fileExists(subString.c_str(), &isDirectory) &&
          !isDirectory) {
        return subString;
      } else {
        space = wcschr(space + 1, L' ');
      }
    }
    return std::wstring(lpCommandLine);
  }
}

BOOL(WINAPI* usvfs::CreateProcessInternalW)(
    LPVOID token, LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment,
    LPCWSTR lpCurrentDirectory, LPSTARTUPINFOW lpStartupInfo,
    LPPROCESS_INFORMATION lpProcessInformation, LPVOID newToken);

BOOL WINAPI usvfs::hook_CreateProcessInternalW(
    LPVOID token, LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment,
    LPCWSTR lpCurrentDirectory, LPSTARTUPINFOW lpStartupInfo,
    LPPROCESS_INFORMATION lpProcessInformation, LPVOID newToken)
{
  BOOL res = FALSE;

  HOOK_START_GROUP(MutExHookGroup::CREATE_PROCESS)
  if (!callContext.active()) {
    res = CreateProcessInternalW(
        token, lpApplicationName, lpCommandLine, lpProcessAttributes,
        lpThreadAttributes, bInheritHandles, dwCreationFlags, lpEnvironment,
        lpCurrentDirectory, lpStartupInfo, lpProcessInformation, newToken);
    callContext.updateLastError();
    return res;
  }

  // remember if the caller wanted the process to be suspended. If so, we
  // don't resume when we're done
  BOOL susp = dwCreationFlags & CREATE_SUSPENDED;
  dwCreationFlags |= CREATE_SUSPENDED;

  RerouteW applicationReroute;
  RerouteW cmdReroute;
  LPWSTR cend = nullptr;

  std::wstring dllPath;
  usvfsParameters callParameters;

  {  // scope for context lock
    auto context = READ_CONTEXT();

    if (RerouteW::interestingPath(lpCommandLine)) {
      // First "argument" in the commandline is the command, we need to identify it and
      // reroute it:
      if (*lpCommandLine == '"') {
        // If the first argument is quoted we trust its is quoted correctly
        for (cend = lpCommandLine; *cend && *cend != ' '; ++cend)
          if (*cend == '"') {
            int escaped = 0;
            for (++cend; *cend && (*cend != '"' || escaped % 2 != 0); ++cend)
              escaped = *cend == '\\' ? escaped + 1 : 0;
          }

        if (*(cend - 1) == '"')
          --cend;
        auto old_cend = *cend;
        *cend         = 0;
        cmdReroute    = RerouteW::create(context, callContext, lpCommandLine + 1);
        *cend         = old_cend;
        if (old_cend == '"')
          ++cend;
      } else {
        // If the first argument we have no choice but to test all the options to quote
        // the command as the real CreateProcess will do this:
        cend = lpCommandLine;
        while (true) {
          while (*cend && *cend != ' ')
            ++cend;

          auto old_cend = *cend;
          *cend         = 0;
          cmdReroute    = RerouteW::create(context, callContext, lpCommandLine);
          *cend         = old_cend;
          if (cmdReroute.wasRerouted() || pathIsFile(cmdReroute.fileName()))
            break;

          while (*cend == ' ')
            ++cend;

          if (!*cend) {
            // if we reached the end of the string we'll just use the whole commandline
            // as is:
            cend = nullptr;
            break;
          }
        }
      }
    }

    applicationReroute = RerouteW::create(context, callContext, lpApplicationName);

    dllPath        = context->dllPath();
    callParameters = context->callParameters();
  }

  std::wstring cmdline;
  if (cend && cmdReroute.fileName()) {
    auto fileName = cmdReroute.fileName();
    cmdline.reserve(wcslen(fileName) + wcslen(cend) + 2);
    if (*fileName != '"')
      cmdline += L"\"";
    cmdline += fileName;
    if (*fileName != '"')
      cmdline += L"\"";
    cmdline += cend;
  }

  PRE_REALCALL
  res = CreateProcessInternalW(token, applicationReroute.fileName(),
                               cmdline.empty() ? lpCommandLine : &cmdline[0],
                               lpProcessAttributes, lpThreadAttributes, bInheritHandles,
                               dwCreationFlags, lpEnvironment, lpCurrentDirectory,
                               lpStartupInfo, lpProcessInformation, newToken);
  POST_REALCALL

  BOOL blacklisted = FALSE;
  {  // limit scope of context
    auto context = READ_CONTEXT();
    blacklisted  = context->executableBlacklisted(applicationReroute.fileName(),
                                                  cmdReroute.fileName());
  }

  if (res) {
    if (!blacklisted) {
      try {
        injectProcess(dllPath, callParameters, *lpProcessInformation);
      } catch (const std::exception& e) {
        spdlog::get("hooks")->error("failed to inject into {0}: {1}",
                                    lpApplicationName != nullptr
                                        ? applicationReroute.fileName()
                                        : static_cast<LPCWSTR>(lpCommandLine),
                                    e.what());
      }
    }

    // resume unless process is supposed to start suspended
    if (!susp && (ResumeThread(lpProcessInformation->hThread) == (DWORD)-1)) {
      spdlog::get("hooks")->error("failed to inject into spawned process");
      res = FALSE;
    }
  }

  LOG_CALL()
      .PARAM(lpApplicationName)
      .PARAM(applicationReroute.fileName())
      .PARAM(cmdReroute.fileName())
      .PARAM(res)
      .PARAM(callContext.lastError())
      .PARAM(cmdline);
  HOOK_END

  return res;
}

BOOL WINAPI usvfs::hook_GetFileAttributesExA(LPCSTR lpFileName,
                                             GET_FILEEX_INFO_LEVELS fInfoLevelId,
                                             LPVOID lpFileInformation)
{
  BOOL res = FALSE;

  HOOK_START_GROUP(MutExHookGroup::FILE_ATTRIBUTES)
  if (!callContext.active() || !RerouteW::interestingPath(lpFileName)) {
    res = GetFileAttributesExA(lpFileName, fInfoLevelId, lpFileInformation);
    callContext.updateLastError();
    return res;
  }
  HOOK_END

  HOOK_START
  const std::wstring fileName = ush::string_cast<std::wstring>(lpFileName);

  PRE_REALCALL
  res = GetFileAttributesExW(fileName.c_str(), fInfoLevelId, lpFileInformation);
  POST_REALCALL

  HOOK_END
  return res;
}

BOOL WINAPI usvfs::hook_GetFileAttributesExW(LPCWSTR lpFileName,
                                             GET_FILEEX_INFO_LEVELS fInfoLevelId,
                                             LPVOID lpFileInformation)
{
  BOOL res = FALSE;

  HOOK_START_GROUP(MutExHookGroup::FILE_ATTRIBUTES)
  if (!callContext.active() || !RerouteW::interestingPath(lpFileName)) {
    res = GetFileAttributesExW(lpFileName, fInfoLevelId, lpFileInformation);
    callContext.updateLastError();
    return res;
  }

  fs::path canonicalFile = RerouteW::canonizePath(RerouteW::absolutePath(lpFileName));

  RerouteW reroute =
      RerouteW::create(READ_CONTEXT(), callContext, canonicalFile.c_str());

  PRE_REALCALL
  res = ::GetFileAttributesExW(reroute.fileName(), fInfoLevelId, lpFileInformation);
  POST_REALCALL

  DWORD originalError = callContext.lastError();
  DWORD fixedError    = originalError;
  // In case the target does not exist the error value varies to differentiate if the
  // parent folder exists (ERROR_FILE_NOT_FOUND) or not (ERROR_PATH_NOT_FOUND).
  // If the original target's parent folder doesn't actually exist it may exist in the
  // the virtualized sense, or if we rerouted the query the parent of the original path
  // might exist while the parent of the rerouted path might not:
  if (!res && fixedError == ERROR_PATH_NOT_FOUND) {
    // first query original file parent (if we rerouted it):
    fs::path originalParent = canonicalFile.parent_path();
    WIN32_FILE_ATTRIBUTE_DATA parentAttr;
    if (reroute.wasRerouted() &&
        ::GetFileAttributesExW(originalParent.c_str(), GetFileExInfoStandard,
                               &parentAttr) &&
        (parentAttr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
      fixedError = ERROR_FILE_NOT_FOUND;
    else {
      // now query the rerouted path for parent (which can be different from the parent
      // of the rerouted path)
      RerouteW rerouteParent =
          RerouteW::create(READ_CONTEXT(), callContext, originalParent.c_str());
      if (rerouteParent.wasRerouted() &&
          ::GetFileAttributesExW(rerouteParent.fileName(), GetFileExInfoStandard,
                                 &parentAttr) &&
          (parentAttr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        fixedError = ERROR_FILE_NOT_FOUND;
    }
  }
  if (fixedError != originalError)
    callContext.updateLastError(fixedError);

  if (reroute.wasRerouted() || fixedError != originalError) {
    DWORD resAttrib;
    if (res && fInfoLevelId == GetFileExInfoStandard && lpFileInformation)
      resAttrib = reinterpret_cast<WIN32_FILE_ATTRIBUTE_DATA*>(lpFileInformation)
                      ->dwFileAttributes;
    else
      resAttrib = (DWORD)-1;
    LOG_CALL()
        .PARAM(lpFileName)
        .PARAM(reroute.fileName())
        .PARAMHEX(fInfoLevelId)
        .PARAMHEX(res)
        .PARAMHEX(resAttrib)
        .PARAM(originalError)
        .PARAM(fixedError);
  }

  HOOK_END

  return res;
}

DWORD WINAPI usvfs::hook_GetFileAttributesA(LPCSTR lpFileName)
{
  BOOL res = FALSE;

  HOOK_START_GROUP(MutExHookGroup::FILE_ATTRIBUTES)
  if (!callContext.active() || !RerouteW::interestingPath(lpFileName)) {
    res = GetFileAttributesA(lpFileName);
    callContext.updateLastError();
    return res;
  }
  HOOK_END

  HOOK_START
  const std::wstring fileName = ush::string_cast<std::wstring>(lpFileName);

  PRE_REALCALL
  res = GetFileAttributesW(fileName.c_str());
  POST_REALCALL

  HOOK_END
  return res;
}

DWORD WINAPI usvfs::hook_GetFileAttributesW(LPCWSTR lpFileName)
{
  DWORD res = 0UL;

  HOOK_START_GROUP(MutExHookGroup::FILE_ATTRIBUTES)
  if (!callContext.active() || !RerouteW::interestingPath(lpFileName)) {
    res = GetFileAttributesW(lpFileName);
    callContext.updateLastError();
    return res;
  }

  fs::path canonicalFile = RerouteW::canonizePath(RerouteW::absolutePath(lpFileName));

  RerouteW reroute =
      RerouteW::create(READ_CONTEXT(), callContext, canonicalFile.c_str());

  if (reroute.wasRerouted())
    PRE_REALCALL
  res = ::GetFileAttributesW(reroute.fileName());
  POST_REALCALL

  DWORD originalError = callContext.lastError();
  DWORD fixedError    = originalError;
  // In case the target does not exist the error value varies to differentiate if the
  // parent folder exists (ERROR_FILE_NOT_FOUND) or not (ERROR_PATH_NOT_FOUND).
  // If the original target's parent folder doesn't actually exist it may exist in the
  // the virtualized sense, or if we rerouted the query the parent of the original path
  // might exist while the parent of the rerouted path might not:
  if (res == INVALID_FILE_ATTRIBUTES && fixedError == ERROR_PATH_NOT_FOUND) {
    // first query original file parent (if we rerouted it):
    fs::path originalParent = canonicalFile.parent_path();
    DWORD attr;
    if (reroute.wasRerouted() &&
        (attr = ::GetFileAttributesW(originalParent.c_str())) !=
            INVALID_FILE_ATTRIBUTES &&
        (attr & FILE_ATTRIBUTE_DIRECTORY))
      fixedError = ERROR_FILE_NOT_FOUND;
    else {
      // now query the rerouted path for parent (which can be different from the parent
      // of the rerouted path)
      RerouteW rerouteParent =
          RerouteW::create(READ_CONTEXT(), callContext, originalParent.c_str());
      if (rerouteParent.wasRerouted() &&
          (attr = ::GetFileAttributesW(rerouteParent.fileName())) !=
              INVALID_FILE_ATTRIBUTES &&
          (attr & FILE_ATTRIBUTE_DIRECTORY))
        fixedError = ERROR_FILE_NOT_FOUND;
    }
  }
  if (fixedError != originalError)
    callContext.updateLastError(fixedError);

  if (reroute.wasRerouted() || fixedError != originalError) {
    LOG_CALL()
        .PARAM(lpFileName)
        .PARAM(reroute.fileName())
        .PARAMHEX(res)
        .PARAM(originalError)
        .PARAM(fixedError);
  }

  HOOK_ENDP(lpFileName);

  return res;
}

DWORD WINAPI usvfs::hook_SetFileAttributesW(LPCWSTR lpFileName, DWORD dwFileAttributes)
{
  DWORD res = 0UL;

  HOOK_START_GROUP(MutExHookGroup::FILE_ATTRIBUTES)
  // Why is the usual if (!callContext.active()... check missing?

  RerouteW reroute = RerouteW::create(READ_CONTEXT(), callContext, lpFileName);
  PRE_REALCALL
  res = ::SetFileAttributesW(reroute.fileName(), dwFileAttributes);
  POST_REALCALL

  if (reroute.wasRerouted()) {
    LOG_CALL().PARAM(reroute.fileName()).PARAM(res).PARAM(callContext.lastError());
  }

  HOOK_END

  return res;
}

BOOL WINAPI usvfs::hook_DeleteFileW(LPCWSTR lpFileName)
{
  BOOL res = FALSE;

  HOOK_START_GROUP(MutExHookGroup::DELETE_FILE)
  // Why is the usual if (!callContext.active()... check missing?

  const std::wstring path =
      RerouteW::canonizePath(RerouteW::absolutePath(lpFileName)).wstring();

  RerouteW reroute = RerouteW::create(READ_CONTEXT(), callContext, path.c_str());

  PRE_REALCALL
  if (reroute.wasRerouted()) {
    res = ::DeleteFileW(reroute.fileName());
  } else {
    res = ::DeleteFileW(path.c_str());
  }
  POST_REALCALL

  if (res) {
    reroute.removeMapping(READ_CONTEXT());
  }

  if (reroute.wasRerouted())
    LOG_CALL()
        .PARAM(lpFileName)
        .PARAM(reroute.fileName())
        .PARAM(res)
        .PARAM(callContext.lastError());

  HOOK_END

  return res;
}

BOOL rewriteChangedDrives(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName,
                          const usvfs::RerouteW& readReroute,
                          const usvfs::CreateRerouter& writeReroute)
{
  return ((readReroute.wasRerouted() || writeReroute.wasRerouted()) &&
          pathsOnDifferentDrives(readReroute.fileName(), writeReroute.fileName()) &&
          !pathsOnDifferentDrives(lpExistingFileName, lpNewFileName));
}

BOOL WINAPI usvfs::hook_MoveFileA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName)
{
  BOOL res = FALSE;

  HOOK_START_GROUP(MutExHookGroup::SHELL_FILEOP)

  if (!callContext.active()) {
    res = MoveFileA(lpExistingFileName, lpNewFileName);
    callContext.updateLastError();
    return res;
  }

  HOOK_END
  HOOK_START

  const auto& existingFileName = ush::string_cast<std::wstring>(lpExistingFileName);
  const auto& newFileName      = ush::string_cast<std::wstring>(lpNewFileName);

  PRE_REALCALL
  res = MoveFileW(existingFileName.c_str(), newFileName.c_str());
  POST_REALCALL

  HOOK_END

  return res;
}

BOOL WINAPI usvfs::hook_MoveFileW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName)
{
  BOOL res = FALSE;

  HOOK_START_GROUP(MutExHookGroup::SHELL_FILEOP)
  if (!callContext.active()) {
    res = MoveFileW(lpExistingFileName, lpNewFileName);
    callContext.updateLastError();
    return res;
  }

  RerouteW readReroute;
  CreateRerouter writeReroute;
  bool callOriginal = true;
  DWORD newFlags    = 0;

  {
    auto context = READ_CONTEXT();
    readReroute  = RerouteW::create(context, callContext, lpExistingFileName);
    callOriginal = writeReroute.rerouteNew(context, callContext, lpNewFileName, false,
                                           "hook_MoveFileW");
  }

  if (callOriginal) {
    bool movedDrives = rewriteChangedDrives(lpExistingFileName, lpNewFileName,
                                            readReroute, writeReroute);
    if (movedDrives)
      newFlags |= MOVEFILE_COPY_ALLOWED;

    bool isDirectory = pathIsDirectory(readReroute.fileName());

    PRE_REALCALL
    if (isDirectory && movedDrives) {
      SHFILEOPSTRUCTW sf = {0};
      sf.wFunc           = FO_MOVE;
      sf.hwnd            = 0;
      sf.fFlags          = FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR | FOF_NOERRORUI;
      sf.pFrom           = readReroute.fileName();
      sf.pTo             = writeReroute.fileName();
      int shRes          = ::SHFileOperationW(&sf);
      switch (shRes) {
      case 0x78:
        callContext.updateLastError(ERROR_ACCESS_DENIED);
        break;
      case 0x7C:
        callContext.updateLastError(ERROR_FILE_NOT_FOUND);
        break;
      case 0x7E:
      case 0x80:
        callContext.updateLastError(ERROR_FILE_EXISTS);
        break;
      default:
        callContext.updateLastError(shRes);
      }
      res = shRes == 0;
    } else if (newFlags)
      res = ::MoveFileExW(readReroute.fileName(), writeReroute.fileName(), newFlags);
    else
      res = ::MoveFileW(readReroute.fileName(), writeReroute.fileName());
    POST_REALCALL

    if (res)
      SetLastError(ERROR_SUCCESS);

    writeReroute.updateResult(callContext, res);

    if (res) {
      readReroute.removeMapping(
          READ_CONTEXT(), isDirectory);  // Updating the rerouteCreate to check deleted
                                         // file entries should make this okay

      if (writeReroute.newReroute()) {
        if (isDirectory)
          RerouteW::addDirectoryMapping(WRITE_CONTEXT(), fs::path(lpNewFileName),
                                        fs::path(writeReroute.fileName()));
        else
          writeReroute.insertMapping(WRITE_CONTEXT());
      }
    }

    if (readReroute.wasRerouted() || writeReroute.wasRerouted() ||
        writeReroute.changedError())
      LOG_CALL()
          .PARAM(readReroute.fileName())
          .PARAM(writeReroute.fileName())
          .PARAMWRAP(newFlags)
          .PARAM(res)
          .PARAM(writeReroute.originalError())
          .PARAM(callContext.lastError());
  }

  HOOK_END

  return res;
}

BOOL WINAPI usvfs::hook_MoveFileExA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName,
                                    DWORD dwFlags)
{
  BOOL res = FALSE;

  HOOK_START_GROUP(MutExHookGroup::SHELL_FILEOP)

  if (!callContext.active()) {
    res = MoveFileExA(lpExistingFileName, lpNewFileName, dwFlags);
    callContext.updateLastError();
    return res;
  }

  HOOK_END
  HOOK_START

  const std::wstring existingFileName =
      ush::string_cast<std::wstring>(lpExistingFileName);

  // careful: lpNewFileName can be null if dwFlags is
  // MOVEFILE_DELAY_UNTIL_REBOOT, so don't blindly cast the string and make sure
  // the null pointer is forwarded correctly
  std::wstring newFileNameWstring;
  const wchar_t* newFileName = nullptr;

  if (lpNewFileName) {
    newFileNameWstring = ush::string_cast<std::wstring>(lpNewFileName);
    newFileName        = newFileNameWstring.c_str();
  }

  PRE_REALCALL
  res = MoveFileExW(existingFileName.c_str(), newFileName, dwFlags);
  POST_REALCALL

  HOOK_END

  return res;
}

BOOL WINAPI usvfs::hook_MoveFileExW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName,
                                    DWORD dwFlags)
{
  BOOL res = FALSE;

  HOOK_START_GROUP(MutExHookGroup::SHELL_FILEOP)
  if (!callContext.active()) {
    res = MoveFileExW(lpExistingFileName, lpNewFileName, dwFlags);
    callContext.updateLastError();
    return res;
  }

  RerouteW readReroute;
  CreateRerouter writeReroute;
  bool callOriginal = true;
  DWORD newFlags    = dwFlags;

  {
    auto context = READ_CONTEXT();
    readReroute  = RerouteW::create(context, callContext, lpExistingFileName);
    callOriginal = writeReroute.rerouteNew(context, callContext, lpNewFileName,
                                           newFlags & MOVEFILE_REPLACE_EXISTING,
                                           "hook_MoveFileExW");
  }

  if (callOriginal) {
    bool movedDrives = rewriteChangedDrives(lpExistingFileName, lpNewFileName,
                                            readReroute, writeReroute);

    bool isDirectory = pathIsDirectory(readReroute.fileName());

    PRE_REALCALL
    if (isDirectory && movedDrives) {
      SHFILEOPSTRUCTW sf = {0};
      sf.wFunc           = FO_MOVE;
      sf.hwnd            = 0;
      sf.fFlags          = FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR | FOF_NOERRORUI;
      sf.pFrom           = readReroute.fileName();
      sf.pTo             = writeReroute.fileName();
      int shRes          = ::SHFileOperationW(&sf);
      switch (shRes) {
      case 0x78:
        callContext.updateLastError(ERROR_ACCESS_DENIED);
        break;
      case 0x7C:
        callContext.updateLastError(ERROR_FILE_NOT_FOUND);
        break;
      case 0x7E:
      case 0x80:
        callContext.updateLastError(ERROR_FILE_EXISTS);
        break;
      default:
        callContext.updateLastError(shRes);
      }
      res = shRes == 0;
    } else
      res = ::MoveFileExW(readReroute.fileName(), writeReroute.fileName(), newFlags);
    POST_REALCALL

    if (res)
      SetLastError(ERROR_SUCCESS);

    writeReroute.updateResult(callContext, res);

    if (res) {
      readReroute.removeMapping(
          READ_CONTEXT(), isDirectory);  // Updating the rerouteCreate to check deleted
                                         // file entries should make this okay

      if (writeReroute.newReroute()) {
        if (isDirectory)
          RerouteW::addDirectoryMapping(WRITE_CONTEXT(), fs::path(lpNewFileName),
                                        fs::path(writeReroute.fileName()));
        else
          writeReroute.insertMapping(WRITE_CONTEXT());
      }
    }

    if (readReroute.wasRerouted() || writeReroute.wasRerouted() ||
        writeReroute.changedError())
      LOG_CALL()
          .PARAM(readReroute.fileName())
          .PARAM(writeReroute.fileName())
          .PARAMWRAP(dwFlags)
          .PARAMWRAP(newFlags)
          .PARAM(res)
          .PARAM(writeReroute.originalError())
          .PARAM(callContext.lastError());
  }

  HOOK_END

  return res;
}

BOOL WINAPI usvfs::hook_MoveFileWithProgressA(LPCSTR lpExistingFileName,
                                              LPCSTR lpNewFileName,
                                              LPPROGRESS_ROUTINE lpProgressRoutine,
                                              LPVOID lpData, DWORD dwFlags)
{
  BOOL res = FALSE;

  HOOK_START_GROUP(MutExHookGroup::SHELL_FILEOP)

  if (!callContext.active()) {
    res = MoveFileWithProgressA(lpExistingFileName, lpNewFileName, lpProgressRoutine,
                                lpData, dwFlags);
    callContext.updateLastError();
    return res;
  }

  HOOK_END
  HOOK_START

  const auto& existingFileName = ush::string_cast<std::wstring>(lpExistingFileName);

  // careful: lpNewFileName can be null if dwFlags is
  // MOVEFILE_DELAY_UNTIL_REBOOT, so don't blindly cast the string and make sure
  // the null pointer is forwarded correctly
  std::wstring newFileNameWstring;
  const wchar_t* newFileName = nullptr;

  if (lpNewFileName) {
    newFileNameWstring = ush::string_cast<std::wstring>(lpNewFileName);
    newFileName        = newFileNameWstring.c_str();
  }

  PRE_REALCALL
  res = MoveFileWithProgressW(existingFileName.c_str(), newFileName, lpProgressRoutine,
                              lpData, dwFlags);
  POST_REALCALL

  HOOK_END

  return res;
}

BOOL WINAPI usvfs::hook_MoveFileWithProgressW(LPCWSTR lpExistingFileName,
                                              LPCWSTR lpNewFileName,
                                              LPPROGRESS_ROUTINE lpProgressRoutine,
                                              LPVOID lpData, DWORD dwFlags)
{

  // TODO: Remove all redundant hooks to moveFile alternatives.
  // it would appear that all other moveFile functions end up calling this one with no
  // additional code.
  BOOL res = FALSE;

  HOOK_START_GROUP(MutExHookGroup::SHELL_FILEOP)
  if (!callContext.active()) {
    res = MoveFileWithProgressW(lpExistingFileName, lpNewFileName, lpProgressRoutine,
                                lpData, dwFlags);
    callContext.updateLastError();
    return res;
  }

  RerouteW readReroute;
  usvfs::CreateRerouter writeReroute;
  bool callOriginal = true;
  DWORD newFlags    = dwFlags;

  {
    auto context = READ_CONTEXT();
    readReroute  = RerouteW::create(context, callContext, lpExistingFileName);
    callOriginal = writeReroute.rerouteNew(context, callContext, lpNewFileName,
                                           newFlags & MOVEFILE_REPLACE_EXISTING,
                                           "hook_MoveFileWithProgressW");
  }

  if (callOriginal) {
    bool movedDrives = rewriteChangedDrives(lpExistingFileName, lpNewFileName,
                                            readReroute, writeReroute);
    if (movedDrives)
      newFlags |= MOVEFILE_COPY_ALLOWED;

    bool isDirectory = pathIsDirectory(readReroute.fileName());

    PRE_REALCALL
    if (isDirectory && movedDrives) {
      SHFILEOPSTRUCTW sf = {0};
      sf.wFunc           = FO_MOVE;
      sf.hwnd            = 0;
      sf.fFlags          = FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR | FOF_NOERRORUI;
      sf.pFrom           = readReroute.fileName();
      sf.pTo             = writeReroute.fileName();
      int shRes          = ::SHFileOperationW(&sf);
      switch (shRes) {
      case 0x78:
        callContext.updateLastError(ERROR_ACCESS_DENIED);
        break;
      case 0x7C:
        callContext.updateLastError(ERROR_FILE_NOT_FOUND);
        break;
      case 0x7E:
      case 0x80:
        callContext.updateLastError(ERROR_FILE_EXISTS);
        break;
      default:
        callContext.updateLastError(shRes);
      }
      res = shRes == 0;
    } else
      res = ::MoveFileWithProgressW(readReroute.fileName(), writeReroute.fileName(),
                                    lpProgressRoutine, lpData, newFlags);
    POST_REALCALL

    if (res)
      SetLastError(ERROR_SUCCESS);

    writeReroute.updateResult(callContext, res);

    if (res) {
      // TODO: this call causes the node to be removed twice in case of
      // MOVEFILE_COPY_ALLOWED as the deleteFile hook lower level already takes care of
      // it, but deleteFile can't be disabled since we are relying on it in case of
      // MOVEFILE_REPLACE_EXISTING for the destination file.
      readReroute.removeMapping(
          READ_CONTEXT(),
          isDirectory);  // Updating the rerouteCreate to check deleted file entries
                         // should make this okay (not related to comments above)

      if (writeReroute.newReroute()) {
        if (isDirectory)
          RerouteW::addDirectoryMapping(WRITE_CONTEXT(), fs::path(lpNewFileName),
                                        fs::path(writeReroute.fileName()));
        else
          writeReroute.insertMapping(WRITE_CONTEXT());
      }
    }

    if (readReroute.wasRerouted() || writeReroute.wasRerouted() ||
        writeReroute.changedError())
      LOG_CALL()
          .PARAM(readReroute.fileName())
          .PARAM(writeReroute.fileName())
          .PARAMWRAP(dwFlags)
          .PARAMWRAP(newFlags)
          .PARAM(res)
          .PARAM(writeReroute.originalError())
          .PARAM(callContext.lastError());
  }

  HOOK_END

  return res;
}

BOOL WINAPI usvfs::hook_CopyFileExW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName,
                                    LPPROGRESS_ROUTINE lpProgressRoutine, LPVOID lpData,
                                    LPBOOL pbCancel, DWORD dwCopyFlags)
{
  BOOL res = FALSE;

  HOOK_START_GROUP(MutExHookGroup::SHELL_FILEOP)
  if (!callContext.active()) {
    res = CopyFileExW(lpExistingFileName, lpNewFileName, lpProgressRoutine, lpData,
                      pbCancel, dwCopyFlags);
    callContext.updateLastError();
    return res;
  }

  RerouteW readReroute;
  usvfs::CreateRerouter writeReroute;
  bool callOriginal = true;

  {
    auto context = READ_CONTEXT();
    readReroute  = RerouteW::create(context, callContext, lpExistingFileName);
    callOriginal = writeReroute.rerouteNew(
        context, callContext, lpNewFileName,
        (dwCopyFlags & COPY_FILE_FAIL_IF_EXISTS) == 0, "hook_CopyFileExW");
  }

  if (callOriginal) {
    PRE_REALCALL
    res = ::CopyFileExW(readReroute.fileName(), writeReroute.fileName(),
                        lpProgressRoutine, lpData, pbCancel, dwCopyFlags);
    POST_REALCALL
    writeReroute.updateResult(callContext, res);

    if (res && writeReroute.newReroute())
      writeReroute.insertMapping(WRITE_CONTEXT());

    if (readReroute.wasRerouted() || writeReroute.wasRerouted() ||
        writeReroute.changedError())
      LOG_CALL()
          .PARAM(readReroute.fileName())
          .PARAM(writeReroute.fileName())
          .PARAM(res)
          .PARAM(writeReroute.originalError())
          .PARAM(callContext.lastError());
  }

  HOOK_END

  return res;
}

DWORD WINAPI usvfs::hook_GetCurrentDirectoryA(DWORD nBufferLength, LPSTR lpBuffer)
{
  DWORD res = 0;

  HOOK_START

  std::wstring buffer;
  buffer.resize(nBufferLength);

  PRE_REALCALL
  res = GetCurrentDirectoryW(nBufferLength, &buffer[0]);
  POST_REALCALL

  if (res > 0) {
    res = WideCharToMultiByte(CP_ACP, 0, buffer.c_str(), res + 1, lpBuffer,
                              nBufferLength, nullptr, nullptr);
  }

  HOOK_END

  return res;
}

DWORD WINAPI usvfs::hook_GetCurrentDirectoryW(DWORD nBufferLength, LPWSTR lpBuffer)
{
  DWORD res = FALSE;

  HOOK_START

  std::wstring actualCWD;

  if (!k32CurrentDirectoryTracker.get(actualCWD)) {
    PRE_REALCALL
    res = ::GetCurrentDirectoryW(nBufferLength, lpBuffer);
    POST_REALCALL
  } else {
    ush::wcsncpy_sz(lpBuffer, &actualCWD[0],
                    std::min(static_cast<size_t>(nBufferLength), actualCWD.size() + 1));

    // yupp, that's how GetCurrentDirectory actually works...
    if (actualCWD.size() < nBufferLength) {
      res = static_cast<DWORD>(actualCWD.size());
    } else {
      res = static_cast<DWORD>(actualCWD.size() + 1);
    }
  }

  if (nBufferLength)
    LOG_CALL()
        .PARAM(std::wstring(lpBuffer, res))
        .PARAM(nBufferLength)
        .PARAM(actualCWD.size())
        .PARAM(res)
        .PARAM(callContext.lastError());

  HOOK_END

  return res;
}

BOOL WINAPI usvfs::hook_SetCurrentDirectoryA(LPCSTR lpPathName)
{
  return SetCurrentDirectoryW(ush::string_cast<std::wstring>(lpPathName).c_str());
}

BOOL WINAPI usvfs::hook_SetCurrentDirectoryW(LPCWSTR lpPathName)
{
  BOOL res = FALSE;

  HOOK_START

  const fs::path& realPath = RerouteW::canonizePath(RerouteW::absolutePath(lpPathName));
  const std::wstring& realPathStr = realPath.wstring();
  std::wstring finalRoute;
  BOOL found = FALSE;

  if (fs::exists(realPath))
    finalRoute = realPathStr;
  else {
    WCHAR processDir[MAX_PATH];
    if (::GetModuleFileNameW(NULL, processDir, MAX_PATH) != 0 &&
        ::PathRemoveFileSpecW(processDir)) {
      WCHAR processName[MAX_PATH];
      ::GetModuleFileNameW(NULL, processName, MAX_PATH);
      fs::path routedName = realPath / processName;
      RerouteW rerouteTest =
          RerouteW::create(READ_CONTEXT(), callContext, routedName.wstring().c_str());
      if (rerouteTest.wasRerouted()) {
        std::wstring reroutedPath = rerouteTest.fileName();
        if (routedName.wstring().find(processDir) != std::string::npos) {
          fs::path finalPath(reroutedPath);
          finalRoute = finalPath.parent_path().wstring();
          found      = TRUE;
        }
      }
    }

    if (!found) {
      RerouteW reroute =
          RerouteW::create(READ_CONTEXT(), callContext, realPathStr.c_str());
      finalRoute = reroute.fileName();
    }
  }

  PRE_REALCALL
  res = ::SetCurrentDirectoryW(finalRoute.c_str());
  POST_REALCALL

  if (res)
    if (!k32CurrentDirectoryTracker.set(realPathStr))
      spdlog::get("usvfs")->warn("Updating actual current directory failed: {} ?!",
                                 string_cast<std::string>(realPathStr));

  LOG_CALL()
      .PARAM(lpPathName)
      .PARAM(realPathStr)
      .PARAM(finalRoute)
      .PARAM(res)
      .PARAM(callContext.lastError());

  HOOK_END

  return res;
}

DLLEXPORT BOOL WINAPI usvfs::hook_CreateDirectoryW(
    LPCWSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
  BOOL res = FALSE;
  HOOK_START

  RerouteW reroute = RerouteW::createOrNew(READ_CONTEXT(), callContext, lpPathName);

  PRE_REALCALL
  res = ::CreateDirectoryW(reroute.fileName(), lpSecurityAttributes);
  POST_REALCALL

  if (res && reroute.newReroute())
    reroute.insertMapping(WRITE_CONTEXT(), true);

  if (reroute.wasRerouted())
    LOG_CALL()
        .PARAM(lpPathName)
        .PARAM(reroute.fileName())
        .PARAM(res)
        .PARAM(callContext.lastError());

  HOOK_END

  return res;
}

DLLEXPORT BOOL WINAPI usvfs::hook_RemoveDirectoryW(LPCWSTR lpPathName)
{

  BOOL res = FALSE;

  HOOK_START_GROUP(MutExHookGroup::DELETE_FILE)
  // Why is the usual if (!callContext.active()... check missing?

  RerouteW reroute = RerouteW::create(READ_CONTEXT(), callContext, lpPathName);

  PRE_REALCALL
  if (reroute.wasRerouted()) {
    res = ::RemoveDirectoryW(reroute.fileName());
  } else {
    res = ::RemoveDirectoryW(lpPathName);
  }
  POST_REALCALL

  if (res) {
    reroute.removeMapping(READ_CONTEXT(), true);
  }

  if (reroute.wasRerouted())
    LOG_CALL()
        .PARAM(lpPathName)
        .PARAM(reroute.fileName())
        .PARAM(res)
        .PARAM(callContext.lastError());

  HOOK_END

  return res;
}

DWORD WINAPI usvfs::hook_GetFullPathNameA(LPCSTR lpFileName, DWORD nBufferLength,
                                          LPSTR lpBuffer, LPSTR* lpFilePart)
{
  DWORD res = 0UL;

  HOOK_START_GROUP(MutExHookGroup::FULL_PATHNAME)
  if (!callContext.active()) {
    res = GetFullPathNameA(lpFileName, nBufferLength, lpBuffer, lpFilePart);
    callContext.updateLastError();
    return res;
  }

  std::string resolvedWithCMD;

  std::wstring actualCWD;
  fs::path filePath = ush::string_cast<std::wstring>(lpFileName, CodePage::UTF8);
  if (k32CurrentDirectoryTracker.get(actualCWD, filePath.wstring().c_str())) {
    if (!filePath.is_absolute())
      resolvedWithCMD = ush::string_cast<std::string>(
          (actualCWD / filePath.relative_path()).wstring());
  }

  PRE_REALCALL
  res =
      ::GetFullPathNameA(resolvedWithCMD.empty() ? lpFileName : resolvedWithCMD.c_str(),
                         nBufferLength, lpBuffer, lpFilePart);
  POST_REALCALL

  if (false && nBufferLength)
    LOG_CALL()
        .PARAM(lpFileName)
        .PARAM(resolvedWithCMD)
        .PARAM(std::string(lpBuffer, res))
        .PARAM(nBufferLength)
        .PARAM(res)
        .PARAM(callContext.lastError());

  HOOK_END

  return res;
}

DWORD WINAPI usvfs::hook_GetFullPathNameW(LPCWSTR lpFileName, DWORD nBufferLength,
                                          LPWSTR lpBuffer, LPWSTR* lpFilePart)
{
  DWORD res = 0UL;

  HOOK_START_GROUP(MutExHookGroup::FULL_PATHNAME)
  if (!callContext.active()) {
    res = GetFullPathNameW(lpFileName, nBufferLength, lpBuffer, lpFilePart);
    callContext.updateLastError();
    return res;
  }

  std::wstring resolvedWithCMD;

  std::wstring actualCWD;
  if (k32CurrentDirectoryTracker.get(actualCWD, lpFileName)) {
    fs::path filePath = lpFileName;
    if (!filePath.is_absolute())
      resolvedWithCMD = (actualCWD / filePath.relative_path()).wstring();
  }

  PRE_REALCALL
  res =
      ::GetFullPathNameW(resolvedWithCMD.empty() ? lpFileName : resolvedWithCMD.c_str(),
                         nBufferLength, lpBuffer, lpFilePart);
  POST_REALCALL

  if (false && nBufferLength)
    LOG_CALL()
        .PARAM(lpFileName)
        .PARAM(resolvedWithCMD)
        .PARAM(std::wstring(lpBuffer, res))
        .PARAM(nBufferLength)
        .PARAM(res)
        .PARAM(callContext.lastError());

  HOOK_END

  return res;
}

DWORD WINAPI usvfs::hook_GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename,
                                            DWORD nSize)
{
  DWORD res = 0UL;

  HOOK_START_GROUP(MutExHookGroup::ALL_GROUPS)

  PRE_REALCALL
  res = ::GetModuleFileNameA(hModule, lpFilename, nSize);
  POST_REALCALL
  if ((res != 0) && callContext.active()) {
    std::vector<char> buf;
    // If GetModuleFileNameA failed because the buffer is not large enough this
    // complicates matters because we are dealing with incomplete information
    // (consider for example the case that we have a long real path which will
    // be routed to a short virtual so the call should actually succeed in such
    // a case). To solve this we simply use our own buffer to find the complete
    // module path:
    DWORD full_res  = res;
    size_t buf_size = nSize;
    while (full_res == buf_size) {
      buf_size = std::max(static_cast<size_t>(MAX_PATH), buf_size * 2);
      buf.resize(buf_size);
      full_res = ::GetModuleFileNameA(hModule, buf.data(), buf_size);
    }

    RerouteW reroute = RerouteW::create(
        READ_CONTEXT(), callContext,
        ush::string_cast<std::wstring>(buf.empty() ? lpFilename : buf.data()).c_str(),
        true);
    if (reroute.wasRerouted()) {
      DWORD reroutedSize = static_cast<DWORD>(wcslen(reroute.fileName()));
      if (reroutedSize >= nSize) {
        reroutedSize = nSize - 1;
        callContext.updateLastError(ERROR_INSUFFICIENT_BUFFER);
        res = nSize;
      } else
        res = reroutedSize;
      memcpy(lpFilename, ush::string_cast<std::string>(reroute.fileName()).c_str(),
             reroutedSize * sizeof(lpFilename[0]));
      lpFilename[reroutedSize] = 0;

      LOG_CALL()
          .PARAM(hModule)
          .addParam("lpFilename", (res != 0UL) ? lpFilename : "<not set>")
          .PARAM(nSize)
          .PARAM(res)
          .PARAM(callContext.lastError());
    }
  }
  HOOK_END

  return res;
}

DWORD WINAPI usvfs::hook_GetModuleFileNameW(HMODULE hModule, LPWSTR lpFilename,
                                            DWORD nSize)
{
  DWORD res = 0UL;

  HOOK_START_GROUP(MutExHookGroup::ALL_GROUPS)

  PRE_REALCALL
  res = ::GetModuleFileNameW(hModule, lpFilename, nSize);
  POST_REALCALL
  if ((res != 0) && callContext.active()) {
    std::vector<WCHAR> buf;
    // If GetModuleFileNameW failed because the buffer is not large enough this
    // complicates matters because we are dealing with incomplete information (consider
    // for example the case that we have a long real path which will be routed to a
    // short virtual so the call should actually succeed in such a case). To solve this
    // we simply use our own buffer to find the complete module path:
    DWORD full_res  = res;
    size_t buf_size = nSize;
    while (full_res == buf_size) {
      buf_size = std::max(static_cast<size_t>(MAX_PATH), buf_size * 2);
      buf.resize(buf_size);
      full_res = ::GetModuleFileNameW(hModule, buf.data(), buf_size);
    }

    RerouteW reroute = RerouteW::create(READ_CONTEXT(), callContext,
                                        buf.empty() ? lpFilename : buf.data(), true);
    if (reroute.wasRerouted()) {
      DWORD reroutedSize = static_cast<DWORD>(wcslen(reroute.fileName()));
      if (reroutedSize >= nSize) {
        reroutedSize = nSize - 1;
        callContext.updateLastError(ERROR_INSUFFICIENT_BUFFER);
        res = nSize;
      } else
        res = reroutedSize;
      memcpy(lpFilename, reroute.fileName(), reroutedSize * sizeof(lpFilename[0]));
      lpFilename[reroutedSize] = 0;

      LOG_CALL()
          .PARAM(hModule)
          .addParam("lpFilename", (res != 0UL) ? lpFilename : L"<not set>")
          .PARAM(nSize)
          .PARAM(res)
          .PARAM(callContext.lastError());
    }
  }
  HOOK_END

  return res;
}

HANDLE WINAPI usvfs::hook_FindFirstFileExW(
    LPCWSTR lpFileName, FINDEX_INFO_LEVELS fInfoLevelId, LPVOID lpFindFileData,
    FINDEX_SEARCH_OPS fSearchOp, LPVOID lpSearchFilter, DWORD dwAdditionalFlags)
{
  HANDLE res = INVALID_HANDLE_VALUE;

  HOOK_START_GROUP(MutExHookGroup::SEARCH_FILES)
  if (!callContext.active()) {
    res = FindFirstFileExW(lpFileName, fInfoLevelId, lpFindFileData, fSearchOp,
                           lpSearchFilter, dwAdditionalFlags);
    callContext.updateLastError();
    return res;
  }

  // FindFirstFileEx() must fail early if the path ends with a slash
  if (lpFileName) {
    const auto len = wcslen(lpFileName);
    if (len > 0) {
      if (lpFileName[len - 1] == L'\\' || lpFileName[len - 1] == L'/') {
        spdlog::get("usvfs")->warn(
            "hook_FindFirstFileExW(): path '{}' ends with slash, always fails",
            fs::path(lpFileName).string());
        return INVALID_HANDLE_VALUE;
      }
    }
  }

  fs::path finalPath;
  RerouteW reroute;
  fs::path originalPath;

  bool usedRewrite = false;

  // We need to do some trickery here, since we only want to use the hooked
  // NtQueryDirectoryFile for rerouted locations we need to check if the Directory path
  // has been routed instead of the full path.
  originalPath = RerouteW::canonizePath(RerouteW::absolutePath(lpFileName));
  PRE_REALCALL
  res = ::FindFirstFileExW(originalPath.c_str(), fInfoLevelId, lpFindFileData,
                           fSearchOp, lpSearchFilter, dwAdditionalFlags);
  POST_REALCALL

  if (res == INVALID_HANDLE_VALUE) {
    fs::path searchPath   = originalPath.filename();
    fs::path parentPath   = originalPath.parent_path();
    std::wstring findPath = parentPath.wstring();
    while (findPath.find(L"*?<>\"", 0, 1) != std::wstring::npos) {
      searchPath = parentPath.filename() / searchPath;
      parentPath = parentPath.parent_path();
      findPath   = parentPath.wstring();
    }
    reroute = RerouteW::create(READ_CONTEXT(), callContext, parentPath.c_str());
    if (reroute.wasRerouted()) {
      finalPath = reroute.fileName();
      finalPath /= searchPath.wstring();
    }
    if (!finalPath.empty()) {
      PRE_REALCALL
      usedRewrite = true;
      res         = ::FindFirstFileExW(finalPath.c_str(), fInfoLevelId, lpFindFileData,
                                       fSearchOp, lpSearchFilter, dwAdditionalFlags);
      POST_REALCALL
    }
  }

  if (res != INVALID_HANDLE_VALUE) {
    // store the original search path for use during iteration
    WRITE_CONTEXT()->customData<SearchHandleMap>(SearchHandles)[res] = lpFileName;
  }

  LOG_CALL()
      .PARAM(lpFileName)
      .PARAM(originalPath.c_str())
      .PARAM(res)
      .PARAM(callContext.lastError());
  if (usedRewrite)
    LOG_CALL()
        .PARAM(lpFileName)
        .PARAM(finalPath.c_str())
        .PARAM(res)
        .PARAM(callContext.lastError());

  HOOK_END

  return res;
}

HRESULT(WINAPI* usvfs::CopyFile2)(PCWSTR pwszExistingFileName, PCWSTR pwszNewFileName,
                                  COPYFILE2_EXTENDED_PARAMETERS* pExtendedParameters);

HRESULT WINAPI usvfs::hook_CopyFile2(PCWSTR pwszExistingFileName,
                                     PCWSTR pwszNewFileName,
                                     COPYFILE2_EXTENDED_PARAMETERS* pExtendedParameters)
{
  HRESULT res = E_FAIL;

  typedef HRESULT(WINAPI * CopyFile2_t)(PCWSTR, PCWSTR, COPYFILE2_EXTENDED_PARAMETERS*);

  HOOK_START_GROUP(MutExHookGroup::SHELL_FILEOP)
  if (!callContext.active()) {
    res = CopyFile2(pwszExistingFileName, pwszNewFileName, pExtendedParameters);
    callContext.updateLastError();
    return res;
  }

  RerouteW readReroute;
  CreateRerouter writeReroute;
  bool callOriginal = true;

  {
    auto context = READ_CONTEXT();
    readReroute  = RerouteW::create(context, callContext, pwszExistingFileName);
    callOriginal = writeReroute.rerouteNew(
        context, callContext, pwszNewFileName,
        pExtendedParameters &&
            (pExtendedParameters->dwCopyFlags & COPY_FILE_FAIL_IF_EXISTS) == 0,
        "hook_CopyFile2");
  }

  if (callOriginal) {
    PRE_REALCALL
    res =
        CopyFile2(readReroute.fileName(), writeReroute.fileName(), pExtendedParameters);
    POST_REALCALL
    writeReroute.updateResult(callContext, SUCCEEDED(res));

    if (SUCCEEDED(res) && writeReroute.newReroute())
      writeReroute.insertMapping(WRITE_CONTEXT());

    if (readReroute.wasRerouted() || writeReroute.wasRerouted() ||
        writeReroute.changedError())
      LOG_CALL()
          .PARAM(readReroute.fileName())
          .PARAM(writeReroute.fileName())
          .PARAM(res)
          .PARAM(writeReroute.originalError())
          .PARAM(callContext.lastError());
  }

  HOOK_END

  return res;
}

DWORD WINAPI usvfs::hook_GetPrivateProfileStringA(LPCSTR lpAppName, LPCSTR lpKeyName,
                                                  LPCSTR lpDefault,
                                                  LPSTR lpReturnedString, DWORD nSize,
                                                  LPCSTR lpFileName)
{
  DWORD res = 0;

  HOOK_START_GROUP(MutExHookGroup::OPEN_FILE)

  if (!callContext.active() || !RerouteW::interestingPath(lpFileName)) {
    res = ::GetPrivateProfileStringA(lpAppName, lpKeyName, lpDefault, lpReturnedString,
                                     nSize, lpFileName);
    callContext.updateLastError();
    return res;
  }

  RerouteW reroute = RerouteW::create(
      READ_CONTEXT(), callContext, ush::string_cast<std::wstring>(lpFileName).c_str());

  PRE_REALCALL
  res = ::GetPrivateProfileStringA(
      lpAppName, lpKeyName, lpDefault, lpReturnedString, nSize,
      ush::string_cast<std::string>(reroute.fileName()).c_str());
  POST_REALCALL

  if (reroute.wasRerouted()) {
    LOG_CALL()
        .PARAM(lpAppName)
        .PARAM(lpKeyName)
        .PARAM(lpFileName)
        .PARAM(reroute.fileName())
        .PARAMHEX(res)
        .PARAM(callContext.lastError());
  }

  HOOK_END

  return res;
}

DWORD WINAPI usvfs::hook_GetPrivateProfileStringW(LPCWSTR lpAppName, LPCWSTR lpKeyName,
                                                  LPCWSTR lpDefault,
                                                  LPWSTR lpReturnedString, DWORD nSize,
                                                  LPCWSTR lpFileName)
{
  DWORD res = 0;

  HOOK_START_GROUP(MutExHookGroup::OPEN_FILE)

  if (!callContext.active() || !RerouteW::interestingPath(lpFileName)) {
    res = ::GetPrivateProfileStringW(lpAppName, lpKeyName, lpDefault, lpReturnedString,
                                     nSize, lpFileName);
    callContext.updateLastError();
    return res;
  }

  RerouteW reroute = RerouteW::create(READ_CONTEXT(), callContext, lpFileName);

  PRE_REALCALL
  res = ::GetPrivateProfileStringW(lpAppName, lpKeyName, lpDefault, lpReturnedString,
                                   nSize, reroute.fileName());
  POST_REALCALL

  if (reroute.wasRerouted()) {
    LOG_CALL()
        .PARAM(lpAppName)
        .PARAM(lpKeyName)
        .PARAM(lpFileName)
        .PARAM(reroute.fileName())
        .PARAMHEX(res)
        .PARAM(callContext.lastError());
  }

  HOOK_END

  return res;
}

DWORD WINAPI usvfs::hook_GetPrivateProfileSectionA(LPCSTR lpAppName,
                                                   LPSTR lpReturnedString, DWORD nSize,
                                                   LPCSTR lpFileName)
{
  DWORD res = 0;

  HOOK_START_GROUP(MutExHookGroup::OPEN_FILE)

  if (!callContext.active() || !RerouteW::interestingPath(lpFileName)) {
    res = ::GetPrivateProfileSectionA(lpAppName, lpReturnedString, nSize, lpFileName);
    callContext.updateLastError();
    return res;
  }

  RerouteW reroute = RerouteW::create(
      READ_CONTEXT(), callContext, ush::string_cast<std::wstring>(lpFileName).c_str());

  PRE_REALCALL
  res = ::GetPrivateProfileSectionA(
      lpAppName, lpReturnedString, nSize,
      ush::string_cast<std::string>(reroute.fileName()).c_str());
  POST_REALCALL

  if (reroute.wasRerouted()) {
    LOG_CALL()
        .PARAM(lpAppName)
        .PARAM(lpFileName)
        .PARAM(reroute.fileName())
        .PARAMHEX(res)
        .PARAM(callContext.lastError());
  }

  HOOK_END

  return res;
}

DWORD WINAPI usvfs::hook_GetPrivateProfileSectionW(LPCWSTR lpAppName,
                                                   LPWSTR lpReturnedString, DWORD nSize,
                                                   LPCWSTR lpFileName)
{
  DWORD res = 0;

  HOOK_START_GROUP(MutExHookGroup::OPEN_FILE)

  if (!callContext.active() || !RerouteW::interestingPath(lpFileName)) {
    res = ::GetPrivateProfileSectionW(lpAppName, lpReturnedString, nSize, lpFileName);
    callContext.updateLastError();
    return res;
  }

  RerouteW reroute = RerouteW::create(READ_CONTEXT(), callContext, lpFileName);

  PRE_REALCALL
  res = ::GetPrivateProfileSectionW(lpAppName, lpReturnedString, nSize,
                                    reroute.fileName());
  POST_REALCALL

  if (reroute.wasRerouted()) {
    LOG_CALL()
        .PARAM(lpAppName)
        .PARAM(lpFileName)
        .PARAM(reroute.fileName())
        .PARAMHEX(res)
        .PARAM(callContext.lastError());
  }

  HOOK_END

  return res;
}

BOOL WINAPI usvfs::hook_WritePrivateProfileStringA(LPCSTR lpAppName, LPCSTR lpKeyName,
                                                   LPCSTR lpString, LPCSTR lpFileName)
{
  BOOL res = false;

  HOOK_START_GROUP(MutExHookGroup::OPEN_FILE)

  if (!callContext.active() || !RerouteW::interestingPath(lpFileName)) {
    res = ::WritePrivateProfileStringA(lpAppName, lpKeyName, lpString, lpFileName);
    callContext.updateLastError();
    return res;
  }

  CreateRerouter reroute;
  bool callOriginal = reroute.rerouteNew(
      READ_CONTEXT(), callContext, ush::string_cast<std::wstring>(lpFileName).c_str(),
      true, "hook_WritePrivateProfileStringA");

  if (callOriginal) {
    PRE_REALCALL
    res = ::WritePrivateProfileStringA(
        lpAppName, lpKeyName, lpString,
        ush::string_cast<std::string>(reroute.fileName()).c_str());
    POST_REALCALL
    reroute.updateResult(callContext, res);

    if (res && reroute.newReroute())
      reroute.insertMapping(WRITE_CONTEXT());

    if (reroute.wasRerouted() || reroute.changedError())
      LOG_CALL()
          .PARAM(lpAppName)
          .PARAM(lpKeyName)
          .PARAM(lpFileName)
          .PARAM(reroute.fileName())
          .PARAMHEX(res)
          .PARAM(reroute.originalError())
          .PARAM(callContext.lastError());
  }

  HOOK_END

  return res;
}

BOOL WINAPI usvfs::hook_WritePrivateProfileStringW(LPCWSTR lpAppName, LPCWSTR lpKeyName,
                                                   LPCWSTR lpString, LPCWSTR lpFileName)
{
  BOOL res = false;

  HOOK_START_GROUP(MutExHookGroup::OPEN_FILE)

  if (!callContext.active() || !RerouteW::interestingPath(lpFileName)) {
    res = ::WritePrivateProfileStringW(lpAppName, lpKeyName, lpString, lpFileName);
    callContext.updateLastError();
    return res;
  }

  CreateRerouter reroute;
  bool callOriginal = reroute.rerouteNew(READ_CONTEXT(), callContext, lpFileName, true,
                                         "hook_WritePrivateProfileStringW");

  if (callOriginal) {
    PRE_REALCALL
    res = ::WritePrivateProfileStringW(lpAppName, lpKeyName, lpString,
                                       reroute.fileName());
    POST_REALCALL
    reroute.updateResult(callContext, res);

    if (res && reroute.newReroute())
      reroute.insertMapping(WRITE_CONTEXT());

    if (reroute.wasRerouted() || reroute.changedError())
      LOG_CALL()
          .PARAM(lpAppName)
          .PARAM(lpKeyName)
          .PARAM(lpFileName)
          .PARAM(reroute.fileName())
          .PARAMHEX(res)
          .PARAM(reroute.originalError())
          .PARAM(callContext.lastError());
  }

  HOOK_END

  return res;
}

VOID WINAPI usvfs::hook_ExitProcess(UINT exitCode)
{
  HOOK_START

  {
    HookContext::Ptr context = WRITE_CONTEXT();

    std::vector<std::future<int>>& delayed = context->delayed();

    if (!delayed.empty()) {
      // ensure all delayed tasks are completed before we exit the process
      for (std::future<int>& delayedOp : delayed) {
        delayedOp.get();
      }
      delayed.clear();
    }
  }

  // exitprocess doesn't return so logging the call after the real call doesn't
  // make much sense.
  // nor does any pre/post call macro
  LOG_CALL().PARAM(exitCode);

  usvfsDisconnectVFS();

  //  HookManager::instance().removeHook("ExitProcess");
  //  PRE_REALCALL
  ::ExitProcess(exitCode);
  //  POST_REALCALL

  HOOK_END
}
