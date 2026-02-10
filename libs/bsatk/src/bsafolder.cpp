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

#include <filesystem>
#include <limits.h>

#include "bsaarchive.h"
#include "bsaexception.h"
#include "bsafile.h"
#include "bsafolder.h"

using std::fstream;

namespace BSA
{

Folder::Folder() : m_Parent(nullptr), m_Name()
{
  m_NameHash  = calculateBSAHash(m_Name);
  m_FileCount = 0;
  m_Offset    = ULONG_MAX;
}

Folder::Ptr Folder::readFolder(std::fstream& file, BSAUInt fileNamesLength,
                               BSAUInt& endPos)
{
  Folder::Ptr result(new Folder());
  result->m_NameHash  = readType<BSAHash>(file);
  result->m_FileCount = readType<unsigned long>(file);
  result->m_Offset    = readType<unsigned long>(file);
  std::streamoff pos  = file.tellg();

  file.seekg(result->m_Offset - fileNamesLength, fstream::beg);

  result->m_Name = readBString(file);

  for (unsigned long i = 0UL; i < result->m_FileCount; ++i) {
    result->m_Files.push_back(File::Ptr(new File(file, result.get())));
  }

  if (static_cast<unsigned long>(file.tellg()) > endPos) {
    endPos = static_cast<BSAULong>(file.tellg());
  }

  file.seekg(pos);

  return result;
}

Folder::Ptr Folder::readFolderSE(std::fstream& file, BSAUInt fileNamesLength,
                                 BSAUInt& endPos)
{
  Folder::Ptr result(new Folder());
  result->m_NameHash  = readType<BSAHash>(file);
  result->m_FileCount = readType<BSAUInt>(file);
  readType<BSAUInt>(file);
  result->m_Offset   = readType<BSAHash>(file);
  std::streamoff pos = file.tellg();

  file.seekg(result->m_Offset - fileNamesLength, fstream::beg);

  result->m_Name = readBString(file);

  for (unsigned long i = 0UL; i < result->m_FileCount; ++i) {
    result->m_Files.push_back(File::Ptr(new File(file, result.get())));
  }

  if (static_cast<unsigned long>(file.tellg()) > endPos) {
    endPos = static_cast<BSAULong>(file.tellg());
  }

  file.seekg(pos);

  return result;
}

void Folder::writeHeader(std::fstream& file) const
{
  writeType<BSAHash>(file, m_NameHash);
  writeType<BSAULong>(file, static_cast<BSAULong>(m_Files.size()));
  writeType<BSAULong>(file, m_OffsetWrite);
}

void Folder::writeData(std::fstream& file, BSAULong fileNamesLength) const
{
  m_OffsetWrite = static_cast<BSAULong>(file.tellp()) + fileNamesLength;
  writeBString(file, getFullPath());
  for (std::vector<File::Ptr>::const_iterator iter = m_Files.begin();
       iter != m_Files.end(); ++iter) {
    (*iter)->writeHeader(file);
  }
}

EErrorCode Folder::writeFileData(std::fstream& sourceFile,
                                 std::fstream& targetFile) const
{
  for (std::vector<File::Ptr>::const_iterator iter = m_Files.begin();
       iter != m_Files.end(); ++iter) {
    EErrorCode error = (*iter)->writeData(sourceFile, targetFile);
    if (error != ERROR_NONE) {
      return error;
    }
  }
  return ERROR_NONE;
}

std::string Folder::getFullPath() const
{
  if (m_Parent != nullptr) {
    std::string temp = m_Parent->getFullPath();
    if (temp.length() != 0) {
      return temp.append("\\").append(m_Name);
    } else {
      return m_Name;
    }
  } else {
    // root folder shouldn't have a name
    return std::string();
  }
}

void Folder::addFolderInt(Folder::Ptr folder)
{
  std::filesystem::path path(folder->m_Name);
  auto it              = path.begin();
  std::string firstStr = it->string();
  std::filesystem::path remaining;
  for (++it; it != path.end(); ++it) {
    remaining /= *it;
  }

  if (m_SubFoldersByName.contains(firstStr)) {
    // remove the matched part of the path and recurse
    folder->m_Name = remaining.string();
    m_SubFoldersByName.at(firstStr)->addFolderInt(folder);
    return;
  }

  // no subfolder matches, create one
  if (remaining.empty()) {
    // no more path components, add the new folder right here
    folder->m_Parent = this;
    m_SubFolders.push_back(folder);
    m_SubFoldersByName[firstStr] = folder;
  } else {
    // add dummy folder for the next path component
    Folder::Ptr dummy(new Folder);
    dummy->m_Parent = this;
    dummy->m_Name   = firstStr;
    folder->m_Name  = remaining.string();
    dummy->addFolderInt(folder);
    m_SubFolders.push_back(dummy);
    m_SubFoldersByName[firstStr] = dummy;
  }
}

Folder::Ptr Folder::addOrFindFolderInt(Folder* folder)
{
  std::filesystem::path path(folder->m_Name);
  auto it              = path.begin();
  std::string firstStr = it->string();
  std::filesystem::path remaining;
  for (++it; it != path.end(); ++it) {
    remaining /= *it;
  }

  if (m_SubFoldersByName.contains(firstStr)) {
    if (!remaining.empty()) {
      // remove the matched part of the path and recurse
      folder->m_Name = remaining.string();
      return m_SubFoldersByName.at(firstStr)->addOrFindFolderInt(folder);
    } else {
      return m_SubFoldersByName.at(firstStr);
    }
  }

  // no subfolder matches, create one
  if (remaining.empty()) {
    // no more path components, add the new folder right here
    folder->m_Parent = this;
    m_SubFolders.push_back(Folder::Ptr(folder));
    m_SubFoldersByName[firstStr] = m_SubFolders.back();
    return m_SubFolders.back();
  } else {
    // add dummy folder for the next path component
    Folder::Ptr dummy(new Folder);
    dummy->m_Parent    = this;
    dummy->m_Name      = firstStr;
    folder->m_Name     = remaining.string();
    Folder::Ptr result = dummy->addOrFindFolderInt(folder);
    m_SubFolders.push_back(dummy);
    m_SubFoldersByName[firstStr] = dummy;
    return result;
  }
}

Folder::Ptr Folder::addFolder(std::fstream& file, BSAUInt fileNamesLength,
                              BSAUInt& endPos, ArchiveType type)
{
  Folder::Ptr temp;
  if (type == ArchiveType::TYPE_SKYRIMSE)
    temp = readFolderSE(file, fileNamesLength, endPos);
  else
    temp = readFolder(file, fileNamesLength, endPos);
  addFolderInt(temp);

  return temp;
}

Folder::Ptr Folder::addFolderFromFile(std::string filePath, BSAUInt size,
                                      BSAHash offset, BSAUInt uncompressedSize,
                                      FO4TextureHeader header,
                                      std::vector<FO4TextureChunk>& texChunks)
{
  std::filesystem::path file(filePath);

  Folder* tempFolder = new Folder();

  tempFolder->m_NameHash = calculateBSAHash(filePath);
  tempFolder->m_Name     = file.parent_path().string();
  Folder::Ptr result     = addOrFindFolderInt(tempFolder);

  if (result.get() != tempFolder) {
    delete tempFolder;
  }

  std::string fileName = file.filename().string();

  result->m_FileCount++;
  result->m_Files.push_back(File::Ptr(new File(fileName, result.get(), size, offset,
                                               uncompressedSize, header, texChunks)));

  return result;
}

bool Folder::resolveFileNames(std::fstream& file, bool testHashes)
{
  bool hashesValid = true;
  for (std::vector<File::Ptr>::iterator iter = m_Files.begin(); iter != m_Files.end();
       ++iter) {
    try {
      (*iter)->readFileName(file, testHashes);
    } catch (const std::exception&) {
      hashesValid = false;
    }
  }
  return hashesValid;
}

const Folder::Ptr Folder::getSubFolder(unsigned int index) const
{
  return m_SubFolders.at(index);
}

unsigned int Folder::countFiles() const
{
  unsigned int result = 0;
  for (std::vector<Folder::Ptr>::const_iterator iter = m_SubFolders.begin();
       iter != m_SubFolders.end(); ++iter) {
    result += (*iter)->countFiles();
  }
  return result + static_cast<unsigned int>(m_Files.size());
}

const File::Ptr Folder::getFile(unsigned int index) const
{
  return m_Files.at(index);
}

Folder::Ptr Folder::addFolder(const std::string& folderName)
{
  Folder::Ptr newFolder(new Folder);
  newFolder->m_Name   = folderName;
  newFolder->m_Parent = this;
  m_SubFolders.push_back(newFolder);
  return newFolder;
}

void Folder::collectFolders(std::vector<Folder::Ptr>& folderList) const
{
  for (std::vector<Folder::Ptr>::const_iterator iter = m_SubFolders.begin();
       iter != m_SubFolders.end(); ++iter) {
    if ((*iter)->m_Files.size() != 0) {
      folderList.push_back(*iter);
    }
    (*iter)->collectFolders(folderList);
  }
}

void Folder::collectFiles(std::vector<File::Ptr>& fileList) const
{
  for (std::vector<File::Ptr>::const_iterator fileIter = m_Files.begin();
       fileIter != m_Files.end(); ++fileIter) {
    fileList.push_back(*fileIter);
  }
  for (std::vector<Folder::Ptr>::const_iterator folderIter = m_SubFolders.begin();
       folderIter != m_SubFolders.end(); ++folderIter) {
    (*folderIter)->collectFiles(fileList);
  }
}

void Folder::collectFileNames(std::vector<std::string>& nameList) const
{
  for (std::vector<File::Ptr>::const_iterator iter = m_Files.begin();
       iter != m_Files.end(); ++iter) {
    nameList.push_back((*iter)->getName());
  }
  for (std::vector<Folder::Ptr>::const_iterator iter = m_SubFolders.begin();
       iter != m_SubFolders.end(); ++iter) {
    (*iter)->collectFileNames(nameList);
  }
}

void Folder::collectFolderNames(std::vector<std::string>& nameList) const
{
  if (m_Files.size() != 0) {
    nameList.push_back(getFullPath());
  }
  for (std::vector<Folder::Ptr>::const_iterator iter = m_SubFolders.begin();
       iter != m_SubFolders.end(); ++iter) {
    (*iter)->collectFolderNames(nameList);
  }
}

}  // namespace BSA
