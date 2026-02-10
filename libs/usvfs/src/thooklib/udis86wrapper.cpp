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
#include "udis86wrapper.h"
#include "pch.h"
#include <boost/predef.h>
#include <shmlogger.h>
#include <stdexcept>

namespace HookLib
{

UDis86Wrapper::UDis86Wrapper()
{
  ud_init(&m_Obj);
  ud_set_syntax(&m_Obj, UD_SYN_INTEL);
#if BOOST_ARCH_X86_64
  ud_set_mode(&m_Obj, 64);
#else
  ud_set_mode(&m_Obj, 32);
#endif
}

void UDis86Wrapper::setInputBuffer(const uint8_t* buffer, size_t size)
{
  m_Buffer = buffer;
  ud_set_input_buffer(&m_Obj, buffer, size);
  ud_set_pc(&m_Obj, reinterpret_cast<uint64_t>(m_Buffer));
}

ud_t& UDis86Wrapper::obj()
{
  return m_Obj;
}

bool UDis86Wrapper::isRelativeJump()
{
  ud_mnemonic_code code = ud_insn_mnemonic(&m_Obj);
  // all conditional jumps and loops are relative, as are unconditional jumps with an
  // offset operand
  return (code == UD_Ijo) || (code == UD_Ijno) || (code == UD_Ijb) ||
         (code == UD_Ijae) || (code == UD_Ijz) || (code == UD_Ijnz) ||
         (code == UD_Ijbe) || (code == UD_Ija) || (code == UD_Ijs) ||
         (code == UD_Ijns) || (code == UD_Ijp) || (code == UD_Ijnp) ||
         (code == UD_Ijl) || (code == UD_Ijge) || (code == UD_Ijle) ||
         (code == UD_Ijg) || (code == UD_Ijcxz) || (code == UD_Ijecxz) ||
         (code == UD_Ijrcxz) || (code == UD_Iloop) || (code == UD_Iloope) ||
         (code == UD_Iloopne) ||
         ((code == UD_Icall) && (ud_insn_opr(&m_Obj, 0)->type == UD_OP_JIMM)) ||
         ((code == UD_Ijmp) && (ud_insn_opr(&m_Obj, 0)->type == UD_OP_JIMM));
}

intptr_t UDis86Wrapper::jumpOffset()
{
  const ud_operand_t* op = ud_insn_opr(&m_Obj, 0);
  switch (op->size) {
  case 8:
    return static_cast<intptr_t>(op->lval.sbyte);
  case 16:
    return static_cast<intptr_t>(op->lval.sword);
  case 32:
    return static_cast<intptr_t>(op->lval.sdword);
  case 64:
    return static_cast<intptr_t>(op->lval.sqword);
  default:
    throw std::runtime_error("unsupported jump size");
  }
}

uint64_t UDis86Wrapper::jumpTarget()
{
  // TODO: assert we're actually on a jump

  uint64_t res = ud_insn_off(&m_Obj) + ud_insn_len(&m_Obj);

  res += jumpOffset();

  if (ud_insn_opr(&m_Obj, 0)->base == UD_R_RIP) {
    res = *reinterpret_cast<uintptr_t*>(res);
  }

  return res;
}

}  // namespace HookLib
