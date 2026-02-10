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
#include <string>
#include <utility>

#include <spdlog/spdlog.h>

#include <boost/filesystem.hpp>

#include "inject.h"
#include <exceptionex.h>
#include <formatters.h>
#include <injectlib.h>
#include <loghelpers.h>
#include <pch.h>
#include <stringcast.h>
#include <stringutils.h>
#include <usvfsparametersprivate.h>
#include <winapi.h>

namespace ush = usvfs::shared;

using namespace winapi;

void usvfs::injectProcess(const std::wstring& applicationPath,
                          const usvfsParameters& parameters,
                          const PROCESS_INFORMATION& processInfo)
{
  injectProcess(applicationPath, parameters, processInfo.hProcess, processInfo.hThread);
}

void usvfs::injectProcess(const std::wstring& applicationPath,
                          const usvfsParameters& parameters, HANDLE processHandle,
                          HANDLE threadHandle)
{
  bool proc64      = false;
  bool sameBitness = false;
  {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    BOOL wow64;
    IsWow64Process(processHandle, &wow64);
    if (wow64) {
      // process is running under wow64 so it has to be a 32bit process running on 64bit
      // windows
      proc64 = false;
      BOOL temp;
      IsWow64Process(GetCurrentProcess(), &temp);
      sameBitness = temp == TRUE;
    } else {
      BOOL selfWow64;
      IsWow64Process(GetCurrentProcess(), &selfWow64);
      if (selfWow64) {
        // WE are a 32 bit process running on 64bit windows. the other process isn't, so
        // its 64bit
        proc64 = true;
      } else {
        sameBitness = true;
        // we have the same bitness as that other process, but which is it?
#ifdef _WIN64
        proc64 = true;
#else
        proc64 = false;
#endif
      }
    }
  }
  boost::filesystem::path binPath = boost::filesystem::path(applicationPath);
  spdlog::get("usvfs")->info("injecting to process {} with {} bitness",
                             ::GetProcessId(processHandle),
                             sameBitness ? "same" : "different");

  if (sameBitness) {
    static constexpr auto USVFS_DLL =
#ifdef _WIN64
        L"usvfs_x64.dll";
#else
        L"usvfs_x86.dll";
#endif
    const auto& preferedDll         = binPath / USVFS_DLL;
    boost::filesystem::path dllPath = preferedDll;
    bool dllFound                   = boost::filesystem::exists(dllPath);
    // support for runing tests using a usvfs dll in lib folder (and proxy under bin):
    if (!dllFound && binPath.filename() == L"bin") {
      dllPath  = binPath.parent_path() / L"lib" / USVFS_DLL;
      dllFound = boost::filesystem::exists(dllPath);
    }
    if (!dllFound) {
      USVFS_THROW_EXCEPTION(
          file_not_found_error()
          << ex_msg(std::string("dll missing: ") +
                    ush::string_cast<std::string>(preferedDll.wstring()).c_str()));
    }

    spdlog::get("usvfs")->info("dll path: {}", dllPath.wstring());

    InjectLib::InjectDLL(processHandle, threadHandle, dllPath.c_str(), "InitHooks",
                         &parameters, sizeof(parameters));

    spdlog::get("usvfs")->info("injection to same bitness process {} successful",
                               ::GetProcessId(processHandle));
  } else {
    // first try platform specific proxy exe:
    static constexpr auto USVFS_PREFERED_EXE =
#ifdef _WIN64
        L"usvfs_proxy_x86.exe";
#else
        L"usvfs_proxy_x64.exe";
#endif
    const auto& preferedExe         = binPath / USVFS_PREFERED_EXE;
    boost::filesystem::path exePath = preferedExe;
    bool exeFound                   = boost::filesystem::exists(exePath);
    // support for runing tests using a usvfs dll in lib folder (and proxy under bin):
    if (!exeFound && binPath.filename() == L"lib") {
      exePath  = binPath.parent_path() / L"bin" / USVFS_PREFERED_EXE;
      exeFound = boost::filesystem::exists(exePath);
    }
    // finally fallback to old proxy naming (but only for 64bit as we don't have a 64bit
    // proxy in this case):
#ifdef _WIN64
    if (!exeFound) {
      exePath  = binPath / L"usvfs_proxy.exe";
      exeFound = boost::filesystem::exists(exePath);
    }
#endif
    if (!exeFound) {
      USVFS_THROW_EXCEPTION(file_not_found_error() << ex_msg(
                                std::string("usvfs proxy not found: ") +
                                ush::string_cast<std::string>(preferedExe.wstring())));
    } else
      spdlog::get("usvfs")->info("using usvfs proxy: {}",
                                 ush::string_cast<std::string>(preferedExe.wstring()));
    // need to use proxy aplication to inject
    auto proxyProcess =
        std::move(wide::createProcess(exePath.wstring())
                      .arg(L"--instance")
                      .arg(ush::string_cast<std::wstring>(parameters.instanceName))
                      .arg(L"--pid")
                      .arg(GetProcessId(processHandle)));

    if (threadHandle != INVALID_HANDLE_VALUE) {
      proxyProcess.arg("--tid").arg(GetThreadId(threadHandle));
    }
    process::Result result = proxyProcess();
    if (!result.valid) {
      USVFS_THROW_EXCEPTION(unknown_error()
                            << ex_msg(std::string("failed to start proxy ") +
                                      ush::string_cast<std::string>(exePath.wstring()))
                            << ex_win_errcode(result.errorCode));
    } else {
      // wait for proxy completion. this shouldn't take long, 15 seconds is very
      // generous
      switch (WaitForSingleObject(result.processInfo.hProcess, 15000)) {
      case WAIT_TIMEOUT: {
        spdlog::get("usvfs")->debug("proxy timeout");
        TerminateProcess(result.processInfo.hProcess, 1);
        USVFS_THROW_EXCEPTION(timeout_error()
                              << ex_msg(std::string("proxy didn't complete in time")));
      } break;
      case WAIT_FAILED: {
        spdlog::get("usvfs")->debug("proxy wait failed");
        TerminateProcess(result.processInfo.hProcess, 1);
        USVFS_THROW_EXCEPTION(unknown_error()
                              << ex_msg(
                                     std::string("failed to wait for proxy completion"))
                              << ex_win_errcode(result.errorCode));
      } break;
      default: {
        spdlog::get("usvfs")->debug("proxy run successful");
        // nop
      } break;
      }
    }
  }
}
