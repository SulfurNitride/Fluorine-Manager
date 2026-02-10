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
#include <format>

#include "formatters.h"
#include "loghelpers.h"
#include "stringcast.h"
#include "stringutils.h"

namespace ush = usvfs::shared;

namespace usvfs::log
{

// this is declared in formatters but is defined here to avoid a whole .cpp
// file for two small functions
//
// comments from old code, taken as is:
//
//    TODO this does not correctly support surrogate pairs since the size used here
//    is the number of 16-bit characters in the buffer whereas toNarrow expects the
//    actual number of characters. It will always underestimate though, so worst
//    case scenario we truncate the string
//
std::string to_string(LPCWSTR value)
{
  if (value == nullptr) {
    return "<null>";
  }
  try {
    return ush::string_cast<std::string>(value, ush::CodePage::UTF8);
  } catch (const std::exception& e) {
    return std::format("<err: {}>", e.what());
  }
}
std::string to_string(PCUNICODE_STRING value)
{
  try {
    return ush::string_cast<std::string>(value->Buffer, ush::CodePage::UTF8,
                                         value->Length / sizeof(WCHAR));
  } catch (const std::exception& e) {
    return std::format("<err: {}>", e.what());
  }
}

spdlog::level::level_enum ConvertLogLevel(LogLevel level)
{
  switch (level) {
  case LogLevel::Debug:
    return spdlog::level::debug;
  case LogLevel::Info:
    return spdlog::level::info;
  case LogLevel::Warning:
    return spdlog::level::warn;
  case LogLevel::Error:
    return spdlog::level::err;
  default:
    return spdlog::level::debug;
  }
}

LogLevel ConvertLogLevel(spdlog::level::level_enum level)
{
  switch (level) {
  case spdlog::level::debug:
    return LogLevel::Debug;
  case spdlog::level::info:
    return LogLevel::Info;
  case spdlog::level::warn:
    return LogLevel::Warning;
  case spdlog::level::err:
    return LogLevel::Error;
  default:
    return LogLevel::Debug;
  }
}

}  // namespace usvfs::log
