#include "Logger.h"

#include "../interop/StdDotNetConverters.h"

OMODFramework::LoggingLevel Logger::OMODLoggingLevel(MOBase::log::Levels level)
{
  switch (level)
  {
  default:
  case MOBase::log::Debug:
    return OMODFramework::LoggingLevel::DEBUG;
  case MOBase::log::Info:
    return OMODFramework::LoggingLevel::INFO;
  case MOBase::log::Warning:
    return OMODFramework::LoggingLevel::WARNING;
  case MOBase::log::Error:
    return OMODFramework::LoggingLevel::ERROR;
  }
}

MOBase::log::Levels Logger::MOLoggingLevel(OMODFramework::LoggingLevel level)
{
  switch (level)
  {
  default:
  case OMODFramework::LoggingLevel::DEBUG:
    return MOBase::log::Debug;
  case OMODFramework::LoggingLevel::INFO:
    return MOBase::log::Info;
  case OMODFramework::LoggingLevel::WARNING:
    return MOBase::log::Warning;
  case OMODFramework::LoggingLevel::ERROR:
    return MOBase::log::Error;
  }
}

void Logger::Init()
{
    //no op
}

void Logger::Log(OMODFramework::LoggingLevel level, System::String^ message, System::DateTime time)
{
  MOBase::log::getDefault().log(MOLoggingLevel(level), "{}", toUTF8String(message));
}
