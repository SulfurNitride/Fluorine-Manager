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
#include "formatters.h"
#include "ntdll_declarations.h"
#include "shmlogger.h"
#include "stringutils.h"

namespace usvfs::log
{

enum class DisplayStyle : uint8_t
{
  Hex = 0x01
};

class CallLoggerDummy
{
public:
  template <typename T>
  CallLoggerDummy& addParam(const char*, const T&, uint8_t style = 0)
  {
    return *this;
  }
};

class CallLogger
{
public:
  explicit CallLogger(const char* function)
  {
    const char* namespaceend = strrchr(function, ':');

    if (namespaceend != nullptr) {
      function = namespaceend + 1;
    }

    m_Message = function;
  }

  ~CallLogger()
  {
    try {
      static std::shared_ptr<spdlog::logger> log = spdlog::get("hooks");
      log->debug("{}", m_Message);
    } catch (...) {
      // suppress all exceptions in destructor
    }
  }

  template <typename T>
  CallLogger& addParam(const char* name, const T& value, uint8_t style = 0);

private:
  std::string m_Message;
};

template <typename T>
CallLogger& CallLogger::addParam(const char* name, const T& value, uint8_t style)
{
  static bool enabled = spdlog::get("hooks")->should_log(spdlog::level::debug);
  typedef std::underlying_type<DisplayStyle>::type DSType;

  if (enabled) {
    if constexpr (std::is_pointer_v<T>) {
      if (value == nullptr) {
        std::format_to(std::back_inserter(m_Message), "[{}=<null>]", name);
      } else {
        std::format_to(std::back_inserter(m_Message), "[{}={}]", name, value);
      }
    } else if constexpr (std::is_integral_v<T>) {
      if (style & static_cast<DSType>(DisplayStyle::Hex)) {
        std::format_to(std::back_inserter(m_Message), "[{}={:x}]", name, value);
      } else {
        std::format_to(std::back_inserter(m_Message), "[{}={}]", name, value);
      }
    } else {
      std::format_to(std::back_inserter(m_Message), "[{}={}]", name, value);
    }
  }

  return *this;
}

spdlog::level::level_enum ConvertLogLevel(LogLevel level);
LogLevel ConvertLogLevel(spdlog::level::level_enum level);

}  // namespace usvfs::log

// prefer the short variant of the function name, without signature.
// Fall back to the portable boost macro
#ifdef __FUNCTION__
#define __MYFUNC__ __FUNCTION__
#else
#define __MYFUNC__ BOOST_CURRENT_FUNCTION
#endif

#define LOG_CALL() usvfs::log::CallLogger(__MYFUNC__)

#define PARAM(val) addParam(#val, val)
#define PARAMHEX(val)                                                                  \
  addParam(#val, val, static_cast<uint8_t>(usvfs::log::DisplayStyle::Hex))
#define PARAMWRAP(val) addParam(#val, usvfs::log::wrap(val))
