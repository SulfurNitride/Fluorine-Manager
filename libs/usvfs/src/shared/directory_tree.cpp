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
#include "directory_tree.h"
#include "tree_container.h"

namespace usvfs::shared
{

fs::path::iterator nextIter(const fs::path::iterator& iter,
                            const fs::path::iterator& end)
{
  fs::path::iterator next = iter;
  advanceIter(next, end);
  return next;
}

void advanceIter(fs::path::iterator& iter, const fs::path::iterator& end)
{
  ++iter;
  while (iter != end && (iter->wstring() == L"/" || iter->wstring() == L"\\" ||
                         iter->wstring() == L"."))
    ++iter;
}

}  // namespace usvfs::shared
