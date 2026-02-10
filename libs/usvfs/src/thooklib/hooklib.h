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

#include <map>
#include <windows_sane.h>

namespace HookLib
{

enum HookError
{
  ERR_NONE,
  ERR_INVALIDPARAMETERS,  // parameters are invalid
  ERR_FUNCEND,            // function is too short to be hooked
  ERR_JUMP,  // function consists only of an unconditional jump. Maybe it has already
             // been hooked?
  ERR_RIP,  // segment of the function to be overwritten contains a instruction-relative
            // operation
  ERR_RELJUMP  // segment of the function to be overwritten contains a relative jump we
               // can't relocated
};

typedef ULONG HOOKHANDLE;
static const HOOKHANDLE INVALID_HOOK = (HOOKHANDLE)-1;

///
/// \brief install a stub (function to be called before the target function)
/// \param functionAddress address of the function to stub
/// \param stubAddress address of the stub function. This function has to have the
/// signature of void foobar(LPVOID address).
///        address receives the address of the function.
/// \param error (optional) if set, the referenced variable will receive an error code
/// describing the problem (if any)
/// \return a handle to reference the hook in later operations or INVALID_HOOK on error
///
HOOKHANDLE InstallStub(LPVOID functionAddress, LPVOID stubAddress,
                       HookError* error = nullptr);

///
/// \brief install a stub (function to be called before the target function)
/// \param module the module containing the function to hook
/// \param functionName name of the function to stub (as exported by the library)
/// \param stubAddress address of the stub function. This function has to have the
/// signature of void foobar(LPVOID address).
///        address receives the address of the function.
/// \param error (optional) if set, the referenced variable will receive an error code
/// describing the problem (if any)
/// \return a handle to reference the hook in later operations or INVALID_HOOK on error
///
HOOKHANDLE InstallStub(HMODULE module, LPCSTR functionName, LPVOID stubAddress,
                       HookError* error = nullptr);

///
/// \brief install a hook (function replacing the existing functionality of the
/// function)
/// \param functionAddress address of the function to hook
/// \param hookAddress address of the replacement function. This function has to have
/// the exact same signature as the replaced function
/// \param error (optional) if set, the referenced variable will receive an error code
/// describing the problem (if any)
/// \return a handle to reference the hook in later operations or INVALID_HOOK on error
///
HOOKHANDLE InstallHook(LPVOID functionAddress, LPVOID hookAddress,
                       HookError* error = nullptr);

///
/// \brief install a hook (function replacing the existing functionality of the
/// function)
/// \param functionName name of the function to hook (as exported by the library)
/// \param hookAddress address of the replacement function. This function has to have
/// the exact same signature as the replaced function
/// \param error (optional) if set, the referenced variable will receive an error code
/// describing the problem (if any)
/// \return a handle to reference the hook in later operations or INVALID_HOOK on error
///
HOOKHANDLE InstallHook(HMODULE module, LPCSTR functionName, LPVOID hookAddress,
                       HookError* error = nullptr);

///
/// \brief remove a hook
/// \param handle handle returned in InstallStub or InstallHook
///
void RemoveHook(HOOKHANDLE handle);

///
/// \brief determine the type of a hook
/// \param handle the handle to look up
/// \return a string describing the used hooking mechanism
///
const char* GetHookType(HOOKHANDLE handle);

///
/// \brief retrieve the address that can be used to directly call a detour
/// \param handle handle for the hook
/// \return function address
///
LPVOID GetDetour(HOOKHANDLE handle);

///
/// \brief resolve an error code to a descriptive string
/// \param err the error code to resolve
/// \return the error string
///
const char* GetErrorString(HookError err);

}  // namespace HookLib
