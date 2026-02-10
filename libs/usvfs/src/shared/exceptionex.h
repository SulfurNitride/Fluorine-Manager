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

#include "logging.h"
#include <boost/exception/all.hpp>
#include <stdexcept>

typedef boost::error_info<struct tag_message, DWORD> ex_win_errcode;
typedef boost::error_info<struct tag_message, std::string> ex_msg;

struct std_boost_exception : virtual boost::exception, virtual std::exception
{
  const char* what() const noexcept override
  {
    return boost::diagnostic_information_what(*this);
  }
};

struct incompatibility_error : std_boost_exception
{};
struct usage_error : std_boost_exception
{};
struct data_error : std_boost_exception
{};
struct file_not_found_error : std_boost_exception
{};
struct timeout_error : std_boost_exception
{};
struct unknown_error : std_boost_exception
{};
struct node_missing_error : std_boost_exception
{};

#define USVFS_THROW_EXCEPTION(x) BOOST_THROW_EXCEPTION(x)

void logExtInfo(const std::exception& e, LogLevel logLevel = LogLevel::Warning);

namespace usvfs::shared
{

class windows_error : public std::runtime_error
{
public:
  windows_error(const std::string& message, int errorcode = GetLastError())
      : runtime_error(constructMessage(message, errorcode)), m_ErrorCode(errorcode)
  {}

  int getErrorCode() const { return m_ErrorCode; }

private:
  int m_ErrorCode;

  std::string constructMessage(const std::string& input, int errorcode);
};

class guard
{
public:
  explicit guard(std::function<void()> f) : m_f(std::move(f)) {}

  ~guard()
  {
    if (m_f) {
      m_f();
    }
  }

  guard(const guard&);
  guard& operator=(const guard&);

private:
  std::function<void()> m_f;
};

}  // namespace usvfs::shared

#define CONCATENATE_DIRECT(s1, s2) s1##s2
#define CONCATENATE(s1, s2) CONCATENATE_DIRECT(s1, s2)
#define ANONYMOUS_VARIABLE(str) CONCATENATE(str, __LINE__)
#define ON_BLOCK_EXIT(f) usvfs::shared::guard ANONYMOUS_VARIABLE(guard)(f)
