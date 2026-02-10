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

#include "fileio.h"

#ifdef _WIN32

inline bool BOOLToBool(BOOL v)
{
  return (v != FALSE);
}

namespace IO
{

// FileBase

bool FileBase::Close() noexcept
{
  if (m_Handle == INVALID_HANDLE_VALUE)
    return true;
  if (!::CloseHandle(m_Handle))
    return false;
  m_Handle = INVALID_HANDLE_VALUE;
  return true;
}

bool FileBase::GetPosition(UInt64& position) noexcept
{
  return Seek(0, FILE_CURRENT, position);
}

bool FileBase::GetLength(UInt64& length) const noexcept
{
  DWORD sizeHigh;
  DWORD sizeLow = ::GetFileSize(m_Handle, &sizeHigh);
  if (sizeLow == 0xFFFFFFFF)
    if (::GetLastError() != NO_ERROR)
      return false;
  length = (((UInt64)sizeHigh) << 32) + sizeLow;
  return true;
}

bool FileBase::Seek(Int64 distanceToMove, DWORD moveMethod,
                    UInt64& newPosition) noexcept
{
  LONG high = (LONG)(distanceToMove >> 32);
  DWORD low = ::SetFilePointer(m_Handle, (LONG)(distanceToMove & 0xFFFFFFFF), &high,
                               moveMethod);
  if (low == 0xFFFFFFFF)
    if (::GetLastError() != NO_ERROR)
      return false;
  newPosition = (((UInt64)(UInt32)high) << 32) + low;
  return true;
}
bool FileBase::Seek(UInt64 position, UInt64& newPosition) noexcept
{
  return Seek(position, FILE_BEGIN, newPosition);
}

bool FileBase::SeekToBegin() noexcept
{
  UInt64 newPosition;
  return Seek(0, newPosition);
}

bool FileBase::SeekToEnd(UInt64& newPosition) noexcept
{
  return Seek(0, FILE_END, newPosition);
}

bool FileBase::Create(std::filesystem::path const& path, DWORD desiredAccess,
                      DWORD shareMode, DWORD creationDisposition,
                      DWORD flagsAndAttributes) noexcept
{
  if (!Close()) {
    return false;
  }

  m_Handle =
      ::CreateFileW(path.c_str(), desiredAccess, shareMode, (LPSECURITY_ATTRIBUTES)NULL,
                    creationDisposition, flagsAndAttributes, (HANDLE)NULL);

  return m_Handle != INVALID_HANDLE_VALUE;
}

bool FileBase::GetFileInformation(std::filesystem::path const& path,
                                  FileInfo* info) noexcept
{
  // Use FileBase to open/close the file:
  FileBase file;
  if (!file.Create(path, 0, FILE_SHARE_READ, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS))
    return false;

  BY_HANDLE_FILE_INFORMATION finfo;
  if (!BOOLToBool(GetFileInformationByHandle(file.m_Handle, &finfo))) {
    return false;
  }

  *info = FileInfo(path, finfo);
  return true;
}

// FileIn

bool FileIn::Open(std::filesystem::path const& filepath, DWORD shareMode,
                  DWORD creationDisposition, DWORD flagsAndAttributes) noexcept
{
  bool res = Create(filepath.c_str(), GENERIC_READ, shareMode, creationDisposition,
                    flagsAndAttributes);
  return res;
}
bool FileIn::OpenShared(std::filesystem::path const& filepath,
                        bool shareForWrite) noexcept
{
  return Open(filepath, FILE_SHARE_READ | (shareForWrite ? FILE_SHARE_WRITE : 0),
              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
}
bool FileIn::Open(std::filesystem::path const& filepath) noexcept
{
  return OpenShared(filepath, false);
}
bool FileIn::Read(void* data, UInt32 size, UInt32& processedSize) noexcept
{
  processedSize = 0;
  do {
    UInt32 processedLoc = 0;
    bool res            = ReadPart(data, size, processedLoc);
    processedSize += processedLoc;
    if (!res)
      return false;
    if (processedLoc == 0)
      return true;
    data = (void*)((unsigned char*)data + processedLoc);
    size -= processedLoc;
  } while (size > 0);
  return true;
}
bool FileIn::Read1(void* data, UInt32 size, UInt32& processedSize) noexcept
{
  DWORD processedLoc = 0;
  bool res      = BOOLToBool(::ReadFile(m_Handle, data, size, &processedLoc, NULL));
  processedSize = (UInt32)processedLoc;
  return res;
}
bool FileIn::ReadPart(void* data, UInt32 size, UInt32& processedSize) noexcept
{
  if (size > kChunkSizeMax)
    size = kChunkSizeMax;
  return Read1(data, size, processedSize);
}

// FileOut

bool FileOut::Open(std::filesystem::path const& fileName, DWORD shareMode,
                   DWORD creationDisposition, DWORD flagsAndAttributes) noexcept
{
  return Create(fileName, GENERIC_WRITE, shareMode, creationDisposition,
                flagsAndAttributes);
}

bool FileOut::Open(std::filesystem::path const& fileName) noexcept
{
  return Open(fileName, FILE_SHARE_READ, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
}

bool FileOut::SetTime(const FILETIME* cTime, const FILETIME* aTime,
                      const FILETIME* mTime) noexcept
{
  return BOOLToBool(::SetFileTime(m_Handle, cTime, aTime, mTime));
}
bool FileOut::SetMTime(const FILETIME* mTime) noexcept
{
  return SetTime(NULL, NULL, mTime);
}
bool FileOut::Write(const void* data, UInt32 size, UInt32& processedSize) noexcept
{
  processedSize = 0;
  do {
    UInt32 processedLoc = 0;
    bool res            = WritePart(data, size, processedLoc);
    processedSize += processedLoc;
    if (!res)
      return false;
    if (processedLoc == 0)
      return true;
    data = (const void*)((const unsigned char*)data + processedLoc);
    size -= processedLoc;
  } while (size > 0);
  return true;
}

bool FileOut::SetLength(UInt64 length) noexcept
{
  UInt64 newPosition;
  if (!Seek(length, newPosition))
    return false;
  if (newPosition != length)
    return false;
  return SetEndOfFile();
}
bool FileOut::SetEndOfFile() noexcept
{
  return BOOLToBool(::SetEndOfFile(m_Handle));
}

bool FileOut::WritePart(const void* data, UInt32 size, UInt32& processedSize) noexcept
{
  if (size > kChunkSizeMax)
    size = kChunkSizeMax;
  DWORD processedLoc = 0;
  bool res      = BOOLToBool(::WriteFile(m_Handle, data, size, &processedLoc, NULL));
  processedSize = (UInt32)processedLoc;
  return res;
}

}  // namespace IO

#else // Linux

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <cstring>
#include <cerrno>

namespace IO
{

// FileBase

bool FileBase::Close() noexcept
{
  if (m_Fd == -1)
    return true;
  if (::close(m_Fd) != 0)
    return false;
  m_Fd = -1;
  return true;
}

bool FileBase::GetPosition(UInt64& position) noexcept
{
  return Seek(0, SEEK_CUR, position);
}

bool FileBase::GetLength(UInt64& length) const noexcept
{
  struct stat st;
  if (fstat(m_Fd, &st) != 0)
    return false;
  length = (UInt64)st.st_size;
  return true;
}

bool FileBase::Seek(Int64 distanceToMove, int whence, UInt64& newPosition) noexcept
{
  off_t result = ::lseek(m_Fd, (off_t)distanceToMove, whence);
  if (result == (off_t)-1)
    return false;
  newPosition = (UInt64)result;
  return true;
}

bool FileBase::Seek(UInt64 position, UInt64& newPosition) noexcept
{
  return Seek((Int64)position, SEEK_SET, newPosition);
}

bool FileBase::SeekToBegin() noexcept
{
  UInt64 newPosition;
  return Seek(0, newPosition);
}

bool FileBase::SeekToEnd(UInt64& newPosition) noexcept
{
  return Seek(0, SEEK_END, newPosition);
}

bool FileBase::Create(std::filesystem::path const& path, int flags, int mode) noexcept
{
  if (!Close()) {
    return false;
  }

  m_Fd = ::open(path.c_str(), flags, mode);
  return m_Fd != -1;
}

bool FileBase::GetFileInformation(std::filesystem::path const& path,
                                  FileInfo* info) noexcept
{
  struct stat st;
  if (::stat(path.c_str(), &st) != 0) {
    return false;
  }

  *info = FileInfo(path, st);
  return true;
}

// FileIn

bool FileIn::Open(std::filesystem::path const& filepath) noexcept
{
  return Create(filepath, O_RDONLY);
}

bool FileIn::Read(void* data, UInt32 size, UInt32& processedSize) noexcept
{
  processedSize = 0;
  do {
    UInt32 processedLoc = 0;
    bool res            = ReadPart(data, size, processedLoc);
    processedSize += processedLoc;
    if (!res)
      return false;
    if (processedLoc == 0)
      return true;
    data = (void*)((unsigned char*)data + processedLoc);
    size -= processedLoc;
  } while (size > 0);
  return true;
}

bool FileIn::Read1(void* data, UInt32 size, UInt32& processedSize) noexcept
{
  ssize_t result = ::read(m_Fd, data, size);
  if (result < 0) {
    processedSize = 0;
    return false;
  }
  processedSize = (UInt32)result;
  return true;
}

bool FileIn::ReadPart(void* data, UInt32 size, UInt32& processedSize) noexcept
{
  if (size > kChunkSizeMax)
    size = kChunkSizeMax;
  return Read1(data, size, processedSize);
}

// FileOut

bool FileOut::Open(std::filesystem::path const& fileName) noexcept
{
  return Create(fileName, O_WRONLY | O_CREAT | O_TRUNC, 0644);
}

bool FileOut::SetTime(const FILETIME* /*cTime*/, const FILETIME* /*aTime*/,
                      const FILETIME* mTime) noexcept
{
  return SetMTime(mTime);
}

bool FileOut::SetMTime(const FILETIME* mTime) noexcept
{
  if (!mTime) return true;

  // Convert FILETIME to timespec
  // FILETIME is 100ns intervals since 1601-01-01
  // Unix time is seconds since 1970-01-01
  constexpr UInt64 EPOCH_DIFF = 116444736000000000ULL;
  UInt64 ticks = ((UInt64)mTime->dwHighDateTime << 32) | mTime->dwLowDateTime;
  if (ticks < EPOCH_DIFF) return false;
  ticks -= EPOCH_DIFF;

  struct timespec times[2];
  // atime - keep current
  times[0].tv_sec = 0;
  times[0].tv_nsec = UTIME_OMIT;
  // mtime
  times[1].tv_sec = (time_t)(ticks / 10000000ULL);
  times[1].tv_nsec = (long)((ticks % 10000000ULL) * 100);

  return futimens(m_Fd, times) == 0;
}

bool FileOut::Write(const void* data, UInt32 size, UInt32& processedSize) noexcept
{
  processedSize = 0;
  do {
    UInt32 processedLoc = 0;
    bool res            = WritePart(data, size, processedLoc);
    processedSize += processedLoc;
    if (!res)
      return false;
    if (processedLoc == 0)
      return true;
    data = (const void*)((const unsigned char*)data + processedLoc);
    size -= processedLoc;
  } while (size > 0);
  return true;
}

bool FileOut::SetLength(UInt64 length) noexcept
{
  UInt64 newPosition;
  if (!Seek(length, newPosition))
    return false;
  if (newPosition != length)
    return false;
  return SetEndOfFile();
}

bool FileOut::SetEndOfFile() noexcept
{
  UInt64 pos;
  if (!GetPosition(pos))
    return false;
  return ftruncate(m_Fd, (off_t)pos) == 0;
}

bool FileOut::WritePart(const void* data, UInt32 size, UInt32& processedSize) noexcept
{
  if (size > kChunkSizeMax)
    size = kChunkSizeMax;
  ssize_t result = ::write(m_Fd, data, size);
  if (result < 0) {
    processedSize = 0;
    return false;
  }
  processedSize = (UInt32)result;
  return true;
}

}  // namespace IO

#endif
