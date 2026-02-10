#include <logging.h>
#include <sharedparameters.h>
#include <usvfsparametersprivate.h>

namespace usvfs
{

ForcedLibrary::ForcedLibrary(const std::string& process, const std::string& path,
                             const shared::VoidAllocatorT& alloc)
    : m_processName(process.begin(), process.end(), alloc),
      m_libraryPath(path.begin(), path.end(), alloc)
{}

std::string ForcedLibrary::processName() const
{
  return {m_processName.begin(), m_processName.end()};
}

std::string ForcedLibrary::libraryPath() const
{
  return {m_libraryPath.begin(), m_libraryPath.end()};
}

SharedParameters::SharedParameters(const usvfsParameters& reference,
                                   const shared::VoidAllocatorT& allocator)
    : m_instanceName(reference.instanceName, allocator),
      m_currentSHMName(reference.currentSHMName, allocator),
      m_currentInverseSHMName(reference.currentInverseSHMName, allocator),
      m_debugMode(reference.debugMode), m_logLevel(reference.logLevel),
      m_crashDumpsType(reference.crashDumpsType),
      m_crashDumpsPath(reference.crashDumpsPath, allocator),
      m_delayProcess(reference.delayProcessMs), m_userCount(1),
      m_processBlacklist(allocator), m_processList(allocator),
      m_fileSuffixSkipList(allocator), m_directorySkipList(allocator),
      m_forcedLibraries(allocator)
{}

usvfsParameters SharedParameters::makeLocal() const
{
  bi::scoped_lock lock(m_mutex);

  return usvfsParameters(m_instanceName.c_str(), m_currentSHMName.c_str(),
                         m_currentInverseSHMName.c_str(), m_debugMode, m_logLevel,
                         m_crashDumpsType, m_crashDumpsPath.c_str(),
                         m_delayProcess.count());
}

std::string SharedParameters::instanceName() const
{
  bi::scoped_lock lock(m_mutex);
  return {m_instanceName.begin(), m_instanceName.end()};
}

std::string SharedParameters::currentSHMName() const
{
  bi::scoped_lock lock(m_mutex);
  return {m_currentSHMName.begin(), m_currentSHMName.end()};
}

std::string SharedParameters::currentInverseSHMName() const
{
  bi::scoped_lock lock(m_mutex);
  return {m_currentInverseSHMName.begin(), m_currentInverseSHMName.end()};
}

void SharedParameters::setSHMNames(const std::string& current,
                                   const std::string& inverse)
{
  bi::scoped_lock lock(m_mutex);

  m_currentSHMName.assign(current.begin(), current.end());
  m_currentInverseSHMName.assign(inverse.begin(), inverse.end());
}

void SharedParameters::setDebugParameters(LogLevel level, CrashDumpsType dumpType,
                                          const std::string& dumpPath,
                                          std::chrono::milliseconds delayProcess)
{
  bi::scoped_lock lock(m_mutex);

  m_logLevel       = level;
  m_crashDumpsType = dumpType;
  m_crashDumpsPath.assign(dumpPath.begin(), dumpPath.end());
  m_delayProcess = delayProcess;
}

std::size_t SharedParameters::userConnected()
{
  bi::scoped_lock lock(m_mutex);
  return ++m_userCount;
}

std::size_t SharedParameters::userDisconnected()
{
  bi::scoped_lock lock(m_mutex);
  return --m_userCount;
}

std::size_t SharedParameters::userCount()
{
  bi::scoped_lock lock(m_mutex);
  return m_userCount;
}

std::size_t SharedParameters::registeredProcessCount() const
{
  bi::scoped_lock lock(m_mutex);
  return m_processList.size();
}

std::vector<DWORD> SharedParameters::registeredProcesses() const
{
  bi::scoped_lock lock(m_mutex);
  return {m_processList.begin(), m_processList.end()};
}

void SharedParameters::registerProcess(DWORD pid)
{
  bi::scoped_lock lock(m_mutex);
  m_processList.insert(pid);
}

void SharedParameters::unregisterProcess(DWORD pid)
{
  {
    bi::scoped_lock lock(m_mutex);

    auto itor = m_processList.find(pid);

    if (itor != m_processList.end()) {
      m_processList.erase(itor);
      return;
    }
  }

  spdlog::get("usvfs")->error("cannot unregister process {}, not in list", pid);
}

void SharedParameters::blacklistExecutable(const std::string& name)
{
  bi::scoped_lock lock(m_mutex);

  m_processBlacklist.insert(
      shared::StringT(name.begin(), name.end(), m_processBlacklist.get_allocator()));
}

void SharedParameters::clearExecutableBlacklist()
{
  bi::scoped_lock lock(m_mutex);
  m_processBlacklist.clear();
}

bool SharedParameters::executableBlacklisted(const std::string& appName,
                                             const std::string& cmdLine) const
{
  bool blacklisted = false;
  std::string log;

  {
    bi::scoped_lock lock(m_mutex);

    for (const shared::StringT& sitem : m_processBlacklist) {
      const auto item = "\\" + std::string(sitem.begin(), sitem.end());

      if (!appName.empty()) {
        if (boost::algorithm::iends_with(appName, item)) {
          blacklisted = true;
          log         = std::format("application {} is blacklisted", appName);
          break;
        }
      }

      if (!cmdLine.empty()) {
        if (boost::algorithm::icontains(cmdLine, item)) {
          blacklisted = true;
          log         = std::format("command line {} is blacklisted", cmdLine);
          break;
        }
      }
    }
  }

  if (blacklisted) {
    spdlog::get("usvfs")->info(log);
    return true;
  }

  return false;
}

void SharedParameters::addSkipFileSuffix(const std::string& fileSuffix)
{
  bi::scoped_lock lock(m_mutex);

  m_fileSuffixSkipList.insert(shared::StringT(fileSuffix.begin(), fileSuffix.end(),
                                              m_fileSuffixSkipList.get_allocator()));
}

void SharedParameters::clearSkipFileSuffixes()
{
  bi::scoped_lock lock(m_mutex);
  m_fileSuffixSkipList.clear();
}

std::vector<std::string> SharedParameters::skipFileSuffixes() const
{
  bi::scoped_lock lock(m_mutex);
  return {m_fileSuffixSkipList.begin(), m_fileSuffixSkipList.end()};
}

void SharedParameters::addSkipDirectory(const std::string& directory)
{
  bi::scoped_lock lock(m_mutex);

  m_directorySkipList.insert(shared::StringT(directory.begin(), directory.end(),
                                             m_directorySkipList.get_allocator()));
}

void SharedParameters::clearSkipDirectories()
{
  bi::scoped_lock lock(m_mutex);
  m_directorySkipList.clear();
}

std::vector<std::string> SharedParameters::skipDirectories() const
{
  bi::scoped_lock lock(m_mutex);
  return {m_directorySkipList.begin(), m_directorySkipList.end()};
}

void SharedParameters::addForcedLibrary(const std::string& processName,
                                        const std::string& libraryPath)
{
  bi::scoped_lock lock(m_mutex);

  m_forcedLibraries.push_front(
      ForcedLibrary(processName, libraryPath, m_forcedLibraries.get_allocator()));
}

std::vector<std::string>
SharedParameters::forcedLibraries(const std::string& processName)
{
  std::vector<std::string> v;

  {
    bi::scoped_lock lock(m_mutex);

    for (const auto& lib : m_forcedLibraries) {
      if (boost::algorithm::iequals(processName, lib.processName())) {
        v.push_back(lib.libraryPath());
      }
    }
  }

  return v;
}

void SharedParameters::clearForcedLibraries()
{
  bi::scoped_lock lock(m_mutex);
  m_forcedLibraries.clear();
}

}  // namespace usvfs
