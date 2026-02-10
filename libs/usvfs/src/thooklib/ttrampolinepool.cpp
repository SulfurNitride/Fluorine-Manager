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
#include "ttrampolinepool.h"
#include <addrtools.h>
#include <shmlogger.h>
// #include <boost/thread/lock_guard.hpp>
#include "udis86wrapper.h"

using namespace asmjit;
#if BOOST_ARCH_X86_64
using namespace x86;
#elif BOOST_ARCH_X86_32
using namespace asmjit::x86;
#endif

using namespace usvfs::shared;

namespace HookLib
{

TrampolinePool* TrampolinePool::s_Instance = nullptr;

TrampolinePool::TrampolinePool() : m_MaxTrampolineSize(sizeof(LPVOID))
{
  m_BarrierAddr = &TrampolinePool::barrier;
  m_ReleaseAddr = &TrampolinePool::release;

  SYSTEM_INFO sysInfo;
  ::ZeroMemory(&sysInfo, sizeof(SYSTEM_INFO));
  GetSystemInfo(&sysInfo);
  m_BufferSize = sysInfo.dwPageSize;

  // if search range = ffffff then addressmask = ffffffffff000000
  // => all jumps between xxxxxxxxxx000000 and xxxxxxxxxxffffff will use the same buffer
  //    for trampolines which is guaranteed to be in that range
  // TODO it should be valid to use 2 ^ 32 as the search range to increase our chances
  //      of finding a memory block we can reserve but then there is a problem with
  //      converting negative jump distances to 32 bit I didn't understand. Everything
  //      up to 2 ^ 31 seems to be fine though
  m_SearchRange = static_cast<size_t>(pow(2, 30)) - 1;
  m_AddressMask = std::numeric_limits<uint64_t>::max() - m_SearchRange;
}

// static
void TrampolinePool::initialize()
{
  if (!s_Instance)
    s_Instance = new TrampolinePool();
}

void TrampolinePool::setBlock(bool block)
{
  m_FullBlock = block;
  if (m_ThreadGuards.get() == nullptr) {
    m_ThreadGuards.reset(new TThreadMap());
  }
}

#if BOOST_ARCH_X86_64
// push all registers (except rax) and flags to the stack
static void pushAll(X86Assembler& assembler)
{
  assembler.pushf();
  assembler.push(rcx);
  assembler.push(rdx);
  assembler.push(rbx);
  assembler.push(rbp);
  assembler.push(rsi);
  assembler.push(rdi);
  assembler.push(r8);
  assembler.push(r9);
  assembler.push(r10);
  assembler.push(r11);
  assembler.push(r12);
  assembler.push(r13);
  assembler.push(r14);
  assembler.push(r15);
}

// pop all registers (except rax) and flags from stack
static void popAll(X86Assembler& assembler)
{
  assembler.pop(r15);
  assembler.pop(r14);
  assembler.pop(r13);
  assembler.pop(r12);
  assembler.pop(r11);
  assembler.pop(r10);
  assembler.pop(r9);
  assembler.pop(r8);
  assembler.pop(rdi);
  assembler.pop(rsi);
  assembler.pop(rbp);
  assembler.pop(rbx);
  assembler.pop(rdx);
  assembler.pop(rcx);
  assembler.popf();
}
#endif  // BOOST_ARCH_X86_64

void TrampolinePool::addBarrier(LPVOID rerouteAddr, LPVOID original,
                                X86Assembler& assembler)
{
  Label skipLabel = assembler.newLabel();

#if BOOST_ARCH_X86_64
  pushAll(assembler);
  assembler.mov(rcx,
                imm(reinterpret_cast<int64_t>(
                    original)));  // set call parameter for call to barrier function
  assembler.mov(rax, imm((intptr_t)(void*)barrier));
  assembler.sub(rsp, 32);
  assembler.call(rax);
  assembler.add(rsp, 32);
  popAll(assembler);
  // test barrier
  assembler.cmp(rax, 0);    // test if the barrier is locked
  assembler.jz(skipLabel);  // skip if barrier was locked

  // call replacement function
  // for this call no registers are saved. the called function is a compiled function so
  // it should correctly save non-volatile registers, and the caller can't expect the
  // volatile ones to remain valid
  assembler.pop(r10);
  assembler.mov(dword_ptr(rax), r10);  // store that return address to the variable
                                       // supplied by the barrier function
  assembler.mov(rax, imm((intptr_t)(LPVOID)rerouteAddr));
  assembler.call(rax);
  assembler.push(rax);  // save away result

  // open the barrier again
  pushAll(assembler);
  assembler.mov(rcx, imm(reinterpret_cast<int64_t>(original)));
  assembler.mov(rax, imm((intptr_t)(void*)release));
  assembler.sub(rsp, 32);
  assembler.call(rax);
  assembler.add(rsp, 32);
  popAll(assembler);
  assembler.pop(r10);   // get the result from the replacement function to a register
  assembler.push(rax);  // push the original return address back on the stack
  assembler.mov(rax, r10);  // move result of actual call to rax
  assembler.ret();          // return, using the original return address
#else                       // BOOST_ARCH_X86_64
  assembler.push(imm(void_ptr_cast<int32_t>(
      original)));  // push original function, as parameter to barrier
  assembler.mov(ecx, (Ptr) static_cast<void*>(TrampolinePool::barrier));
  assembler.call(ecx);  // call barrier function
  assembler.cmp(eax, 0);
  assembler.jz(skipLabel);  // if barrier is locked, jump to end of function

  // case a: we got through the barrier
  assembler.pop(ecx);                  // pop the return address into ecx
  assembler.mov(dword_ptr(eax), ecx);  // store that return address in the variable
                                       // supplied by the barrier function

  assembler.mov(eax, (Ptr) static_cast<void*>(rerouteAddr));
  assembler.call(eax);  // call replacement function (pointer was stored in front of the
                        // trampoline) (this function gets the parameters that were on
                        // the stack already and cleans
                        //  them up itself (stdcall convention))
  assembler.push(eax);  // save away result
  assembler.push(imm(void_ptr_cast<int32_t>(original)));  // open the barrier again
  assembler.mov(eax, (Ptr) static_cast<void*>(TrampolinePool::release));
  assembler.call(eax);
  assembler.pop(ecx);       // pop the result from the actual call to ecx
  assembler.push(eax);      // push the original return address (returned by
                            // TTrampolinePool::release) back on the stack
  assembler.mov(eax, ecx);  // move result of actual call to eax
  assembler.ret();          // return, using the original return address
#endif                      // BOOST_ARCH_X86_64

  assembler.bind(skipLabel);
}

LPVOID TrampolinePool::roundAddress(LPVOID address) const
{
  return reinterpret_cast<LPVOID>(reinterpret_cast<intptr_t>(address) & m_AddressMask);
}

TrampolinePool::BufferList& TrampolinePool::getBufferList(LPVOID address)
{
  LPVOID rounded = roundAddress(address);
  auto iter      = m_Buffers.find(rounded);
  if (iter == m_Buffers.end()) {
    BufferList newBufList = {0, std::vector<LPVOID>()};
    m_Buffers[rounded]    = newBufList;
    iter                  = allocateBuffer(address);
  }
  return iter->second;
}

LPVOID TrampolinePool::storeStub(LPVOID reroute, LPVOID original, LPVOID returnAddress)
{
  BufferList& bufferList = getBufferList(original);
  // first test to increase likelyhood we don't have to reallocate later
  if (bufferList.offset + m_MaxTrampolineSize > m_BufferSize) {
    allocateBuffer(original);
  }

  LPVOID spot = AddrAdd(*bufferList.buffers.rbegin(), bufferList.offset);

  // ??? write address of reroute to trampoline and move past the address
  *reinterpret_cast<LPVOID*>(spot) = reroute;
  // coverity[suspicious_sizeof]
  spot = AddrAdd(spot, sizeof(LPVOID));
  bufferList.offset += sizeof(LPVOID);

  JitRuntime runtime;
#if BOOST_ARCH_X86_64
  X86Assembler assembler(&runtime);
#else
  X86Assembler assembler(&runtime);
#endif
  addCallToStub(assembler, original, reroute);
  addAbsoluteJump(assembler, reinterpret_cast<uint64_t>(returnAddress));

  size_t codeSize = assembler.getCodeSize();

  m_MaxTrampolineSize =
      std::max(m_MaxTrampolineSize, static_cast<int>(codeSize + sizeof(LPVOID)));

  // final test to see if we can store the trampoline in the buffer
  if ((bufferList.offset + codeSize) > m_BufferSize) {
    // can't place function in buffer, allocate another and try again
    allocateBuffer(original);
    // we could relocate the code and the data but this is simpler
    return storeStub(reroute, original, returnAddress);
  }

  // adjust relative jumps for move to buffer
  codeSize = assembler.relocCode(spot);

  uint8_t* code = assembler.getBuffer();
  memcpy(spot, code, codeSize);

  bufferList.offset += codeSize;

  return spot;
}

LPVOID TrampolinePool::storeTrampoline(LPVOID reroute, LPVOID original,
                                       LPVOID returnAddress)
{
  BufferList& bufferList = getBufferList(original);
  // first test to increase likelyhood we don't have to reallocate later
  if (bufferList.offset + m_MaxTrampolineSize > m_BufferSize) {
    allocateBuffer(original);
  }

  LPVOID spot = AddrAdd(*bufferList.buffers.rbegin(), bufferList.offset);

  *reinterpret_cast<LPVOID*>(spot) = reroute;
  // coverity[suspicious_sizeof]
  spot = AddrAdd(spot, sizeof(LPVOID));
  bufferList.offset += sizeof(LPVOID);

  JitRuntime runtime;
  X86Assembler assembler(&runtime);
  addBarrier(reroute, original, assembler);
#if BOOST_ARCH_X86_64
  assembler.mov(rax, imm((intptr_t)(void*)(returnAddress)));
  assembler.jmp(rax);
#else
  assembler.mov(eax, imm((intptr_t)(void*)(returnAddress)));
  assembler.jmp(eax);
#endif
  size_t codeSize = assembler.getCodeSize();

  m_MaxTrampolineSize =
      std::max(m_MaxTrampolineSize, static_cast<int>(codeSize + sizeof(LPVOID)));

  // final test to see if we can store the trampoline in the buffer
  if ((bufferList.offset + codeSize) > m_BufferSize) {
    // can't place function in buffer, allocate another and try again
    allocateBuffer(original);
    // we could relocate the code and the data but this is simpler
    return storeTrampoline(reroute, original, returnAddress);
  }

  // adjust relative jumps for move to buffer
  codeSize = assembler.relocCode(spot);

  // copy code to buffer
  uint8_t* code = assembler.getBuffer();
  memcpy(spot, code, codeSize);

  bufferList.offset += codeSize;
  return spot;
}

#if BOOST_ARCH_X86_64
void TrampolinePool::copyCode(X86Assembler& assembler, LPVOID source, size_t numBytes)
{
  static UDis86Wrapper disasm;

  disasm.setInputBuffer(static_cast<const uint8_t*>(source), numBytes);

  size_t offset = 0;

  while (ud_disassemble(disasm) != 0) {
    // rewrite relative jumps, blind copy everything else

    offset += ud_insn_len(disasm);

    // WARNING: doesn't support conditional jumps
    if ((ud_insn_mnemonic(disasm) == UD_Ijmp) &&
        (ud_insn_opr(disasm, 0)->type == UD_OP_JIMM)) {
      uintptr_t dest = disasm.jumpTarget();
      assembler.mov(rax, imm(static_cast<uint64_t>(dest)));
      assembler.jmp(rax);
    } else {
      assembler.embed(ud_insn_ptr(&disasm.obj()), ud_insn_len(&disasm.obj()));
      //      assembler.data();
    }
  }
}
#endif

void TrampolinePool::addCallToStub(X86Assembler& assembler, LPVOID original,
                                   LPVOID reroute)
{
#if BOOST_ARCH_X86_64
  pushAll(assembler);
  assembler.mov(rcx, imm(reinterpret_cast<int64_t>(original)));
  assembler.mov(rax, imm((intptr_t)(LPVOID)reroute));
  assembler.sub(rsp, 32);
  assembler.call(rax);
  assembler.add(rsp, 32);
  popAll(assembler);
#else   // BOOST_ARCH_X86_64
  assembler.push(reinterpret_cast<int64_t>(original));
  assembler.mov(ecx, imm((intptr_t)(LPVOID)reroute));
  assembler.call(ecx);
  assembler.pop(ecx);  // remove argument from stack
#endif  // BOOST_ARCH_X86_64
}

void TrampolinePool::addAbsoluteJump(X86Assembler& assembler, uint64_t destination)
{
#if BOOST_ARCH_X86_64
  assembler.push(rax);
  assembler.push(rax);
  assembler.mov(rax, imm(destination));
  assembler.mov(ptr(rsp, 8), rax);
  assembler.pop(rax);
  assembler.ret();
#else   // BOOST_ARCH_X86_64
  assembler.push(imm(destination));
  assembler.ret();
#endif  // BOOST_ARCH_X86_64
}

LPVOID TrampolinePool::storeStub(LPVOID reroute, LPVOID original, size_t preambleSize,
                                 size_t* rerouteOffset)
{
  BufferList& bufferList = getBufferList(original);
  // first test to increase likelyhood we don't have to reallocate later
  if (bufferList.offset + m_MaxTrampolineSize > m_BufferSize) {
    allocateBuffer(original);
  }

  LPVOID spot = AddrAdd(*bufferList.buffers.rbegin(), bufferList.offset);

  *reinterpret_cast<LPVOID*>(spot) = reroute;
  // coverity[suspicious_sizeof]
  spot = AddrAdd(spot, sizeof(LPVOID));
  bufferList.offset += sizeof(LPVOID);

  JitRuntime runtime;
  X86Assembler assembler(&runtime);
  addCallToStub(assembler, original, reroute);
#if BOOST_ARCH_X86_64
  // insert backup code
  *rerouteOffset = assembler.getCodeSize();
  copyCode(assembler, original, preambleSize);
#else   // BOOST_ARCH_X86_64
  assembler.embed(original, preambleSize);
#endif  // BOOST_ARCH_X86_64
  addAbsoluteJump(assembler, reinterpret_cast<uint64_t>(original) + preambleSize);

  // adjust relative jumps for move to buffer
  size_t codeSize = assembler.getCodeSize();

  m_MaxTrampolineSize =
      std::max(m_MaxTrampolineSize, static_cast<int>(codeSize + sizeof(LPVOID)));

  // final test to see if we can store the trampoline in the buffer
  if ((bufferList.offset + codeSize) > m_BufferSize) {
    // can't place function in buffer, allocate another and try again
    allocateBuffer(original);
    // we could relocate the code and the data but this is simpler
    return storeStub(reroute, original, preambleSize, rerouteOffset);
  }

  // copy code to buffer
  codeSize = assembler.relocCode(spot);

  bufferList.offset += preambleSize + codeSize;
  return spot;
}

LPVOID TrampolinePool::storeTrampoline(LPVOID reroute, LPVOID original,
                                       size_t preambleSize, size_t* rerouteOffset)
{
  BufferList& bufferList = getBufferList(original);
  // first test to increase likelyhood we don't have to reallocate later
  if (bufferList.offset + m_MaxTrampolineSize > m_BufferSize) {
    allocateBuffer(original);
  }

  LPVOID spot = AddrAdd(*bufferList.buffers.rbegin(), bufferList.offset);

  *reinterpret_cast<LPVOID*>(spot) = reroute;
  // coverity[suspicious_sizeof]
  spot = AddrAdd(spot, sizeof(LPVOID));
  bufferList.offset += sizeof(LPVOID);

  JitRuntime runtime;
  X86Assembler assembler(&runtime);
  addBarrier(reroute, original, assembler);
  // insert backup code
  *rerouteOffset = assembler.getCodeSize();
  assembler.embed(original, static_cast<uint32_t>(preambleSize));
  addAbsoluteJump(assembler, reinterpret_cast<uint64_t>(original) + preambleSize);

  // adjust relative jumps for move to buffer
  size_t codeSize = assembler.getCodeSize();

  m_MaxTrampolineSize =
      std::max(m_MaxTrampolineSize, static_cast<int>(codeSize + sizeof(LPVOID)));

  // TODO this does not take into account that the code size may technically change
  // after relocation in which case the following test may determine the code fits into
  // the buffer when it really doesnt't. asmjit doesn't seem to provide a way to adjust
  // jumps without actually moving the code though

  // final test to see if we can store the trampoline in the buffer
  if ((bufferList.offset + codeSize) > m_BufferSize) {
    // can't place function in buffer, allocate another and try again
    allocateBuffer(original);
    // we could relocate the code and the data but this is simpler
    return storeTrampoline(reroute, original, preambleSize, rerouteOffset);
  }

  // copy code to buffer
  codeSize = static_cast<size_t>(assembler.relocCode(spot));

  bufferList.offset += preambleSize + codeSize;

  return spot;
}

LPVOID TrampolinePool::currentBufferAddress(LPVOID addressNear)
{
  LPVOID rounded     = roundAddress(addressNear);
  auto lookupAddress = m_Buffers.find(rounded);

  if (lookupAddress == m_Buffers.end()) {
    lookupAddress = m_Buffers.insert(std::make_pair(rounded, BufferList())).first;
  }
  if (lookupAddress->second.buffers.size() == 0) {
    allocateBuffer(addressNear);
  }

  LPVOID res = *(lookupAddress->second.buffers.rbegin());
  return res;
}

void TrampolinePool::forceUnlockBarrier()
{
  if (m_ThreadGuards.get() != nullptr) {
    for (auto funcId : *m_ThreadGuards) {
      (*m_ThreadGuards)[funcId.first] = nullptr;
    }
  }  // else no barriers to unlock
}

TrampolinePool::BufferMap::iterator TrampolinePool::allocateBuffer(LPVOID addressNear)
{
  // allocate a buffer that we can write to and that is executable
  SYSTEM_INFO sysInfo;
  ::ZeroMemory(&sysInfo, sizeof(SYSTEM_INFO));
  GetSystemInfo(&sysInfo);

  LPVOID rounded     = roundAddress(addressNear);
  auto iter          = m_Buffers.find(rounded);
  uintptr_t lowerEnd = reinterpret_cast<uintptr_t>(rounded);
  if (iter->second.buffers.size() > 0) {
    // start searching were we last found a buffer
    lowerEnd = reinterpret_cast<uintptr_t>(*iter->second.buffers.rbegin()) +
               sysInfo.dwPageSize;
  }

  uintptr_t start =
      std::max(std::max(lowerEnd, MIN_ALLOC_ADDR),
               reinterpret_cast<uintptr_t>(sysInfo.lpMinimumApplicationAddress));
  uintptr_t upperEnd = reinterpret_cast<uintptr_t>(rounded) + m_SearchRange;
  uintptr_t end      = std::min(
      upperEnd, reinterpret_cast<uintptr_t>(sysInfo.lpMaximumApplicationAddress));

  LPVOID buffer = nullptr;
  for (uintptr_t cur = start; cur < end; cur += sysInfo.dwPageSize) {
    buffer = VirtualAlloc(reinterpret_cast<LPVOID>(cur), m_BufferSize,
                          MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (buffer != nullptr) {
      break;
    }
  }
  if (buffer == nullptr) {
    throw std::runtime_error("failed to allocate buffer in range");
  }

  // the caller must have looked up the bufferlist in order to determine that a
  // buffer has to be allocated
  assert(iter != m_Buffers.end());

  iter->second.offset = 0;
  iter->second.buffers.push_back(buffer);
  spdlog::get("usvfs")->debug(
      "allocated trampoline buffer for jumps between {0:p} and {1:x} at {2:p}"
      "(size {3})",
      rounded, (reinterpret_cast<uintptr_t>(rounded) + m_SearchRange), buffer,
      m_BufferSize);
  return iter;
}

LPVOID TrampolinePool::barrier(LPVOID function)
{
  DWORD err  = GetLastError();
  LPVOID res = instance().barrierInt(function);
  SetLastError(err);
  return res;
}

LPVOID TrampolinePool::release(LPVOID function)
{
  DWORD err  = GetLastError();
  LPVOID res = instance().releaseInt(function);
  SetLastError(err);
  return res;
}

LPVOID TrampolinePool::barrierInt(LPVOID func)
{
  if (m_FullBlock) {
    return nullptr;
  }

  if (m_ThreadGuards.get() == nullptr) {
    m_ThreadGuards.reset(new TThreadMap());
  }

  auto iter = m_ThreadGuards->find(func);
  if ((iter == m_ThreadGuards->end()) || (iter->second == nullptr)) {
    (*m_ThreadGuards)[func] = reinterpret_cast<LPVOID>(1);
    return &(*m_ThreadGuards)[func];
  } else {
    return nullptr;
  }
}

LPVOID TrampolinePool::releaseInt(LPVOID func)
{
  DWORD lastError = GetLastError();
  if (m_ThreadGuards.get() == nullptr) {
    m_ThreadGuards.reset(new TThreadMap());
  }

  auto iter = m_ThreadGuards->find(func);
  if (iter == m_ThreadGuards->end()) {
    spdlog::get("hooks")->error("failed to release barrier for func {}", func);
    ::SetLastError(lastError);
    return nullptr;
  }

  LPVOID res              = (*m_ThreadGuards)[func];
  (*m_ThreadGuards)[func] = nullptr;

  ::SetLastError(lastError);
  return res;
}

}  // namespace HookLib
