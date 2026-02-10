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

#include "hookcontext.h"
#include <hooklib.h>
#include <usvfsparameters.h>

namespace usvfs
{

class HookManager
{
public:
  HookManager(const usvfsParameters& params, HMODULE module);
  ~HookManager();

  HookManager(const HookManager& reference) = delete;

  HookManager& operator=(const HookManager& reference) = delete;

  static HookManager& instance();

  HookContext* context() { return &m_Context; }

  ///
  /// \brief retrieve address of the detour of a function
  /// \param functionName name of the function to look up
  /// \return function address that can be used to directly execute the original code
  ///
  LPVOID detour(const char* functionName);

  ///
  /// \brief remove the hook on the specified function.
  /// \param functionName name of the function to unhook
  /// \note This function is only exposed to allow a workaround for ExitProcess and may
  /// be
  ///       removed if a better solution is found there. If you have another legit use
  ///       case, please let me know!
  ///
  void removeHook(const std::string& functionName);

private:
  void logStubInt(LPVOID address);
  static void logStub(LPVOID address);

  void installHook(HMODULE module1, HMODULE module2, const std::string& functionName,
                   LPVOID hook, LPVOID* fillFuncAddr);
  void installStub(HMODULE module1, HMODULE module2, const std::string& functionName);
  void initHooks();
  void removeHooks();

private:
  static HookManager* s_Instance;

  std::map<std::string, HookLib::HOOKHANDLE> m_Hooks;

  std::map<LPVOID, std::string> m_Stubs;

  HookContext m_Context;
};

}  // namespace usvfs
