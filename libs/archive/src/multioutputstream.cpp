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

#include "multioutputstream.h"
#include "compat.h"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

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

//////////////////////////
// MultiOutputStream

MultiOutputStream::MultiOutputStream(WriteCallback callback) : m_WriteCallback(callback)
{}

MultiOutputStream::~MultiOutputStream() {}

HRESULT MultiOutputStream::Close()
{
  for (auto& file : m_Files) {
    file.Close();
  }
  return S_OK;
}

bool MultiOutputStream::Open(std::vector<std::filesystem::path> const& filepaths)
{
  m_ProcessedSize = 0;
  bool ok         = true;
  m_Files.clear();
  for (auto& path : filepaths) {
    m_Files.emplace_back();
    if (!m_Files.back().Open(path)) {
      ok = false;
    }
  }
  return ok;
}

STDMETHODIMP MultiOutputStream::Write(const void* data, UInt32 size,
                                      UInt32* processedSize) throw()
{
  bool update_processed(true);
  for (auto& file : m_Files) {
    UInt32 realProcessedSize;
    if (!file.Write(data, size, realProcessedSize)) {
      return ConvertBoolToHRESULT(false);
    }
    if (update_processed) {
      m_ProcessedSize += realProcessedSize;
      if (m_WriteCallback) {
        m_WriteCallback(realProcessedSize, m_ProcessedSize);
      }
      update_processed = false;
    }
    if (processedSize != nullptr) {
      *processedSize = realProcessedSize;
    }
  }
  return S_OK;
}

STDMETHODIMP MultiOutputStream::Seek(Int64 offset, UInt32 seekOrigin,
                                     UInt64* newPosition) throw()
{
  if (seekOrigin >= 3)
    return STG_E_INVALIDFUNCTION;

  bool result = true;
  for (auto& file : m_Files) {
    UInt64 realNewPosition;
    result = file.Seek(offset, seekOrigin, realNewPosition);
    if (newPosition)
      *newPosition = realNewPosition;
  }
  return ConvertBoolToHRESULT(result);
}

STDMETHODIMP MultiOutputStream::SetSize(UInt64 newSize) throw()
{
  bool result = true;
  for (auto& file : m_Files) {
    UInt64 currentPos;
#ifdef _WIN32
    if (!file.Seek(0, FILE_CURRENT, currentPos))
#else
    if (!file.Seek(0, SEEK_CUR, currentPos))
#endif
      return E_FAIL;
    bool cresult = file.SetLength(newSize);
    UInt64 currentPos2;
    result = result && cresult && file.Seek(currentPos, currentPos2);
  }
  return result ? S_OK : E_FAIL;
}

HRESULT MultiOutputStream::GetSize(UInt64* size)
{
  if (m_Files.empty()) {
    return ConvertBoolToHRESULT(false);
  }
  return ConvertBoolToHRESULT(m_Files[0].GetLength(*size));
}

bool MultiOutputStream::SetMTime(FILETIME const* mTime)
{
  for (auto& file : m_Files) {
    file.SetMTime(mTime);
  }
  return true;
}
