/*
Mod Organizer BSA handling

Copyright (C) 2012 Sebastian Herbord. All rights reserved.

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

#include "bsafile.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <memory>
#include <stdexcept>

#include "bsaexception.h"
#include "bsafolder.h"
#include "filehash.h"

using std::fstream;
using std::ifstream;
using std::ofstream;

namespace BSA
{

bool ByOffset(const File::Ptr& LHS, const File::Ptr& RHS)
{
  return LHS->getDataOffset() < RHS->getDataOffset();
}

static const unsigned long CHUNK_SIZE = 128 * 1024;

File::File(std::fstream& file, Folder* folder)
    : m_Folder(folder), m_New(false), m_FileSize(0), m_UncompressedFileSize(0),
      m_ToggleCompressedWrite(false), m_DataOffsetWrite(0)
{
  m_NameHash         = readType<BSAHash>(file);
  m_FileSize         = readType<BSAULong>(file);
  m_DataOffset       = readType<BSAULong>(file);
  m_ToggleCompressed = m_FileSize & COMPRESSMASK;
  m_FileSize         = m_FileSize & SIZEMASK;
}

File::File(const std::string& name, Folder* folder, BSAULong fileSize,
           BSAHash dataOffset, BSAULong uncompressedFileSize, FO4TextureHeader header,
           std::vector<FO4TextureChunk>& texChunks)
    : m_Folder(folder), m_New(false), m_Name(name), m_FileSize(fileSize),
      m_UncompressedFileSize(uncompressedFileSize), m_DataOffset(dataOffset),
      m_TextureHeader(header), m_ToggleCompressedWrite(false),
      m_TextureChunks(texChunks), m_DataOffsetWrite(0)
{
  m_NameHash         = calculateBSAHash(name);
  m_ToggleCompressed = false;
  if (m_FileSize > 0 && m_UncompressedFileSize > 0)
    m_ToggleCompressed = true;
}

File::File(const std::string& name, const std::string& sourceFile, Folder* folder,
           bool toggleCompressed)
    : m_Folder(folder), m_New(true), m_Name(name), m_FileSize(0),
      m_UncompressedFileSize(0), m_DataOffset(0), m_ToggleCompressed(toggleCompressed),
      m_SourceFile(sourceFile), m_ToggleCompressedWrite(toggleCompressed),
      m_DataOffsetWrite(0)
{
  m_NameHash = calculateBSAHash(name);
}

std::string File::getFilePath() const
{
  return m_Folder->getFullPath() + "\\" + m_Name;
}

void File::writeHeader(fstream& file) const
{
  writeType<BSAHash>(file, m_NameHash);
  BSAULong size = m_FileSize;
  if (m_ToggleCompressed) {
    size |= (1 << 30);
  }
  writeType<BSAULong>(file, size);
  writeType<BSAULong>(file, m_DataOffsetWrite);
}

EErrorCode File::writeData(fstream& sourceArchive, fstream& targetArchive) const
{
  m_DataOffsetWrite = static_cast<BSAULong>(targetArchive.tellp());
  EErrorCode result = ERROR_NONE;

  std::unique_ptr<char[]> inBuffer(new char[CHUNK_SIZE]);

  if (m_SourceFile.length() == 0) {
    // copy from source archive
#pragma message("we may have to compress/decompress!")
    sourceArchive.seekg(m_DataOffset, fstream::beg);

    try {
      unsigned long sizeLeft = m_FileSize;
      while (sizeLeft > 0) {
        int chunkSize = (std::min)(sizeLeft, CHUNK_SIZE);
        sourceArchive.read(inBuffer.get(), chunkSize);
        targetArchive.write(inBuffer.get(), chunkSize);
        sizeLeft -= chunkSize;
      }
    } catch (const std::exception&) {
      result = ERROR_INVALIDDATA;
    }
  } else {
    // copy from file on disc
    fstream sourceFile;
    sourceFile.open(m_SourceFile.c_str());
    if (!sourceFile.is_open()) {
      return ERROR_SOURCEFILEMISSING;
    }
    sourceFile.seekg(0, fstream::end);
    m_FileSize             = static_cast<BSAULong>(sourceFile.tellg());
    unsigned long sizeLeft = m_FileSize;
    sourceFile.seekg(0, fstream::beg);
    while (sizeLeft > 0) {
      int chunkSize = (std::min)(sizeLeft, CHUNK_SIZE);
      sourceFile.read(inBuffer.get(), chunkSize);
      targetArchive.write(inBuffer.get(), chunkSize);
      sizeLeft -= chunkSize;
    }
  }
  return result;
}

void File::readFileName(fstream& file, bool testHashes)
{
  m_Name = readZString(file);
  if (testHashes) {
    if (calculateBSAHash(m_Name) != m_NameHash) {
      throw data_invalid_exception(
          makeString("invalid name hash for \"%s\" (%lx vs %lx)", m_Name.c_str(),
                     calculateBSAHash(m_Name), m_NameHash));
    }
  }
}

}  // namespace BSA
