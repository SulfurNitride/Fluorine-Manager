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
#include <boost/format.hpp>
#include <boost/predef.h>
#include <map>
#pragma warning(push, 3)
#include <asmjit/asmjit.h>
#pragma warning(pop)
#include "hooklib.h"
#include "ttrampolinepool.h"
#include "utility.h"
#include <addrtools.h>
#include <formatters.h>
#include <shmlogger.h>
#include <winapi.h>

#if BOOST_ARCH_X86_64
#pragma message("64bit build")
#define JUMP_SIZE 13
#elif BOOST_ARCH_X86_32
#define JUMP_SIZE 5
#else
#error "unsupported architecture"
#endif

using namespace asmjit;
// from here on out I'll only test for 64 or "other"

using namespace HookLib;
using namespace asmjit;

using namespace usvfs;

struct THookInfo
{
  LPVOID originalFunction;
  LPVOID replacementFunction;
  LPVOID detour;      // detour to call the original function after hook was installed.
  LPVOID trampoline;  // code fragment that decides whether the replacement function or
                      // detour is executed (preventing endless loops)
  std::vector<uint8_t>
      preamble;  // part of the detour that needs to be re-inserted into the original
                 // function to return it to vanilla state
  bool stub;  // if this is true, the trampoline calls the "replacement"-function that
              // before the original function, not instead of it
  enum
  {
    TYPE_HOTPATCH,    // official hot-patch variant as used on 32-bit windows
    TYPE_WIN64PATCH,  // custom patch variant used on 64-bit windows
    TYPE_CHAINPATCH,  // the hook is part of the hook chain (and not the first)
    TYPE_OVERWRITE,   // full jump overwrite used if none of the above work
    TYPE_RIPINDIRECT  // the function already started on a rip-relative jump so we only
                      // modified that variable
  } type;
};

UDis86Wrapper& disasm()
{
  static UDis86Wrapper instance;
  return instance;
}

void PauseOtherThreads()
{
  // TODO: implement me!
}

void ResumePausedThreads()
{
  // TODO: implement me! should resume only the threads paused by PauseOtherThreads
}

// not using the disassembler because this is simpler
LPBYTE ShortJumpTarget(LPBYTE address)
{
  int8_t off = *(address + 1);
  return address + 2 + off;
}

size_t GetJumpSize(LPBYTE, LPVOID)
{
  // TODO: it would be neater to use asmjit to generate this jump and ask asmjit for
  // the size of this jump but with asmjit I can only generate absolute jumps, which
  // take too much space.

  // Since trampoline buffers is always allocated within 32-bit
  // address range of jump, we can say with confidence that a 5-byte jump is possible
  return 5;
}

void WriteLongJump(LPBYTE jumpAddr, LPVOID destination)
{
  // TODO: not using asmjit here because I couldn't figure out how to generate
  // a working, space-optimized, relative jump to outside the generated code and
  // we do want to optimize this jump
#if BOOST_ARCH_X86_64
  intptr_t dist = reinterpret_cast<intptr_t>(destination) -
                  (reinterpret_cast<intptr_t>(jumpAddr) + 5);
  int32_t distShort = static_cast<int32_t>(dist);
#else
  int32_t distShort = reinterpret_cast<intptr_t>(destination) -
                      (reinterpret_cast<intptr_t>(jumpAddr) + 5);
#endif
  *jumpAddr                                 = 0xE9;
  *reinterpret_cast<int32_t*>(jumpAddr + 1) = distShort;
}

void WriteSingleJump(THookInfo& hookInfo, HookError* error)
{
  DWORD oldprotect, ignore;
  // Set the target function to copy on write, so we don't modify code for other
  // processes
  if (!VirtualProtect(hookInfo.originalFunction, JUMP_SIZE, PAGE_EXECUTE_WRITECOPY,
                      &oldprotect)) {
    throw std::runtime_error("failed to change virtual protection");
  }

  WriteLongJump(reinterpret_cast<LPBYTE>(hookInfo.originalFunction),
                hookInfo.trampoline);

  // restore old memory protection
  if (!VirtualProtect(hookInfo.originalFunction, JUMP_SIZE, oldprotect, &ignore)) {
    throw std::runtime_error("failed to change virtual protection");
  }

  if (error != nullptr) {
    *error = ERR_NONE;
  }
}

void WriteShortJump(LPBYTE jumpAddr, const signed char offset)
{
  *jumpAddr       = 0xEB;
  *(jumpAddr + 1) = offset;
}

void WriteIndirectJump(THookInfo& hookInfo, size_t jumpSize, HookError* error)
{
  DWORD oldProtect = 0;
  LPBYTE jumpAddr  = reinterpret_cast<LPBYTE>(hookInfo.originalFunction) - jumpSize;
  // allow write to jump address + the short jump inside the function
  if (!VirtualProtect(jumpAddr, jumpSize + 2, PAGE_EXECUTE_WRITECOPY, &oldProtect)) {
    throw std::runtime_error("failed to change virtual protection");
  }

  // insert the long jump first, then the short jump to the long jump, thus
  // activating the reroute
  WriteLongJump(jumpAddr, hookInfo.trampoline);

  PauseOtherThreads();
  WriteShortJump(jumpAddr + jumpSize, -(static_cast<int8_t>(jumpSize) + 2));
  ResumePausedThreads();

  // restore access protection
  if (!VirtualProtect(jumpAddr, jumpSize + 2, oldProtect, &oldProtect)) {
    throw std::runtime_error("failed to change virtual protection");
  }
  if (error != nullptr) {
    *error = ERR_NONE;
  }
}

/// implements function hooking using the mechanism intended for hot patching
/// Explanation: the visual studio compiler offers an option to prepare functions
/// for hot patching. In this case the compiler leaves room for one far jump before
/// the actual function and a 2-byte nop inside the function.
/// to hook such a function we write a jump to our replacement function to the
/// space before the function and short jump to that jump to where the 2-byte nop
/// was.
/// On 32-bit windows, MS seems to have compiled all relevant functions in the
/// windows API for hot patching starting with Windows XP SP3
/// On 64-bit windows a lot of functions have the space for a jump before the function
/// but they don't have the 2-byte nop so this function doesn't work
/// \param hookInfo info about the hook to be installed
/// \return true on success, false on error
BOOL HookHotPatch(THookInfo& hookInfo, HookError* error)
{
  LPVOID original = reinterpret_cast<LPVOID>(hookInfo.originalFunction);

  if (hookInfo.stub) {
    hookInfo.trampoline = TrampolinePool::instance().storeStub(
        hookInfo.replacementFunction,
        reinterpret_cast<LPVOID>(hookInfo.originalFunction),
        shared::AddrAdd(original, 2));
  } else {
    hookInfo.trampoline = TrampolinePool::instance().storeTrampoline(
        hookInfo.replacementFunction,
        reinterpret_cast<LPVOID>(hookInfo.originalFunction),
        shared::AddrAdd(original, 2));
  }

  WriteIndirectJump(hookInfo, JUMP_SIZE, error);
  hookInfo.type = THookInfo::TYPE_HOTPATCH;
  // in this case we don't need a separate detour, we simply jump past the 2-byte nop
  hookInfo.detour = reinterpret_cast<LPBYTE>(hookInfo.originalFunction) + 2;

  return TRUE;
}

uintptr_t followJumps(THookInfo& hookInfo)
{
  LPBYTE original    = reinterpret_cast<LPBYTE>(hookInfo.originalFunction);
  LPBYTE shortTarget = ShortJumpTarget(original);

  // disassemble the long jump
  disasm().setInputBuffer(shortTarget, JUMP_SIZE);

  ud_disassemble(disasm());
  if (ud_insn_mnemonic(disasm()) != UD_Ijmp) {
    // this shouldn't happen, we only call this if the jump was discovered before
    throw std::runtime_error("failed to find jump in patch");
  }

  uint64_t res = ud_insn_off(disasm()) + ud_insn_len(disasm());
  res += disasm().jumpOffset();

  return static_cast<uintptr_t>(res);
}

///
/// \brief hook a call that is implemented as a short-jump to a long jump using a
/// rip-relative address variable
/// \param hookInfo info about the hook to be installed
/// \param error if the return code is false and this is not null, the referred-to
/// variable is set to an error code
/// \return true on success, false on error
///
BOOL HookRIPIndirection(THookInfo& hookInfo, HookError* error)
{
  uintptr_t res = followJumps(hookInfo);

  const ud_operand_t* op = ud_insn_opr(disasm(), 0);

  if (op->base != UD_R_RIP) {
    throw std::runtime_error("expected rip-relative addressing");
  }

  auto chainNext = disasm().jumpTarget();
  if (hookInfo.stub) {
    hookInfo.trampoline = TrampolinePool::instance().storeStub(
        hookInfo.replacementFunction,
        reinterpret_cast<LPVOID>(hookInfo.originalFunction),
        reinterpret_cast<LPVOID>(chainNext));
  } else {
    hookInfo.trampoline = TrampolinePool::instance().storeTrampoline(
        hookInfo.replacementFunction,
        reinterpret_cast<LPVOID>(hookInfo.originalFunction),
        reinterpret_cast<LPVOID>(chainNext));
  }

  DWORD oldProtect = 0;
  if (!VirtualProtect(reinterpret_cast<LPVOID>(res), 2, PAGE_EXECUTE_WRITECOPY,
                      &oldProtect)) {
    throw std::runtime_error("failed to change virtual protection");
  }

  *reinterpret_cast<uintptr_t*>(res) = reinterpret_cast<uintptr_t>(hookInfo.trampoline);

  if (!VirtualProtect(reinterpret_cast<LPVOID>(res), 2, oldProtect, &oldProtect)) {
    throw std::runtime_error("failed to change virtual protection");
  }

  hookInfo.type   = THookInfo::TYPE_RIPINDIRECT;
  hookInfo.detour = reinterpret_cast<LPVOID>(chainNext);

  if (error != nullptr) {
    *error = ERR_NONE;
  }

  return TRUE;
}

BOOL HookChainHook(THookInfo& hookInfo, LPBYTE jumpPos, HookError* error)
{
  // disassemble the long jump
  disasm().setInputBuffer(jumpPos, JUMP_SIZE);

  ud_disassemble(disasm());
  if (ud_insn_mnemonic(disasm()) != UD_Ijmp) {
    // this shouldn't happen, we only call this if the jump was discovered before
    throw std::runtime_error("failed to find jump in patch");
  }

  auto chainTarget = disasm().jumpTarget();

  size_t size = ud_insn_len(disasm());

  // save the original code for the preamble so we can restore it later
  hookInfo.preamble.resize(size);
  memcpy(&hookInfo.preamble[0], jumpPos, size);

  spdlog::get("usvfs")->info("existing hook to {0:x} in {1}", chainTarget,
                             shared::string_cast<std::string>(
                                 winapi::ex::wide::getSectionName((void*)chainTarget)));

  if (hookInfo.stub) {
    hookInfo.trampoline = TrampolinePool::instance().storeStub(
        hookInfo.replacementFunction,
        reinterpret_cast<LPVOID>(hookInfo.originalFunction),
        reinterpret_cast<LPVOID>(chainTarget));
  } else {
    hookInfo.trampoline = TrampolinePool::instance().storeTrampoline(
        hookInfo.replacementFunction,
        reinterpret_cast<LPVOID>(hookInfo.originalFunction),
        reinterpret_cast<LPVOID>(chainTarget));
  }

  DWORD oldProtect = 0;
  if (!VirtualProtect(jumpPos, size, PAGE_EXECUTE_WRITECOPY, &oldProtect)) {
    throw std::runtime_error("failed to change virtual protection");
  }

  WriteLongJump(jumpPos, hookInfo.trampoline);

  if (!VirtualProtect(jumpPos, size, oldProtect, &oldProtect)) {
    throw std::runtime_error("failed to change virtual protection");
  }

  hookInfo.type   = THookInfo::TYPE_CHAINPATCH;
  hookInfo.detour = reinterpret_cast<LPVOID>(chainTarget);

  if (error != nullptr) {
    *error = ERR_NONE;
  }

  return TRUE;
}

///
/// implements hooking by chaining to an existing hook
/// \param hookInfo info about the hook to be installed
/// \param error if the return code is false and this is not null,
///        the referred-to variable is set to an error code
/// \return true on success, false on error
///
BOOL HookChainHook(THookInfo& hookInfo, HookError* error)
{
  return HookChainHook(hookInfo, reinterpret_cast<LPBYTE>(hookInfo.originalFunction),
                       error);
}

///
/// implements hooking by chaining to an existing hot patch
/// \param hookInfo info about the hook to be installed
/// \param error if the return code is false and this is not null,
///        the referred-to variable is set to an error code
/// \return true on success, false on error
///
BOOL HookChainPatch(THookInfo& hookInfo, HookError* error)
{
  LPBYTE original    = reinterpret_cast<LPBYTE>(hookInfo.originalFunction);
  LPBYTE shortTarget = ShortJumpTarget(original);

  return HookChainHook(hookInfo, shortTarget, error);
}

/// implements function hooking by overwriting the first n bytes of the function
/// with a jump to the replacement function. Since this is destructive to the original
/// function code the first n bytes of the function need to be copied somewhere else
/// and that code needs to be called via a detour. This is a lot more complex than
/// the hotpatch mechanism.
/// \param hookInfo info about the hook to be installed
/// \param error if the return code is false and this is not null, the referred-to
/// variable is set to an error code
/// \return true on success, false on error
BOOL HookDisasm(THookInfo& hookInfo, HookError* error)
{
  LPBYTE address = reinterpret_cast<LPBYTE>(hookInfo.originalFunction);
  ud_set_input_buffer(disasm(), address, 40);

  size_t jumpSize = GetJumpSize(
      static_cast<LPBYTE>(hookInfo.originalFunction),
      TrampolinePool::instance().currentBufferAddress(hookInfo.originalFunction));

  // test if we have room for a jump before the function
  bool jumpspace = true;
  for (size_t i = 0; i < jumpSize; ++i) {
    if (*(address - i - 1) != 0x90) {
      jumpspace = false;
      break;
    }
  }

  size_t minSize = jumpspace ? 2 : jumpSize;

  // iterate over all instructions that overlap with the jump instructions
  // we want to write.
  // TODO right now, this does not test if the function is smaller than the jump.
  size_t size = 0;
  while (size < minSize) {
    if (ud_disassemble(disasm()) == 0) {
      throw std::runtime_error("premature end of file in disassembly");
    }

    if ((size == 0) && (ud_insn_mnemonic(disasm()) == UD_Ijmp)) {
      if (error != nullptr) {
        *error = ERR_JUMP;
      }
      return FALSE;
    }

    size += ud_insn_len(disasm());

    // ret instruction or int3 smells like function end
    if ((ud_insn_mnemonic(disasm()) == UD_Iret) ||
        (ud_insn_mnemonic(disasm()) == UD_Iint3)) {
      if (error != nullptr) {
        *error = ERR_FUNCEND;
      }
      return FALSE;
    }

    // check the operands for relative addressing (not supported)
    for (int i = 0; i < 3; ++i) {
      const ud_operand* op = ud_insn_opr(disasm(), i);
      if (op) {
        if (
            // rip-relative are not handled, usually relative jumps
            op->base == UD_R_RIP

            // rsp-relative call are not handled (there are valid rsp-relative
            // instructions, e.g. sub)
            || (ud_insn_mnemonic(disasm()) == UD_Icall && op->base == UD_R_RSP)) {
          if (error != nullptr) {
            *error = ERR_RIP;
          }
          return FALSE;
        }
      }
    }
  }

  // save the original code for the preamble so we can restore it later
  hookInfo.preamble.resize(size);
  memcpy(&hookInfo.preamble[0], hookInfo.originalFunction, size);

  size_t rerouteOffset = 0;
  if (hookInfo.stub) {
    hookInfo.trampoline = TrampolinePool::instance().storeStub(
        hookInfo.replacementFunction, hookInfo.originalFunction, size, &rerouteOffset);
  } else {
    hookInfo.trampoline = TrampolinePool::instance().storeTrampoline(
        hookInfo.replacementFunction, hookInfo.originalFunction, size, &rerouteOffset);
  }

  if (jumpspace) {
    WriteIndirectJump(hookInfo, jumpSize, error);
    hookInfo.type = THookInfo::TYPE_WIN64PATCH;
  } else {
    WriteSingleJump(hookInfo, error);
    hookInfo.type = THookInfo::TYPE_OVERWRITE;
  }
  hookInfo.detour = reinterpret_cast<LPBYTE>(hookInfo.trampoline) + rerouteOffset;

  return TRUE;
}

enum EPreamble
{
  PRE_PATCHFREE,
  PRE_PATCHUSED,
  PRE_RIPINDIRECT,
  PRE_FOREIGNHOOK,
  PRE_UNKNOWN
};

EPreamble DeterminePreamble(LPBYTE address)
{
  ud_set_input_buffer(disasm(), address, JUMP_SIZE);
  ud_disassemble(disasm());

  if ((ud_insn_mnemonic(disasm()) == UD_Imov) &&
      (ud_insn_opr(disasm(), 0) == ud_insn_opr(disasm(), 1)) &&
      (ud_insn_opr(disasm(), 0)->type == UD_OP_REG)) {
    // mov edi, edi
    return PRE_PATCHFREE;
  } else if ((ud_insn_mnemonic(disasm()) == UD_Ijmp) && (ud_insn_len(disasm()) == 2)) {
    // determine target of the short jump
    LPBYTE shortTarget = ShortJumpTarget(address);

    // test if that short jump leads to a long jump
    ud_set_input_buffer(disasm(), shortTarget, JUMP_SIZE);
    ud_disassemble(disasm());
    if (ud_insn_mnemonic(disasm()) == UD_Ijmp) {
      const ud_operand* op = ud_insn_opr(disasm(), 0);
      if (op->base == UD_R_RIP) {
        return PRE_RIPINDIRECT;
      } else {
        return PRE_PATCHUSED;
      }
    } else {
      return PRE_UNKNOWN;
    }
  } else if (ud_insn_mnemonic(disasm()) == UD_Ijmp) {
    return PRE_FOREIGNHOOK;
  } else {
    return PRE_UNKNOWN;
  }
}

static std::map<HOOKHANDLE, THookInfo> s_Hooks;

static HOOKHANDLE GenerateHandle()
{
  static ULONG NextHandle = 1;
  return NextHandle++;
}

HOOKHANDLE applyHook(THookInfo info, HookError* error)
{
  // apply the correct hook function depending on how the function start looks
  EPreamble preamble = DeterminePreamble((LPBYTE)info.originalFunction);

  BOOL success = FALSE;
  switch (preamble) {
  case PRE_PATCHUSED: {
    success = HookChainPatch(info, error);
  } break;
  case PRE_PATCHFREE: {
    success = HookHotPatch(info, error);
  } break;
  case PRE_RIPINDIRECT: {
    success = HookRIPIndirection(info, error);
  } break;
  case PRE_FOREIGNHOOK: {
    success = HookChainHook(info, error);
  } break;
  default: {
    success = HookDisasm(info, error);
  } break;
  }

  if (success == TRUE) {
    HOOKHANDLE handle = GenerateHandle();
    s_Hooks[handle]   = info;
    return handle;
  } else {
    return INVALID_HOOK;
  }
}

HOOKHANDLE HookLib::InstallStub(LPVOID functionAddress, LPVOID stubAddress,
                                HookError* error)
{
  if (functionAddress == nullptr) {
    if (error != nullptr)
      *error = ERR_INVALIDPARAMETERS;
    return INVALID_HOOK;
  }

  THookInfo info;
  info.originalFunction    = functionAddress;
  info.replacementFunction = stubAddress;
  info.stub                = true;
  info.detour              = nullptr;
  info.trampoline          = nullptr;
  info.type                = THookInfo::TYPE_OVERWRITE;

  return applyHook(info, error);
}

HOOKHANDLE HookLib::InstallStub(HMODULE module, LPCSTR functionName, LPVOID stubAddress,
                                HookError* error)
{
  LPVOID funcAddr = MyGetProcAddress(module, functionName);
  return InstallStub(funcAddr, stubAddress, error);
}

HOOKHANDLE HookLib::InstallHook(LPVOID functionAddress, LPVOID hookAddress,
                                HookError* error)
{
  if (functionAddress == nullptr) {
    if (error != nullptr)
      *error = ERR_INVALIDPARAMETERS;
    return INVALID_HOOK;
  }
  THookInfo info;
  info.originalFunction    = functionAddress;
  info.replacementFunction = hookAddress;
  info.stub                = false;
  info.detour              = nullptr;
  info.trampoline          = nullptr;
  info.type                = THookInfo::TYPE_OVERWRITE;

  return applyHook(info, error);
}

HOOKHANDLE HookLib::InstallHook(HMODULE module, LPCSTR functionName, LPVOID hookAddress,
                                HookError* error)
{
  LPVOID funcAddr = MyGetProcAddress(module, functionName);
  return InstallHook(funcAddr, hookAddress, error);
}

void HookLib::RemoveHook(HOOKHANDLE handle)
{
  auto iter = s_Hooks.find(handle);
  if (iter != s_Hooks.end()) {
    THookInfo info = iter->second;
    PauseOtherThreads();
    LPBYTE address = reinterpret_cast<LPBYTE>(info.originalFunction);
    if (info.type == THookInfo::TYPE_HOTPATCH) {
      // return the short jump to 2-byte nop
      // TODO: This doesn't take into account if we chain-loaded another hook

      DWORD oldProtect = 0;
      if (!VirtualProtect(address, 2, PAGE_EXECUTE_WRITECOPY, &oldProtect)) {
        throw shared::windows_error("failed to gain write access to remove hook");
      }
      *address       = 0x8b;
      *(address + 1) = 0xff;
      VirtualProtect(address, 2, oldProtect, &oldProtect);
    } else if ((info.type == THookInfo::TYPE_OVERWRITE) ||
               (info.type == THookInfo::TYPE_WIN64PATCH)) {
      DWORD oldProtect = 0;
      if (!VirtualProtect(address, info.preamble.size(), PAGE_EXECUTE_WRITECOPY,
                          &oldProtect)) {
        throw shared::windows_error("failed to gain write access to remove hook");
      }
      // TODO: remove hook by restoring the original function. This only works if we
      // have the exact code available somewhere
      memcpy(address, &info.preamble[0], info.preamble.size());
      VirtualProtect(address, info.preamble.size(), oldProtect, &oldProtect);
    } else if (info.type == THookInfo::TYPE_CHAINPATCH) {
      // we could attempt to restore the original function preamble but I'm not
      // sure we can reliably write the jump with same (or lower) size.
      // Instead overwrite our own trampoline
      disasm().setInputBuffer(static_cast<uint8_t*>(info.originalFunction), JUMP_SIZE);
      ud_disassemble(disasm());
      if (ud_insn_mnemonic(disasm()) != UD_Ijmp) {
        // this shouldn't happen, we only call this if the jump was discovered before
        throw std::runtime_error("failed to find jump in patch");
      }

      LPBYTE jumpPos = static_cast<LPBYTE>(info.originalFunction);
      if (ud_insn_len(disasm()) == 2) {
        jumpPos = reinterpret_cast<LPBYTE>(disasm().jumpTarget());
      }

      DWORD oldProtect = 0;
      if (!VirtualProtect(jumpPos, info.preamble.size(), PAGE_EXECUTE_WRITECOPY,
                          &oldProtect)) {
        throw shared::windows_error("failed to gain write access to remove hook");
      }
      memcpy(jumpPos, info.preamble.data(), info.preamble.size());
      VirtualProtect(jumpPos, info.preamble.size(), oldProtect, &oldProtect);
    } else if (info.type == THookInfo::TYPE_RIPINDIRECT) {
      uintptr_t res    = followJumps(info);
      DWORD oldProtect = 0;
      if (!VirtualProtect(reinterpret_cast<LPVOID>(res), JUMP_SIZE,
                          PAGE_EXECUTE_WRITECOPY, &oldProtect)) {
        throw shared::windows_error("failed to gain write access to remove hook");
      }
      *reinterpret_cast<uintptr_t*>(res) =
          reinterpret_cast<uintptr_t>(info.originalFunction);
      VirtualProtect(reinterpret_cast<LPVOID>(res), JUMP_SIZE, oldProtect, &oldProtect);
    } else {
      spdlog::get("usvfs")->critical("can't remove hook, unknown hook type!");
    }

    s_Hooks.erase(iter);
    ResumePausedThreads();
  } else {
    spdlog::get("usvfs")->info("handle unknown: {0:x}", handle);
  }
}

const char* HookLib::GetErrorString(HookError err)
{
  switch (err) {
  case ERR_NONE:
    return "No Error";
  case ERR_INVALIDPARAMETERS:
    return "Invalid parameters";
  case ERR_FUNCEND:
    return "Function too short";
  case ERR_JUMP:
    return "Function starts on a jump";
  case ERR_RIP:
    return "RIP-relative addressing can't be relocated.";
  case ERR_RELJUMP:
    return "Relative Jump can't be relocated.";
  default:
    return "Unkown error code";
  }
}

const char* HookLib::GetHookType(HOOKHANDLE handle)
{
  auto iter = s_Hooks.find(handle);
  if (iter != s_Hooks.end()) {
    THookInfo info = iter->second;
    switch (info.type) {
    case THookInfo::TYPE_HOTPATCH:
      return "hot patch";
    case THookInfo::TYPE_WIN64PATCH:
      return "64-bit hot patch";
    case THookInfo::TYPE_CHAINPATCH:
      return "chained patch";
    case THookInfo::TYPE_OVERWRITE:
      return "overwrite";
    case THookInfo::TYPE_RIPINDIRECT:
      return "rip indirection modified";
    default: {
      spdlog::get("usvfs")->error("invalid hook type {0}", info.type);
      return "invalid hook type";
    }
    }
  }
  return "invalid handle";
}

LPVOID HookLib::GetDetour(HOOKHANDLE handle)
{
  auto iter = s_Hooks.find(handle);
  if (iter != s_Hooks.end()) {
    THookInfo info = iter->second;
    return info.detour;
  }
  return nullptr;
}
