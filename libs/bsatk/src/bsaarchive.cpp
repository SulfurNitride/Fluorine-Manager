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

#include "bsaarchive.h"
#include "bsaexception.h"
#include "bsafile.h"
#include "bsafolder.h"
#include <algorithm>
#include <boost/interprocess/sync/interprocess_semaphore.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/shared_array.hpp>
#include <boost/thread.hpp>
#include <cstring>
#include <fstream>
#include <iostream>
#include <lz4.h>
#include <lz4frame.h>
#include <memory>
#include <queue>
#include <sys/stat.h>
#include <zlib.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

using std::fstream;

using namespace boost::posix_time;

namespace BSA
{

Archive::Archive()
    : m_RootFolder(new Folder), m_ArchiveFlags(FLAG_HASDIRNAMES | FLAG_HASFILENAMES),
      m_Type(TYPE_SKYRIM)
{}

Archive::~Archive()
{
  if (m_File.is_open()) {
    m_File.close();
  }
  std::vector<Folder::Ptr> folders;
  m_RootFolder->collectFolders(folders);
  cleanFolder(m_RootFolder);
  m_RootFolder.reset();
}

ArchiveType Archive::typeFromID(BSAULong typeID)
{
  switch (typeID) {
  case 0x100:
    return TYPE_MORROWIND;
  case 0x67:
    return TYPE_OBLIVION;
  case 0x68:
    return TYPE_FALLOUT3;
  case 0x69:
    return TYPE_SKYRIMSE;
  case 0x01:
    return TYPE_FALLOUT4;
  case 0x02:
    return TYPE_STARFIELD;
  case 0x03:
    return TYPE_STARFIELD_LZ4_TEXTURE;
  case 0x07:
    return TYPE_FALLOUT4NG_7;
  case 0x08:
    return TYPE_FALLOUT4NG_8;
  default:
    throw data_invalid_exception(makeString("invalid type %d", typeID));
  }
}

BSAULong Archive::typeToID(ArchiveType type)
{
  switch (type) {
  case TYPE_MORROWIND:
    return 0x100;
  case TYPE_OBLIVION:
    return 0x67;
  case TYPE_FALLOUT3:
    return 0x68;
  case TYPE_SKYRIMSE:
    return 0x69;
  case TYPE_FALLOUT4:
    return 0x01;
  case TYPE_STARFIELD:
    return 0x02;
  case TYPE_STARFIELD_LZ4_TEXTURE:
    return 0x03;
  case TYPE_FALLOUT4NG_7:
    return 0x07;
  case TYPE_FALLOUT4NG_8:
    return 0x08;
  default:
    throw data_invalid_exception(makeString("invalid type %d", type));
  }
}

Archive::Header Archive::readHeader(std::fstream& infile)
{
  Header result;

  result.fileIdentifier = readType<uint32_t>(infile);
  if (result.fileIdentifier != 0x00415342 && result.fileIdentifier != 0x58445442 &&
      result.fileIdentifier != 0x00000100) {
    throw data_invalid_exception(makeString("not a bsa or ba2 file"));
  }

  if (result.fileIdentifier != 0x00000100) {
    ArchiveType type = typeFromID(readType<BSAUInt>(infile));
    if (type == TYPE_FALLOUT4 || type == TYPE_STARFIELD ||
        type == TYPE_STARFIELD_LZ4_TEXTURE || type == TYPE_FALLOUT4NG_7 ||
        type == TYPE_FALLOUT4NG_8) {
      result.type = type;
      infile.read(result.archType, 4);
      result.archType[4]     = '\0';
      result.fileCount       = readType<BSAUInt>(infile);
      result.nameTableOffset = readType<BSAHash>(infile);
      result.archiveFlags    = FLAG_HASDIRNAMES | FLAG_HASFILENAMES;
    } else {
      result.type             = type;
      result.offset           = readType<BSAUInt>(infile);
      result.archiveFlags     = readType<BSAUInt>(infile);
      result.folderCount      = readType<BSAUInt>(infile);
      result.fileCount        = readType<BSAUInt>(infile);
      result.folderNameLength = readType<BSAUInt>(infile);
      result.fileNameLength   = readType<BSAUInt>(infile);
      result.fileFlags        = readType<BSAUInt>(infile);
    }
  } else {
    result.type         = TYPE_MORROWIND;
    result.offset       = readType<BSAUInt>(infile);
    result.fileCount    = readType<BSAUInt>(infile);
    result.archiveFlags = FLAG_HASDIRNAMES | FLAG_HASFILENAMES;
  }

  return result;
}

EErrorCode Archive::read(const char* fileName, bool testHashes)
{
  m_File.open(fileName, fstream::in | fstream::binary);
  if (!m_File.is_open()) {
    return ERROR_FILENOTFOUND;
  }
  m_File.exceptions(std::ios_base::badbit);
  try {
    Header header;
    try {
      header = readHeader(m_File);
    } catch (const data_invalid_exception& e) {
      throw data_invalid_exception(makeString("%s (filename: %s)", e.what(), fileName));
    }
    m_ArchiveFlags = header.archiveFlags;
    m_Type         = header.type;
    if (m_Type == TYPE_FALLOUT4 || m_Type == TYPE_STARFIELD ||
        m_Type == TYPE_STARFIELD_LZ4_TEXTURE || m_Type == TYPE_FALLOUT4NG_7 ||
        m_Type == TYPE_FALLOUT4NG_8) {

      m_File.seekg(header.nameTableOffset);

      std::vector<std::string> fileNames;
      for (unsigned int i = 0; i < header.fileCount; ++i) {
        BSAUShort length = readType<BSAUShort>(m_File);

        char* file = new char[length + 1];
        m_File.read(file, length);
        file[length] = '\0';

        fileNames.push_back(file);
        delete[] file;
      }
      std::streamoff offset;
      switch (m_Type) {
      case TYPE_STARFIELD:
        offset = 32;
        break;
      case TYPE_STARFIELD_LZ4_TEXTURE:
        offset = 36;
        break;
      default:
        offset = 24;
      }
      if (strcmp(header.archType, "GNRL") == 0) {
        m_File.seekg(offset, std::ios::beg);
        for (unsigned int i = 0; i < header.fileCount; ++i) {
          BSAUInt nameHash = readType<BSAUInt>(m_File);
          char* extension  = new char[4];
          m_File.read(extension, 4);
          BSAUInt dirHash = readType<BSAUInt>(m_File);
          m_File.seekg(4, std::ios::cur);
          BSAHash offset       = readType<BSAHash>(m_File);
          BSAUInt packedSize   = readType<BSAUInt>(m_File);
          BSAUInt unpackedSize = readType<BSAUInt>(m_File);
          m_File.seekg(4, std::ios::cur);
          std::vector<FO4TextureChunk> dummy;
          Folder::Ptr newDir = m_RootFolder->addFolderFromFile(
              fileNames[i], packedSize, offset, unpackedSize, {}, dummy);
          delete[] extension;
        }
      } else if (strcmp(header.archType, "DX10") == 0) {
        m_File.seekg(offset, std::ios::beg);
        for (unsigned int i = 0; i < header.fileCount; ++i) {
          FO4TextureHeader texHeader;
          texHeader.nameHash = readType<BSAUInt>(m_File);
          m_File.read(texHeader.extension, 4);
          texHeader.dirHash         = readType<BSAUInt>(m_File);
          texHeader.unknown1        = readType<BSAUChar>(m_File);
          texHeader.chunkNumber     = readType<BSAUChar>(m_File);
          texHeader.chunkHeaderSize = readType<BSAUShort>(m_File);
          texHeader.height          = readType<BSAUShort>(m_File);
          texHeader.width           = readType<BSAUShort>(m_File);
          texHeader.mipCount        = readType<BSAUChar>(m_File);
          texHeader.format    = static_cast<DXGI_FORMAT>(readType<BSAUChar>(m_File));
          texHeader.isCubemap = readType<bool>(m_File);
          texHeader.unknown2  = readType<BSAUChar>(m_File);
          std::vector<FO4TextureChunk> chunks;
          for (unsigned int j = 0; j < texHeader.chunkNumber; ++j) {
            FO4TextureChunk chunk;
            chunk.offset       = readType<BSAHash>(m_File);
            chunk.packedSize   = readType<BSAUInt>(m_File);
            chunk.unpackedSize = readType<BSAUInt>(m_File);
            chunk.startMip     = readType<BSAUShort>(m_File);
            chunk.endMip       = readType<BSAUShort>(m_File);
            chunk.unknown      = readType<BSAUInt>(m_File);
            chunks.push_back(chunk);
          }
          Folder::Ptr newDir = m_RootFolder->addFolderFromFile(
              fileNames[i], chunks[0].packedSize, chunks[0].offset,
              chunks[0].unpackedSize, texHeader, chunks);
        }
      }

      return ERROR_NONE;
    } else if (m_Type == TYPE_MORROWIND) {
      BSAUInt dataOffset = 12 + header.offset + header.fileCount * 8;

      std::vector<MorrowindFileOffset> fileSizeOffset(header.fileCount);
      m_File.read((char*)fileSizeOffset.data(),
                  header.fileCount * sizeof(MorrowindFileOffset));
      std::vector<BSAUInt> fileNameOffset(header.fileCount);
      m_File.read((char*)fileNameOffset.data(), header.fileCount * sizeof(BSAUInt));
      BSAUInt last = header.offset - 12 * header.fileCount;
      for (uint32_t i = 0; i < header.fileCount; ++i) {
        uint32_t index = 0;
        if (i + 1 == header.fileCount)
          index = last;
        else
          index = fileNameOffset[i + 1] - fileNameOffset[i];
        char* filePath = new char[index + 1];
        m_File.read(filePath, index);
        filePath[index] = '\0';

        std::vector<FO4TextureChunk> dummy;
        Folder::Ptr newDir = m_RootFolder->addFolderFromFile(
            filePath, fileSizeOffset[i].size, dataOffset + fileSizeOffset[i].offset, 0,
            {}, dummy);
        delete[] filePath;
      }
      return ERROR_NONE;
    } else {
      // flat list of folders as they were stored in the archive
      std::vector<Folder::Ptr> folders;

      for (unsigned long i = 0; i < header.folderCount; ++i) {
        folders.push_back(m_RootFolder->addFolder(m_File, header.fileNameLength,
                                                  header.offset, header.type));
      }

      m_File.seekg(header.offset);

      bool hashesValid = true;
      for (std::vector<Folder::Ptr>::iterator iter = folders.begin();
           iter != folders.end(); ++iter) {
        if (!(*iter)->resolveFileNames(m_File, testHashes)) {
          hashesValid = false;
        }
      }
      return hashesValid ? ERROR_NONE : ERROR_INVALIDHASHES;
    }
  } catch (std::ios_base::failure&) {
    return ERROR_INVALIDDATA;
  }
}

void Archive::close()
{
  m_File.close();
}

BSAULong Archive::countFiles() const
{
  return m_RootFolder->countFiles();
}

std::vector<std::string> Archive::collectFolderNames() const
{
  std::vector<std::string> result;
  m_RootFolder->collectFolderNames(result);
  return result;
}

std::vector<std::string> Archive::collectFileNames() const
{
  std::vector<std::string> result;
  m_RootFolder->collectFileNames(result);
  return result;
}

BSAULong Archive::countCharacters(const std::vector<std::string>& list) const
{
  size_t sum = 0;
  for (std::vector<std::string>::const_iterator iter = list.begin(); iter != list.end();
       ++iter) {
    sum += iter->length() + 1;
  }
  return static_cast<BSAULong>(sum);
}

#ifndef WIN32
#define _stricmp strcasecmp
#endif  // WIN32

static bool endsWith(const std::string& fileName, const char* extension)
{
  size_t endLength = strlen(extension);
  if (fileName.length() < endLength) {
    return false;
  }
  return _stricmp(&fileName[fileName.length() - endLength], extension) == 0;
}

BSAULong Archive::determineFileFlags(const std::vector<std::string>& fileList) const
{
  BSAULong result = 0;

  bool hasNIF = false;
  bool hasDDS = false;
  bool hasXML = false;
  bool hasWAV = false;
  bool hasMP3 = false;
  bool hasTXT = false;
  bool hasSPT = false;
  bool hasTEX = false;
  bool hasCTL = false;

  for (std::vector<std::string>::const_iterator iter = fileList.begin();
       iter != fileList.end(); ++iter) {
    if (!hasNIF && endsWith(*iter, ".nif")) {
      hasNIF = true;
      result |= 1 << 0;
    } else if (!hasDDS && endsWith(*iter, ".dds")) {
      hasDDS = true;
      result |= 1 << 1;
    } else if (!hasXML && endsWith(*iter, ".xml")) {
      hasXML = true;
      result |= 1 << 2;
    } else if (!hasWAV && endsWith(*iter, ".wav")) {
      hasWAV = true;
      result |= 1 << 3;
    } else if (!hasMP3 && endsWith(*iter, ".mp3")) {
      hasMP3 = true;
      result |= 1 << 4;
    } else if (!hasTXT && endsWith(*iter, ".txt")) {
      hasTXT = true;
      result |= 1 << 5;
    } else if (!hasSPT && endsWith(*iter, ".spt")) {
      hasSPT = true;
      result |= 1 << 6;
    } else if (!hasTEX && endsWith(*iter, ".tex")) {
      hasTEX = true;
      result |= 1 << 7;
    } else if (!hasCTL && endsWith(*iter, ".ctl")) {
      hasCTL = true;
      result |= 1 << 8;
    }
  }
  return result;
}

void Archive::writeHeader(std::fstream& outfile, BSAULong fileFlags,
                          BSAULong numFolders, BSAULong folderNamesLength,
                          BSAULong fileNamesLength)
{
  outfile.write("BSA\0", 4);
  writeType<BSAULong>(outfile, typeToID(m_Type));
  writeType<BSAULong>(outfile, 0x24);  // header size is static
  writeType<BSAULong>(outfile, m_ArchiveFlags);
  writeType<BSAULong>(outfile, numFolders);
  writeType<BSAULong>(outfile, countFiles());
  writeType<BSAULong>(outfile, folderNamesLength);
  writeType<BSAULong>(outfile, fileNamesLength);
  writeType<BSAULong>(outfile, fileFlags);
}

EErrorCode Archive::write(const char* fileName)
{
  std::fstream outfile;
  outfile.open(fileName, fstream::out | fstream::binary);
  if (!outfile.is_open()) {
    return ERROR_ACCESSFAILED;
  }
  outfile.exceptions(std::ios_base::badbit);

  std::vector<Folder::Ptr> folders;
  m_RootFolder->collectFolders(folders);

  std::vector<std::string> folderNames;
  std::vector<std::string> fileNames;
  BSAULong folderNamesLength = 0;
  BSAULong fileNamesLength   = 0;
  for (std::vector<Folder::Ptr>::const_iterator folderIter = folders.begin();
       folderIter != folders.end(); ++folderIter) {
    std::string fullPath = (*folderIter)->getFullPath();
    folderNames.push_back(fullPath);
    folderNamesLength += static_cast<BSAULong>(fullPath.length());
    for (std::vector<File::Ptr>::const_iterator fileIter =
             (*folderIter)->m_Files.begin();
         fileIter != (*folderIter)->m_Files.end(); ++fileIter) {
      fileNames.push_back((*fileIter)->m_Name);
      fileNamesLength += static_cast<BSAULong>((*fileIter)->m_Name.length());
    }
  }

  try {
    writeHeader(outfile, determineFileFlags(fileNames),
                static_cast<BSAULong>(folderNames.size()), folderNamesLength,
                fileNamesLength);
#pragma message("folders (and files?) need to be sorted by hash!")
    // dummy-pass: before we can store the actual folder data

    // prepare folder and file headers
#pragma message("it's unnecessary to write actual data, placeholders are sufficient")
    for (std::vector<Folder::Ptr>::const_iterator folderIter = folders.begin();
         folderIter != folders.end(); ++folderIter) {
      (*folderIter)->writeHeader(outfile);
    }

    for (std::vector<Folder::Ptr>::const_iterator folderIter = folders.begin();
         folderIter != folders.end(); ++folderIter) {
      (*folderIter)->writeData(outfile, fileNamesLength);
    }

    // write file names
    for (std::vector<std::string>::const_iterator folderIter = fileNames.begin();
         folderIter != fileNames.end(); ++folderIter) {
      writeZString(outfile, *folderIter);
    }

    // write file data
    for (std::vector<Folder::Ptr>::iterator folderIter = folders.begin();
         folderIter != folders.end(); ++folderIter) {
      (*folderIter)->writeFileData(m_File, outfile);
    }

    outfile.seekp(0x24, fstream::beg);

    // re-write folder and file structure, this time with the correct
    // offsets
    for (std::vector<Folder::Ptr>::const_iterator folderIter = folders.begin();
         folderIter != folders.end(); ++folderIter) {
      (*folderIter)->writeHeader(outfile);
    }

    for (std::vector<Folder::Ptr>::const_iterator folderIter = folders.begin();
         folderIter != folders.end(); ++folderIter) {
      (*folderIter)->writeData(outfile, fileNamesLength);
    }

    outfile.close();
    return ERROR_NONE;
  } catch (std::ios_base::failure&) {
    outfile.close();
    return ERROR_INVALIDDATA;
  }
}

DirectX::DDS_HEADER Archive::getDDSHeader(File::Ptr file,
                                          DirectX::DDS_HEADER_DXT10& DX10Header,
                                          bool& isDX10) const
{
  DirectX::DDS_HEADER DDSHeaderData = {};
  DDSHeaderData.size                = sizeof(DDSHeaderData);
  DDSHeaderData.flags =
      DDS_HEADER_FLAGS_TEXTURE | DDS_HEADER_FLAGS_LINEARSIZE | DDS_HEADER_FLAGS_MIPMAP;
  DDSHeaderData.height      = file->m_TextureHeader.height;
  DDSHeaderData.width       = file->m_TextureHeader.width;
  DDSHeaderData.mipMapCount = file->m_TextureHeader.mipCount;
  DDSHeaderData.ddspf.size  = sizeof(DirectX::DDS_PIXELFORMAT);
  DDSHeaderData.caps        = DDS_SURFACE_FLAGS_TEXTURE | DDS_SURFACE_FLAGS_MIPMAP;

  if (file->m_TextureHeader.isCubemap)
    DDSHeaderData.caps2 = DDS_CUBEMAP_ALLFACES;

  bool supported = true;

  switch (file->m_TextureHeader.format) {
  case DXGI_FORMAT_BC1_UNORM:
  case DXGI_FORMAT_BC1_UNORM_SRGB:
    DDSHeaderData.ddspf = DirectX::DDSPF_DXT1;
    DDSHeaderData.pitchOrLinearSize =
        file->m_TextureHeader.width * file->m_TextureHeader.height / 2;
    break;

  case DXGI_FORMAT_BC2_UNORM:
  case DXGI_FORMAT_BC2_UNORM_SRGB:
    DDSHeaderData.ddspf = DirectX::DDSPF_DXT3;
    DDSHeaderData.pitchOrLinearSize =
        file->m_TextureHeader.width * file->m_TextureHeader.height;
    break;

  case DXGI_FORMAT_BC3_UNORM:
  case DXGI_FORMAT_BC3_UNORM_SRGB:
    DDSHeaderData.ddspf = DirectX::DDSPF_DXT5;
    DDSHeaderData.pitchOrLinearSize =
        file->m_TextureHeader.width * file->m_TextureHeader.height;
    break;

  case DXGI_FORMAT_BC4_UNORM:
    DDSHeaderData.ddspf = DirectX::DDSPF_BC4_UNORM;
    DDSHeaderData.pitchOrLinearSize =
        file->m_TextureHeader.width * file->m_TextureHeader.height;
    break;

  case DXGI_FORMAT_BC4_SNORM:
    DDSHeaderData.ddspf = DirectX::DDSPF_BC4_SNORM;
    DDSHeaderData.pitchOrLinearSize =
        file->m_TextureHeader.width * file->m_TextureHeader.height;
    break;

  case DXGI_FORMAT_BC5_UNORM:
    DDSHeaderData.ddspf = DirectX::DDSPF_BC5_UNORM;
    DDSHeaderData.pitchOrLinearSize =
        file->m_TextureHeader.width * file->m_TextureHeader.height;
    break;

  case DXGI_FORMAT_BC5_SNORM:
    DDSHeaderData.ddspf = DirectX::DDSPF_BC5_SNORM;
    DDSHeaderData.pitchOrLinearSize =
        file->m_TextureHeader.width * file->m_TextureHeader.height;
    break;

  case DXGI_FORMAT_BC7_UNORM:
  case DXGI_FORMAT_BC7_UNORM_SRGB:
    DDSHeaderData.ddspf = DirectX::DDSPF_DX10;
    DDSHeaderData.pitchOrLinearSize =
        file->m_TextureHeader.width * file->m_TextureHeader.height;

    isDX10                = true;
    DX10Header.dxgiFormat = file->m_TextureHeader.format;
    break;

  case DXGI_FORMAT_R8G8B8A8_UNORM:
    DDSHeaderData.ddspf = DirectX::DDSPF_A8B8G8R8;
    DDSHeaderData.pitchOrLinearSize =
        file->m_TextureHeader.width * file->m_TextureHeader.height * 4;  // 32bpp
    break;

  case DXGI_FORMAT_B8G8R8A8_UNORM:
    DDSHeaderData.ddspf = DirectX::DDSPF_A8R8G8B8;
    DDSHeaderData.pitchOrLinearSize =
        file->m_TextureHeader.width * file->m_TextureHeader.height * 4;  // 32bpp
    break;

  case DXGI_FORMAT_B8G8R8X8_UNORM:
    DDSHeaderData.ddspf = DirectX::DDSPF_X8R8G8B8;
    break;

  case DXGI_FORMAT_R8_UNORM:
    DDSHeaderData.ddspf = DirectX::DDSPF_L8;
    DDSHeaderData.pitchOrLinearSize =
        file->m_TextureHeader.width * file->m_TextureHeader.height;  // 8bpp
    break;

  case DXGI_FORMAT_R16_UNORM:
    DDSHeaderData.ddspf = DirectX::DDSPF_L16;
    DDSHeaderData.pitchOrLinearSize =
        file->m_TextureHeader.width * file->m_TextureHeader.height * 2;  // 16bpp
    break;

  case DXGI_FORMAT_R8G8_UNORM:
    DDSHeaderData.ddspf = DirectX::DDSPF_A8L8;
    DDSHeaderData.pitchOrLinearSize =
        file->m_TextureHeader.width * file->m_TextureHeader.height * 2;  // 16bpp
    break;

  default:
    return {};
    break;
  }

  return DDSHeaderData;
}

void Archive::getDX10Header(DirectX::DDS_HEADER_DXT10& DX10Header, File::Ptr file,
                            DirectX::DDS_HEADER DDSHeader) const
{
  DX10Header.resourceDimension = DirectX::DDS_DIMENSION_TEXTURE2D;
  DX10Header.miscFlag          = 0;
  DX10Header.arraySize         = 1;
  DX10Header.miscFlags2        = 0;
}

static const unsigned long CHUNK_SIZE = 128 * 1024;

EErrorCode Archive::extractDirect(File::Ptr file, std::ofstream& outFile) const
{
  EErrorCode result = ERROR_NONE;
  if (file->m_FileSize == 0) {
    // don't try to read empty file
    return result;
  }

  m_File.clear();
  m_File.seekg(static_cast<std::ifstream::pos_type>(file->m_DataOffset), std::ios::beg);

  if (m_Type == TYPE_FALLOUT4 || m_Type == TYPE_STARFIELD ||
      m_Type == TYPE_STARFIELD_LZ4_TEXTURE || m_Type == TYPE_FALLOUT4NG_7 ||
      m_Type == TYPE_FALLOUT4NG_8) {
    if (!file->m_TextureChunks.size()) {
      BSAULong size = file->m_UncompressedFileSize;
      std::unique_ptr<char[]> buffer(new char[size]);
      m_File.read(buffer.get(), size);
      outFile.write(buffer.get(), size);
    } else {
      bool isDX10                              = false;
      DirectX::DDS_HEADER_DXT10 DX10HeaderData = {};
      DirectX::DDS_HEADER DDSHeaderData = getDDSHeader(file, DX10HeaderData, isDX10);

      outFile.write("DDS ", 4);
      char* DDSHeader = new char[sizeof(DDSHeaderData)];
      memcpy(DDSHeader, &DDSHeaderData, sizeof(DDSHeaderData));
      outFile.write(DDSHeader, sizeof(DDSHeaderData));
      delete[] DDSHeader;

      if (isDX10) {
        getDX10Header(DX10HeaderData, file, DDSHeaderData);
        char* DX10Header = new char[sizeof(DX10HeaderData)];
        memcpy(DX10Header, &DX10HeaderData, sizeof(DX10HeaderData));
        outFile.write(DX10Header, sizeof(DX10HeaderData));
        delete[] DX10Header;
      }

      for (BSAUInt i = 0; i < file->m_TextureChunks.size(); ++i) {
        BSAULong length = file->m_TextureChunks[i].unpackedSize;
        std::unique_ptr<char[]> chunk(new char[length]);
        m_File.read(chunk.get(), length);
        outFile.write(chunk.get(), length);
      }
    }
  } else {
    BSAULong size = file->m_FileSize;
    if (namePrefixed()) {
      std::string fullName = readBString(m_File);
      if (size <= fullName.length()) {
#pragma message("report error!")
        return result;
      }
      size -= fullName.length() + 1;
    }
    std::unique_ptr<unsigned char[]> buffer(new unsigned char[size]);
    m_File.read(reinterpret_cast<char*>(buffer.get()), size);
    if (result == ERROR_NONE)
      outFile.write(reinterpret_cast<char*>(buffer.get()), size);
  }
  std::unique_ptr<char[]> inBuffer(new char[CHUNK_SIZE]);

  try {
    unsigned long sizeLeft = file->m_FileSize;

    while (sizeLeft > 0) {
      int chunkSize = (std::min)(sizeLeft, CHUNK_SIZE);
      m_File.read(inBuffer.get(), chunkSize);
      outFile.write(inBuffer.get(), chunkSize);
      sizeLeft -= chunkSize;
    }
  } catch (const std::exception&) {
    result = ERROR_INVALIDDATA;
  }
  return result;
}

std::shared_ptr<unsigned char[]> Archive::decompress(unsigned char* inBuffer,
                                                     BSAULong inSize,
                                                     EErrorCode& result,
                                                     BSAULong& outSize)
{
  if (outSize == 0) {
    memcpy(&outSize, inBuffer, sizeof(BSAULong));
    inBuffer += sizeof(BSAULong);
    inSize -= sizeof(BSAULong);
  }

  if ((inSize == 0) || (outSize == 0)) {
    return std::shared_ptr<unsigned char[]>();
  }

  std::shared_ptr<unsigned char[]> outBuffer(new unsigned char[outSize]);

  z_stream stream;
  try {
    stream.zalloc   = Z_NULL;
    stream.zfree    = Z_NULL;
    stream.opaque   = Z_NULL;
    stream.avail_in = inSize;
    stream.next_in  = static_cast<Bytef*>(inBuffer);
    int zlibRet     = inflateInit2(&stream, 15 + 32);
    if (zlibRet != Z_OK) {
      result = ERROR_ZLIBINITFAILED;
      return std::shared_ptr<unsigned char[]>();
    }

    do {
      stream.avail_out = outSize;
      stream.next_out  = reinterpret_cast<Bytef*>(outBuffer.get());
      zlibRet          = inflate(&stream, Z_NO_FLUSH);
      if ((zlibRet != Z_OK) && (zlibRet != Z_STREAM_END) && (zlibRet != Z_BUF_ERROR)) {
#pragma message("pass result code to caller")
        throw std::runtime_error("invalid data");
      }
    } while (stream.avail_out == 0);
    inflateEnd(&stream);
    return outBuffer;
  } catch (const std::exception&) {
    result = ERROR_INVALIDDATA;
    inflateEnd(&stream);
    return std::shared_ptr<unsigned char[]>();
  }
}

EErrorCode Archive::extractCompressed(File::Ptr file, std::ofstream& outFile) const
{
  EErrorCode result = ERROR_NONE;
  if (file->m_FileSize == 0) {
    // don't try to read empty file
    return result;
  }

  m_File.clear();
  m_File.seekg(static_cast<std::ifstream::pos_type>(file->m_DataOffset), std::ios::beg);

  if (m_Type == TYPE_FALLOUT4 || m_Type == TYPE_STARFIELD ||
      m_Type == TYPE_STARFIELD_LZ4_TEXTURE || m_Type == TYPE_FALLOUT4NG_7 ||
      m_Type == TYPE_FALLOUT4NG_8) {
    if (!file->m_TextureChunks.size()) {
      BSAULong inSize = file->m_FileSize;
      std::unique_ptr<unsigned char[]> inBuffer(new unsigned char[inSize]);
      m_File.read(reinterpret_cast<char*>(inBuffer.get()), inSize);
      BSAULong length = file->m_UncompressedFileSize;
      std::shared_ptr<unsigned char[]> buffer =
          decompress(inBuffer.get(), inSize, result, length);
      if (result == ERROR_NONE) {
        outFile.write(reinterpret_cast<char*>(buffer.get()), length);
      }
    } else {
      bool isDX10                              = false;
      DirectX::DDS_HEADER_DXT10 DX10HeaderData = {};
      DirectX::DDS_HEADER DDSHeaderData = getDDSHeader(file, DX10HeaderData, isDX10);

      outFile.write("DDS ", 4);
      char* DDSHeader = new char[sizeof(DDSHeaderData)];
      memcpy(DDSHeader, &DDSHeaderData, sizeof(DDSHeaderData));
      outFile.write(DDSHeader, sizeof(DDSHeaderData));
      delete[] DDSHeader;

      if (isDX10) {
        getDX10Header(DX10HeaderData, file, DDSHeaderData);
        char* DX10Header = new char[sizeof(DX10HeaderData)];
        memcpy(DX10Header, &DX10HeaderData, sizeof(DX10HeaderData));
        outFile.write(DX10Header, sizeof(DX10HeaderData));
        delete[] DX10Header;
      }

      for (BSAUInt i = 0; i < file->m_TextureChunks.size(); ++i) {
        BSAULong length = file->m_TextureChunks[i].unpackedSize;
        if (file->m_TextureChunks[i].packedSize > 0) {
          unsigned char* chunk = new unsigned char[file->m_TextureChunks[i].packedSize];
          m_File.read(reinterpret_cast<char*>(chunk),
                      file->m_TextureChunks[i].packedSize);
          if (m_Type == TYPE_STARFIELD_LZ4_TEXTURE) {
            char* unpackedChunk = new char[length];
            LZ4_decompress_safe(reinterpret_cast<char*>(chunk), unpackedChunk,
                                file->m_TextureChunks[i].packedSize, length);
            outFile.write(unpackedChunk, length);
            delete[] unpackedChunk;
          } else {
            std::shared_ptr<unsigned char[]> unpackedChunk =
                decompress(chunk, file->m_TextureChunks[i].packedSize, result, length);
            if (result == ERROR_NONE) {
              outFile.write(reinterpret_cast<char*>(unpackedChunk.get()), length);
            }
          }
          delete[] chunk;
        } else {
          char* chunk = new char[length];
          m_File.read(chunk, length);
          outFile.write(chunk, length);
          delete[] chunk;
        }
      }
    }
  } else if (m_Type == TYPE_SKYRIMSE) {
    BSAULong inSize = file->m_FileSize;
    if (namePrefixed()) {
      std::string fullName = readBString(m_File);
      if (inSize <= fullName.length()) {
#pragma message("report error!")
        return result;
      }
      inSize -= fullName.length() + 1;
    }
    BSAULong outSize = readType<BSAULong>(m_File);
    inSize -= sizeof(BSAULong);
    std::unique_ptr<unsigned char[]> inBuffer(new unsigned char[inSize]);
    m_File.read(reinterpret_cast<char*>(inBuffer.get()), inSize);

    LZ4F_decompressionContext_t dcContext = nullptr;
    LZ4F_decompressOptions_t options      = {};
    LZ4F_createDecompressionContext(&dcContext, LZ4F_VERSION);
    size_t lzOutSize = outSize;
    size_t lzInSize  = inSize;

    std::unique_ptr<unsigned char[]> outBuffer(new unsigned char[outSize]);
    LZ4F_decompress(dcContext, outBuffer.get(), &lzOutSize, inBuffer.get(), &lzInSize,
                    &options);

    outFile.write(reinterpret_cast<char*>(outBuffer.get()), outSize);
  } else {
    BSAULong inSize = file->m_FileSize;
    if (namePrefixed()) {
      std::string fullName = readBString(m_File);
      if (inSize <= fullName.length()) {
#pragma message("report error!")
        return result;
      }
      inSize -= fullName.length() + 1;
    }
    std::unique_ptr<unsigned char[]> inBuffer(new unsigned char[inSize]);
    m_File.read(reinterpret_cast<char*>(inBuffer.get()), inSize);
    BSAULong length = 0UL;
    std::shared_ptr<unsigned char[]> buffer =
        decompress(inBuffer.get(), inSize, result, length);
    if (result == ERROR_NONE) {
      outFile.write(reinterpret_cast<char*>(buffer.get()), length);
    }
  }

  return result;
}

EErrorCode Archive::extract(File::Ptr file, const char* outputDirectory) const
{
  std::string fileName = makeString("%s/%s", outputDirectory, file->getName().c_str());
  std::ofstream outputFile(fileName.c_str(),
                           fstream::out | fstream::binary | fstream::trunc);
  if (!outputFile.is_open()) {
    return ERROR_ACCESSFAILED;
  }

  EErrorCode result = ERROR_NONE;
  if (compressed(file)) {
    result = extractCompressed(file, outputFile);
  } else {
    result = extractDirect(file, outputFile);
  }
  outputFile.close();
  return result;
}

void Archive::readFiles(std::queue<FileInfo>& queue, boost::mutex& mutex,
                        boost::interprocess::interprocess_semaphore& bufferCount,
                        boost::interprocess::interprocess_semaphore& queueFree,
                        std::vector<File::Ptr>::iterator begin,
                        std::vector<File::Ptr>::iterator end)
{
  for (; begin != end && !boost::this_thread::interruption_requested(); ++begin) {
    queueFree.wait();

    FileInfo fileInfo;
    fileInfo.file = *begin;
    size_t size   = static_cast<size_t>(fileInfo.file->m_FileSize);

    m_File.seekg(fileInfo.file->m_DataOffset);
    if (m_Type != TYPE_FALLOUT4 && m_Type != TYPE_STARFIELD &&
        m_Type != TYPE_STARFIELD_LZ4_TEXTURE && m_Type != TYPE_FALLOUT4NG_7 &&
        m_Type != TYPE_FALLOUT4NG_8) {
      if (namePrefixed()) {
        std::string fullName = readBString(m_File);
        if (size <= fullName.length()) {
#pragma message("report error!")
          continue;
        }
        size -= fullName.length() + 1;
      }
      if (m_Type == TYPE_SKYRIMSE && compressed(fileInfo.file)) {
        fileInfo.file->m_UncompressedFileSize = readType<BSAULong>(m_File);
        size -= 4;
      }
      if (!fileInfo.file->m_TextureChunks.size()) {
        fileInfo.data =
            std::make_pair(std::shared_ptr<unsigned char[]>(new unsigned char[size]),
                           static_cast<BSAULong>(size));
        m_File.read(reinterpret_cast<char*>(fileInfo.data.first.get()), size);
      }
    } else {
      if (!fileInfo.file->m_TextureChunks.size()) {
        if (size == 0)
          size = fileInfo.file->m_UncompressedFileSize;
        fileInfo.data =
            std::make_pair(std::shared_ptr<unsigned char[]>(new unsigned char[size]),
                           static_cast<BSAULong>(size));
        m_File.read(reinterpret_cast<char*>(fileInfo.data.first.get()), size);
      } else {
        fileInfo.file->m_UncompressedFileSize = 0L;
        BSAULong totalSize                    = 0U;
        for (BSAUInt i = 0; i < fileInfo.file->m_TextureChunks.size(); ++i) {
          totalSize += fileInfo.file->m_TextureChunks[i].unpackedSize;
        }
        char* chunkData     = new char[totalSize];
        BSAULong currentPos = 0U;
        for (BSAUInt i = 0; i < fileInfo.file->m_TextureChunks.size(); ++i) {
          BSAULong length = fileInfo.file->m_TextureChunks[i].unpackedSize;
          if (fileInfo.file->m_TextureChunks[i].packedSize > 0) {
            char* chunk = new char[fileInfo.file->m_TextureChunks[i].packedSize];
            m_File.read(chunk, fileInfo.file->m_TextureChunks[i].packedSize);
            if (m_Type == TYPE_FALLOUT4 || m_Type == TYPE_STARFIELD ||
                m_Type == TYPE_FALLOUT4NG_7 || m_Type == TYPE_FALLOUT4NG_8) {
              EErrorCode result = ERROR_NONE;
              try {
                std::shared_ptr<unsigned char[]> unpackedChunk = decompress(
                    reinterpret_cast<unsigned char*>(chunk),
                    fileInfo.file->m_TextureChunks[i].packedSize, result, length);
                memcpy(chunkData + currentPos,
                       reinterpret_cast<char*>(unpackedChunk.get()), length);
                unpackedChunk.reset();
              } catch (const std::exception&) {
#pragma message("report error!")
                continue;
              }
            } else {
              char* unpackedChunk = new char[length];
              LZ4_decompress_safe(chunk, unpackedChunk,
                                  fileInfo.file->m_TextureChunks[i].packedSize, length);
              memcpy(chunkData + currentPos, unpackedChunk, length);
              delete[] unpackedChunk;
            }
            delete[] chunk;
            fileInfo.file->m_UncompressedFileSize += length;
          } else {
            char* chunk = new char[length];
            m_File.read(chunk, length);
            memcpy(chunkData + currentPos, chunk, length);
            delete[] chunk;
          }
          currentPos += length;
        }
        fileInfo.file->m_FileSize = 0;
        fileInfo.data             = std::make_pair(std::shared_ptr<unsigned char[]>(
                                           reinterpret_cast<unsigned char*>(chunkData)),
                                                   static_cast<BSAULong>(totalSize));
      }
    }

    {
      boost::interprocess::scoped_lock<boost::mutex> lock(mutex);
      queue.push(fileInfo);
    }
    bufferCount.post();
  }
}

inline bool fileExists(const std::string& name)
{
  struct stat buffer;
  return stat(name.c_str(), &buffer) != -1;
}

void Archive::extractFiles(const std::string& targetDirectory,
                           std::queue<FileInfo>& queue, boost::mutex& mutex,
                           boost::interprocess::interprocess_semaphore& bufferCount,
                           boost::interprocess::interprocess_semaphore& queueFree,
                           int totalFiles, bool overwrite, int& filesDone)
{
  for (int i = 0; i < totalFiles; ++i) {
    bufferCount.wait();
    if (boost::this_thread::interruption_requested()) {
      break;
    }

    FileInfo fileInfo;

    {
      boost::interprocess::scoped_lock<boost::mutex> lock(mutex);
      fileInfo = queue.front();
      ++filesDone;
      queue.pop();
    }
    queueFree.post();

    DataBuffer dataBuffer = fileInfo.data;

    std::string fileName = makeString("%s\\%s", targetDirectory.c_str(),
                                      fileInfo.file->getFilePath().c_str());
    if (!overwrite && fileExists(fileName)) {
      continue;
    }

    std::ofstream outputFile(fileName.c_str(),
                             fstream::out | fstream::binary | fstream::trunc);

    if (!outputFile.is_open()) {
#pragma message("report error!")
      continue;
      // return ERROR_ACCESSFAILED;
    }

    if (m_Type != TYPE_FALLOUT4 && m_Type != TYPE_STARFIELD &&
        m_Type != TYPE_STARFIELD_LZ4_TEXTURE && m_Type != TYPE_FALLOUT4NG_7 &&
        m_Type != TYPE_FALLOUT4NG_8) {
      // BSA extraction
      if (compressed(fileInfo.file)) {
        // Decompress data
        if (m_Type != TYPE_SKYRIMSE) {
          // Oblivion - Skyrim LE use gzip compression
          EErrorCode result = ERROR_NONE;
          try {
            BSAULong length = 0UL;
            std::shared_ptr<unsigned char[]> buffer =
                decompress(dataBuffer.first.get(), dataBuffer.second, result, length);
            if (buffer.get() != nullptr) {
              outputFile.write(reinterpret_cast<char*>(buffer.get()), length);
              buffer.reset();
            }
          } catch (const std::exception&) {
#pragma message("report error!")
            dataBuffer.first.reset();
            fileInfo.data.first.reset();
            continue;
          }
        } else {
          // Skyrim SE uses LZ4 Frame compression
          if (!fileInfo.file->m_TextureChunks.size()) {
            char* outBuffer = new char[fileInfo.file->m_UncompressedFileSize];

            LZ4F_decompressionContext_t dcContext = nullptr;
            LZ4F_decompressOptions_t options      = {};
            LZ4F_createDecompressionContext(&dcContext, LZ4F_VERSION);
            size_t outSize = fileInfo.file->m_UncompressedFileSize;
            size_t inSize  = dataBuffer.second;

            LZ4F_decompress(dcContext, outBuffer, &outSize, dataBuffer.first.get(),
                            &inSize, &options);
            outputFile.write(outBuffer, fileInfo.file->m_UncompressedFileSize);
            LZ4F_freeDecompressionContext(dcContext);
            delete[] outBuffer;
          }
        }
      } else {
        // No compression - just write the data.
        outputFile.write(reinterpret_cast<char*>(dataBuffer.first.get()),
                         dataBuffer.second);
      }
    } else {
      // BA2 format
      if (fileInfo.file->m_TextureChunks.size()) {
        // Texture stream format - requires building the header data for the DDS file
        bool isDX10                              = false;
        DirectX::DDS_HEADER_DXT10 DX10HeaderData = {};
        DirectX::DDS_HEADER DDSHeaderData =
            getDDSHeader(fileInfo.file, DX10HeaderData, isDX10);

        outputFile.write("DDS ", 4);
        char* DDSHeader = new char[sizeof(DDSHeaderData)];
        memcpy(DDSHeader, &DDSHeaderData, sizeof(DDSHeaderData));
        outputFile.write(DDSHeader, sizeof(DDSHeaderData));
        delete[] DDSHeader;

        if (isDX10) {
          // This format requires DX10 header info
          getDX10Header(DX10HeaderData, fileInfo.file, DDSHeaderData);

          char* DX10Header = new char[sizeof(DX10HeaderData)];
          memcpy(DX10Header, &DX10HeaderData, sizeof(DX10HeaderData));
          outputFile.write(DX10Header, sizeof(DX10HeaderData));
          delete[] DX10Header;
        }
      }

      EErrorCode result = ERROR_NONE;
      try {
        if (fileInfo.file->m_FileSize > 0 && !fileInfo.file->m_TextureChunks.size()) {
          BSAULong length = fileInfo.file->m_UncompressedFileSize;
          std::shared_ptr<unsigned char[]> buffer =
              decompress(dataBuffer.first.get(), dataBuffer.second, result, length);
          if (buffer.get() != nullptr) {
            outputFile.write(reinterpret_cast<char*>(buffer.get()), length);
            buffer.reset();
          }
        } else {
          outputFile.write(reinterpret_cast<char*>(dataBuffer.first.get()),
                           dataBuffer.second);
        }
      } catch (const std::exception&) {
#pragma message("report error!")
        dataBuffer.first.reset();
        fileInfo.data.first.reset();
        continue;
      }
    }
    dataBuffer.first.reset();
    fileInfo.data.first.reset();
  }
}

void Archive::createFolders(const std::string& targetDirectory, Folder::Ptr folder)
{
  for (std::vector<Folder::Ptr>::iterator iter = folder->m_SubFolders.begin();
       iter != folder->m_SubFolders.end(); ++iter) {
    std::string subDirName = targetDirectory + "/" + (*iter)->getName();
#ifdef _WIN32
    ::CreateDirectoryA(subDirName.c_str(), nullptr);
#else
    mkdir(subDirName.c_str(), 0755);
#endif
    createFolders(subDirName, *iter);
  }
}

EErrorCode Archive::extractAll(
    const char* outputDirectory,
    const std::function<bool(int value, std::string fileName)>& progress,
    bool overwrite)
{
#pragma message("report errors")
  createFolders(outputDirectory, m_RootFolder);

  std::vector<File::Ptr> fileList;
  m_RootFolder->collectFiles(fileList);
  std::sort(fileList.begin(), fileList.end(), ByOffset);
  m_File.seekg((*(fileList.begin()))->m_DataOffset);

  std::queue<FileInfo> buffers;
  boost::mutex queueMutex;
  int filesDone = 0;
  boost::interprocess::interprocess_semaphore bufferCount(0);
  boost::interprocess::interprocess_semaphore queueFree(100);

  boost::thread readerThread(boost::bind(&Archive::readFiles, this, boost::ref(buffers),
                                         boost::ref(queueMutex),
                                         boost::ref(bufferCount), boost::ref(queueFree),
                                         fileList.begin(), fileList.end()));

  boost::thread extractThread(boost::bind(
      &Archive::extractFiles, this, outputDirectory, boost::ref(buffers),
      boost::ref(queueMutex), boost::ref(bufferCount), boost::ref(queueFree),
      static_cast<int>(fileList.size()), overwrite, boost::ref(filesDone)));

  bool readerDone  = false;
  bool extractDone = false;
  bool canceled    = false;
  while (!readerDone || !extractDone) {
    if (!readerDone) {
      readerDone = readerThread.timed_join(boost::posix_time::millisec(100));
    }
    if (readerDone) {
      extractDone = extractThread.timed_join(boost::posix_time::millisec(100));
      // don't cancel extractor before reader is done or else reader may be stuck trying
      // to write to a queue
      if (canceled) {
        // ensure the extract thread wakes up.
        extractThread.interrupt();
        bufferCount.post();
      }
    }
    size_t index = (std::min)(static_cast<size_t>(filesDone), fileList.size() - 1);
    if (!progress((filesDone * 100) / static_cast<int>(fileList.size()),
                  fileList[index]->getName()) &&
        !canceled) {
      readerThread.interrupt();
      canceled = true;  // don't interrupt repeatedly
    }
  }

  return ERROR_NONE;
}

bool Archive::compressed(const File::Ptr& file) const
{
  if (m_Type != TYPE_FALLOUT4 && m_Type != TYPE_FALLOUT4NG_7 &&
      m_Type != TYPE_FALLOUT4NG_8)
    return file->compressToggled() ^ defaultCompressed();
  return (file->m_FileSize > 0);
}

File::Ptr Archive::createFile(const std::string& name, const std::string& sourceName,
                              bool compressed)
{
  return File::Ptr(
      new File(name, sourceName, nullptr, defaultCompressed() != compressed));
}

void Archive::cleanFolder(Folder::Ptr folder)
{
  std::vector<Folder::Ptr> folders;
  folder->collectFolders(folders);
  for (Folder::Ptr subFolder : folders) {
    cleanFolder(subFolder);
  }
  std::vector<File::Ptr> files;
  folder->collectFiles(files);
  for (File::Ptr file : files) {
    file.reset();
  }
  folder.reset();
}

}  // namespace BSA
