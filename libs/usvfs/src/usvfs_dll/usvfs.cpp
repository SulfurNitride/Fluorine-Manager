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
#include "usvfs.h"
#include "hookmanager.h"
#include "loghelpers.h"
#include "redirectiontree.h"
#include "usvfs_version.h"
#include "usvfsparametersprivate.h"
#include <inject.h>
#include <shmlogger.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <stringcast.h>
#include <ttrampolinepool.h>
#include <winapi.h>

// note that there's a mix of boost and std filesystem stuff in this file and
// that they're not completely compatible
#include <filesystem>

namespace bfs = boost::filesystem;
namespace ush = usvfs::shared;
namespace bip = boost::interprocess;
namespace ba  = boost::algorithm;

using usvfs::log::ConvertLogLevel;

usvfs::HookManager* manager    = nullptr;
usvfs::HookContext* context    = nullptr;
HMODULE dllModule              = nullptr;
PVOID exceptionHandler         = nullptr;
CrashDumpsType usvfs_dump_type = CrashDumpsType::None;
std::wstring usvfs_dump_path;

// this is called for every single file, so it's a bit long winded, but it's
// as fast as it gets, probably
//
template <std::size_t LongestExtension, std::size_t ExtensionsCount>
bool extensionMatchesCI(
    std::string_view name,
    const std::array<std::string_view, ExtensionsCount>& extensionsLC,
    const std::array<std::string_view, ExtensionsCount>& extensionsUC)
{
  constexpr std::size_t longestExtensionWithDot = LongestExtension + 1;

  // quick check
  if (name.size() < longestExtensionWithDot) {
    return false;
  }

  // for each extension
  for (std::size_t i = 0; i < ExtensionsCount; ++i) {
    const std::size_t extensionLength        = extensionsLC[i].size();
    const std::size_t extensionLengthWithDot = extensionLength + 1;

    // check size
    if (name.size() < extensionLengthWithDot) {
      continue;
    }

    // check dot
    if (name[name.size() - extensionLengthWithDot] != '.') {
      continue;
    }

    // starts at one past the dot
    const auto* p = name.data() + name.size() - extensionLength;

    // set to false as soon as a character doesn't match
    bool found = true;

    // for each character in extension
    for (std::size_t c = 0; c < extensionLength; ++c) {
      // checking both lowercase and uppercase
      if (*p != extensionsLC[i][c] && *p != extensionsUC[i][c]) {
        // neither
        found = false;
        break;
      }

      // matches, check next
      ++p;
    }

    if (found) {
      return true;
    }
  }

  return false;
}

bool shouldAddToInverseTree(std::string_view name)
{
  static std::array<std::string_view, 3> extensionsLC{"exe", "dll"};
  static std::array<std::string_view, 3> extensionsUC{"EXE", "DLL"};

  // must be changed if any extension longer than 3 letters is added
  constexpr std::size_t longestExtension = 3;

  return extensionMatchesCI<longestExtension>(name, extensionsLC, extensionsUC);
}

//
// Logging
//

void InitLoggingInternal(bool toConsole, bool connectExistingSHM)
{
  try {
    if (!toConsole && !SHMLogger::isInstantiated()) {
      if (connectExistingSHM) {
        SHMLogger::open("usvfs");
      } else {
        SHMLogger::create("usvfs");
      }
    }

    // a temporary logger was created in DllMain
    spdlog::drop("usvfs");
#pragma message("need a customized name for the shm")
    auto logger = spdlog::get("usvfs");
    if (logger.get() == nullptr) {
      logger = toConsole ? spdlog::create<spdlog::sinks::stdout_sink_mt>("usvfs")
                         : spdlog::create<usvfs::sinks::shm_sink>("usvfs", "usvfs");
      logger->set_pattern("%H:%M:%S.%e [%L] %v");
    }
    logger->set_level(spdlog::level::debug);

    spdlog::drop("hooks");
    logger = spdlog::get("hooks");
    if (logger.get() == nullptr) {
      logger = toConsole ? spdlog::create<spdlog::sinks::stdout_sink_mt>("hooks")
                         : spdlog::create<usvfs::sinks::shm_sink>("hooks", "usvfs");
      logger->set_pattern("%H:%M:%S.%e <%P:%t> [%L] %v");
    }
    logger->set_level(spdlog::level::debug);
  } catch (const std::exception&) {
    // TODO should really report this
    // OutputDebugStringA((boost::format("init exception: %1%\n") %
    // e.what()).str().c_str());
    if (spdlog::get("usvfs").get() == nullptr) {
      spdlog::create<spdlog::sinks::null_sink_mt>("usvfs");
    }
    if (spdlog::get("hooks").get() == nullptr) {
      spdlog::create<spdlog::sinks::null_sink_mt>("hooks");
    }
  }

  spdlog::get("usvfs")->info("usvfs dll {} initialized in process {}",
                             USVFS_VERSION_STRING, GetCurrentProcessId());
}

void WINAPI usvfsInitLogging(bool toConsole)
{
  InitLoggingInternal(toConsole, false);
}

extern "C" DLLEXPORT bool WINAPI usvfsGetLogMessages(LPSTR buffer, size_t size,
                                                     bool blocking)
{
  buffer[0] = '\0';
  try {
    if (blocking) {
      SHMLogger::instance().get(buffer, size);
      return true;
    } else {
      return SHMLogger::instance().tryGet(buffer, size);
    }
  } catch (const std::exception& e) {
    _snprintf_s(buffer, size, _TRUNCATE, "Failed to retrieve log messages: %s",
                e.what());
    return false;
  }
}

void SetLogLevel(LogLevel level)
{
  spdlog::get("usvfs")->set_level(ConvertLogLevel(level));
  spdlog::get("hooks")->set_level(ConvertLogLevel(level));
}

void WINAPI usvfsUpdateParameters(usvfsParameters* p)
{
  spdlog::get("usvfs")->info("updating parameters:\n"
                             " . debugMode: {}\n"
                             " . log level: {}\n"
                             " . dump type: {}\n"
                             " . dump path: {}\n"
                             " . delay process: {}ms",
                             p->debugMode, usvfsLogLevelToString(p->logLevel),
                             usvfsCrashDumpTypeToString(p->crashDumpsType),
                             p->crashDumpsPath, p->delayProcessMs);

  // update actual values used:
  usvfs_dump_type = p->crashDumpsType;
  usvfs_dump_path =
      ush::string_cast<std::wstring>(p->crashDumpsPath, ush::CodePage::UTF8);
  SetLogLevel(p->logLevel);

  // update parameters in context so spawned process will inherit changes:
  context->setDebugParameters(p->logLevel, p->crashDumpsType, p->crashDumpsPath,
                              std::chrono::milliseconds(p->delayProcessMs));
}

//
// Structured Exception handling
//

std::wstring generate_minidump_name(const wchar_t* dumpPath)
{
  DWORD pid = GetCurrentProcessId();
  wchar_t pname[100];
  if (GetModuleBaseName(GetCurrentProcess(), NULL, pname, _countof(pname)) == 0)
    return std::wstring();

  // find an available name:
  wchar_t dmpFile[MAX_PATH];
  int count = 0;
  _snwprintf_s(dmpFile, _TRUNCATE, L"%s\\%s-%lu.dmp", dumpPath, pname, pid);
  while (winapi::ex::wide::fileExists(dmpFile)) {
    if (++count > 99)
      return std::wstring();
    _snwprintf_s(dmpFile, _TRUNCATE, L"%s\\%s-%lu_%02d.dmp", dumpPath, pname, pid,
                 count);
  }
  return dmpFile;
}

int createMiniDumpImpl(PEXCEPTION_POINTERS exceptionPtrs, CrashDumpsType type,
                       const wchar_t* dumpPath, HMODULE dbgDLL)
{
  typedef BOOL(WINAPI * FuncMiniDumpWriteDump)(
      HANDLE process, DWORD pid, HANDLE file, MINIDUMP_TYPE dumpType,
      const PMINIDUMP_EXCEPTION_INFORMATION exceptionParam,
      const PMINIDUMP_USER_STREAM_INFORMATION userStreamParam,
      const PMINIDUMP_CALLBACK_INFORMATION callbackParam);

  // notice we avoid logging here on purpose because this is called from the VEHandler
  // and the logger can crash it in extreme cases.
  // additionally it is also called for MO crashes which use it's own logging.
  winapi::ex::wide::createPath(dumpPath);

  auto dmpName = generate_minidump_name(dumpPath);
  if (dmpName.empty())
    return 4;

  FuncMiniDumpWriteDump funcDump = reinterpret_cast<FuncMiniDumpWriteDump>(
      GetProcAddress(dbgDLL, "MiniDumpWriteDump"));
  if (!funcDump)
    return 5;

  HANDLE dumpFile = winapi::wide::createFile(dmpName)
                        .createAlways()
                        .access(GENERIC_WRITE)
                        .share(FILE_SHARE_WRITE)();
  if (dumpFile != INVALID_HANDLE_VALUE) {
    DWORD dumpType = MiniDumpNormal | MiniDumpWithHandleData |
                     MiniDumpWithUnloadedModules | MiniDumpWithProcessThreadData;
    if (type == CrashDumpsType::Data)
      dumpType |= MiniDumpWithDataSegs;
    if (type == CrashDumpsType::Full)
      dumpType |= MiniDumpWithFullMemory;

    _MINIDUMP_EXCEPTION_INFORMATION exceptionInfo;
    exceptionInfo.ThreadId          = GetCurrentThreadId();
    exceptionInfo.ExceptionPointers = exceptionPtrs;
    exceptionInfo.ClientPointers    = FALSE;

    BOOL success = funcDump(GetCurrentProcess(), GetCurrentProcessId(), dumpFile,
                            static_cast<MINIDUMP_TYPE>(dumpType), &exceptionInfo,
                            nullptr, nullptr);

    CloseHandle(dumpFile);

    return success ? 0 : 7;
  } else
    return 6;
}

int WINAPI usvfsCreateMiniDump(PEXCEPTION_POINTERS exceptionPtrs, CrashDumpsType type,
                               const wchar_t* dumpPath)
{
  if (type == CrashDumpsType::None)
    return 0;

  int res = 1;
  if (HMODULE dbgDLL = LoadLibraryW(L"dbghelp.dll")) {
    try {
      res = createMiniDumpImpl(exceptionPtrs, type, dumpPath, dbgDLL);
    } catch (...) {
      res = 2;
    }
    FreeLibrary(dbgDLL);
  }
  return res;
}

static bool exceptionInUSVFS(PEXCEPTION_POINTERS exceptionPtrs)
{
  if (!dllModule)  // shouldn't happen, check just in case
    return true;   // create dump to better understand how this could happen

  std::pair<uintptr_t, uintptr_t> range = winapi::ex::getSectionRange(dllModule);

  uintptr_t exceptionAddress =
      reinterpret_cast<uintptr_t>(exceptionPtrs->ExceptionRecord->ExceptionAddress);

  return range.first <= exceptionAddress && exceptionAddress < range.second;
}

LONG WINAPI VEHandler(PEXCEPTION_POINTERS exceptionPtrs)
{
  // NOTICE: don't use logger in VEHandler as it can cause another fault causing
  // VEHandler to be called again and so on.

  if ((exceptionPtrs->ExceptionRecord->ExceptionCode < 0x80000000)  // non-critical
      ||
      (exceptionPtrs->ExceptionRecord->ExceptionCode == 0xe06d7363)) {  // cpp exception
    // don't report non-critical exceptions
    return EXCEPTION_CONTINUE_SEARCH;
  }
  /*
  if (((exceptionPtrs->ExceptionRecord->ExceptionFlags & EXCEPTION_NONCONTINUABLE) != 0)
  || (exceptionPtrs->ExceptionRecord->ExceptionCode == 0xe06d7363)) {
    // don't want to break on non-critical exceptions. 0xe06d7363 indicates a C++
  exception. why are those marked non-continuable? return EXCEPTION_CONTINUE_SEARCH;
  }
  */

  // VEHandler is called on "first-chance" exceptions which might be caught and handled.
  // Ideally we would like to use an UnhandledExceptionFilter but that fails to catch
  // crashes inside our hooks at least on x64, which is the main reason why want a crash
  // collection from usvfs. As a workaround/compromise we catch vectored exception but
  // only ones that originate directly within the usvfs code:
  if (!exceptionInUSVFS(exceptionPtrs))
    return EXCEPTION_CONTINUE_SEARCH;

  // disable our hooking mechanism to increase chances the dump writing won't crash
  HookLib::TrampolinePool& trampPool = HookLib::TrampolinePool::instance();
  if (&trampPool) {  // need to test this in case of crash before TrampolinePool
                     // initialized
    trampPool.forceUnlockBarrier();
    trampPool.setBlock(true);
  }

  usvfsCreateMiniDump(exceptionPtrs, usvfs_dump_type, usvfs_dump_path.c_str());

  return EXCEPTION_CONTINUE_SEARCH;
}

//
// Exported functions
//

void __cdecl InitHooks(LPVOID parameters, size_t)
{
  InitLoggingInternal(false, true);

  const usvfsParameters* params = reinterpret_cast<usvfsParameters*>(parameters);

  // there is already a wait in the constructor of HookManager, but this one is useful
  // to debug code here (from experience... ), should not wait twice since the second
  // will return true immediately
  if (params->debugMode) {
    while (!::IsDebuggerPresent()) {
      // wait for debugger to attach
      ::Sleep(100);
    }
  }

  usvfs_dump_type = params->crashDumpsType;
  usvfs_dump_path =
      ush::string_cast<std::wstring>(params->crashDumpsPath, ush::CodePage::UTF8);

  if (params->delayProcessMs > 0) {
    ::Sleep(static_cast<unsigned long>(params->delayProcessMs));
  }

  SetLogLevel(params->logLevel);

  if (exceptionHandler == nullptr) {
    if (usvfs_dump_type != CrashDumpsType::None)
      exceptionHandler = ::AddVectoredExceptionHandler(0, VEHandler);
  } else {
    spdlog::get("usvfs")->info("vectored exception handler already active");
    // how did this happen??
  }

  spdlog::get("usvfs")->info(
      "inithooks called {0} in process {1}:{2} (log level {3}, dump type {4}, dump "
      "path {5})",
      params->instanceName, winapi::ansi::getModuleFileName(nullptr),
      ::GetCurrentProcessId(), static_cast<int>(params->logLevel),
      static_cast<int>(params->crashDumpsType), params->crashDumpsPath);

  try {
    manager = new usvfs::HookManager(*params, dllModule);

    auto context   = manager->context();
    auto exePath   = boost::dll::program_location();
    auto libraries = context->librariesToForceLoad(exePath.filename().c_str());
    for (auto library : libraries) {
      if (std::filesystem::exists(library)) {
        const auto ret = LoadLibraryExW(library.c_str(), NULL, 0);
        if (ret) {
          spdlog::get("usvfs")->info("inithooks succeeded to force load {0}",
                                     ush::string_cast<std::string>(library).c_str());
        } else {
          spdlog::get("usvfs")->critical(
              "inithooks failed to force load {0}",
              ush::string_cast<std::string>(library).c_str());
        }
      }
    }

    spdlog::get("usvfs")->info("inithooks in process {0} successful",
                               ::GetCurrentProcessId());

  } catch (const std::exception& e) {
    spdlog::get("usvfs")->debug("failed to initialise hooks: {0}", e.what());
  }
}

void WINAPI usvfsGetCurrentVFSName(char* buffer, size_t size)
{
  ush::strncpy_sz(buffer, context->callParameters().currentSHMName, size);
}

BOOL WINAPI usvfsCreateVFS(const usvfsParameters* p)
{
  usvfs::HookContext::remove(p->instanceName);
  return usvfsConnectVFS(p);
}

BOOL WINAPI usvfsConnectVFS(const usvfsParameters* params)
{
  if (spdlog::get("usvfs").get() == nullptr) {
    // create temporary logger so we don't get null-pointer exceptions
    spdlog::create<spdlog::sinks::null_sink_mt>("usvfs");
  }

  try {
    usvfsDisconnectVFS();
    context = new usvfs::HookContext(*params, dllModule);

    return TRUE;
  } catch (const std::exception& e) {
    spdlog::get("usvfs")->debug("failed to connect to vfs: {}", e.what());
    return FALSE;
  }
}

void WINAPI usvfsDisconnectVFS()
{
  if (spdlog::get("usvfs").get() == nullptr) {
    // create temporary logger so we don't get null-pointer exceptions
    spdlog::create<spdlog::sinks::null_sink_mt>("usvfs");
  }

  spdlog::get("usvfs")->debug("remove from process {}", GetCurrentProcessId());

  if (manager != nullptr) {
    delete manager;
    manager = nullptr;
  }

  if (context != nullptr) {
    delete context;
    context = nullptr;
    spdlog::get("usvfs")->debug("vfs unloaded");
  }
}

bool processStillActive(DWORD pid)
{
  HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);

  if (proc == nullptr) {
    return false;
  }

  ON_BLOCK_EXIT([&]() {
    if (proc != INVALID_HANDLE_VALUE)
      ::CloseHandle(proc);
  });

  DWORD exitCode;
  if (!GetExitCodeProcess(proc, &exitCode)) {
    spdlog::get("usvfs")->warn("failed to query exit code on process {}: {}", pid,
                               ::GetLastError());
    return false;
  } else {
    return exitCode == STILL_ACTIVE;
  }
}

BOOL WINAPI usvfsGetVFSProcessList(size_t* count, LPDWORD processIDs)
{
  if (count == nullptr) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }

  if (context == nullptr) {
    *count = 0;
  } else {
    std::vector<DWORD> pids = context->registeredProcesses();
    size_t realCount        = 0;
    for (DWORD pid : pids) {
      if (processStillActive(pid)) {
        if ((realCount < *count) && (processIDs != nullptr)) {
          processIDs[realCount] = pid;
        }

        ++realCount;
      }  // else the process has already ended
    }
    *count = realCount;
  }
  return TRUE;
}

BOOL WINAPI usvfsGetVFSProcessList2(size_t* count, DWORD** buffer)
{
  if (!count || !buffer) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }

  *count  = 0;
  *buffer = nullptr;

  std::vector<DWORD> pids = context->registeredProcesses();
  auto last               = std::remove_if(pids.begin(), pids.end(), [](DWORD id) {
    return !processStillActive(id);
  });

  pids.erase(last, pids.end());

  if (pids.empty()) {
    return TRUE;
  }

  *count  = pids.size();
  *buffer = static_cast<DWORD*>(std::calloc(pids.size(), sizeof(DWORD)));

  std::copy(pids.begin(), pids.end(), *buffer);

  return TRUE;
}

void WINAPI usvfsClearVirtualMappings()
{
  context->redirectionTable()->clear();
  context->inverseTable()->clear();
}

/// ensure the specified path exists. If a physical path of the same name
/// exists, it is inserted into the virtual directory as an empty reference. If
/// the path doesn't exist virtually and can't be cloned from a physical
/// directory, this returns false
/// \todo if this fails (i.e. not all intermediate directories exists) any
/// intermediate directories already created aren't removed
bool assertPathExists(usvfs::RedirectionTreeContainer& table, LPCWSTR path)
{
  bfs::path p(path);
  p = p.parent_path();

  usvfs::RedirectionTree::NodeT* current = table.get();

  for (auto iter = p.begin(); iter != p.end(); iter = ush::nextIter(iter, p.end())) {
    if (current->exists(iter->string().c_str())) {
      // subdirectory exists virtually, all good
      usvfs::RedirectionTree::NodePtrT found = current->node(iter->string().c_str());
      current                                = found.get().get();
    } else {
      // targetPath is relative to the last rerouted "real" path. This means
      // that if virtual c:/foo maps to real c:/windows then creating virtual
      // c:/foo/bar will map to real c:/windows/bar
      bfs::path targetPath = current->data().linkTarget.size() > 0
                                 ? bfs::path(current->data().linkTarget.c_str()) / *iter
                                 : *iter / "\\";

      // is_directory returns false for symlinks and reparse points,
      // which causes this function to fail if the target path contains
      // either of those. paths containing reparse points is a common
      // scenario when running under Wine, so check for those explicitly.
      // this check could have a false positive if the path contains a
      // symlink to a file, but such a scenario is extremely unlikely.
      if (is_directory(targetPath) || is_symlink(targetPath) ||
          status(targetPath).type() == bfs::file_type::reparse_file) {
        usvfs::RedirectionTree::NodePtrT newNode =
            table.addDirectory(current->path() / *iter, targetPath.string().c_str(),
                               ush::FLAG_DUMMY, false);
        current = newNode.get().get();
      } else {
        spdlog::get("usvfs")->info("{} doesn't exist", targetPath.c_str());
        return false;
      }
    }
  }

  return true;
}

static bool fileNameInSkipSuffixes(const std::string& fileNameUtf8,
                                   const std::vector<std::string>& skipFileSuffixes)
{
  for (const auto& skipFileSuffix : skipFileSuffixes) {
    if (boost::algorithm::iends_with(fileNameUtf8, skipFileSuffix)) {
      spdlog::get("usvfs")->debug(
          "file '{}' should be skipped, matches file suffix '{}'", fileNameUtf8,
          skipFileSuffix);
      return true;
    }
  }
  return false;
}

static bool fileNameInSkipDirectories(const std::string& directoryNameUtf8,
                                      const std::vector<std::string>& skipDirectories)
{
  for (const auto& skipDir : skipDirectories) {
    if (boost::algorithm::iequals(directoryNameUtf8, skipDir)) {
      spdlog::get("usvfs")->debug("directory '{}' should be skipped",
                                  directoryNameUtf8);
      return true;
    }
  }
  return false;
}

BOOL WINAPI usvfsVirtualLinkFile(LPCWSTR source, LPCWSTR destination,
                                 unsigned int flags)
{
  // TODO difference between winapi and ntdll api regarding system32 vs syswow64
  // (and other windows links?)
  try {
    if (!assertPathExists(context->redirectionTable(), destination)) {
      SetLastError(ERROR_PATH_NOT_FOUND);
      return FALSE;
    }

    const auto skipFileSuffixes = context->skipFileSuffixes();

    std::string sourceU8 = ush::string_cast<std::string>(source, ush::CodePage::UTF8);

    // Check if the file should be skipped
    if (fileNameInSkipSuffixes(sourceU8, skipFileSuffixes)) {
      // return false when we want to fail when the file is skipped
      return (flags & LINKFLAG_FAILIFSKIPPED) ? FALSE : TRUE;
    }

    auto res = context->redirectionTable().addFile(
        bfs::path(destination), usvfs::RedirectionDataLocal(sourceU8),
        !(flags & LINKFLAG_FAILIFEXISTS));

    if (shouldAddToInverseTree(sourceU8)) {
      std::string destinationU8 =
          ush::string_cast<std::string>(destination, ush::CodePage::UTF8);

      context->inverseTable().addFile(bfs::path(source),
                                      usvfs::RedirectionDataLocal(destinationU8), true);
    }

    context->updateParameters();

    if (res.get() == nullptr) {
      // the tree structure currently doesn't provide useful error codes but
      // this is currently the only reason
      // we would return a nullptr.
      SetLastError(ERROR_FILE_EXISTS);
      return FALSE;
    } else {
      return TRUE;
    }
  } catch (const std::exception& e) {
    spdlog::get("usvfs")->error("failed to copy file {}", e.what());
    // TODO: no clue what's wrong
    SetLastError(ERROR_INVALID_DATA);
    return FALSE;
  }
}

/**
 * @brief extract the flags relevant to redirection
 */
static usvfs::shared::TreeFlags convertRedirectionFlags(unsigned int flags)
{
  usvfs::shared::TreeFlags result = 0;
  if (flags & LINKFLAG_CREATETARGET) {
    result |= usvfs::shared::FLAG_CREATETARGET;
  }
  return result;
}

BOOL WINAPI usvfsVirtualLinkDirectoryStatic(LPCWSTR source, LPCWSTR destination,
                                            unsigned int flags)
{
  // TODO change notification not yet implemented
  try {
    if ((flags & LINKFLAG_FAILIFEXISTS) && winapi::ex::wide::fileExists(destination)) {
      SetLastError(ERROR_FILE_EXISTS);
      return FALSE;
    }

    if (!assertPathExists(context->redirectionTable(), destination)) {
      SetLastError(ERROR_PATH_NOT_FOUND);
      return FALSE;
    }

    std::string sourceU8 =
        ush::string_cast<std::string>(source, ush::CodePage::UTF8) + "\\";

    context->redirectionTable().addDirectory(
        destination, usvfs::RedirectionDataLocal(sourceU8),
        usvfs::shared::FLAG_DIRECTORY | convertRedirectionFlags(flags),
        (flags & LINKFLAG_CREATETARGET) != 0);

    const auto skipDirectories  = context->skipDirectories();
    const auto skipFileSuffixes = context->skipFileSuffixes();

    if ((flags & LINKFLAG_RECURSIVE) != 0) {
      std::wstring sourceP(source);
      std::wstring sourceW      = sourceP + L"\\";
      std::wstring destinationW = std::wstring(destination) + L"\\";
      if (sourceP.length() >= MAX_PATH && !ush::startswith(sourceP.c_str(), LR"(\\?\)"))
        sourceP = LR"(\\?\)" + sourceP;

      for (winapi::ex::wide::FileResult file :
           winapi::ex::wide::quickFindFiles(sourceP.c_str(), L"*")) {
        if (file.attributes & FILE_ATTRIBUTE_DIRECTORY) {
          if ((file.fileName != L".") && (file.fileName != L"..")) {

            const auto nameU8 = ush::string_cast<std::string>(file.fileName.c_str(),
                                                              ush::CodePage::UTF8);
            // Check if the directory should be skipped
            if (fileNameInSkipDirectories(nameU8, skipDirectories)) {
              // Fail if we desire to fail when a dir/file is skipped
              if (flags & LINKFLAG_FAILIFSKIPPED) {
                spdlog::get("usvfs")->debug(
                    "directory '{}' skipped, failing as defined by link flags", nameU8);
                return FALSE;
              }

              continue;
            }

            usvfsVirtualLinkDirectoryStatic((sourceW + file.fileName).c_str(),
                                            (destinationW + file.fileName).c_str(),
                                            flags);
          }
        } else {
          const auto nameU8 =
              ush::string_cast<std::string>(file.fileName.c_str(), ush::CodePage::UTF8);

          // Check if the file should be skipped
          if (fileNameInSkipSuffixes(nameU8, skipFileSuffixes)) {
            // Fail if we desire to fail when a dir/file is skipped
            if (flags & LINKFLAG_FAILIFSKIPPED) {
              spdlog::get("usvfs")->debug(
                  "file '{}' skipped, failing as defined by link flags", nameU8);
              return FALSE;
            }

            continue;
          }

          // TODO could save memory here by storing only the file name for the
          // source and constructing the full name using the parent directory
          context->redirectionTable().addFile(
              bfs::path(destination) / nameU8,
              usvfs::RedirectionDataLocal(sourceU8 + nameU8), true);

          if (shouldAddToInverseTree(nameU8)) {
            std::string destinationU8 =
                ush::string_cast<std::string>(destination, ush::CodePage::UTF8) + "\\";

            context->inverseTable().addFile(
                bfs::path(source) / nameU8,
                usvfs::RedirectionDataLocal(destinationU8 + nameU8), true);
          }
        }
      }
    }

    context->updateParameters();

    return TRUE;
  } catch (const std::exception& e) {
    spdlog::get("usvfs")->error("failed to copy file {}", e.what());
    // TODO: no clue what's wrong
    SetLastError(ERROR_INVALID_DATA);
    return FALSE;
  }
}

BOOL WINAPI usvfsCreateProcessHooked(LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
                                     LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                     LPSECURITY_ATTRIBUTES lpThreadAttributes,
                                     BOOL bInheritHandles, DWORD dwCreationFlags,
                                     LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
                                     LPSTARTUPINFOW lpStartupInfo,
                                     LPPROCESS_INFORMATION lpProcessInformation)
{
  BOOL susp   = dwCreationFlags & CREATE_SUSPENDED;
  DWORD flags = dwCreationFlags | CREATE_SUSPENDED;

  BOOL blacklisted = context->executableBlacklisted(lpApplicationName, lpCommandLine);

  BOOL res = CreateProcessW(lpApplicationName, lpCommandLine, lpProcessAttributes,
                            lpThreadAttributes, bInheritHandles, flags, lpEnvironment,
                            lpCurrentDirectory, lpStartupInfo, lpProcessInformation);
  if (!res) {
    spdlog::get("usvfs")->error("failed to spawn {}",
                                ush::string_cast<std::string>(lpCommandLine));
    return FALSE;
  }

  if (!blacklisted) {
    std::wstring applicationDirPath = winapi::wide::getModuleFileName(dllModule);
    boost::filesystem::path p(applicationDirPath);
    try {
      usvfs::injectProcess(p.parent_path().wstring(), context->callParameters(),
                           *lpProcessInformation);
    } catch (const std::exception& e) {
      spdlog::get("usvfs")->error("failed to inject: {}", e.what());
      logExtInfo(e, LogLevel::Error);
      ::TerminateProcess(lpProcessInformation->hProcess, 1);
      ::SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
    }
  }

  if (!susp) {
    ResumeThread(lpProcessInformation->hThread);
  }

  return TRUE;
}

BOOL WINAPI usvfsCreateVFSDump(LPSTR buffer, size_t* size)
{
  assert(size != nullptr);
  std::ostringstream output;
  usvfs::shared::dumpTree(output, *context->redirectionTable().get());
  std::string str = output.str();
  if ((buffer != NULL) && (*size > 0)) {
    strncpy_s(buffer, *size, str.c_str(), _TRUNCATE);
  }
  bool success = *size >= str.length();
  *size        = str.length();
  return success ? TRUE : FALSE;
}

VOID WINAPI usvfsBlacklistExecutable(LPCWSTR executableName)
{
  context->blacklistExecutable(executableName);
}

VOID WINAPI usvfsClearExecutableBlacklist()
{
  context->clearExecutableBlacklist();
}

VOID WINAPI usvfsAddSkipFileSuffix(LPCWSTR fileSuffix)
{
  context->addSkipFileSuffix(fileSuffix);
}

VOID WINAPI usvfsClearSkipFileSuffixes()
{
  context->clearSkipFileSuffixes();
}

VOID WINAPI usvfsAddSkipDirectory(LPCWSTR directory)
{
  context->addSkipDirectory(directory);
}

VOID WINAPI usvfsClearSkipDirectories()
{
  context->clearSkipDirectories();
}

VOID WINAPI usvfsForceLoadLibrary(LPCWSTR processName, LPCWSTR libraryPath)
{
  context->forceLoadLibrary(processName, libraryPath);
}

VOID WINAPI usvfsClearLibraryForceLoads()
{
  context->clearLibraryForceLoads();
}

VOID WINAPI usvfsPrintDebugInfo()
{
  spdlog::get("usvfs")->warn("===== debug {} =====",
                             context->redirectionTable().shmName());
  void* buffer      = nullptr;
  size_t bufferSize = 0;
  context->redirectionTable().getBuffer(buffer, bufferSize);
  std::ostringstream temp;
  for (size_t i = 0; i < bufferSize; ++i) {
    temp << std::hex << std::setfill('0') << std::setw(2)
         << (unsigned)reinterpret_cast<char*>(buffer)[i] << " ";
    if ((i % 16) == 15) {
      spdlog::get("usvfs")->info("{}", temp.str());
      temp.str("");
      temp.clear();
    }
  }
  if (!temp.str().empty()) {
    spdlog::get("usvfs")->info("{}", temp.str());
  }
  spdlog::get("usvfs")->warn("===== / debug {} =====",
                             context->redirectionTable().shmName());
}

const char* WINAPI usvfsVersionString()
{
  return USVFS_VERSION_STRING;
}

//
// DllMain
//

BOOL APIENTRY DllMain(HMODULE module, DWORD reasonForCall, LPVOID)
{
  switch (reasonForCall) {
  case DLL_PROCESS_ATTACH: {
    dllModule = module;
  } break;
  case DLL_PROCESS_DETACH: {
    if (exceptionHandler)
      ::RemoveVectoredExceptionHandler(exceptionHandler);
  } break;
  case DLL_THREAD_ATTACH: {
  } break;
  case DLL_THREAD_DETACH: {
  } break;
  }

  return TRUE;
}
