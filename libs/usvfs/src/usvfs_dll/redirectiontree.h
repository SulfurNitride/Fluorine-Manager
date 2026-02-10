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

#include <directory_tree.h>

namespace usvfs
{

// typedef boost::interprocess::basic_string<char, std::char_traits<char>,
// shared::CharAllocatorT> StringT;

namespace shared
{
  static const TreeFlags FLAG_CREATETARGET = FLAG_FIRSTUSERFLAG + 0x00;
}

struct RedirectionDataLocal
{

  RedirectionDataLocal(const char* target) : linkTarget(target) {}

  RedirectionDataLocal(const std::string& target) : linkTarget(target) {}

  std::string linkTarget;
};

struct RedirectionData
{

  RedirectionData(const RedirectionData& reference,
                  const shared::VoidAllocatorT& allocator)
      : linkTarget(reference.linkTarget.c_str(), allocator)
  {}

  RedirectionData(const RedirectionDataLocal& reference,
                  const shared::VoidAllocatorT& allocator)
      : linkTarget(reference.linkTarget.c_str(), allocator)
  {}

  RedirectionData(const char* target, const shared::VoidAllocatorT& allocator)
      : linkTarget(target, allocator)
  {}

  shared::StringT linkTarget;
};

std::ostream& operator<<(std::ostream& stream, const RedirectionData& data);

template <>
inline void shared::dataAssign<RedirectionData>(RedirectionData& destination,
                                                const RedirectionData& source)
{
  destination.linkTarget.assign(source.linkTarget.c_str());
}

template <>
inline RedirectionData
shared::createDataEmpty<RedirectionData>(const VoidAllocatorT& allocator)
{
  return RedirectionData("", allocator);
}

template <typename T>
struct shared::SHMDataCreator<RedirectionData, T>
{
  static RedirectionData create(T source, const VoidAllocatorT& allocator)
  {
    return RedirectionData(source, allocator);
  }
};

template <>
struct shared::SHMDataCreator<RedirectionData, RedirectionData>
{
  static RedirectionData create(const RedirectionData& source,
                                const VoidAllocatorT& allocator)
  {
    return RedirectionData(source, allocator);
  }
};

using RedirectionTree          = shared::DirectoryTree<RedirectionData>;
using RedirectionTreeContainer = shared::TreeContainer<RedirectionTree>;

}  // namespace usvfs
