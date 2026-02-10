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
#include <cstdio>
#include <stdexcept>

#include <boost/filesystem.hpp>
#include <boost/predef.h>
#include <boost/static_assert.hpp>
#include <boost/type_traits.hpp>
namespace fs = boost::filesystem;

#include "injectlib.h"
#include <addrtools.h>
#include <exceptionex.h>
#include <stringcast.h>
#include <stringutils.h>
// local version of asmjit with warning suppression
#include "asmjit_sane.h"
#include <TlHelp32.h>
#include <spdlog/spdlog.h>

using namespace asmjit;
using namespace usvfs::shared;

#if BOOST_ARCH_X86_64
#pragma message("64bit build")
using namespace x86;
#elif BOOST_ARCH_X86_32
#pragma message("32bit build")
using namespace asmjit::x86;
#else
#error "unsupported architecture"
#endif

typedef HMODULE(WINAPI* TLoadLibraryType)(LPCWSTR);
typedef FARPROC(WINAPI* TGetProcAddressType)(HMODULE, LPCSTR);
typedef DWORD(WINAPI* TGetLastErrorType)();

typedef BOOL(WINAPI* TSetXStateFeaturesMaskType)(PCONTEXT, DWORD64);

static const size_t MAX_FUNCTIONAME = 20;

struct TDataRemote
{
  TLoadLibraryType loadLibrary;
  TGetProcAddressType getProcAddress;
  TGetLastErrorType getLastError;
  REGWORD returnAddress;

  char initFunction[MAX_FUNCTIONAME + 1];
  WCHAR dllName[MAX_PATH];
};

#if BOOST_ARCH_X86_64
void pushAll(X86Assembler& assembler)
{
  assembler.pushf();
  assembler.push(rax);
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

void popAll(X86Assembler& assembler)
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
  assembler.pop(rax);
  assembler.popf();
}
#endif  // BOOST_ARCH_X86_64

void addStub(size_t userDataSize, X86Assembler& assembler, bool skipInit,
             TDataRemote* localData, TDataRemote* remoteData, LPCSTR initFunction)
{
  Label Label_DLLLoaded = assembler.newLabel();

#if BOOST_ARCH_X86_64
  pushAll(assembler);
  // call load library for the actual injection
  assembler.mov(rcx, imm(reinterpret_cast<int64_t>(&remoteData->dllName)));
  assembler.mov(rax, imm((intptr_t)(void*)localData->loadLibrary));
  assembler.sub(rsp, 32);
  assembler.call(rax);
  assembler.add(rsp, 32);

  // cancel out of here if we failed to load the dll
  // TODO: would be great to report this error. But how?
  assembler.test(rax, rax);
  assembler.jnz(Label_DLLLoaded);
  /* this commented out code may seem pointless but it is a simple way to get at the
    error code when debugging. assembler.mov(rax,
    imm((intptr_t)(void*)localData->getLastError)); assembler.sub(rsp, 32);
    assembler.call(rax);
    assembler.add(rsp, 32);
    assembler.int3();*/
  popAll(assembler);
  assembler.ret();
  assembler.bind(Label_DLLLoaded);

  // determine address of the init function
  if (initFunction != nullptr) {
    Label Label_SkipInit = assembler.newLabel();
    assembler.mov(rcx, rax);  // handle of the dll
    assembler.mov(rdx, imm(reinterpret_cast<int64_t>(
                           remoteData->initFunction)));  // name of init function
    assembler.mov(rax, imm((intptr_t)(void*)localData->getProcAddress));
    assembler.sub(rsp, 32);
    assembler.call(rax);
    assembler.add(rsp, 32);

    if (skipInit) {
      assembler.test(rax, rax);
      assembler.jz(Label_SkipInit);
    }

    // call the init function with user data
    assembler.mov(rcx,
                  imm(reinterpret_cast<int64_t>(remoteData) + sizeof(TDataRemote)));
    assembler.mov(rdx, imm(static_cast<int64_t>(userDataSize)));
    assembler.sub(rsp, 32);
    assembler.call(rax);
    assembler.add(rsp, 32);
    assembler.bind(Label_SkipInit);
  }

  // restore registers
  popAll(assembler);
#else

  // save registers
  assembler.push(eax);
  assembler.pushf();

  // call load library for the actual injection
  assembler.push(imm(void_ptr_cast<int64_t>(remoteData->dllName)));
  //  assembler.call(ptr_abs(static_cast<void*>(remoteData->loadLibrary)));
  assembler.mov(eax, imm(void_ptr_cast<int64_t>(localData->loadLibrary)));
  assembler.call(eax);

  assembler.test(eax, eax);
  assembler.jnz(Label_DLLLoaded);
  /* this commented out code may seem pointless but it is a simple way to get at the
    error code when debugging. assembler.mov(eax,
    imm((intptr_t)(void*)localData->getLastError)); assembler.call(eax);
    assembler.int3();*/
  assembler.popf();
  assembler.pop(eax);
  assembler.ret();
  assembler.bind(Label_DLLLoaded);

  // determine address of the init function
  if (initFunction != nullptr) {
    Label Label_SkipInit = assembler.newLabel();
    assembler.push(imm(
        void_ptr_cast<int64_t>(remoteData->initFunction)));  // name of init function
    assembler.push(eax);                                     // handle of the dll
    assembler.mov(eax, imm(void_ptr_cast<int64_t>(localData->getProcAddress)));
    assembler.call(eax);
    if (skipInit) {
      assembler.cmp(eax, 0);
      assembler.jz(Label_SkipInit);
    } else {
      assembler.cmp(eax, 0);
      assembler.jnz(Label_SkipInit);
      // heading for a crash! give an attached debugger a chance to analyse the error
      assembler.mov(eax, imm(void_ptr_cast<int64_t>(localData->getLastError)));
      assembler.call(eax);
      assembler.int3();
      assembler.bind(Label_SkipInit);
    }

    // call the init function with user data
    assembler.push(userDataSize);
    assembler.push(imm(void_ptr_cast<int64_t>(remoteData) + sizeof(TDataRemote)));
    assembler.call(eax);
    // init function is declared __cdecl so we have to remove parameters from the stack
    assembler.pop(eax);
    assembler.pop(eax);
    if (skipInit) {
      assembler.bind(Label_SkipInit);
    }
  }

  // restore registers
  assembler.popf();
  assembler.pop(eax);

#endif
}

REGWORD WriteInjectionStub(HANDLE processHandle, LPCWSTR dllName, LPCSTR initFunction,
                           LPCVOID userData, size_t userDataSize, bool skipInit,
                           REGWORD returnAddress)
{
  HMODULE k32mod   = ::LoadLibrary(__TEXT("kernel32.dll"));
  TDataRemote data = {0};

  if (k32mod != nullptr) {
    data.loadLibrary =
        reinterpret_cast<TLoadLibraryType>(GetProcAddress(k32mod, "LoadLibraryW"));
    data.getProcAddress =
        reinterpret_cast<TGetProcAddressType>(GetProcAddress(k32mod, "GetProcAddress"));
    data.getLastError =
        reinterpret_cast<TGetLastErrorType>(GetProcAddress(k32mod, "GetLastError"));
    if ((data.loadLibrary == nullptr) || (data.getProcAddress == nullptr) ||
        (data.getLastError == nullptr)) {
      throw windows_error("failed to determine address for required functions");
    }
  } else {
    throw windows_error("kernel32.dll not loaded?");
  }

  data.returnAddress = returnAddress;

  if (initFunction != nullptr) {
    strncpy_s(data.initFunction, MAX_FUNCTIONAME, initFunction, MAX_FUNCTIONAME);
    data.initFunction[MAX_FUNCTIONAME] = '\0';
  } else {
    data.initFunction[0] = '\0';
  }

  wcsncpy_s(data.dllName, MAX_PATH, dllName, MAX_PATH - 1);
  data.dllName[MAX_PATH - 1] = L'\0';

  size_t totalSize = sizeof(TDataRemote) + userDataSize;

  // allocate memory in the target process and write the data-block there
  LPVOID remoteMem = VirtualAllocEx(processHandle, nullptr, totalSize,
                                    MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

  if (remoteMem == nullptr) {
    throw windows_error("failed to allocate memory in target process");
  }

  SIZE_T written;
  if (!WriteProcessMemory(processHandle, remoteMem, &data, sizeof(TDataRemote),
                          &written)) {
    throw windows_error("failed to write control data to target process");
  }
  if (written != sizeof(TDataRemote)) {
    throw windows_error("failed to write whole control data to target process");
  }

  // write user data to remote memory if necessary
  if (userData != nullptr) {
    if (!WriteProcessMemory(processHandle, AddrAdd(remoteMem, sizeof(TDataRemote)),
                            userData, userDataSize, &written)) {
      throw windows_error("failed to write user data to target process");
    }
    if (written != userDataSize) {
      throw windows_error("failed to write whole user data to target process");
    }
  }

  TDataRemote* remoteData = reinterpret_cast<TDataRemote*>(remoteMem);

  // now for the interesting part: write a stub into the target process that is run
  // before any code of the original binary.

  JitRuntime runtime;
#if BOOST_ARCH_X86_64
  X86Assembler assembler(&runtime);
  if (returnAddress != 0) {
    // put return address on the stack
    // (this damages rax which hopefully doesn't matter)
    assembler.mov(rax, imm((intptr_t)(void*)data.returnAddress));
    assembler.push(rax);
  }  // otherwise no return address was specified here. It better be on the stack
     // already
#else
  X86Assembler assembler(&runtime);
  if (returnAddress != 0) {
    assembler.push(imm((intptr_t)(void*)data.returnAddress));
  }
#endif

  addStub(userDataSize, assembler, skipInit, &data, remoteData, initFunction);
  assembler.ret(0);

  size_t stubSize = assembler.getCodeSize();

  // reserve memory for the stub
  PBYTE stubRemote = reinterpret_cast<PBYTE>(
      VirtualAllocEx(processHandle, nullptr, stubSize, MEM_COMMIT | MEM_RESERVE,
                     PAGE_EXECUTE_READWRITE));
  if (stubRemote == nullptr) {
    throw windows_error("failed to allocate memory for stub");
  }

  // almost there. copy stub to target process
  if (!WriteProcessMemory(processHandle, stubRemote, assembler.getBuffer(), stubSize,
                          &written) ||
      (written != stubSize)) {
    throw windows_error("failed to write stub to target process");
  }

  return reinterpret_cast<REGWORD>(stubRemote);
}

void InjectDLLEIP(HANDLE processHandle, HANDLE threadHandle, LPCWSTR dllName,
                  LPCSTR initFunction, LPCVOID userData, size_t userDataSize,
                  bool skipInit)
{
  threadHandle =
      OpenThread((THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME),
                 FALSE, GetThreadId(threadHandle));

  if (threadHandle == nullptr) {
    throw windows_error("failed to open thread");
  }

  CONTEXT threadContext;
  threadContext.ContextFlags = CONTEXT_CONTROL;

  // documentation says starting with Win7 SP1 you HAVE to call SetXStateFeaturesMask
  HMODULE k32mod = ::LoadLibrary(__TEXT("kernel32.dll"));
  if (k32mod == nullptr) {
    throw windows_error("failed to load kernel32.dll");
  }
  TSetXStateFeaturesMaskType sxsfm = reinterpret_cast<TSetXStateFeaturesMaskType>(
      GetProcAddress(k32mod, "SetXStateFeaturesMask"));
  if (sxsfm != nullptr) {
    sxsfm(&threadContext, 0);
  }
  ::FreeLibrary(k32mod);

  if (GetThreadContext(threadHandle, &threadContext) == 0) {
    throw windows_error("failed to access thread context.");
  }

#if BOOST_ARCH_X86_64
  REGWORD returnAddress = threadContext.Rip;
#else
  REGWORD returnAddress = threadContext.Eip;
#endif

  REGWORD stubAddress =
      WriteInjectionStub(processHandle, dllName, initFunction, userData, userDataSize,
                         skipInit, returnAddress);

  // make the stub the new next thing for the thread to execute
#if BOOST_ARCH_X86_64
  threadContext.Rip = stubAddress;
#else
  threadContext.Eip = stubAddress;
#endif

  if (SetThreadContext(threadHandle, &threadContext) == 0) {
    throw windows_error("failed to overwrite thread context");
  }
}

void InjectDLLRemoteThread(HANDLE processHandle, LPCWSTR dllName, LPCSTR initFunction,
                           LPCVOID userData, size_t userDataSize, bool skipInit)
{
  REGWORD stubAddress = WriteInjectionStub(processHandle, dllName, initFunction,
                                           userData, userDataSize, skipInit, 0);

  DWORD threadId      = 0UL;
  HANDLE threadHandle = CreateRemoteThread(
      processHandle, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(stubAddress),
      nullptr, 0, &threadId);
  if (threadHandle == nullptr) {
    throw windows_error("failed to start remote thread");
  }
  ResumeThread(threadHandle);
  spdlog::get("usvfs")->info("waiting for {0:x} to complete",
                             GetThreadId(threadHandle));
  ::WaitForSingleObject(threadHandle, 100);
  ::CloseHandle(threadHandle);
}

void InjectLib::InjectDLL(HANDLE processHandle, HANDLE threadHandle, LPCWSTR dllName,
                          LPCSTR initFunction, LPCVOID userData, size_t userDataSize,
                          bool skipInit)
{
  namespace bfs = boost::filesystem;
  if (!exists(bfs::path(dllName))) {
    USVFS_THROW_EXCEPTION(file_not_found_error()
                          << ex_msg(string_cast<std::string>(dllName)));
  }
  if (threadHandle == INVALID_HANDLE_VALUE) {
#pragma message(                                                                       \
    "doesn't seem to work as usvfs causes an exception in the first static initialization or pretty much on any function call. Because process is in different session? CRT related?")
    /*
    InjectDLLRemoteThread(processHandle, dllName,
                 initFunction, userData, userDataSize, skipInit);
                 */

    DWORD pid       = GetProcessId(processHandle);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 threadInfo;
    threadInfo.dwSize = sizeof(THREADENTRY32);
    BOOL moreThreads  = Thread32First(snapshot, &threadInfo);
    std::vector<HANDLE> threadHandles;
    HANDLE injectThread = INVALID_HANDLE_VALUE;
    FILETIME injectThreadTime;
    spdlog::get("usvfs")->info("inject dll to process {0}", pid);
    while (moreThreads) {
      if (threadInfo.th32OwnerProcessID == pid) {
        HANDLE thread = ::OpenThread(THREAD_ALL_ACCESS, FALSE, threadInfo.th32ThreadID);

        if (thread != nullptr) {
          DWORD suspCount = SuspendThread(thread);
          if (suspCount == 0) {
            FILETIME creationTime, exitTime, kernelTime, userTime;
            ::GetThreadTimes(thread, &creationTime, &exitTime, &kernelTime, &userTime);

            if ((injectThread == INVALID_HANDLE_VALUE) ||
                (CompareFileTime(&creationTime, &injectThreadTime) < 0)) {
              spdlog::get("usvfs")->info("candidate for oldest thread: {0}",
                                         threadInfo.th32ThreadID);
              injectThread     = thread;
              injectThreadTime = creationTime;
            }
          }
          threadHandles.push_back(thread);
        }
      }
      moreThreads = Thread32Next(snapshot, &threadInfo);
    }
    if (injectThread != INVALID_HANDLE_VALUE) {
      spdlog::get("usvfs")->debug("going to inject dll");
      InjectDLLEIP(processHandle, injectThread, dllName, initFunction, userData,
                   userDataSize, skipInit);
    } else {
      spdlog::get("usvfs")->critical("found no thread to use for injecting");
    }

    for (HANDLE hdl : threadHandles) {
      spdlog::get("usvfs")->info("resuming thread {0}", ::GetThreadId(hdl));
      ResumeThread(hdl);
      CloseHandle(hdl);
    }
    CloseHandle(snapshot);
  } else {
    InjectDLLEIP(processHandle, threadHandle, dllName, initFunction, userData,
                 userDataSize, skipInit);
  }
}
