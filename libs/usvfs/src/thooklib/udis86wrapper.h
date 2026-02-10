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

#include <udis86.h>
#undef inline  // libudis86/types.h defines inline to __inline which is no longer legal
               // since vs2012

namespace HookLib
{

class UDis86Wrapper
{

public:
  UDis86Wrapper();

  void setInputBuffer(const uint8_t* buffer, size_t size);

  ud_t& obj();

  operator ud_t*() { return &m_Obj; }

  bool isRelativeJump();

  intptr_t jumpOffset();

  ///
  /// determines the absolute jump target at the current instruction, taking into
  /// account relative instructions of all sizes and RIP-relative addressing.
  /// \return absolute address of the jump at the current disassembler instruction
  /// \note this works correctly ONLY if the input buffer has been set with
  /// setInputBuffer or
  ///       if ud_set_pc has been called
  ///
  uint64_t jumpTarget();

private:
private:
  ud_t m_Obj;
  const uint8_t* m_Buffer{nullptr};
};

}  // namespace HookLib
