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

#include "asmjit_sane.h"
#include <logging.h>
#include <map>
#include <vector>
#include <windows_sane.h>

#ifdef _MSC_VER
#pragma warning(disable : 4714)
#include <boost/config/compiler/visualc.hpp>
#else
#include <boost/config/compiler/gcc.hpp>
#endif
#include <boost/predef.h>
#include <boost/thread.hpp>
#include <mutex>

// #include <boost/thread/mutex.hpp>

namespace HookLib
{

///
/// trampolines are runtime-generated mini-functions that are used to call the
/// original code of a function
///
class TrampolinePool
{
public:
  /// Call initialize before you use the TrampolinePool
  static void initialize();

  static TrampolinePool& instance()
  {
    // This is a very very sensitive place so we want to keep this function as simple as
    // possible and having it inlined probably cann't hurt
    return *s_Instance;
  }

  void setBlock(bool block);

  ///
  /// store a stub without moving code from the original function. This is used in cases
  /// where the hook can be placed without overwriting logic (i.e. hot-patchable
  /// functions and when chaining hooks)
  /// \param reroute the stub function to call before the regular function (on x86 this
  /// needs to be cdecl calling convention!)
  /// \param original the original function
  /// \param returnAddress address under which the original functionality can be
  /// reached.
  ///                      for the first hook this should be (original + 2), otherwise
  ///                      the address of the next hook in the chain
  /// \return address of the created trampoline function
  ///
  LPVOID storeStub(LPVOID reroute, LPVOID original, LPVOID returnAddress);

  ///
  /// store a stub, moving part of the original function to the trampoline
  /// \param reroute the stub function to call before the regular function (on x86 this
  /// needs to be cdecl calling convention!)
  /// \param original the original function
  /// \param preambleSize number of bytes from the original function to backup. Needs to
  /// correspond to complete instructions
  /// \param rerouteOffset offset in bytes from the created trampoline to the preamble
  /// that leads back to the original code
  /// \return address of the created trampoline function
  ///
  LPVOID storeStub(LPVOID reroute, LPVOID original, size_t preambleSize,
                   size_t* rerouteOffset);

  ///
  /// store a trampoline for hot-patchable functions, where the original function
  /// is unharmed.
  /// \param reroute the reroute function
  /// \param original original function
  /// \param returnAddress address under which the original functionality can be
  /// reached.
  /// \return address of the trampoline function
  ///
  LPVOID storeTrampoline(LPVOID reroute, LPVOID original, LPVOID returnAddress);

  ///
  /// store a trampoline, copying a part of the original function to the trampoline.
  /// This is used for the case where the hooking mechanism needs to overwrite part of
  /// the function
  /// \param reroute the reroute function
  /// \param original original function
  /// \param preambleSize number of bytes from the original function to backup. Needs to
  /// correspond to complete instructions
  /// \param rerouteOffset offset in bytes from the created trampoline to the preamble
  /// that leads us back to the original code
  /// \return address of the trampoline function
  ///
  LPVOID storeTrampoline(LPVOID reroute, LPVOID original, size_t preambleSize,
                         size_t* rerouteOffset);

  ///
  /// \param addressNear used to find a trampoline buffer near the jump instruction
  /// \return retrieve address of current trampoline buffer
  ///
  LPVOID currentBufferAddress(LPVOID addressNear);

  ///
  /// \brief forces the barrier(s) for the current thread to be released
  ///
  void forceUnlockBarrier();

private:
  struct BufferList
  {
    size_t offset;
    std::vector<LPVOID> buffers;
  };

  typedef std::map<LPVOID, BufferList> BufferMap;
  static const intptr_t ADDRESS_MASK =
      0xFFFFFFFFFF000000LL;  // mask to "round" addresses to consolidate near
                             // trampolines

private:
  TrampolinePool();

  TrampolinePool& operator=(const TrampolinePool& reference);  // not implemented

  /**
   * @brief allocates a buffer with read, write and execute rights near the
   * specified adress. The purpose is that we want to be able to jump from
   * adressNear to generated code with a 5-byte jump, even on x64 systems.
   * @param addressNear the reference adress
   * @note the resulting buffer is stored in the m_Buffers map
   */
  BufferMap::iterator allocateBuffer(LPVOID addressNear);

  void addBarrier(LPVOID rerouteAddr, LPVOID original, asmjit::X86Assembler& assembler);

#if BOOST_ARCH_X86_64
  void copyCode(asmjit::X86Assembler& assembler, LPVOID source, size_t numBytes);
#endif  // BOOST_ARCH_X86_64

  BufferList& getBufferList(LPVOID address);

  LPVOID roundAddress(LPVOID address) const;

public:
  static LPVOID __stdcall barrier(LPVOID function);
  static LPVOID __stdcall release(LPVOID function);

  LPVOID barrierInt(LPVOID function);
  LPVOID releaseInt(LPVOID function);

  void addCallToStub(asmjit::X86Assembler& assembler, LPVOID original, LPVOID reroute);

private:
  ///
  /// \brief add a jump to an address outside the custom generated asm code (without
  /// modifying registers)
  /// \param assembler the assembler generator to write to
  /// \param destination destination address
  /// \note this currently generates a lot code on x64, may be overly complicated
  ///
  void addAbsoluteJump(asmjit::X86Assembler& assembler, uint64_t destination);

  DWORD determinePageSize();

private:
#if BOOST_ARCH_X86_64
  static const int SIZE_OF_JUMP = 13;
#elif BOOST_ARCH_X86_32
  static const int SIZE_OF_JUMP = 5;
#endif

  // the hook lib tries to allocate buffer close to the hooked functions,
  // for 32-bits application, this often falls on 0x40000000, which is the
  // address at which DLLs can be loaded on Windows
  //
  // some old-style DRM (e.g. Dragon Age 2) check that address and prevent
  // the game from running if it's not 0x40000000, so we give them a bit of
  // spaces before allocating our buffers
  //
  static constexpr uintptr_t MIN_ALLOC_ADDR = 0x40000000 + 0x200000;

  static TrampolinePool* s_Instance;

  bool m_FullBlock{false};

  BufferMap m_Buffers;

  typedef std::map<void*, void*> TThreadMap;
  boost::thread_specific_ptr<TThreadMap> m_ThreadGuards;

  LPVOID m_BarrierAddr;
  LPVOID m_ReleaseAddr;

  DWORD m_BufferSize = {1024};
  size_t m_SearchRange;
  uint64_t m_AddressMask;

  int m_MaxTrampolineSize;
};

}  // namespace HookLib
