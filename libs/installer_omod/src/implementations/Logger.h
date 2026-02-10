#pragma once

using namespace cli;

#include <uibase/log.h>

ref class Logger : OMODFramework::ILogger
{
public:
  static OMODFramework::LoggingLevel OMODLoggingLevel(MOBase::log::Levels level);

  static MOBase::log::Levels MOLoggingLevel(OMODFramework::LoggingLevel level);

  virtual void Init();

  virtual void Log(OMODFramework::LoggingLevel level, System::String^ message, System::DateTime time);
};
