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

#include "windows_sane.h"

namespace usvfs::shared
{

#ifdef _M_AMD64
typedef DWORD64 REGWORD;
#elif _M_IX86
typedef DWORD REGWORD;
#endif

inline LPVOID AddrAdd(LPVOID address, size_t offset)
{
  return reinterpret_cast<LPVOID>(reinterpret_cast<LPBYTE>(address) + offset);
}

inline std::ptrdiff_t AddrDiff(LPVOID lhs, LPVOID rhs)
{
  return reinterpret_cast<LPBYTE>(lhs) - reinterpret_cast<LPBYTE>(rhs);
}

// implicitly cast pointer to void*, from there cast to target type.
// This is supposed to be safer than directly reinterpret-casting
template <typename T>
inline T void_ptr_cast(void* ptr)
{
  return reinterpret_cast<T>(ptr);
}

template <>
inline int64_t void_ptr_cast(void* ptr)
{
  return reinterpret_cast<int64_t>(ptr);
}

template <>
inline uint64_t void_ptr_cast(void* ptr)
{
  return reinterpret_cast<uint64_t>(ptr);
}

}  // namespace usvfs::shared
