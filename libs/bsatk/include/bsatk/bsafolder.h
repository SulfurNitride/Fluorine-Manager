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

#ifndef BSAFOLDER_H
#define BSAFOLDER_H

#include "bsafile.h"
#include "bsatypes.h"
#include "errorcodes.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace BSA
{

class Folder
{

  friend class Archive;

public:
  typedef std::shared_ptr<Folder> Ptr;

public:
  /**
   * @return name of this folder
   */
  const std::string& getName() const { return m_Name; }
  /**
   * @return full path to this folder
   */
  std::string getFullPath() const;
  /**
   * @return the number of subfolders within this folder
   */
  unsigned int getNumSubFolders() const
  {
    return static_cast<unsigned int>(m_SubFolders.size());
  }
  /**
   * @param index index of a subfolder within this folder
   * @return a descriptor for the subfolder
   * @throw out_of_range this will throw an exception if the index is invalid
   */
  const Folder::Ptr getSubFolder(unsigned int index) const;
  /**
   * @return the number of files in this folder
   */
  unsigned int getNumFiles() const { return static_cast<unsigned int>(m_Files.size()); }
  /**
   * @return the number of files in this folder and subfolder
   */
  unsigned int countFiles() const;
  /**
   * @param index index of a file in this folder
   * @return a descriptor for the file
   * @throw out_of_range this will throw an exception if the index is invalid
   */
  const File::Ptr getFile(unsigned int index) const;
  /**
   * adds a new file to the folder
   * @param file the new file to add
   */
  void addFile(const File::Ptr& file) { m_Files.push_back(file); }
  /**
   * add an empty folder as a subfolder to this one.
   * @param folderName name of the new folder
   * @return pointer to the new folder
   * @note this folder will not be written to the bsa if it has no content
   */
  Folder::Ptr addFolder(const std::string& folderName);

private:
  /**
   * construct a new, empty, folder
   */
  Folder();

  // copy constructor not implemented
  Folder(const Folder& reference);

  // assignment operator - not implemented
  Folder& operator=(const Folder& reference);

  /**
   * factory function to read a folder object from disc. This also reads part of the
   * information about the files within
   * @param file file stream to read from, already placed at the correct position
   * @param fileNamesLength length of the file names list. This is required to correctly
   * calculate offsets
   * @param endPos position inside file where the last folder header ends. This is
   *               updated by the constructor so that it is the correct value after all
   * folders are read
   * @return the new Folder object
   */
  Folder::Ptr readFolder(std::fstream& file, BSAUInt fileNamesLength, BSAUInt& endPos);

  Folder::Ptr readFolderSE(std::fstream& file, BSAUInt fileNamesLength,
                           BSAUInt& endPos);

  /**
   * recursive function to determine the correct subfolder to place the new
   * folder in
   */
  void addFolderInt(Folder::Ptr folder);

  /**
   * recursive function that returns an existing folder match or generates the structure
   * for a new folder
   * @param folder folder to find or create
   * @return the final determined / generated Folder
   */
  Folder::Ptr addOrFindFolderInt(Folder* folder);

  /**
   * Add a new folder to the structure.
   * It will automatically be added to the correct sub-folder if applicable.
   */
  Folder::Ptr addFolder(std::fstream& file, BSAUInt fileNamesLength, BSAUInt& endPos,
                        ArchiveType type);

  Folder::Ptr addFolderFromFile(std::string filePath, BSAUInt size, BSAHash offset,
                                BSAUInt uncompressedSize, FO4TextureHeader header,
                                std::vector<FO4TextureChunk>& texChunks);

  bool resolveFileNames(std::fstream& file, bool testHashes);

  void writeHeader(std::fstream& file) const;
  void writeData(std::fstream& file, BSAULong fileNamesLength) const;
  EErrorCode writeFileData(std::fstream& sourceFile, std::fstream& targetFile) const;
  void collectFolders(std::vector<Folder::Ptr>& folderList) const;
  void collectFiles(std::vector<File::Ptr>& fileList) const;
  void collectFileNames(std::vector<std::string>& nameList) const;
  void collectFolderNames(std::vector<std::string>& nameList) const;

private:
  Folder* m_Parent;
  BSAHash m_NameHash;
  std::string m_Name;
  BSAULong m_FileCount;
  BSAHash m_Offset;
  std::vector<Folder::Ptr> m_SubFolders;
  std::unordered_map<std::string, Folder::Ptr> m_SubFoldersByName;
  std::vector<File::Ptr> m_Files;

  mutable BSAULong m_OffsetWrite;
};

}  // namespace BSA

#endif /* BSAFOLDER_H */
