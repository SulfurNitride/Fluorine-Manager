#pragma once

#include "dllimport.h"
#include "usvfsparameters.h"
#include <shared_memory.h>

namespace usvfs
{

class ForcedLibrary
{
public:
  ForcedLibrary(const std::string& processName, const std::string& libraryPath,
                const shared::VoidAllocatorT& allocator);

  std::string processName() const;
  std::string libraryPath() const;

private:
  shared::StringT m_processName;
  shared::StringT m_libraryPath;
};

class DLLEXPORT SharedParameters
{
public:
  SharedParameters()                                             = delete;
  SharedParameters(const SharedParameters& reference)            = delete;
  SharedParameters& operator=(const SharedParameters& reference) = delete;

  SharedParameters(const usvfsParameters& reference,
                   const shared::VoidAllocatorT& allocator);

  usvfsParameters makeLocal() const;

  std::string instanceName() const;
  std::string currentSHMName() const;
  std::string currentInverseSHMName() const;
  void setSHMNames(const std::string& current, const std::string& inverse);

  void setDebugParameters(LogLevel level, CrashDumpsType dumpType,
                          const std::string& dumpPath,
                          std::chrono::milliseconds delayProcess);

  std::size_t userConnected();
  std::size_t userDisconnected();
  std::size_t userCount();

  std::size_t registeredProcessCount() const;
  std::vector<DWORD> registeredProcesses() const;
  void registerProcess(DWORD pid);
  void unregisterProcess(DWORD pid);

  void blacklistExecutable(const std::string& name);
  void clearExecutableBlacklist();
  bool executableBlacklisted(const std::string& app, const std::string& cmd) const;

  void addSkipFileSuffix(const std::string& fileSuffix);
  void clearSkipFileSuffixes();
  std::vector<std::string> skipFileSuffixes() const;

  void addSkipDirectory(const std::string& directory);
  void clearSkipDirectories();
  std::vector<std::string> skipDirectories() const;

  void addForcedLibrary(const std::string& process, const std::string& path);
  std::vector<std::string> forcedLibraries(const std::string& processName);
  void clearForcedLibraries();

private:
  using StringAllocatorT = shared::VoidAllocatorT::rebind<shared::StringT>::other;

  using DWORDAllocatorT = shared::VoidAllocatorT::rebind<DWORD>::other;

  using ForcedLibraryAllocatorT = shared::VoidAllocatorT::rebind<ForcedLibrary>::other;

  using ProcessBlacklist =
      boost::container::flat_set<shared::StringT, std::less<shared::StringT>,
                                 StringAllocatorT>;

  using ProcessList =
      boost::container::flat_set<DWORD, std::less<DWORD>, DWORDAllocatorT>;

  using FileSuffixSkipList =
      boost::container::flat_set<shared::StringT, std::less<shared::StringT>,
                                 StringAllocatorT>;

  using DirectorySkipList =
      boost::container::flat_set<shared::StringT, std::less<shared::StringT>,
                                 StringAllocatorT>;

  using ForcedLibraries =
      boost::container::slist<ForcedLibrary, ForcedLibraryAllocatorT>;

  mutable bi::interprocess_mutex m_mutex;
  shared::StringT m_instanceName;
  shared::StringT m_currentSHMName;
  shared::StringT m_currentInverseSHMName;
  bool m_debugMode;
  LogLevel m_logLevel;
  CrashDumpsType m_crashDumpsType;
  shared::StringT m_crashDumpsPath;
  std::chrono::milliseconds m_delayProcess;
  uint32_t m_userCount;
  ProcessBlacklist m_processBlacklist;
  ProcessList m_processList;
  FileSuffixSkipList m_fileSuffixSkipList;
  DirectorySkipList m_directorySkipList;
  ForcedLibraries m_forcedLibraries;
};

}  // namespace usvfs
