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
#include "exceptionex.h"
#include "winapi.h"
#include <spdlog/spdlog.h>

namespace usvfs::shared
{

std::string windows_error::constructMessage(const std::string& input, int inErrorCode)
{
  std::ostringstream finalMessage;
  finalMessage << input;

  LPSTR buffer = nullptr;

  DWORD errorCode = inErrorCode != -1 ? inErrorCode : GetLastError();

  // TODO: the message is not english?
  if (FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                     nullptr, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                     (LPSTR)&buffer, 0, nullptr) == 0) {
    finalMessage << " (errorcode " << errorCode << ")";
  } else {
    LPSTR lastChar = buffer + strlen(buffer) - 2;
    *lastChar      = '\0';
    finalMessage << " (" << buffer << " [" << errorCode << "])";
    LocalFree(buffer);  // allocated by FormatMessage
  }

  SetLastError(
      errorCode);  // restore error code because FormatMessage might have modified it
  return finalMessage.str();
}

}  // namespace usvfs::shared

void logExtInfo(const std::exception& e, LogLevel logLevel)
{
  std::string content;

  if (const std::string* msg = boost::get_error_info<ex_msg>(e)) {
    content = *msg;
  }

  if (const DWORD* errorCode = boost::get_error_info<ex_win_errcode>(e)) {
    content = std::string("error: ") + winapi::ex::ansi::errorString(*errorCode);
  }

  switch (logLevel) {
  case LogLevel::Debug:
    spdlog::get("usvfs")->debug(content);
    break;
  case LogLevel::Info:
    spdlog::get("usvfs")->info(content);
    break;
  case LogLevel::Warning:
    spdlog::get("usvfs")->warn(content);
    break;
  case LogLevel::Error:
    spdlog::get("usvfs")->error(content);
    break;
  }
}
