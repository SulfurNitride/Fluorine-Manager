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
#include <stringcast.h>

namespace usvfs
{
namespace shared
{

  template <typename ToT, typename CharT, typename Traits, typename Allocator>
  class string_cast_impl<ToT, boost::container::basic_string<CharT, Traits, Allocator>>
  {
  public:
    static ToT
    cast(const boost::container::basic_string<CharT, Traits, Allocator>& source,
         CodePage codePage, size_t sourceLength)
    {
      return string_cast_impl<ToT, const CharT*>::cast(source.c_str(), codePage,
                                                       sourceLength);
    }
  };

}  // namespace shared
}  // namespace usvfs
