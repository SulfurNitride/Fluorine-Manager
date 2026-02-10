#pragma once
#include "usvfsparameters.h"

struct usvfsParameters
{
  char instanceName[65];
  char currentSHMName[65];
  char currentInverseSHMName[65];
  bool debugMode;
  LogLevel logLevel{LogLevel::Debug};
  CrashDumpsType crashDumpsType{CrashDumpsType::None};
  char crashDumpsPath[260];
  int delayProcessMs;

  usvfsParameters();
  usvfsParameters(const usvfsParameters&)            = default;
  usvfsParameters& operator=(const usvfsParameters&) = default;

  usvfsParameters(const char* instanceName, const char* currentSHMName,
                  const char* currentInverseSHMName, bool debugMode, LogLevel logLevel,
                  CrashDumpsType crashDumpsType, const char* crashDumpsPath,
                  int delayProcessMs);

  usvfsParameters(const USVFSParameters& oldParams);

  void setInstanceName(const char* name);
  void setDebugMode(bool debugMode);
  void setLogLevel(LogLevel level);
  void setCrashDumpType(CrashDumpsType type);
  void setCrashDumpPath(const char* path);
  void setProcessDelay(int milliseconds);
};
