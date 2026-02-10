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

#include "archive.h"
#include "compat.h"

#include "extractcallback.h"
#include "inputstream.h"
#include "library.h"
#include "opencallback.h"
#include "propertyvariant.h"

#include <algorithm>
#include <map>
#include <sstream>
#include <stddef.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace PropID = NArchive::NHandlerPropID;

class FileDataImpl : public FileData
{
  friend class Archive;

public:
  FileDataImpl(std::wstring const& fileName, UInt64 size, UInt64 crc, bool isDirectory)
      : m_FileName(fileName), m_Size(size), m_CRC(crc), m_IsDirectory(isDirectory)
  {}

  virtual std::wstring getArchiveFilePath() const override { return m_FileName; }
  virtual uint64_t getSize() const override { return m_Size; }

  virtual void addOutputFilePath(std::wstring const& fileName) override
  {
    m_OutputFilePaths.push_back(fileName);
  }
  virtual const std::vector<std::wstring>& getOutputFilePaths() const override
  {
    return m_OutputFilePaths;
  }

  virtual void clearOutputFilePaths() override { m_OutputFilePaths.clear(); }

  bool isEmpty() const { return m_OutputFilePaths.empty(); }
  virtual bool isDirectory() const override { return m_IsDirectory; }
  virtual uint64_t getCRC() const override { return m_CRC; }

private:
  std::wstring m_FileName;
  UInt64 m_Size;
  UInt64 m_CRC;
  std::vector<std::wstring> m_OutputFilePaths;
  bool m_IsDirectory;
};

/// represents the connection to one archive and provides common functionality
class ArchiveImpl : public Archive
{

  // Callback that does nothing but avoid having to check if the callback is present
  // everytime.
  static LogCallback DefaultLogCallback;

public:
  ArchiveImpl();
  virtual ~ArchiveImpl();

  virtual bool isValid() const { return m_Valid; }

  virtual Error getLastError() const { return m_LastError; }
  virtual void setLogCallback(LogCallback logCallback) override
  {
    // Wrap the callback so that we do not have to check if it is set everywhere:
    m_LogCallback = logCallback ? logCallback : DefaultLogCallback;
  }

  virtual bool open(std::wstring const& archiveName,
                    PasswordCallback passwordCallback) override;
  virtual void close() override;
  const std::vector<FileData*>& getFileList() const override { return m_FileList; }
  virtual bool extract(std::wstring const& outputDirectory,
                       ProgressCallback progressCallback,
                       FileChangeCallback fileChangeCallback,
                       ErrorCallback errorCallback) override;

  virtual void cancel() override;

private:
  void clearFileList();
  void resetFileList();

  HRESULT loadFormats();

private:
  typedef UINT32(WINAPI* CreateObjectFunc)(const GUID* clsID, const GUID* interfaceID,
                                           void** outObject);
  CreateObjectFunc m_CreateObjectFunc;

  // A note: In 7zip source code this not is what this typedef is called, the old
  // GetHandlerPropertyFunc appears to be deprecated.
  typedef UInt32(WINAPI* GetPropertyFunc)(UInt32 index, PROPID propID,
                                          PROPVARIANT* value);
  GetPropertyFunc m_GetHandlerPropertyFunc;

  template <typename T>
  T readHandlerProperty(UInt32 index, PROPID propID) const;

  template <typename T>
  T readProperty(UInt32 index, PROPID propID) const;

  bool m_Valid;
  Error m_LastError;

  ALibrary m_Library;
  std::wstring m_ArchiveName;  // TBH I don't think this is required
  CComPtr<IInArchive> m_ArchivePtr;
  CArchiveExtractCallback* m_ExtractCallback;

  LogCallback m_LogCallback;
  PasswordCallback m_PasswordCallback;

  std::vector<FileData*> m_FileList;

  std::wstring m_Password;

  struct ArchiveFormatInfo
  {
    CLSID m_ClassID;
    std::wstring m_Name;
    std::vector<std::string> m_Signatures;
    std::wstring m_Extensions;
    std::wstring m_AdditionalExtensions;
    UInt32 m_SignatureOffset;
  };

  typedef std::vector<ArchiveFormatInfo> Formats;
  Formats m_Formats;

  typedef std::unordered_map<std::wstring, Formats> FormatMap;
  FormatMap m_FormatMap;

  // I don't think one signature could possibly describe two formats.
  typedef std::map<std::string, ArchiveFormatInfo> SignatureMap;
  SignatureMap m_SignatureMap;

  std::size_t m_MaxSignatureLen = 0;
};

Archive::LogCallback ArchiveImpl::DefaultLogCallback([](LogLevel, std::wstring const&) {
});

template <typename T>
T ArchiveImpl::readHandlerProperty(UInt32 index, PROPID propID) const
{
  PropertyVariant prop;
  if (m_GetHandlerPropertyFunc(index, propID, &prop) != S_OK) {
    throw std::runtime_error("Failed to read property");
  }
  return static_cast<T>(prop);
}

template <typename T>
T ArchiveImpl::readProperty(UInt32 index, PROPID propID) const
{
  PropertyVariant prop;
  if (m_ArchivePtr->GetProperty(index, propID, &prop) != S_OK) {
    throw std::runtime_error("Failed to read property");
  }
  return static_cast<T>(prop);
}

// Seriously, there is one format returned in the list that has no registered
// extension and no signature. WTF?
HRESULT ArchiveImpl::loadFormats()
{
  typedef UInt32(WINAPI * GetNumberOfFormatsFunc)(UInt32 * numFormats);
  GetNumberOfFormatsFunc getNumberOfFormats =
      m_Library.resolve<GetNumberOfFormatsFunc>("GetNumberOfFormats");
  if (getNumberOfFormats == nullptr) {
    return E_FAIL;
  }

  UInt32 numFormats;
  RINOK(getNumberOfFormats(&numFormats));

  for (UInt32 i = 0; i < numFormats; ++i) {
    ArchiveFormatInfo item;

    item.m_Name = readHandlerProperty<std::wstring>(i, PropID::kName);

    item.m_ClassID = readHandlerProperty<GUID>(i, PropID::kClassID);

    // Should split up the extensions and map extension to type, and see what we get
    // from that for preference then try all extensions anyway...
    item.m_Extensions = readHandlerProperty<std::wstring>(i, PropID::kExtension);

    // This is unnecessary currently for our purposes. Basically, for each
    // extension, there's an 'addext' which, if set (to other than *) means that
    // theres a double encoding going on. For instance, the bzip format is like this
    // addext = "* * .tar .tar"
    // ext    = "bz2 bzip2 tbz2 tbz"
    // which means that tbz2 and tbz should uncompress to a tar file which can be
    // further processed as if it were a tar file. Having said which, we don't
    // need to support this at all, so I'm storing it but ignoring it.
    item.m_AdditionalExtensions =
        readHandlerProperty<std::wstring>(i, PropID::kAddExtension);

    std::string signature = readHandlerProperty<std::string>(i, PropID::kSignature);
    if (!signature.empty()) {
      item.m_Signatures.push_back(signature);
      if (m_MaxSignatureLen < signature.size()) {
        m_MaxSignatureLen = signature.size();
      }
      m_SignatureMap[signature] = item;
    }

    std::string multiSig = readHandlerProperty<std::string>(i, PropID::kMultiSignature);
    const char* multiSigBytes = multiSig.c_str();
    std::size_t size          = multiSig.length();
    while (size > 0) {
      unsigned len = *multiSigBytes++;
      size--;
      if (len > size)
        break;
      std::string sig(multiSigBytes, multiSigBytes + len);
      multiSigBytes = multiSigBytes + len;
      size -= len;
      item.m_Signatures.push_back(sig);
      if (m_MaxSignatureLen < sig.size()) {
        m_MaxSignatureLen = sig.size();
      }
      m_SignatureMap[sig] = item;
    }

    UInt32 offset          = readHandlerProperty<UInt32>(i, PropID::kSignatureOffset);
    item.m_SignatureOffset = offset;

    // Now split the extension up from the space separated string and create
    // a map from each extension to the possible formats
    // We could make these pointers but it's not a massive overhead and nobody
    // should be changing this
    std::wistringstream s(item.m_Extensions);
    std::wstring t;
    while (s >> t) {
      m_FormatMap[t].push_back(item);
    }
    m_Formats.push_back(item);
  }
  return S_OK;
}

ArchiveImpl::ArchiveImpl()
    : m_Valid(false), m_LastError(Error::ERROR_NONE), m_Library("dlls/7zip.dll"),
      m_PasswordCallback{}
{
  // Reset the log callback:
  setLogCallback({});

  if (!m_Library) {
    m_LastError = Error::ERROR_LIBRARY_NOT_FOUND;
    return;
  }

  m_CreateObjectFunc = m_Library.resolve<CreateObjectFunc>("CreateObject");
  if (m_CreateObjectFunc == nullptr) {
    m_LastError = Error::ERROR_LIBRARY_INVALID;
    return;
  }

  m_GetHandlerPropertyFunc = m_Library.resolve<GetPropertyFunc>("GetHandlerProperty2");
  if (m_GetHandlerPropertyFunc == nullptr) {
    m_LastError = Error::ERROR_LIBRARY_INVALID;
    return;
  }

  try {
    if (loadFormats() != S_OK) {
      m_LastError = Error::ERROR_LIBRARY_INVALID;
      return;
    }

    m_Valid = true;
    return;
  } catch (std::exception const& e) {
    m_LogCallback(LogLevel::Error, std::format(L"Caught exception {}.", e));
    m_LastError = Error::ERROR_LIBRARY_INVALID;
  }
}

ArchiveImpl::~ArchiveImpl()
{
  close();
}

bool ArchiveImpl::open(std::wstring const& archiveName,
                       PasswordCallback passwordCallback)
{
  m_ArchiveName = archiveName;  // Just for debugging, not actually used...

  Formats formatList = m_Formats;

  // Convert to long path if it's not already:
  std::filesystem::path filepath = IO::make_path(archiveName);

  // If it doesn't exist or is a directory, error
  if (!exists(filepath) || is_directory(filepath)) {
    m_LastError = Error::ERROR_ARCHIVE_NOT_FOUND;
    return false;
  }

  // in rars the password seems to be requested during extraction, not on open, so we
  // need to hold on to the callback for now
  m_PasswordCallback = passwordCallback;

  CComPtr<InputStream> file(new InputStream);

  if (!file->Open(filepath)) {
    m_LastError = Error::ERROR_FAILED_TO_OPEN_ARCHIVE;
    return false;
  }

  CComPtr<CArchiveOpenCallback> openCallbackPtr;
  try {
    openCallbackPtr =
        new CArchiveOpenCallback(passwordCallback, m_LogCallback, filepath);
  } catch (std::runtime_error const&) {
    m_LastError = Error::ERROR_FAILED_TO_OPEN_ARCHIVE;
    return false;
  }

  // Try to open the archive

  bool sigMismatch = false;

  {
    // Get the first iterator that is strictly > the signature we're looking for.
    for (auto signatureInfo : m_SignatureMap) {
      // Read the signature of the file and look that up.
      std::vector<char> buff;
      buff.reserve(m_MaxSignatureLen);
      UInt32 act;
      file->Seek(0, STREAM_SEEK_SET, nullptr);
      file->Read(buff.data(), static_cast<UInt32>(m_MaxSignatureLen), &act);
      file->Seek(0, STREAM_SEEK_SET, nullptr);
      std::string signature = std::string(buff.data(), act);
      if (signatureInfo.first == std::string(buff.data(), signatureInfo.first.size())) {
        if (m_CreateObjectFunc(&signatureInfo.second.m_ClassID, &IID_IInArchive,
                               (void**)&m_ArchivePtr) != S_OK) {
          m_LastError = Error::ERROR_LIBRARY_ERROR;
          return false;
        }

        if (m_ArchivePtr->Open(file, 0, openCallbackPtr) != S_OK) {
          m_LogCallback(LogLevel::Debug,
                        std::format(L"Failed to open {} using {} (from signature).",
                                    archiveName, signatureInfo.second.m_Name));
          m_ArchivePtr.Release();
        } else {
          m_LogCallback(LogLevel::Debug,
                        std::format(L"Opened {} using {} (from signature).",
                                    archiveName, signatureInfo.second.m_Name));

          // Retrieve the extension (warning: .extension() contains the dot):
          std::wstring ext =
              ArchiveStrings::towlower(filepath.extension().wstring().substr(1));
          std::wistringstream s(signatureInfo.second.m_Extensions);
          std::wstring t;
          bool found = false;
          while (s >> t) {
            if (t == ext) {
              found = true;
              break;
            }
          }
          if (!found) {
            m_LogCallback(LogLevel::Warning,
                          L"The extension of this file did not match the expected "
                          L"extensions for this format.");
            sigMismatch = true;
          }
        }
        // Arguably we should give up here if it's not OK if 7zip can't even start
        // to decode even though we've found the format from the signature.
        // Sadly, the 7zip API documentation is pretty well non-existant.
        break;
      }
      std::vector<ArchiveFormatInfo>::iterator iter = std::find_if(
          formatList.begin(), formatList.end(), [=](ArchiveFormatInfo a) -> bool {
            return a.m_Name == signatureInfo.second.m_Name;
          });
      if (iter != formatList.end())
        formatList.erase(iter);
    }
  }

  {
    // determine archive type based on extension
    Formats const* formats = nullptr;
    std::wstring ext =
        ArchiveStrings::towlower(filepath.extension().wstring().substr(1));
    FormatMap::const_iterator map_iter = m_FormatMap.find(ext);
    if (map_iter != m_FormatMap.end()) {
      formats = &map_iter->second;
      if (formats != nullptr) {
        if (m_ArchivePtr == nullptr) {
          // OK, we have some potential formats. If there is only one, try it now. If
          // there are multiple formats, we'll try by signature lookup first.
          for (ArchiveFormatInfo format : *formats) {
            if (m_CreateObjectFunc(&format.m_ClassID, &IID_IInArchive,
                                   (void**)&m_ArchivePtr) != S_OK) {
              m_LastError = Error::ERROR_LIBRARY_ERROR;
              return false;
            }

            if (m_ArchivePtr->Open(file, 0, openCallbackPtr) != S_OK) {
              m_LogCallback(LogLevel::Debug,
                            std::format(L"Failed to open {} using {} (from signature).",
                                        archiveName, format.m_Name));
              m_ArchivePtr.Release();
            } else {
              m_LogCallback(LogLevel::Debug,
                            std::format(L"Opened {} using {} (from signature).",
                                        archiveName, format.m_Name));
              break;
            }

            std::vector<ArchiveFormatInfo>::iterator iter = std::find_if(
                formatList.begin(), formatList.end(), [=](ArchiveFormatInfo a) -> bool {
                  return a.m_Name == format.m_Name;
                });
            if (iter != formatList.end())
              formatList.erase(iter);
          }
        } else if (sigMismatch) {
          std::vector<std::wstring> vformats;
          for (ArchiveFormatInfo format : *formats) {
            vformats.push_back(format.m_Name);
          }
          m_LogCallback(
              LogLevel::Warning,
              std::format(L"The format(s) expected for this extension are: {}.",
                          ArchiveStrings::join(vformats, L", ")));
        }
      }
    }
  }

  if (m_ArchivePtr == nullptr) {
    m_LogCallback(LogLevel::Warning, L"Trying to open an archive but could not "
                                     L"recognize the extension or signature.");
    m_LogCallback(
        LogLevel::Debug,
        L"Attempting to open the file with the remaining formats as a fallback...");
    for (auto format : formatList) {
      if (m_CreateObjectFunc(&format.m_ClassID, &IID_IInArchive,
                             (void**)&m_ArchivePtr) != S_OK) {
        m_LastError = Error::ERROR_LIBRARY_ERROR;
        return false;
      }
      if (m_ArchivePtr->Open(file, 0, openCallbackPtr) == S_OK) {
        m_LogCallback(LogLevel::Debug,
                      std::format(L"Opened {} using {} (from signature).", archiveName,
                                  format.m_Name));
        m_LogCallback(LogLevel::Warning,
                      L"This archive likely has an incorrect extension.");
        break;
      } else
        m_ArchivePtr.Release();
    }
  }

  if (m_ArchivePtr == nullptr) {
    m_LastError = Error::ERROR_INVALID_ARCHIVE_FORMAT;
    return false;
  }

  m_Password = openCallbackPtr->GetPassword();
  /*
    UInt32 subFile = ULONG_MAX;
    {
      NCOM::CPropVariant prop;
      if (m_ArchivePtr->GetArchiveProperty(kpidMainSubfile, &prop) != S_OK) {
        throw std::runtime_error("failed to get property kpidMainSubfile");
      }

      if (prop.vt == VT_UI4) {
        subFile = prop.ulVal;
      }
    }

    if (subFile != ULONG_MAX) {
      std::wstring subPath = GetArchiveItemPath(m_ArchivePtr, subFile);

      CMyComPtr<IArchiveOpenSetSubArchiveName> setSubArchiveName;
      openCallbackPtr.QueryInterface(IID_IArchiveOpenSetSubArchiveName, (void
    **)&setSubArchiveName); if (setSubArchiveName) {
        setSubArchiveName->SetSubArchiveName(subPath.c_str());
      }
    }*/

  m_LastError = Error::ERROR_NONE;

  resetFileList();
  return true;
}

void ArchiveImpl::close()
{
  if (m_ArchivePtr != nullptr) {
    m_ArchivePtr->Close();
  }
  clearFileList();
  m_ArchivePtr.Release();
  m_PasswordCallback = {};
}

void ArchiveImpl::clearFileList()
{
  for (std::vector<FileData*>::iterator iter = m_FileList.begin();
       iter != m_FileList.end(); ++iter) {
    delete *iter;
  }
  m_FileList.clear();
}

void ArchiveImpl::resetFileList()
{
  UInt32 numItems = 0;
  clearFileList();

  m_ArchivePtr->GetNumberOfItems(&numItems);

  for (UInt32 i = 0; i < numItems; ++i) {
    m_FileList.push_back(new FileDataImpl(
        readProperty<std::wstring>(i, kpidPath), readProperty<UInt64>(i, kpidSize),
        readProperty<UInt64>(i, kpidCRC), readProperty<bool>(i, kpidIsDir)));
  }
}

bool ArchiveImpl::extract(std::wstring const& outputDirectory,
                          ProgressCallback progressCallback,
                          FileChangeCallback fileChangeCallback,
                          ErrorCallback errorCallback)

{
  // Retrieve the list of indices we want to extract:
  std::vector<UInt32> indices;
  UInt64 totalSize = 0;
  for (std::size_t i = 0; i < m_FileList.size(); ++i) {
    FileDataImpl* fileData = static_cast<FileDataImpl*>(m_FileList[i]);
    if (!fileData->isEmpty()) {
      indices.push_back(static_cast<UInt32>(i));
      totalSize += fileData->getSize();
    }
  }

  m_ExtractCallback = new CArchiveExtractCallback(
      progressCallback, fileChangeCallback, errorCallback, m_PasswordCallback,
      m_LogCallback, m_ArchivePtr, outputDirectory, &m_FileList[0], m_FileList.size(),
      totalSize, &m_Password);
  HRESULT result = m_ArchivePtr->Extract(
      indices.data(), static_cast<UInt32>(indices.size()), false, m_ExtractCallback);
  // Note: m_ExtractCallBack is deleted by Extract
  switch (result) {
  case S_OK: {
    // nop
  } break;
  case E_ABORT: {
    m_LastError = Error::ERROR_EXTRACT_CANCELLED;
  } break;
  case E_OUTOFMEMORY: {
    m_LastError = Error::ERROR_OUT_OF_MEMORY;
  } break;
  default: {
    m_LastError = Error::ERROR_LIBRARY_ERROR;
  } break;
  }

  return result == S_OK;
}

void ArchiveImpl::cancel()
{
  m_ExtractCallback->SetCanceled(true);
}

std::unique_ptr<Archive> CreateArchive()
{
  return std::make_unique<ArchiveImpl>();
}
