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

#include "inputstream.h"
#include "compat.h"

static inline HRESULT ConvertBoolToHRESULT(bool result)
{
  if (result) {
    return S_OK;
  }
  DWORD lastError = ::GetLastError();
  if (lastError == 0) {
    return E_FAIL;
  }
  return HRESULT_FROM_WIN32(lastError);
}

InputStream::InputStream() {}

InputStream::~InputStream() {}

bool InputStream::Open(std::filesystem::path const& filename)
{
  return m_File.Open(filename);
}

STDMETHODIMP InputStream::Read(void* data, UInt32 size, UInt32* processedSize) throw()
{
  UInt32 realProcessedSize;
  bool result = m_File.Read(data, size, realProcessedSize);

  if (processedSize != nullptr) {
    *processedSize = realProcessedSize;
  }

  if (result) {
    return S_OK;
  }

  return HRESULT_FROM_WIN32(::GetLastError());
}

STDMETHODIMP InputStream::Seek(Int64 offset, UInt32 seekOrigin, UInt64* newPosition) throw()
{
  UInt64 realNewPosition = offset;
  bool result            = m_File.Seek(offset, seekOrigin, realNewPosition);

  if (newPosition) {
    *newPosition = realNewPosition;
  }
  return ConvertBoolToHRESULT(result);
}
