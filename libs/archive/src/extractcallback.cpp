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

#include "compat.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <stdexcept>
#include <string>

#include "archive.h"
#include "extractcallback.h"
#include "propertyvariant.h"

std::wstring operationResultToString(Int32 operationResult)
{
  namespace R = NArchive::NExtract::NOperationResult;

  switch (operationResult) {
  case R::kOK:
    return {};

  case R::kUnsupportedMethod:
    return L"Encoding method unsupported";

  case R::kDataError:
    return L"Data error";

  case R::kCRCError:
    return L"CRC error";

  case R::kUnavailable:
    return L"Unavailable";

  case R::kUnexpectedEnd:
    return L"Unexpected end of archive";

  case R::kDataAfterEnd:
    return L"Data after end of archive";

  case R::kIsNotArc:
    return L"Not an ARC";

  case R::kHeadersError:
    return L"Bad headers";

  case R::kWrongPassword:
    return L"Wrong password";

  default:
    return std::format(L"Unknown error {}", operationResult);
  }
}

CArchiveExtractCallback::CArchiveExtractCallback(
    Archive::ProgressCallback progressCallback,
    Archive::FileChangeCallback fileChangeCallback,
    Archive::ErrorCallback errorCallback, Archive::PasswordCallback passwordCallback,
    Archive::LogCallback logCallback, IInArchive* archiveHandler,
    std::wstring const& directoryPath, FileData* const* fileData, std::size_t nbFiles,
    UInt64 totalFileSize, std::wstring* password)
    : m_ArchiveHandler(archiveHandler), m_Total(0), m_DirectoryPath(),
      m_Extracting(false), m_Canceled(false), m_Timers{}, m_ProcessedFileInfo{},
      m_OutputFileStream{}, m_OutFileStreamCom{}, m_FileData(fileData),
      m_NbFiles(nbFiles), m_TotalFileSize(totalFileSize), m_LastCallbackFileSize(0),
      m_ExtractedFileSize(0), m_ProgressCallback(progressCallback),
      m_FileChangeCallback(fileChangeCallback), m_ErrorCallback(errorCallback),
      m_PasswordCallback(passwordCallback), m_LogCallback(logCallback),
      m_Password(password)
{
  m_DirectoryPath = IO::make_path(directoryPath);
}

CArchiveExtractCallback::~CArchiveExtractCallback()
{
#ifdef INSTRUMENT_ARCHIVE
  m_LogCallback(Archive::LogLevel::Debug, m_Timers.GetStream.toString(L"GetStream"));
  m_LogCallback(Archive::LogLevel::Debug, m_Timers.SetOperationResult.SetMTime.toString(
                                              L"SetOperationResult.SetMTime"));
  m_LogCallback(Archive::LogLevel::Debug, m_Timers.SetOperationResult.Close.toString(
                                              L"SetOperationResult.Close"));
  m_LogCallback(Archive::LogLevel::Debug, m_Timers.SetOperationResult.Release.toString(
                                              L"SetOperationResult.Release"));
  m_LogCallback(Archive::LogLevel::Debug,
                m_Timers.SetOperationResult.SetFileAttributesW.toString(
                    L"SetOperationResult.SetFileAttributesW"));
#endif
}

STDMETHODIMP CArchiveExtractCallback::SetTotal(UInt64 size) throw()
{
  m_Total = size;
  return S_OK;
}

STDMETHODIMP CArchiveExtractCallback::SetCompleted(const UInt64* completed) throw()
{
  if (m_ProgressCallback) {
    m_ProgressCallback(Archive::ProgressType::ARCHIVE, *completed, m_Total);
  }
  return m_Canceled ? E_ABORT : S_OK;
}

template <typename T>
bool CArchiveExtractCallback::getOptionalProperty(UInt32 index, int property,
                                                  T* result) const
{
  PropertyVariant prop;
  if (m_ArchiveHandler->GetProperty(index, property, &prop) != S_OK) {
    m_LogCallback(Archive::LogLevel::Error,
                  std::format(L"Error getting property {}.", property));
    return false;
  }
  if (prop.is_empty()) {
    return false;
  }
  *result = static_cast<T>(prop);
  return true;
}

template <typename T>
bool CArchiveExtractCallback::getProperty(UInt32 index, int property, T* result) const
{
  PropertyVariant prop;
  if (m_ArchiveHandler->GetProperty(index, property, &prop) != S_OK) {
    m_LogCallback(Archive::LogLevel::Error,
                  std::format(L"Error getting property {}.", property));
    return false;
  }

  *result = static_cast<T>(prop);
  return true;
}

STDMETHODIMP CArchiveExtractCallback::GetStream(UInt32 index,
                                                ISequentialOutStream** outStream,
                                                Int32 askExtractMode) throw()
{
  [[maybe_unused]] auto guard = m_Timers.GetStream.instrument();
  namespace fs = std::filesystem;

  *outStream = nullptr;
  m_OutFileStreamCom.Release();

  m_FullProcessedPaths.clear();
  m_Extracting = false;

  if (askExtractMode != NArchive::NExtract::NAskMode::kExtract) {
    return S_OK;
  }

  std::vector<std::wstring> filenames = m_FileData[index]->getOutputFilePaths();
  m_FileData[index]->clearOutputFilePaths();
  if (filenames.empty()) {
    return S_OK;
  }

#ifndef _WIN32
  // Archives from Windows contain backslash path separators which are valid
  // filename characters on Linux - convert them to forward slashes.
  for (auto& fn : filenames) {
    std::replace(fn.begin(), fn.end(), L'\\', L'/');
  }
#endif

  try {
    m_ProcessedFileInfo.AttribDefined =
        getOptionalProperty(index, kpidAttrib, &m_ProcessedFileInfo.Attrib);

    if (!getProperty(index, kpidIsDir, &m_ProcessedFileInfo.isDir)) {
      return E_ABORT;
    }

    // Why do we do this? And if we are doing this, shouldn't we copy the created
    // and accessed times (kpidATime, kpidCTime) as well?
    m_ProcessedFileInfo.MTimeDefined =
        getOptionalProperty(index, kpidMTime, &m_ProcessedFileInfo.MTime);

    if (m_ProcessedFileInfo.isDir) {
      for (auto const& filename : filenames) {
        auto fullpath = m_DirectoryPath / fs::path(filename).make_preferred();
        std::error_code ec;
        std::filesystem::create_directories(fullpath, ec);
        if (ec) {
          reportError(L"cannot created directory '{}': {}", fullpath, ec);
          return E_ABORT;
        }
        m_FullProcessedPaths.push_back(fullpath);
      }
    } else {
      for (auto const& filename : filenames) {
        auto fullProcessedPath = m_DirectoryPath / fs::path(filename).make_preferred();
        // If the filename contains a '/' we want to make the directory
        auto directoryPath = fullProcessedPath.parent_path();
        if (!fs::exists(directoryPath)) {
          // Make the containing directory
          std::error_code ec;
          std::filesystem::create_directories(directoryPath, ec);
          if (ec) {
            reportError(L"cannot created directory '{}': {}", directoryPath, ec);
            return E_ABORT;
          }
          // m_DirectoryPath.mkpath(filename.left(slashPos));
        }
        // If the file already exists, delete it
        if (fs::exists(fullProcessedPath)) {
          std::error_code ec;
          if (!fs::remove(fullProcessedPath, ec)) {
            reportError(L"cannot delete output file '{}': {}", fullProcessedPath, ec);
            return E_ABORT;
          }
        }
        m_FullProcessedPaths.push_back(fullProcessedPath);
      }

      m_OutputFileStream = new MultiOutputStream([this](UInt32 size, UInt64) {
        m_ExtractedFileSize += size;
        if (m_ProgressCallback) {
          m_ProgressCallback(Archive::ProgressType::EXTRACTION, m_ExtractedFileSize,
                             m_TotalFileSize);
        }
      });
      CComPtr<MultiOutputStream> outStreamCom(m_OutputFileStream);

      if (!m_OutputFileStream->Open(m_FullProcessedPaths)) {
        reportError(L"cannot open output file '{}': {}", m_FullProcessedPaths[0],
                    ::GetLastError());
        return E_ABORT;
      }

      UInt64 fileSize;
      auto fileSizeFound = getOptionalProperty(index, kpidSize, &fileSize);
      if (fileSizeFound && m_OutputFileStream->SetSize(fileSize) != S_OK) {
        m_LogCallback(Archive::LogLevel::Error,
                      std::format(L"SetSize() failed on {}.", m_FullProcessedPaths[0]));
      }

      // This is messy but I can't find another way of doing it. A simple
      // assignment of m_outFileStream to *outStream doesn't increase the
      // reference count.
      m_OutFileStreamCom = outStreamCom;
      *outStream         = outStreamCom.Detach();
    }

    if (m_FileChangeCallback) {
      m_FileChangeCallback(Archive::FileChangeType::EXTRACTION_START, filenames[0]);
    }

    return S_OK;
  } catch (std::exception const& e) {
    m_LogCallback(Archive::LogLevel::Error,
                  std::format(L"Caught exception {} in GetStream.", e));
  }
  return E_FAIL;
}

STDMETHODIMP CArchiveExtractCallback::PrepareOperation(Int32 askExtractMode) throw()
{
  if (m_Canceled) {
    return E_ABORT;
  }
  m_Extracting = askExtractMode == NArchive::NExtract::NAskMode::kExtract;
  return S_OK;
}

STDMETHODIMP CArchiveExtractCallback::SetOperationResult(Int32 operationResult) throw()
{
  if (operationResult != NArchive::NExtract::NOperationResult::kOK) {
    reportError(operationResultToString(operationResult));
  }

  if (m_OutFileStreamCom) {
    if (m_ProcessedFileInfo.MTimeDefined) {
      [[maybe_unused]] auto guard = m_Timers.SetOperationResult.SetMTime.instrument();
      m_OutputFileStream->SetMTime(&m_ProcessedFileInfo.MTime);
    }
    [[maybe_unused]] auto guard = m_Timers.SetOperationResult.Close.instrument();
    RINOK(m_OutputFileStream->Close())
  }

  {
    [[maybe_unused]] auto guard = m_Timers.SetOperationResult.Release.instrument();
    m_OutFileStreamCom.Release();
  }

  [[maybe_unused]] auto guard2 = m_Timers.SetOperationResult.SetFileAttributesW.instrument();
  if (m_Extracting && m_ProcessedFileInfo.AttribDefined) {
    // this is moderately annoying. I can't do this on the file handle because if
    // the file in question is a directory there isn't a file handle.
    // Also I'd like to convert the attributes to QT attributes but I'm not sure
    // if that's possible. Hence the conversions and strange string.
    for (auto& path : m_FullProcessedPaths) {
#ifdef _WIN32
      std::wstring const fn = L"\\\\?\\" + path.native();
      // If the attributes are POSIX-based, fix that
      if (m_ProcessedFileInfo.Attrib & 0xF0000000)
        m_ProcessedFileInfo.Attrib &= 0x7FFF;

      // Should probably log any errors here somehow
      ::SetFileAttributesW(fn.c_str(), m_ProcessedFileInfo.Attrib);
#else
      // On Linux, we could set file permissions based on the attributes,
      // but Windows file attributes don't map well to POSIX permissions.
      // For now, we only handle the read-only attribute.
      if (m_ProcessedFileInfo.Attrib & FILE_ATTRIBUTE_READONLY) {
        std::filesystem::permissions(path,
          std::filesystem::perms::owner_write,
          std::filesystem::perm_options::remove);
      }
#endif
    }
  }

  return S_OK;
}

STDMETHODIMP CArchiveExtractCallback::CryptoGetTextPassword(BSTR* passwordOut) throw()
{
  // if we've already got a password, don't ask again (and again...)
  if (m_Password->empty() && m_PasswordCallback) {
    *m_Password = m_PasswordCallback();
  }

  *passwordOut = ::SysAllocString(m_Password->c_str());
  return *passwordOut != 0 ? S_OK : E_OUTOFMEMORY;
}

void CArchiveExtractCallback::SetCanceled(bool aCanceled)
{
  m_Canceled = aCanceled;
}

void CArchiveExtractCallback::reportError(std::wstring const& message)
{
  if (m_ErrorCallback) {
    m_ErrorCallback(message);
  }
}
