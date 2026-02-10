/*
Mod Organizer archive handling

Copyright (C) 2020 MO2 Team. All rights reserved.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 3 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef ARCHIVE_LIBRARY_H
#define ARCHIVE_LIBRARY_H

#ifdef _WIN32
#include <Windows.h>
#else
#include <Common/MyWindows.h>
#include <dlfcn.h>
#include <string>
#include <unistd.h>
#endif

/**
 * Very small wrapper around shared libraries (DLL on Windows, .so on Linux).
 */
class ALibrary
{
public:
#ifdef _WIN32
  ALibrary(const char* path) : m_Module{nullptr}, m_LastError{ERROR_SUCCESS}
  {
    m_Module = LoadLibraryA(path);
    if (m_Module == nullptr) {
      updateLastError();
    }
  }

  ~ALibrary()
  {
    if (m_Module) {
      FreeLibrary(m_Module);
    }
  }

  template <class T>
  T resolve(const char* procName)
  {
    if (!m_Module) {
      return nullptr;
    }
    auto proc = GetProcAddress(m_Module, procName);
    if (!proc) {
      updateLastError();
      return nullptr;
    }
    return reinterpret_cast<T>(proc);
  }

  DWORD getLastError() const { return m_LastError; }
  bool isOpen() const { return m_Module != nullptr; }
  operator bool() const { return isOpen(); }

private:
  void updateLastError() { m_LastError = ::GetLastError(); }

  HMODULE m_Module;
  DWORD m_LastError;

#else // Linux

  ALibrary(const char* path) : m_Handle{nullptr}, m_LastError{0}
  {
    // Find the directory containing our own executable
    std::string exeDir;
    char selfPath[4096];
    ssize_t len = readlink("/proc/self/exe", selfPath, sizeof(selfPath) - 1);
    if (len > 0) {
      selfPath[len] = '\0';
      exeDir = std::string(selfPath);
      auto slash = exeDir.rfind('/');
      if (slash != std::string::npos)
        exeDir = exeDir.substr(0, slash);
    }

    // Try bundled 7z.so locations first (same dir as executable, then dlls/ subdir)
    std::string tryPaths[] = {
      exeDir + "/7z.so",
      exeDir + "/dlls/7z.so",
      "7z.so",
      "dlls/7z.so",
      "/usr/lib/p7zip/7z.so",
      "/usr/lib64/p7zip/7z.so",
      "/usr/libexec/p7zip/7z.so",
    };

    for (const auto& tryPath : tryPaths) {
      m_Handle = dlopen(tryPath.c_str(), RTLD_LAZY);
      if (m_Handle) break;
    }

    if (!m_Handle) {
      // Last resort: try the original path as-is
      m_Handle = dlopen(path, RTLD_LAZY);
    }

    if (!m_Handle) {
      m_LastError = 1;
    }
  }

  ~ALibrary()
  {
    if (m_Handle) {
      dlclose(m_Handle);
    }
  }

  template <class T>
  T resolve(const char* procName)
  {
    if (!m_Handle) {
      return nullptr;
    }
    void* sym = dlsym(m_Handle, procName);
    if (!sym) {
      m_LastError = 1;
      return nullptr;
    }
    return reinterpret_cast<T>(sym);
  }

  DWORD getLastError() const { return m_LastError; }
  bool isOpen() const { return m_Handle != nullptr; }
  operator bool() const { return isOpen(); }

private:
  void* m_Handle;
  DWORD m_LastError;

#endif
};

#endif
