/*
Userspace Virtual Filesystem

Copyright (C) 2024. All rights reserved.

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

#include <format>
#include <type_traits>

#include "ntdll_declarations.h"

// formatters for standard types

namespace usvfs::log
{
std::string to_string(LPCWSTR value);
std::string to_string(PCUNICODE_STRING value);
}  // namespace usvfs::log

template <class Enum, class CharT>
  requires std::is_enum_v<Enum>
struct std::formatter<Enum, CharT> : std::formatter<std::underlying_type_t<Enum>, CharT>
{
  template <class FmtContext>
  FmtContext::iterator format(Enum v, FmtContext& ctx) const
  {
    return std::formatter<std::underlying_type_t<Enum>, CharT>::format(
        static_cast<std::underlying_type_t<Enum>>(v), ctx);
  }
};

template <>
struct std::formatter<LPCWSTR, char> : std::formatter<std::string, char>
{
  template <class FmtContext>
  FmtContext::iterator format(LPCWSTR v, FmtContext& ctx) const
  {
    return std::formatter<std::string, char>::format(usvfs::log::to_string(v), ctx);
  }
};

template <>
struct std::formatter<std::wstring, char> : std::formatter<LPCWSTR, char>
{
  template <class FmtContext>
  FmtContext::iterator format(const std::wstring& v, FmtContext& ctx) const
  {
    return std::formatter<LPCWSTR, char>::format(v.c_str(), ctx);
  }
};

template <>
struct std::formatter<PCUNICODE_STRING, char> : std::formatter<std::string, char>
{
  template <class FmtContext>
  FmtContext::iterator format(PCUNICODE_STRING v, FmtContext& ctx) const
  {
    return std::formatter<std::string, char>::format(usvfs::log::to_string(v), ctx);
  }
};

template <>
struct std::formatter<UNICODE_STRING, char> : std::formatter<std::string, char>
{
  template <class FmtContext>
  FmtContext::iterator format(UNICODE_STRING v, FmtContext& ctx) const
  {
    return std::formatter<std::string, char>::format(usvfs::log::to_string(&v), ctx);
  }
};

template <class Pointer>
  requires(std::is_pointer_v<Pointer> && !std::is_same_v<Pointer, const char*> &&
           !std::is_same_v<Pointer, char*> && !std::is_same_v<Pointer, const void*> &&
           !std::is_same_v<Pointer, void*>)
struct std::formatter<Pointer, char> : std::formatter<const void*, char>
{
  template <class FmtContext>
  FmtContext::iterator format(Pointer v, FmtContext& ctx) const
  {
    return std::formatter<const void*, char>::format(v, ctx);
  }
};

namespace usvfs::log
{

/**
 * a small helper class to wrap any object. The whole point is to give us a way
 * to ensure our own operator<< is used in addParam calls
 */
template <typename T>
class Wrap
{
public:
  explicit Wrap(const T& data) : m_Data(data) {}
  Wrap(Wrap<T>&& reference) : m_Data(std::move(reference.m_Data)) {}

  Wrap(const Wrap<T>& reference)               = delete;
  Wrap<T>& operator=(const Wrap<T>& reference) = delete;

private:
  friend struct ::std::formatter<Wrap<T>, char>;
  const T& m_Data;
};

template <typename T>
Wrap<T> wrap(const T& data)
{
  return Wrap<T>(data);
}

}  // namespace usvfs::log

template <>
struct std::formatter<usvfs::log::Wrap<DWORD>, char> : std::formatter<DWORD, char>
{
  template <class FmtContext>
  FmtContext::iterator format(const usvfs::log::Wrap<DWORD>& v, FmtContext& ctx) const
  {
    return std::format_to(ctx.out(), "{:x}", v.m_Data);
  }
};

template <>
struct std::formatter<usvfs::log::Wrap<NTSTATUS>, char> : std::formatter<NTSTATUS, char>
{
  template <class FmtContext>
  FmtContext::iterator format(const usvfs::log::Wrap<NTSTATUS>& v,
                              FmtContext& ctx) const
  {
    switch (v.m_Data) {
    case 0x00000000:
      return std::format_to(ctx.out(), "ok");
    case 0xC0000022:
      return std::format_to(ctx.out(), "access denied");
    case 0xC0000035:
      return std::format_to(ctx.out(), "exists already");
    }
    return std::format_to(ctx.out(), "err {:x}", v.m_Data);
  }
};
