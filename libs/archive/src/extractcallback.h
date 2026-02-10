/*
Mod Organizer archive handling

Copyright (C) 2012 Sebastian Herbord, 2020 MO2 Team. All rights reserved.

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

#ifndef EXTRACTCALLBACK_H
#define EXTRACTCALLBACK_H

#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>

#include <7zip/Archive/IArchive.h>
#include <7zip/IPassword.h>

#include "compat.h"

#include "archive.h"
#include "formatter.h"
#include "instrument.h"
#include "multioutputstream.h"
#include "unknown_impl.h"

class FileData;

class CArchiveExtractCallback : public IArchiveExtractCallback,
                                public ICryptoGetTextPassword
{

  // A note: It appears that the IArchiveExtractCallback interface includes the
  // IProgress interface, swo we need to respond to it
  UNKNOWN_3_INTERFACE(IArchiveExtractCallback, ICryptoGetTextPassword, IProgress);

public:
  CArchiveExtractCallback(Archive::ProgressCallback progressCallback,
                          Archive::FileChangeCallback fileChangeCallback,
                          Archive::ErrorCallback errorCallback,
                          Archive::PasswordCallback passwordCallback,
                          Archive::LogCallback logCallback, IInArchive* archiveHandler,
                          std::wstring const& directoryPath, FileData* const* fileData,
                          std::size_t nbFiles, UInt64 totalFileSize,
                          std::wstring* password);

  virtual ~CArchiveExtractCallback();

  void SetCanceled(bool aCanceled);

  Z7_IFACE_COM7_IMP(IProgress)
  Z7_IFACE_COM7_IMP(IArchiveExtractCallback)

  // ICryptoGetTextPassword
  STDMETHOD(CryptoGetTextPassword)(BSTR* aPassword) throw();

private:
  void reportError(const std::wstring& message);

  template <class... Args>
  void reportError(std::wformat_string<Args...> format, Args&&... args)
  {
    reportError(std::format(format, std::forward<Args>(args)...));
  }

  template <typename T>
  bool getOptionalProperty(UInt32 index, int property, T* result) const;
  template <typename T>
  bool getProperty(UInt32 index, int property, T* result) const;

private:
  CComPtr<IInArchive> m_ArchiveHandler;

  UInt64 m_Total;

  std::filesystem::path m_DirectoryPath;
  bool m_Extracting;
  std::atomic<bool> m_Canceled;

  struct
  {
    ArchiveTimers::Timer GetStream;
    struct
    {
      ArchiveTimers::Timer SetMTime;
      ArchiveTimers::Timer Close;
      ArchiveTimers::Timer Release;
      ArchiveTimers::Timer SetFileAttributesW;
    } SetOperationResult;
  } m_Timers;

  struct CProcessedFileInfo
  {
    FILETIME MTime;
    UInt32 Attrib;
    bool isDir;
    bool AttribDefined;
    bool MTimeDefined;
  } m_ProcessedFileInfo;

  MultiOutputStream* m_OutputFileStream;
  CComPtr<MultiOutputStream> m_OutFileStreamCom;

  std::vector<std::filesystem::path> m_FullProcessedPaths;

  FileData* const* m_FileData;
  std::size_t m_NbFiles;
  UInt64 m_TotalFileSize;
  UInt64 m_LastCallbackFileSize;
  UInt64 m_ExtractedFileSize;

  Archive::ProgressCallback m_ProgressCallback;
  Archive::FileChangeCallback m_FileChangeCallback;
  Archive::ErrorCallback m_ErrorCallback;
  Archive::PasswordCallback m_PasswordCallback;
  Archive::LogCallback m_LogCallback;
  std::wstring* m_Password;
};

#endif  // EXTRACTCALLBACK_H
