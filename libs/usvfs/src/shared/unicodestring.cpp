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
#include "unicodestring.h"
#include "logging.h"
#include "stringcast.h"
#include "stringutils.h"

namespace usvfs
{

UnicodeString::UnicodeString(LPCWSTR string, size_t length)
{
  if (length == std::string::npos) {
    length = wcslen(string);
  }
  m_Buffer.resize(length + 1);
  memcpy(m_Buffer.data(), string, length * sizeof(wchar_t));
  update();
}

UnicodeString::UnicodeString(const std::wstring& string)
{
  m_Buffer.resize(string.length() + 1);
  memcpy(m_Buffer.data(), string.data(), string.length() * sizeof(wchar_t));
  update();
}

UnicodeString& UnicodeString::operator=(const std::wstring& string)
{
  m_Buffer.resize(string.length() + 1);
  memcpy(m_Buffer.data(), string.data(), string.length() * sizeof(wchar_t));
  update();
  return *this;
}

UnicodeString& UnicodeString::appendPath(PUNICODE_STRING path)
{
  if (path != nullptr && path->Buffer && path->Length) {
    auto appendAt = size();
    if (appendAt) {
      m_Buffer.resize(m_Buffer.size() + path->Length / sizeof(WCHAR) + 1);
      m_Buffer[appendAt++] = L'\\';
    } else
      m_Buffer.resize(path->Length / sizeof(WCHAR) + 1);
    memcpy(&m_Buffer[appendAt], path->Buffer, path->Length);
    update();
  }

  return *this;
}

void UnicodeString::update()
{
  m_Data.Length        = static_cast<USHORT>(size() * sizeof(WCHAR));
  m_Data.MaximumLength = static_cast<USHORT>((m_Buffer.capacity() - 1) * sizeof(WCHAR));
  m_Data.Buffer        = m_Buffer.data();
}

}  // namespace usvfs
