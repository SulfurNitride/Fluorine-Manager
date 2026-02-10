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

#include "ntdll_declarations.h"
#include "windows_sane.h"

#include "formatters.h"

namespace usvfs
{

/**
 * @brief C++ wrapper for the windows UNICODE_STRING structure
 */
class UnicodeString
{
public:
  UnicodeString() : m_Buffer(1) { update(); }

  UnicodeString(const UnicodeString& other) : m_Buffer(other.m_Buffer) { update(); }

  UnicodeString(UnicodeString&& other) : m_Buffer(std::move(other.m_Buffer))
  {
    update();
  }

  UnicodeString(const std::wstring& string);
  UnicodeString(LPCWSTR string, size_t length = std::string::npos);

  UnicodeString& operator=(const std::wstring& string);

  UnicodeString& operator=(const UnicodeString& other)
  {
    m_Buffer = other.m_Buffer;
    update();
    return *this;
  }

  UnicodeString& operator=(UnicodeString&& other)
  {
    m_Buffer = std::move(other.m_Buffer);
    update();
    return *this;
  }

  /**
   * @brief convert to a WinNt Api-style unicode string. This is only valid as long
   *        as the string isn't modified
   */
  explicit operator PUNICODE_STRING() { return &m_Data; }

  /**
   * @brief convert to a Win32 Api-style unicode string. This is only valid as long
   *        as the string isn't modified
   */
  explicit operator LPCWSTR() const { return m_Data.Buffer; }

  /**
   * @return length of the string in 16-bit words (not including zero termination)
   */
  size_t size() const { return m_Buffer.size() - 1; }

  wchar_t operator[](size_t pos) const { return m_Buffer[pos]; }

  UnicodeString& appendPath(PUNICODE_STRING path);

private:
  friend struct ::std::formatter<UnicodeString, char>;

  void update();

  UNICODE_STRING m_Data;
  std::vector<wchar_t> m_Buffer;
};

}  // namespace usvfs

template <>
struct std::formatter<usvfs::UnicodeString, char>
    : std::formatter<PCUNICODE_STRING, char>
{
  template <class FmtContext>
  FmtContext::iterator format(const usvfs::UnicodeString& v, FmtContext& ctx) const
  {
    if (v.size() == 0) {
      return std::format_to(ctx.out(), "<empty string>");
    } else {
      return std::formatter<PCUNICODE_STRING, char>::format(&v.m_Data, ctx);
    }
  }
};
