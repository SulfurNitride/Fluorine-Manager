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

#include "dllimport.h"
#include "redirectiontree.h"
#include "semaphore.h"
#include "tree_container.h"
#include <directory_tree.h>
#include <exceptionex.h>
#include <usvfsparameters.h>
#include <usvfsparametersprivate.h>
#include <winapi.h>

namespace usvfs
{

class DLLEXPORT SharedParameters;

/**
 * @brief context available to hooks. This is protected by a many-reader
 * single-writer mutex
 */
class HookContext
{

public:
  typedef std::unique_ptr<const HookContext, void (*)(const HookContext*)> ConstPtr;
  typedef std::unique_ptr<HookContext, void (*)(HookContext*)> Ptr;
  typedef unsigned int DataIDT;

public:
  HookContext(const usvfsParameters& params, HMODULE module);

  HookContext(const HookContext& reference) = delete;

  DLLEXPORT ~HookContext();

  HookContext& operator=(const HookContext& reference) = delete;

  static void remove(const char* instance);

  /**
   * @brief get read access to the context.
   * @return smart ptr to the context. mutex will automatically be released when
   * this leaves scope
   */
  static ConstPtr readAccess(const char* source);

  /**
   * @brief get write access to the context.
   * @return smart ptr to the context. mutex will automatically be released when
   * this leaves scope
   */
  static Ptr writeAccess(const char* source);

  /**
   * @return table containing file redirection information
   */
  RedirectionTreeContainer& redirectionTable() { return m_Tree; }

  /**
   * @return table containing file redirection information
   */
  const RedirectionTreeContainer& redirectionTable() const { return m_Tree; }

  RedirectionTreeContainer& inverseTable() { return m_InverseTree; }

  const RedirectionTreeContainer& inverseTable() const { return m_InverseTree; }

  /**
   * @return the parameters passed in on dll initialisation
   */
  usvfsParameters callParameters() const;

  /**
   * @return path to the calling library itself
   */
  std::wstring dllPath() const;

  /**
   * @brief get access to custom data
   * @note the caller gains write access to the data, independent on the lock on
   * the context
   *       as a whole. The caller himself has to ensure thread safety
   */
  template <typename T>
  T& customData(DataIDT id) const
  {
    auto iter = m_CustomData.find(id);
    if (iter == m_CustomData.end()) {
      iter = m_CustomData.insert(std::make_pair(id, T())).first;
    }
    // std::map is supposed to not invalidate any iterators when elements are
    // added
    // so it should be safe to return a pointer here
    T* res = boost::any_cast<T>(&iter->second);
    return *res;
  }

  void registerProcess(DWORD pid);
  void unregisterCurrentProcess();
  std::vector<DWORD> registeredProcesses() const;

  void blacklistExecutable(const std::wstring& executableName);
  void clearExecutableBlacklist();
  BOOL executableBlacklisted(LPCWSTR lpApplicationName, LPCWSTR lpCommandLine) const;

  void addSkipFileSuffix(const std::wstring& fileSuffix);
  void clearSkipFileSuffixes();
  std::vector<std::string> skipFileSuffixes() const;

  void addSkipDirectory(const std::wstring& directory);
  void clearSkipDirectories();
  std::vector<std::string> skipDirectories() const;

  void forceLoadLibrary(const std::wstring& processName,
                        const std::wstring& libraryPath);
  void clearLibraryForceLoads();
  std::vector<std::wstring> librariesToForceLoad(const std::wstring& processName);

  void setDebugParameters(LogLevel level, CrashDumpsType dumpType,
                          const std::string& dumpPath,
                          std::chrono::milliseconds delayProcess);

  void updateParameters() const;

  void registerDelayed(std::future<int> delayed);

  std::vector<std::future<int>>& delayed();

private:
  static void unlock(HookContext* instance);
  static void unlockShared(const HookContext* instance);

  SharedParameters* retrieveParameters(const usvfsParameters& params);

private:
  static HookContext* s_Instance;

  shared::SharedMemoryT m_ConfigurationSHM;
  SharedParameters* m_Parameters{nullptr};
  RedirectionTreeContainer m_Tree;
  RedirectionTreeContainer m_InverseTree;

  std::vector<std::future<int>> m_Futures;

  mutable std::map<DataIDT, boost::any> m_CustomData;

  HMODULE m_DLLModule;

  //  mutable std::recursive_mutex m_Mutex;
  mutable RecursiveBenaphore m_Mutex;
};

}  // namespace usvfs

extern "C" DLLEXPORT usvfs::HookContext* WINAPI
usvfsCreateHookContext(const usvfsParameters& params, HMODULE module);

class PreserveGetLastError
{
public:
  PreserveGetLastError() : m_err(GetLastError()) {}
  ~PreserveGetLastError() { SetLastError(m_err); }

private:
  DWORD m_err;
};

// declare an identifier that is guaranteed to be unique across the application
#define DATA_ID(name) static const usvfs::HookContext::DataIDT name = __COUNTER__

// set of macros. These ensure a call context is created but most of all these
// ensure exceptions are caught.

#define READ_CONTEXT() HookContext::readAccess(__MYFUNC__)
#define WRITE_CONTEXT() HookContext::writeAccess(__MYFUNC__)

#define HOOK_START_GROUP(group)                                                        \
  try {                                                                                \
    HookCallContext callContext(group);

#define HOOK_START                                                                     \
  try {                                                                                \
    HookCallContext callContext;

#define HOOK_END                                                                       \
  }                                                                                    \
  catch (const std::exception& e)                                                      \
  {                                                                                    \
    spdlog::get("usvfs")->error("exception in {0}: {1}", __MYFUNC__, e.what());        \
    logExtInfo(e);                                                                     \
  }

#define HOOK_ENDP(param)                                                               \
  }                                                                                    \
  catch (const std::exception& e)                                                      \
  {                                                                                    \
    spdlog::get("usvfs")->error("exception in {0} ({1}): {2}", __MYFUNC__, param,      \
                                e.what());                                             \
    logExtInfo(e);                                                                     \
  }

#define PRE_REALCALL callContext.restoreLastError();
#define POST_REALCALL callContext.updateLastError();
