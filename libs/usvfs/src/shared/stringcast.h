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

#include "exceptionex.h"

namespace usvfs::shared
{

enum class CodePage
{
  LOCAL,
  LATIN1,
  UTF8
};

template <typename ToT, typename FromT>
class string_cast_impl
{
public:
  static ToT cast(const FromT& source, CodePage codePage, size_t sourceLength);
};

template <typename ToT, typename FromT>
ToT string_cast(FromT source, CodePage codePage = CodePage::LOCAL,
                size_t sourceLength = std::numeric_limits<size_t>::max())
{
  return string_cast_impl<ToT, FromT>::cast(source, codePage, sourceLength);
}

template <typename ToT, typename CharT>
class string_cast_impl<ToT, std::basic_string<CharT>>
{
public:
  static ToT cast(const std::basic_string<CharT>& source, CodePage codePage,
                  size_t sourceLength)
  {
    return string_cast_impl<ToT, const CharT*>::cast(source.c_str(), codePage,
                                                     sourceLength);
  }
};

template <typename ToT, typename CharT>
class string_cast_impl<ToT, CharT*>
{
  BOOST_STATIC_ASSERT(!boost::is_base_and_derived<ToT, CharT>::value);

public:
  static ToT cast(CharT* source, CodePage codePage, size_t sourceLength)
  {
    return string_cast_impl<ToT, const CharT*>::cast(source, codePage, sourceLength);
  }
};

template <typename ToT, typename CharT, int N>
class string_cast_impl<ToT, CharT[N]>
{
public:
  static ToT cast(CharT (&source)[N], CodePage codePage, size_t sourceLength)
  {
    return string_cast_impl<ToT, const CharT*>::cast(source, codePage, sourceLength);
  }
};

UINT windowsCP(CodePage codePage);

template <>
class string_cast_impl<std::string, const wchar_t*>
{
public:
  static std::string cast(const wchar_t* const& source, CodePage codePage,
                          size_t sourceLength)
  {
    std::string result;

    if (sourceLength == std::numeric_limits<size_t>::max()) {
      sourceLength = wcslen(source);
    }

    if (sourceLength > 0) {
      // use utf8 or local 8-bit encoding depending on user choice
      UINT cp = windowsCP(codePage);
      // preflight to find out the required buffer size
      int outLength = WideCharToMultiByte(cp, 0, source, static_cast<int>(sourceLength),
                                          nullptr, 0, nullptr, nullptr);
      if (outLength == 0) {
        throw windows_error("string conversion failed");
      }
      result.resize(outLength);
      outLength = WideCharToMultiByte(cp, 0, source, static_cast<int>(sourceLength),
                                      &result[0], outLength, nullptr, nullptr);
      if (outLength == 0) {
        throw windows_error("string conversion failed");
      }
      // fix output string length (i.e. in case of unconvertible characters
      while (result[outLength - 1] == L'\0') {
        result.resize(--outLength);
      }
    }

    return result;
  }
};

template <>
class string_cast_impl<std::wstring, const char*>
{
public:
  static std::wstring cast(const char* const& source, CodePage codePage,
                           size_t sourceLength)
  {
    std::wstring result;

    if (sourceLength == std::numeric_limits<size_t>::max()) {
      sourceLength = strlen(source);
    }

    if (sourceLength > 0) {
      // use utf8 or local 8-bit encoding depending on user choice
      UINT cp = windowsCP(codePage);
      // preflight to find out the required source size
      int outLength = MultiByteToWideChar(cp, 0, source, static_cast<int>(sourceLength),
                                          &result[0], 0);
      if (outLength == 0) {
        throw windows_error("string conversion failed");
      }
      result.resize(outLength);
      outLength = MultiByteToWideChar(cp, 0, source, static_cast<int>(sourceLength),
                                      &result[0], outLength);
      if (outLength == 0) {
        throw windows_error("string conversion failed");
      }
      while (result[outLength - 1] == L'\0') {
        result.resize(--outLength);
      }
    }

    return result;
  }
};

template <>
class string_cast_impl<std::wstring, const wchar_t*>
{
public:
  static std::wstring cast(const wchar_t* const& source, CodePage, size_t)
  {
    return std::wstring(source);
  }
};

}  // namespace usvfs::shared
