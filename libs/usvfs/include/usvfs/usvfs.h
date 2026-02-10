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

#include "dllimport.h"
#include "usvfsparameters.h"

/*
 * Virtual operations:
 *   - link file
 *   - link directory (empty)
 *   - link directory (static)
 *   - link directory (dynamic)
 *   - delete file
 *   - delete directory
 * Maybe:
 *   - rename/move (= copy + delete)
 *   - copy-on-write semantics (changes to files are done in a separate copy of the
 * file, the original is kept on disc but hidden)
 */

static const unsigned int LINKFLAG_FAILIFEXISTS =
    0x00000001;  // if set, linking fails in case of an error
static const unsigned int LINKFLAG_MONITORCHANGES =
    0x00000002;  // if set, changes to the source directory after the link operation
                 // will be updated in the virtual fs. only relevant in static
                 // link directory operations
static const unsigned int LINKFLAG_CREATETARGET =
    0x00000004;  // if set, file creation (including move or copy) operations to
                 // destination will be redirected to the source. Only one createtarget
                 // can be set for a destination folder so this flag will replace
                 // the previous create target.
                 // If there different create-target have been set for an element and
                 // one of its ancestors, the inner-most create-target is used
static const unsigned int LINKFLAG_RECURSIVE =
    0x00000008;  // if set, directories are linked recursively
static const unsigned int LINKFLAG_FAILIFSKIPPED =
    0x00000010;  // if set, linking fails if the file or directory is skipped
                 // files or directories are skipped depending on whats been added to
                 // the skip file suffixes or skip directories list in
                 // the sharedparameters class, those lists are checked during virtual
                 // linking

extern "C"
{

  /**
   * removes all virtual mappings
   */
  DLLEXPORT void WINAPI usvfsClearVirtualMappings();

  /**
   * link a file virtually
   * @note: the directory the destination file resides in has to exist - at least
   * virtually.
   */
  DLLEXPORT BOOL WINAPI usvfsVirtualLinkFile(LPCWSTR source, LPCWSTR destination,
                                             unsigned int flags);

  /**
   * link a directory virtually. This static variant recursively links all files
   * individually, change notifications are used to update the information.
   * @param failIfExists if true, this call fails if the destination directory exists
   * (virtually or physically)
   */
  DLLEXPORT BOOL WINAPI usvfsVirtualLinkDirectoryStatic(LPCWSTR source,
                                                        LPCWSTR destination,
                                                        unsigned int flags);

  /**
   * connect to a virtual filesystem as a controller, without hooking the calling
   * process. Please note that you can only be connected to one vfs, so this will
   * silently disconnect from a previous vfs.
   */
  DLLEXPORT BOOL WINAPI usvfsConnectVFS(const usvfsParameters* p);

  /**
   * @brief create a new VFS. This is similar to ConnectVFS except it guarantees
   *   the vfs is reset before use.
   */
  DLLEXPORT BOOL WINAPI usvfsCreateVFS(const usvfsParameters* p);

  /**
   * disconnect from a virtual filesystem. This removes hooks if necessary
   */
  DLLEXPORT void WINAPI usvfsDisconnectVFS();

  DLLEXPORT void WINAPI usvfsGetCurrentVFSName(char* buffer, size_t size);

  /**
   * retrieve a list of all processes connected to the vfs
   */
  DLLEXPORT BOOL WINAPI usvfsGetVFSProcessList(size_t* count, LPDWORD processIDs);

  // retrieve a list of all processes connected to the vfs, stores an array
  // of `count` elements in `*buffer`
  //
  // if this returns TRUE and `count` is not 0, the caller must release the buffer
  // with `free(*buffer)`
  //
  // return values:
  //   - ERROR_INVALID_PARAMETERS:  either `count` or `buffer` is NULL
  //   - ERROR_TOO_MANY_OPEN_FILES: there seems to be way too many usvfs processes
  //                                running, probably some internal error
  //   - ERROR_NOT_ENOUGH_MEMORY:   malloc() failed
  //
  DLLEXPORT BOOL WINAPI usvfsGetVFSProcessList2(size_t* count, DWORD** buffer);

  /**
   * spawn a new process that can see the virtual file system. The signature is
   * identical to CreateProcess
   */
  DLLEXPORT BOOL WINAPI usvfsCreateProcessHooked(
      LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
      LPSECURITY_ATTRIBUTES lpProcessAttributes,
      LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles,
      DWORD dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
      LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation);

  /**
   * retrieve a single log message.
   * FIXME There is currently no way to unblock from the caller side
   * FIXME retrieves log messages from all instances, the logging queue is not separated
   */
  DLLEXPORT bool WINAPI usvfsGetLogMessages(LPSTR buffer, size_t size,
                                            bool blocking = false);

  /**
   * retrieves a readable representation of the vfs tree
   * @param buffer the buffer to write to. this may be null if you only want to
   * determine the required buffer size
   * @param size   pointer to a variable that contains the buffer. After the call
   *               this value will have been updated to contain the required size,
   *               even if this is bigger than the buffer size
   */
  DLLEXPORT BOOL WINAPI usvfsCreateVFSDump(LPSTR buffer, size_t* size);

  /**
   * adds an executable to the blacklist so it doesn't get exposed to the virtual
   * file system
   * @param executableName  name of the executable
   */
  DLLEXPORT VOID WINAPI usvfsBlacklistExecutable(LPCWSTR executableName);

  /**
   * clears the executable blacklist
   */
  DLLEXPORT VOID WINAPI usvfsClearExecutableBlacklist();

  /**
   * adds a file suffix to a list to skip during file linking
   * .txt and some_file.txt are both valid file suffixes,
   * not to be confused with file extensions
   * @param fileSuffix  a valid file suffix
   */
  DLLEXPORT VOID WINAPI usvfsAddSkipFileSuffix(LPCWSTR fileSuffix);

  /**
   * clears the file suffix skip-list
   */
  DLLEXPORT VOID WINAPI usvfsClearSkipFileSuffixes();

  /**
   * adds a directory name that will be skipped during directory linking.
   * Not a path. Any directory matching the name will be skipped,
   * regardless of it's path, for example if .git is added,
   * any sub-path or root-path containing a .git directory
   * will have the .git directory skipped during directory linking
   * @param directory  name of the directory
   */
  DLLEXPORT VOID WINAPI usvfsAddSkipDirectory(LPCWSTR directory);

  /**
   * clears the directory skip-list
   */
  DLLEXPORT VOID WINAPI usvfsClearSkipDirectories();

  /**
   * adds a library to be force loaded when the given process is injected
   * @param
   */
  DLLEXPORT VOID WINAPI usvfsForceLoadLibrary(LPCWSTR processName, LPCWSTR libraryPath);

  /**
   * clears all previous calls to ForceLoadLibrary
   */
  DLLEXPORT VOID WINAPI usvfsClearLibraryForceLoads();

  /**
   * print debugging info about the vfs. The format is currently not fixed and may
   * change between usvfs versions
   */
  DLLEXPORT VOID WINAPI usvfsPrintDebugInfo();

  // #if defined(UNITTEST) || defined(_WINDLL)
  DLLEXPORT void WINAPI usvfsInitLogging(bool toLocal = false);
  // #endif

  /**
   * used internally to initialize a process at startup-time as a "slave". Don't call
   * directly
   */
  DLLEXPORT void __cdecl InitHooks(LPVOID userData, size_t userDataSize);

  // the instance and shm names are not updated
  //
  DLLEXPORT void WINAPI usvfsUpdateParameters(usvfsParameters* p);

  DLLEXPORT int WINAPI usvfsCreateMiniDump(PEXCEPTION_POINTERS exceptionPtrs,
                                           CrashDumpsType type,
                                           const wchar_t* dumpPath);

  DLLEXPORT const char* WINAPI usvfsVersionString();
}
