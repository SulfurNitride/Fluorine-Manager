/*
Mod Organizer archive handling

Copyright (C) 2020 MO2 Team. All rights reserved.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 3 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef ARCHIVE_FORMAT_H
#define ARCHIVE_FORMAT_H

// This header specialize formatter() for some useful types. It should not be
// exposed outside of the library. It also contains some useful methods for
// string manipulation.

#include <format>

#include <cwctype>
#include <filesystem>
#include <stdexcept>
#include <string>

template <>
struct std::formatter<std::string, wchar_t> : std::formatter<std::wstring, wchar_t>
{
  template <typename FormatContext>
  auto format(std::string const& s, FormatContext& ctx) const
  {
    return std::formatter<std::wstring, wchar_t>::format(
        std::wstring(s.begin(), s.end()), ctx);
  }
};

template <>
struct std::formatter<std::exception, wchar_t> : std::formatter<std::string, wchar_t>
{
  template <typename FormatContext>
  auto format(std::exception const& ex, FormatContext& ctx) const
  {
    return std::formatter<std::string, wchar_t>::format(ex.what(), ctx);
  }
};

template <>
struct std::formatter<std::error_code, wchar_t> : std::formatter<std::string, wchar_t>
{
  template <typename FormatContext>
  auto format(std::error_code const& ec, FormatContext& ctx) const
  {
    return std::formatter<std::string, wchar_t>::format(ec.message(), ctx);
  }
};

template <>
struct std::formatter<std::filesystem::path, wchar_t>
    : std::formatter<std::wstring, wchar_t>
{
  template <typename FormatContext>
  auto format(std::filesystem::path const& path, FormatContext& ctx) const
  {
    return std::formatter<std::wstring, wchar_t>::format(path.wstring(), ctx);
  }
};

namespace ArchiveStrings
{

/**
 * @brief Join the element of the given container using the given separator.
 *
 * @param c The container. Must be satisfy standard container requirements.
 * @param sep The separator.
 *
 * @return a string containing the element joint, or an empty string if c
 *     is empty.
 */
template <class C>
std::wstring join(C const& c, std::wstring const& sep)
{
  auto begin = std::begin(c), end = std::end(c);

  if (begin == end) {
    return {};
  }
  std::wstring r = *begin++;
  for (; begin != end; ++begin) {
    r += *begin + sep;
  }

  return r;
}

/**
 * @brief Conver the given string to lowercase.
 *
 * @param s The string to convert.
 *
 * @return the converted string.
 */
inline std::wstring towlower(std::wstring s)
{
  std::transform(std::begin(s), std::end(s), std::begin(s), [](wchar_t c) {
    return static_cast<wchar_t>(::towlower(c));
  });
  return s;
}
}  // namespace ArchiveStrings

#endif
