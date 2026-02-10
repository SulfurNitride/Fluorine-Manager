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

#ifndef MULTIOUTPUTSTREAM_H
#define MULTIOUTPUTSTREAM_H

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include <7zip/IStream.h>

#include "fileio.h"
#include "unknown_impl.h"

/** This class allows you to open and output to multiple file handles at a time.
 * It implements the ISequentalOutputStream interface and has some extra functions
 * which are used by the CArchiveExtractCallback class to basically open and
 * set the timestamp on all the files.
 *
 * Note that the handling on errors could be better.
 */
class MultiOutputStream : public IOutStream
{

  UNKNOWN_1_INTERFACE(IOutStream);

public:
  // Callback for write. Args are: 1) number of bytes that have been
  // written for this call, 2) number of bytes that have been written
  // in total.
  using WriteCallback = std::function<void(UInt32, UInt64)>;

  MultiOutputStream(WriteCallback callback = {});

  virtual ~MultiOutputStream();

  /** Opens the supplied files.
   *
   * @returns true if all went OK, false if any file failed to open
   */
  bool Open(std::vector<std::filesystem::path> const& fileNames);

  /** Closes all the files opened by the last open
   *
   * Note if there are any errors, the code will merely report the last one.
   */
  HRESULT Close();

  /** Sets the modification time on the open files
   *
   * @returns true if all files had the time set succesfully, false otherwise
   * note that this will give up as soon as it gets an error
   */
  bool SetMTime(FILETIME const* mTime);

  // ISequentialOutStream interface

  /** Write data to all the streams
   *
   * The processedSize returned will be the same as size, unless
   * there was an error, in which case it might or might not be different.
   * @warn If an error happens, the code will not attempt any further writing,
   * so some files might not get written to at all
   */
  STDMETHOD(Write)(const void* data, UInt32 size, UInt32* processedSize) throw() override;

  STDMETHOD(Seek)(Int64 offset, UInt32 seekOrigin, UInt64* newPosition) throw() override;
  STDMETHOD(SetSize)(UInt64 newSize) throw() override;
  HRESULT GetSize(UInt64* size);

private:
  WriteCallback m_WriteCallback;

  /** This is the amount of data written to *any one* file.
   *
   * If there are errors writing to one of the files, this might or might
   * not match what was actually written to another of the files
   */
  UInt64 m_ProcessedSize;

  /** All the files opened for this 'stream'
   *
   */
  std::vector<IO::FileOut> m_Files;
};

#endif  // MULTIOUTPUTSTREAM_H
