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

#ifndef ARCHIVE_FILEIO_H
#define ARCHIVE_FILEIO_H

// This code is adapted from 7z client code.

#ifdef _WIN32
#include <Windows.h>
#include "7zip//Archive/IArchive.h"
#else
#include <Common/MyWindows.h>
#include <7zip/Archive/IArchive.h>
#endif

#include <filesystem>
#include <string>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace IO
{

#ifdef _WIN32

/**
 * Small class that wraps windows BY_HANDLE_FILE_INFORMATION and returns
 * type matching 7z types.
 */
class FileInfo
{
public:
  FileInfo() : m_Valid{false} {};
  FileInfo(std::filesystem::path const& path, BY_HANDLE_FILE_INFORMATION fileInfo)
      : m_Valid{true}, m_Path(path), m_FileInfo{fileInfo}
  {}

  bool isValid() const { return m_Valid; }

  const std::filesystem::path& path() const { return m_Path; }

  UInt32 fileAttributes() const { return m_FileInfo.dwFileAttributes; }
  FILETIME creationTime() const { return m_FileInfo.ftCreationTime; }
  FILETIME lastAccessTime() const { return m_FileInfo.ftLastAccessTime; }
  FILETIME lastWriteTime() const { return m_FileInfo.ftLastWriteTime; }
  UInt32 volumeSerialNumber() const { return m_FileInfo.dwVolumeSerialNumber; }
  UInt64 fileSize() const
  {
    return ((UInt64)m_FileInfo.nFileSizeHigh) << 32 | m_FileInfo.nFileSizeLow;
  }
  UInt32 numberOfLinks() const { return m_FileInfo.nNumberOfLinks; }
  UInt64 fileInfex() const
  {
    return ((UInt64)m_FileInfo.nFileIndexHigh) << 32 | m_FileInfo.nFileIndexLow;
  }

  bool isArchived() const { return MatchesMask(FILE_ATTRIBUTE_ARCHIVE); }
  bool isCompressed() const { return MatchesMask(FILE_ATTRIBUTE_COMPRESSED); }
  bool isDir() const { return MatchesMask(FILE_ATTRIBUTE_DIRECTORY); }
  bool isEncrypted() const { return MatchesMask(FILE_ATTRIBUTE_ENCRYPTED); }
  bool isHidden() const { return MatchesMask(FILE_ATTRIBUTE_HIDDEN); }
  bool isNormal() const { return MatchesMask(FILE_ATTRIBUTE_NORMAL); }
  bool isOffline() const { return MatchesMask(FILE_ATTRIBUTE_OFFLINE); }
  bool isReadOnly() const { return MatchesMask(FILE_ATTRIBUTE_READONLY); }
  bool iasReparsePoint() const { return MatchesMask(FILE_ATTRIBUTE_REPARSE_POINT); }
  bool isSparse() const { return MatchesMask(FILE_ATTRIBUTE_SPARSE_FILE); }
  bool isSystem() const { return MatchesMask(FILE_ATTRIBUTE_SYSTEM); }
  bool isTemporary() const { return MatchesMask(FILE_ATTRIBUTE_TEMPORARY); }

private:
  bool MatchesMask(UINT32 mask) const
  {
    return ((m_FileInfo.dwFileAttributes & mask) != 0);
  }

  bool m_Valid;
  std::filesystem::path m_Path;
  BY_HANDLE_FILE_INFORMATION m_FileInfo;
};

#else // Linux

/**
 * FileInfo class for Linux - uses stat() to get file information.
 * Returns 7zip-compatible types.
 */
class FileInfo
{
public:
  FileInfo() : m_Valid{false}, m_Stat{} {};
  FileInfo(std::filesystem::path const& path, struct stat const& st)
      : m_Valid{true}, m_Path(path), m_Stat{st}
  {}

  bool isValid() const { return m_Valid; }

  const std::filesystem::path& path() const { return m_Path; }

  UInt32 fileAttributes() const {
    UInt32 attr = 0;
    if (S_ISDIR(m_Stat.st_mode)) attr |= FILE_ATTRIBUTE_DIRECTORY;
    if (!(m_Stat.st_mode & S_IWUSR)) attr |= FILE_ATTRIBUTE_READONLY;
    if (attr == 0) attr = FILE_ATTRIBUTE_NORMAL;
    return attr;
  }

  // Convert timespec to FILETIME (100ns intervals since 1601-01-01)
  static FILETIME timespecToFiletime(struct timespec const& ts) {
    // Offset between 1601-01-01 and 1970-01-01 in 100ns intervals
    constexpr UInt64 EPOCH_DIFF = 116444736000000000ULL;
    UInt64 ticks = (UInt64)ts.tv_sec * 10000000ULL + (UInt64)ts.tv_nsec / 100ULL + EPOCH_DIFF;
    FILETIME ft;
    ft.dwLowDateTime = (DWORD)(ticks & 0xFFFFFFFF);
    ft.dwHighDateTime = (DWORD)(ticks >> 32);
    return ft;
  }

  FILETIME creationTime() const {
    // Linux doesn't have creation time in all filesystems, use ctime (status change)
    return timespecToFiletime(m_Stat.st_ctim);
  }
  FILETIME lastAccessTime() const {
    return timespecToFiletime(m_Stat.st_atim);
  }
  FILETIME lastWriteTime() const {
    return timespecToFiletime(m_Stat.st_mtim);
  }
  UInt32 volumeSerialNumber() const { return (UInt32)m_Stat.st_dev; }
  UInt64 fileSize() const { return (UInt64)m_Stat.st_size; }
  UInt32 numberOfLinks() const { return (UInt32)m_Stat.st_nlink; }
  UInt64 fileInfex() const { return (UInt64)m_Stat.st_ino; }

  bool isArchived() const { return false; }
  bool isCompressed() const { return false; }
  bool isDir() const { return S_ISDIR(m_Stat.st_mode); }
  bool isEncrypted() const { return false; }
  bool isHidden() const { return false; }
  bool isNormal() const { return S_ISREG(m_Stat.st_mode); }
  bool isOffline() const { return false; }
  bool isReadOnly() const { return !(m_Stat.st_mode & S_IWUSR); }
  bool iasReparsePoint() const { return S_ISLNK(m_Stat.st_mode); }
  bool isSparse() const { return false; }
  bool isSystem() const { return false; }
  bool isTemporary() const { return false; }

private:
  bool m_Valid;
  std::filesystem::path m_Path;
  struct stat m_Stat;
};

#endif // _WIN32

class FileBase
{
public:  // Constructors, destructor, assignment.
#ifdef _WIN32
  FileBase() noexcept : m_Handle{INVALID_HANDLE_VALUE} {}

  FileBase(FileBase&& other) noexcept : m_Handle{other.m_Handle}
  {
    other.m_Handle = INVALID_HANDLE_VALUE;
  }
#else
  FileBase() noexcept : m_Fd{-1} {}

  FileBase(FileBase&& other) noexcept : m_Fd{other.m_Fd}
  {
    other.m_Fd = -1;
  }
#endif

  ~FileBase() noexcept { Close(); }

  FileBase(FileBase const&)            = delete;
  FileBase& operator=(FileBase const&) = delete;
  FileBase& operator=(FileBase&&)      = delete;

public:  // Operations
  bool Close() noexcept;

  bool GetPosition(UInt64& position) noexcept;
  bool GetLength(UInt64& length) const noexcept;

#ifdef _WIN32
  bool Seek(Int64 distanceToMove, DWORD moveMethod, UInt64& newPosition) noexcept;
#else
  bool Seek(Int64 distanceToMove, int whence, UInt64& newPosition) noexcept;
#endif
  bool Seek(UInt64 position, UInt64& newPosition) noexcept;
  bool SeekToBegin() noexcept;
  bool SeekToEnd(UInt64& newPosition) noexcept;

  // Note: Only the static version (unlike in 7z) because I want FileInfo to hold the
  // path to the file, and the non-static version is never used (except by the static
  // version).
  static bool GetFileInformation(std::filesystem::path const& path,
                                 FileInfo* info) noexcept;

protected:
#ifdef _WIN32
  bool Create(std::filesystem::path const& path, DWORD desiredAccess, DWORD shareMode,
              DWORD creationDisposition, DWORD flagsAndAttributes) noexcept;
#else
  bool Create(std::filesystem::path const& path, int flags, int mode = 0644) noexcept;
#endif

protected:
  static constexpr UInt32 kChunkSizeMax = (1 << 22);

#ifdef _WIN32
  HANDLE m_Handle;
#else
  int m_Fd;
#endif
};

class FileIn : public FileBase
{
public:
  using FileBase::FileBase;

public:  // Operations
#ifdef _WIN32
  bool Open(std::filesystem::path const& filepath, DWORD shareMode,
            DWORD creationDisposition, DWORD flagsAndAttributes) noexcept;
  bool OpenShared(std::filesystem::path const& filepath, bool shareForWrite) noexcept;
#endif
  bool Open(std::filesystem::path const& filepath) noexcept;

  bool Read(void* data, UInt32 size, UInt32& processedSize) noexcept;

protected:
  bool Read1(void* data, UInt32 size, UInt32& processedSize) noexcept;
  bool ReadPart(void* data, UInt32 size, UInt32& processedSize) noexcept;
};

class FileOut : public FileBase
{
public:
  using FileBase::FileBase;

public:  // Operations:
#ifdef _WIN32
  bool Open(std::filesystem::path const& fileName, DWORD shareMode,
            DWORD creationDisposition, DWORD flagsAndAttributes) noexcept;
#endif
  bool Open(std::filesystem::path const& fileName) noexcept;

  bool SetTime(const FILETIME* cTime, const FILETIME* aTime,
               const FILETIME* mTime) noexcept;
  bool SetMTime(const FILETIME* mTime) noexcept;
  bool Write(const void* data, UInt32 size, UInt32& processedSize) noexcept;

  bool SetLength(UInt64 length) noexcept;
  bool SetEndOfFile() noexcept;

protected:  // Protected Operations:
  bool WritePart(const void* data, UInt32 size, UInt32& processedSize) noexcept;
};

/**
 * @brief Convert the given wide-string to a path object.
 *
 * On Windows: adds the long-path prefix if not present.
 * On Linux: simply converts wstring to path (no long-path prefix needed).
 *
 * @param path The string containing the path.
 *
 * @return the created path.
 */
inline std::filesystem::path make_path(std::wstring const& pathstr)
{
  namespace fs = std::filesystem;

#ifdef _WIN32
  constexpr const wchar_t* lprefix     = L"\\\\?\\";
  constexpr const wchar_t* unc_prefix  = L"\\\\";
  constexpr const wchar_t* unc_lprefix = L"\\\\?\\UNC\\";

  // If path is already a long path, just return it:
  if (pathstr.starts_with(lprefix)) {
    return fs::path{pathstr}.make_preferred();
  }

  fs::path path{pathstr};

  // Convert to an absolute path:
  if (!path.is_absolute()) {
    path = fs::absolute(path);
  }

  // backslashes
  path = path.make_preferred();

  // Get rid of duplicate separators and relative moves
  path = path.lexically_normal();

  const std::wstring pathstr_fixed = path.native();

  // If this is a UNC, the prefix is different
  if (pathstr_fixed.starts_with(unc_prefix)) {
    return fs::path{unc_lprefix + pathstr_fixed.substr(2)};
  }

  // Add the long-path prefix
  return fs::path{lprefix + pathstr_fixed};
#else
  // On Linux, simply convert wstring to path (no long-path prefix needed)
  fs::path path{pathstr};

  if (!path.is_absolute()) {
    path = fs::absolute(path);
  }

  path = path.lexically_normal();
  return path;
#endif
}

}  // namespace IO

#endif
