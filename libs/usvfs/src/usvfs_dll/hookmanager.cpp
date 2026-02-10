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
#include "hookmanager.h"
#include "../thooklib/ttrampolinepool.h"
#include "../thooklib/utility.h"
#include "exceptionex.h"
#include "hooks/kernel32.h"
#include "hooks/ntdll.h"
#include "usvfs.h"
#include <VersionHelpers.h>
#include <directory_tree.h>
#include <logging.h>
#include <shmlogger.h>
#include <usvfsparameters.h>
#include <winapi.h>

using namespace HookLib;
namespace bf = boost::filesystem;

namespace usvfs
{

HookManager* HookManager::s_Instance = nullptr;

HookManager::HookManager(const usvfsParameters& params, HMODULE module)
    : m_Context(params, module)
{
  if (s_Instance != nullptr) {
    throw std::runtime_error("singleton duplicate instantiation (HookManager)");
  }

  s_Instance = this;

  m_Context.registerProcess(::GetCurrentProcessId());
  spdlog::get("usvfs")->info("Process registered in shared process list : {}",
                             ::GetCurrentProcessId());

  winapi::ex::OSVersion version = winapi::ex::getOSVersion();
  spdlog::get("usvfs")->info(
      "Windows version {}.{}.{} sp {} platform {} ({})", version.major, version.minor,
      version.build, version.servicpack, version.platformid,
      shared::string_cast<std::string>(winapi::ex::wide::getWindowsBuildLab(true))
          .c_str());

  initHooks();

  if (params.debugMode) {
    while (!::IsDebuggerPresent()) {
      // wait for debugger to attach
      ::Sleep(100);
    }
  }
}

HookManager::~HookManager()
{
  spdlog::get("hooks")->debug("end hook of process {}", GetCurrentProcessId());
  removeHooks();
  m_Context.unregisterCurrentProcess();
}

HookManager& HookManager::instance()
{
  if (s_Instance == nullptr) {
    throw std::runtime_error("singleton not instantiated");
  }

  return *s_Instance;
}

LPVOID HookManager::detour(const char* functionName)
{
  auto iter = m_Hooks.find(functionName);
  if (iter != m_Hooks.end()) {
    return GetDetour(iter->second);
  } else {
    return nullptr;
  }
}

void HookManager::removeHook(const std::string& functionName)
{
  auto iter = m_Hooks.find(functionName);
  if (iter != m_Hooks.end()) {
    try {
      RemoveHook(iter->second);
      m_Hooks.erase(iter);
      spdlog::get("usvfs")->info("removed hook for {}", functionName);
    } catch (const std::exception& e) {
      spdlog::get("usvfs")->critical("failed to remove hook of {}: {}", functionName,
                                     e.what());
    }
  } else {
    spdlog::get("usvfs")->info("{} wasn't hooked", functionName);
  }
}

void HookManager::logStubInt(LPVOID address)
{
  if (m_Stubs.find(address) != m_Stubs.end()) {
    spdlog::get("hooks")->warn("{0} called", m_Stubs[address]);
  } else {
    spdlog::get("hooks")->warn("unknown function at {0} called", address);
  }
}

void HookManager::logStub(LPVOID address)
{
  try {
    instance().logStubInt(address);
  } catch (const std::exception& e) {
    spdlog::get("hooks")->debug("function at {0} called after shutdown: {1}", address,
                                e.what());
  }
}

void HookManager::installHook(HMODULE module1, HMODULE module2,
                              const std::string& functionName, LPVOID hook,
                              LPVOID* fillFuncAddr = nullptr)
{
  BOOST_ASSERT(hook != nullptr);
  HOOKHANDLE handle  = INVALID_HOOK;
  HookError err      = ERR_NONE;
  LPVOID funcAddr    = nullptr;
  HMODULE usedModule = nullptr;
  // both module1 and module2 are allowed to be null
  if (module1 != nullptr) {
    funcAddr = MyGetProcAddress(module1, functionName.c_str());
    if (funcAddr != nullptr) {
      handle = InstallHook(funcAddr, hook, &err);
    }
    if (handle != INVALID_HOOK)
      usedModule = module1;
  }

  if ((handle == INVALID_HOOK) && (module2 != nullptr)) {
    funcAddr = MyGetProcAddress(module2, functionName.c_str());
    if (funcAddr != nullptr) {
      handle = InstallHook(funcAddr, hook, &err);
    }
    if (handle != INVALID_HOOK)
      usedModule = module2;
  }

  if (fillFuncAddr)
    *fillFuncAddr = funcAddr;

  if (handle == INVALID_HOOK) {
    spdlog::get("usvfs")->error("failed to hook {0}: {1}", functionName,
                                GetErrorString(err));
  } else {
    m_Stubs.insert(make_pair(funcAddr, functionName));
    m_Hooks.insert(make_pair(std::string(functionName), handle));
    spdlog::get("usvfs")->info("hooked {0} ({1}) in {2} type {3}", functionName,
                               funcAddr, winapi::ansi::getModuleFileName(usedModule),
                               GetHookType(handle));
  }
}

void HookManager::installStub(HMODULE module1, HMODULE module2,
                              const std::string& functionName)
{
  HOOKHANDLE handle  = INVALID_HOOK;
  HookError err      = ERR_NONE;
  LPVOID funcAddr    = nullptr;
  HMODULE usedModule = nullptr;
  // both module1 and module2 are allowed to be null
  if (module1 != nullptr) {
    funcAddr = MyGetProcAddress(module1, functionName.c_str());
    if (funcAddr != nullptr) {
      handle = InstallStub(funcAddr, logStub, &err);
    } else {
      spdlog::get("usvfs")->debug("{} doesn't contain {}",
                                  winapi::ansi::getModuleFileName(module1),
                                  functionName);
    }
    if (handle != INVALID_HOOK)
      usedModule = module1;
  }

  if ((handle == INVALID_HOOK) && (module2 != nullptr)) {
    funcAddr = MyGetProcAddress(module2, functionName.c_str());
    if (funcAddr != nullptr) {
      handle = InstallStub(funcAddr, logStub, &err);
    } else {
      spdlog::get("usvfs")->debug("{} doesn't contain {}",
                                  winapi::ansi::getModuleFileName(module2),
                                  functionName);
    }
    if (handle != INVALID_HOOK)
      usedModule = module2;
  }

  if (handle == INVALID_HOOK) {
    spdlog::get("usvfs")->error("failed to stub {0}: {1}", functionName,
                                GetErrorString(err));
  } else {
    m_Stubs.insert(make_pair(funcAddr, functionName));
    m_Hooks.insert(make_pair(std::string(functionName), handle));
    spdlog::get("usvfs")->info("stubbed {0} ({1}) in {2} type {3}", functionName,
                               funcAddr, winapi::ansi::getModuleFileName(usedModule),
                               GetHookType(handle));
  }
}

void HookManager::initHooks()
{
  TrampolinePool::initialize();

  HookLib::TrampolinePool::instance().setBlock(true);

  HMODULE k32Mod = GetModuleHandleA("kernel32.dll");
  spdlog::get("usvfs")->debug("kernel32.dll at {0:x}",
                              reinterpret_cast<uintptr_t>(k32Mod));
  // kernelbase.dll contains the actual implementation for functions formerly in
  // kernel32.dll and advapi32.dll, starting with Windows 7
  // http://msdn.microsoft.com/en-us/library/windows/desktop/dd371752(v=vs.85).aspx
  HMODULE kbaseMod = GetModuleHandleA("kernelbase.dll");
  spdlog::get("usvfs")->debug("kernelbase.dll at {0:x}",
                              reinterpret_cast<uintptr_t>(kbaseMod));

  installHook(kbaseMod, k32Mod, "GetFileAttributesExA", hook_GetFileAttributesExA);
  installHook(kbaseMod, k32Mod, "GetFileAttributesA", hook_GetFileAttributesA);
  installHook(kbaseMod, k32Mod, "GetFileAttributesExW", hook_GetFileAttributesExW);
  installHook(kbaseMod, k32Mod, "GetFileAttributesW", hook_GetFileAttributesW);
  installHook(kbaseMod, k32Mod, "SetFileAttributesW", hook_SetFileAttributesW);

  installHook(kbaseMod, k32Mod, "CreateDirectoryW", hook_CreateDirectoryW);
  installHook(kbaseMod, k32Mod, "RemoveDirectoryW", hook_RemoveDirectoryW);
  installHook(kbaseMod, k32Mod, "DeleteFileW", hook_DeleteFileW);
  installHook(kbaseMod, k32Mod, "GetCurrentDirectoryA", hook_GetCurrentDirectoryA);
  installHook(kbaseMod, k32Mod, "GetCurrentDirectoryW", hook_GetCurrentDirectoryW);
  installHook(kbaseMod, k32Mod, "SetCurrentDirectoryA", hook_SetCurrentDirectoryA);
  installHook(kbaseMod, k32Mod, "SetCurrentDirectoryW", hook_SetCurrentDirectoryW);

  installHook(kbaseMod, k32Mod, "ExitProcess", hook_ExitProcess);

  installHook(kbaseMod, k32Mod, "CreateProcessInternalW", hook_CreateProcessInternalW,
              reinterpret_cast<LPVOID*>(&CreateProcessInternalW));

  installHook(kbaseMod, k32Mod, "MoveFileA", hook_MoveFileA);
  installHook(kbaseMod, k32Mod, "MoveFileW", hook_MoveFileW);
  installHook(kbaseMod, k32Mod, "MoveFileExA", hook_MoveFileExA);
  installHook(kbaseMod, k32Mod, "MoveFileExW", hook_MoveFileExW);
  installHook(kbaseMod, k32Mod, "MoveFileWithProgressA", hook_MoveFileWithProgressA);
  installHook(kbaseMod, k32Mod, "MoveFileWithProgressW", hook_MoveFileWithProgressW);

  installHook(kbaseMod, k32Mod, "CopyFileExW", hook_CopyFileExW);
  if (IsWindows8OrGreater())
    installHook(kbaseMod, k32Mod, "CopyFile2", hook_CopyFile2,
                reinterpret_cast<LPVOID*>(&CopyFile2));

  installHook(kbaseMod, k32Mod, "GetPrivateProfileStringA",
              hook_GetPrivateProfileStringA);
  installHook(kbaseMod, k32Mod, "GetPrivateProfileStringW",
              hook_GetPrivateProfileStringW);
  installHook(kbaseMod, k32Mod, "GetPrivateProfileSectionA",
              hook_GetPrivateProfileSectionA);
  installHook(kbaseMod, k32Mod, "GetPrivateProfileSectionW",
              hook_GetPrivateProfileSectionW);
  installHook(kbaseMod, k32Mod, "WritePrivateProfileStringA",
              hook_WritePrivateProfileStringA);
  installHook(kbaseMod, k32Mod, "WritePrivateProfileStringW",
              hook_WritePrivateProfileStringW);

  installHook(kbaseMod, k32Mod, "GetFullPathNameA", hook_GetFullPathNameA);
  installHook(kbaseMod, k32Mod, "GetFullPathNameW", hook_GetFullPathNameW);

  installHook(kbaseMod, k32Mod, "FindFirstFileExW", hook_FindFirstFileExW);

  HMODULE ntdllMod = GetModuleHandleA("ntdll.dll");
  spdlog::get("usvfs")->debug("ntdll.dll at {0:x}",
                              reinterpret_cast<uintptr_t>(ntdllMod));
  installHook(ntdllMod, nullptr, "NtQueryFullAttributesFile",
              hook_NtQueryFullAttributesFile);
  installHook(ntdllMod, nullptr, "NtQueryAttributesFile", hook_NtQueryAttributesFile);
  installHook(ntdllMod, nullptr, "NtQueryDirectoryFile", hook_NtQueryDirectoryFile);
  installHook(ntdllMod, nullptr, "NtQueryDirectoryFileEx", hook_NtQueryDirectoryFileEx);
  installHook(ntdllMod, nullptr, "NtQueryObject", hook_NtQueryObject);
  installHook(ntdllMod, nullptr, "NtQueryInformationFile", hook_NtQueryInformationFile);
  installHook(ntdllMod, nullptr, "NtQueryInformationByName",
              hook_NtQueryInformationByName);
  installHook(ntdllMod, nullptr, "NtOpenFile", hook_NtOpenFile);
  installHook(ntdllMod, nullptr, "NtCreateFile", hook_NtCreateFile);
  installHook(ntdllMod, nullptr, "NtClose", hook_NtClose);
  installHook(ntdllMod, nullptr, "NtTerminateProcess", hook_NtTerminateProcess);

  installHook(kbaseMod, k32Mod, "LoadLibraryExA", hook_LoadLibraryExA);
  installHook(kbaseMod, k32Mod, "LoadLibraryExW", hook_LoadLibraryExW);

  // install this hook late as usvfs is calling it itself for debugging purposes
  installHook(kbaseMod, k32Mod, "GetModuleFileNameA", hook_GetModuleFileNameA);
  installHook(kbaseMod, k32Mod, "GetModuleFileNameW", hook_GetModuleFileNameW);

  spdlog::get("usvfs")->debug("hooks installed");
  HookLib::TrampolinePool::instance().setBlock(false);
}

void HookManager::removeHooks()
{
  while (m_Hooks.size() > 0) {
    auto iter = m_Hooks.begin();
    try {
      RemoveHook(iter->second);
      spdlog::get("usvfs")->debug("removed hook {}", iter->first);
    } catch (const std::exception& e) {
      spdlog::get("usvfs")->critical("failed to remove hook: {}", e.what());
    }

    // remove either way, otherwise this is an endless loop
    m_Hooks.erase(iter);
  }
}

}  // namespace usvfs
