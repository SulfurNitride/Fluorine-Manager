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

#ifndef OPENCALLBACK_H
#define OPENCALLBACK_H

#include <filesystem>
#include <string>

#include <7zip/Archive/IArchive.h>
#include <7zip/IPassword.h>

#include "archive.h"
#include "fileio.h"
#include "unknown_impl.h"

class CArchiveOpenCallback : public IArchiveOpenCallback,
                             public IArchiveOpenVolumeCallback,
                             public ICryptoGetTextPassword,
                             public IArchiveOpenSetSubArchiveName
{

  UNKNOWN_4_INTERFACE(IArchiveOpenCallback, IArchiveOpenVolumeCallback,
                      ICryptoGetTextPassword, IArchiveOpenSetSubArchiveName);

public:
  CArchiveOpenCallback(Archive::PasswordCallback passwordCallback,
                       Archive::LogCallback logCallback,
                       std::filesystem::path const& filepath);

  virtual ~CArchiveOpenCallback() {}

  const std::wstring& GetPassword() const { return m_Password; }

  Z7_IFACE_COM7_IMP(IArchiveOpenCallback)
  Z7_IFACE_COM7_IMP(IArchiveOpenVolumeCallback)

  // ICryptoGetTextPassword interface
  STDMETHOD(CryptoGetTextPassword)(BSTR* password) throw();
  // Not implemented STDMETHOD(CryptoGetTextPassword2)(Int32 *passwordIsDefined, BSTR
  // *password);

  // IArchiveOpenSetSubArchiveName interface
  STDMETHOD(SetSubArchiveName)(const wchar_t* name) throw();

private:
  Archive::PasswordCallback m_PasswordCallback;
  Archive::LogCallback m_LogCallback;
  std::wstring m_Password;

  std::filesystem::path m_Path;
  IO::FileInfo m_FileInfo;

  bool m_SubArchiveMode;
  std::wstring m_SubArchiveName;
};

#endif  // OPENCALLBACK_H
