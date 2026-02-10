#include "usvfsparametersprivate.h"
#include <algorithm>

usvfsParameters::usvfsParameters()
    : debugMode(false), logLevel(LogLevel::Debug), crashDumpsType(CrashDumpsType::None),
      delayProcessMs(0)
{
  std::fill(std::begin(instanceName), std::end(instanceName), 0);
  std::fill(std::begin(currentSHMName), std::end(currentSHMName), 0);
  std::fill(std::begin(currentInverseSHMName), std::end(currentInverseSHMName), 0);
  std::fill(std::begin(crashDumpsPath), std::end(crashDumpsPath), 0);
}

usvfsParameters::usvfsParameters(const char* instanceName, const char* currentSHMName,
                                 const char* currentInverseSHMName, bool debugMode,
                                 LogLevel logLevel, CrashDumpsType crashDumpsType,
                                 const char* crashDumpsPath, int delayProcessMs)
    : usvfsParameters()
{
  strncpy_s(this->instanceName, instanceName, _TRUNCATE);
  strncpy_s(this->currentSHMName, currentSHMName, _TRUNCATE);
  strncpy_s(this->currentInverseSHMName, currentInverseSHMName, _TRUNCATE);
  this->debugMode      = debugMode;
  this->logLevel       = logLevel;
  this->crashDumpsType = crashDumpsType;
  strncpy_s(this->crashDumpsPath, crashDumpsPath, _TRUNCATE);
  this->delayProcessMs = delayProcessMs;
}

usvfsParameters::usvfsParameters(const USVFSParameters& oldParams)
    : usvfsParameters(oldParams.instanceName, oldParams.currentSHMName,
                      oldParams.currentInverseSHMName, oldParams.debugMode,
                      oldParams.logLevel, oldParams.crashDumpsType,
                      oldParams.crashDumpsPath, 0)
{}

void usvfsParameters::setInstanceName(const char* name)
{
  strncpy_s(instanceName, name, _TRUNCATE);
  strncpy_s(currentSHMName, 60, name, _TRUNCATE);
  memset(currentInverseSHMName, '\0', _countof(currentInverseSHMName));
  _snprintf(currentInverseSHMName, 60, "inv_%s", name);
}

void usvfsParameters::setDebugMode(bool b)
{
  debugMode = b;
}

void usvfsParameters::setLogLevel(LogLevel level)
{
  logLevel = level;
}

void usvfsParameters::setCrashDumpType(CrashDumpsType type)
{
  crashDumpsType = type;
}

void usvfsParameters::setCrashDumpPath(const char* path)
{
  if (path && *path && strlen(path) < _countof(crashDumpsPath)) {
    memcpy(crashDumpsPath, path, strlen(path) + 1);
  } else {
    // crashDumpsPath invalid or overflow of USVFSParameters variable so disable
    // crash dumps:
    crashDumpsPath[0] = 0;
    crashDumpsType    = CrashDumpsType::None;
  }
}

void usvfsParameters::setProcessDelay(int milliseconds)
{
  delayProcessMs = milliseconds;
}

extern "C"
{

  const char* usvfsLogLevelToString(LogLevel lv)
  {
    switch (lv) {
    case LogLevel::Debug:
      return "debug";

    case LogLevel::Info:
      return "info";

    case LogLevel::Warning:
      return "warning";

    case LogLevel::Error:
      return "error";

    default:
      return "unknown";
    }
  }

  const char* usvfsCrashDumpTypeToString(CrashDumpsType t)
  {
    switch (t) {
    case CrashDumpsType::None:
      return "none";

    case CrashDumpsType::Mini:
      return "mini";

    case CrashDumpsType::Data:
      return "data";

    case CrashDumpsType::Full:
      return "full";

    default:
      return "unknown";
    }
  }

  usvfsParameters* usvfsCreateParameters()
  {
    return new (std::nothrow) usvfsParameters;
  }

  usvfsParameters* usvfsDupeParameters(usvfsParameters* p)
  {
    if (!p) {
      return nullptr;
    }

    auto* dupe = usvfsCreateParameters();
    if (!dupe) {
      return nullptr;
    }

    *dupe = *p;

    return dupe;
  }

  void usvfsCopyParameters(const usvfsParameters* source, usvfsParameters* dest)
  {
    *dest = *source;
  }

  void usvfsFreeParameters(usvfsParameters* p)
  {
    delete p;
  }

  void usvfsSetInstanceName(usvfsParameters* p, const char* name)
  {
    if (p) {
      p->setInstanceName(name);
    }
  }

  void usvfsSetDebugMode(usvfsParameters* p, BOOL debugMode)
  {
    if (p) {
      p->setDebugMode(debugMode);
    }
  }

  void usvfsSetLogLevel(usvfsParameters* p, LogLevel level)
  {
    if (p) {
      p->setLogLevel(level);
    }
  }

  void usvfsSetCrashDumpType(usvfsParameters* p, CrashDumpsType type)
  {
    if (p) {
      p->setCrashDumpType(type);
    }
  }

  void usvfsSetCrashDumpPath(usvfsParameters* p, const char* path)
  {
    if (p) {
      p->setCrashDumpPath(path);
    }
  }

  void usvfsSetProcessDelay(usvfsParameters* p, int milliseconds)
  {
    if (p) {
      p->setProcessDelay(milliseconds);
    }
  }

}  // extern "C"
