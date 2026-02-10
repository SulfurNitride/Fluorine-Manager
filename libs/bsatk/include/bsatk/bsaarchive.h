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

#ifndef BSA_ARCHIVE_H
#define BSA_ARCHIVE_H

#include <functional>
#include <memory>
#include <queue>
#include <vector>

#include <dxgiformat.h>

#include <DDS.h>

#include "bsafolder.h"
#include "bsatypes.h"
#include "errorcodes.h"
namespace boost
{
class mutex;
namespace interprocess
{
  class interprocess_semaphore;
}
}  // namespace boost

namespace BSA
{

class File;

/**
 * @brief top level structure to represent a bsa file
 */
class Archive
{
public:
  using DataBuffer = std::pair<std::shared_ptr<unsigned char[]>, BSAULong>;

private:
  static const unsigned int FLAG_HASDIRNAMES       = 0x00000001;
  static const unsigned int FLAG_HASFILENAMES      = 0x00000002;
  static const unsigned int FLAG_DEFAULTCOMPRESSED = 0x00000004;
  static const unsigned int FLAG_NAMEPREFIXED =
      0x00000100;  // if set, the full file name is prefixed before a data block

  /* Record flags */
#define OB_BSAFILE_FLAG_COMPRESS                                                       \
  0xC0000000  //!< Bit mask with OBBSAFileInfo::sizeFlags to get the compression status

public:
  /**
   * constructor
   */
  Archive();
  ~Archive();
  /**
   * read the archive from file
   * @param fileName name of the file to read from
   * @param testHashes if true, the hashes of file names will be checked to ensure the
   * file is valid. This can be skipped for performance reasons
   * @return ERROR_NONE on success or an error code
   */
  EErrorCode read(const char* fileName, bool testHashes);
  /**
   * write the archive to disc
   * @param fileName name of the file to write to
   * @return ERROR_NONE on success or an error code
   */
  EErrorCode write(const char* fileName);
  /**
   * @brief close the archive
   */
  void close();
  /**
   * change the archive type
   * @param type new archive type
   */
  void setType(ArchiveType type) { m_Type = type; }
  /**
   * @return type of the archive (supported game)
   */
  ArchiveType getType() const { return m_Type; }
  /**
   * retrieve top-level folder
   * @return descriptor of the root folder
   */
  Folder::Ptr getRoot() { return m_RootFolder; }
  /**
   * extract a file from the archive
   * @param file descriptor of the file to extract
   * @param outputDirectory name of the directory to extract to.
   *                        may be absolute or relative
   * @return ERROR_NONE on success or an error code
   */
  EErrorCode extract(File::Ptr file, const char* outputDirectory) const;
  /**
   * @return archive flags
   */
  BSAULong getFlags() const { return m_ArchiveFlags; }

  /**
   * extract all files. this is potentially faster than iterating over all files and
   * extracting each
   * @param outputDirectory name of the directory to extract to.
   *                        may be absolute or relative
   * @param progress callback function called on progress
   * @param overwrite if true (default) files are overwritten if they exist
   * @return ERROR_NONE on success or an error code
   */
  EErrorCode
  extractAll(const char* outputDirectory,
             const std::function<bool(int value, std::string fileName)>& progress,
             bool overwrite = true);

  /**
   * @param file the file to check
   * @return true if the file is compressed, false otherwise
   */
  bool compressed(const File::Ptr& file) const;
  /**
   * create a new file to be placed in this archive. The new file is NOT
   * added to a folder, use BSA::Folder::addFile for that
   * @param name name of the file to be used inside the archive
   * @param sourceName filename path to the file to add
   * @param compressed true if the file should be compressed (not supported yet!)
   * @return pointer to the new file
   */
  File::Ptr createFile(const std::string& name, const std::string& sourceName,
                       bool compressed);

private:
  struct Header
  {
    uint32_t fileIdentifier;
    char archType[5];
    ArchiveType type;
    BSAUInt offset;
    BSAUInt archiveFlags;
    BSAUInt folderCount;
    BSAUInt fileCount;
    BSAUInt folderNameLength;
    BSAUInt fileNameLength;
    BSAUInt fileFlags;
    BSAHash nameTableOffset;
  };

  struct FileInfo
  {
    File::Ptr file;
    DataBuffer data;
  };

private:
  static Header readHeader(std::fstream& infile);

  static ArchiveType typeFromID(BSAULong typeID);

  static std::shared_ptr<unsigned char[]> decompress(unsigned char* inBuffer,
                                                     BSAULong inSize,
                                                     EErrorCode& result,
                                                     BSAULong& outSize);

  BSAULong typeToID(ArchiveType type);

  Folder readFolderRecord(std::fstream& file);

  //  EErrorCode extractDirect(const File &fileInfo, std::ofstream &outFile);
  //  EErrorCode extractCompressed(const File &fileInfo, std::ofstream &outFile);

  bool defaultCompressed() const { return m_ArchiveFlags & FLAG_DEFAULTCOMPRESSED; }
  // starting with FO3 the bsa may prefix the file name to the file blob if archive flag
  // 0x100 is set
  bool namePrefixed() const
  {
    return (m_Type != TYPE_OBLIVION) && ((m_ArchiveFlags & FLAG_NAMEPREFIXED) != 0);
  }

  BSAULong countFiles() const;

  std::vector<std::string> collectFolderNames() const;
  std::vector<std::string> collectFileNames() const;

  BSAULong countCharacters(const std::vector<std::string>& list) const;
  BSAULong determineFileFlags(const std::vector<std::string>& fileList) const;

  void writeHeader(std::fstream& outfile, BSAULong fileFlags, BSAULong numFolders,
                   BSAULong folderNamesLength, BSAULong fileNamesLength);

  DirectX::DDS_HEADER getDDSHeader(File::Ptr file,
                                   DirectX::DDS_HEADER_DXT10& DX10Header,
                                   bool& isDX10) const;
  void getDX10Header(DirectX::DDS_HEADER_DXT10& DX10Header, File::Ptr file,
                     DirectX::DDS_HEADER DDSHeader) const;

  EErrorCode extractDirect(File::Ptr file, std::ofstream& outFile) const;
  EErrorCode extractCompressed(File::Ptr file, std::ofstream& outFile) const;

  void createFolders(const std::string& targetDirectory, Folder::Ptr folder);

  void readFiles(std::queue<FileInfo>& queue, boost::mutex& mutex,
                 boost::interprocess::interprocess_semaphore& bufferCount,
                 boost::interprocess::interprocess_semaphore& queueFree,
                 std::vector<File::Ptr>::iterator begin,
                 std::vector<File::Ptr>::iterator end);

  void extractFiles(const std::string& targetDirectory, std::queue<FileInfo>& queue,
                    boost::mutex& mutex,
                    boost::interprocess::interprocess_semaphore& bufferCount,
                    boost::interprocess::interprocess_semaphore& queueFree,
                    int totalFiles, bool overwrite, int& filesDone);

  void cleanFolder(Folder::Ptr folder);

private:
  mutable std::fstream m_File;

  Folder::Ptr m_RootFolder;

  BSAULong m_ArchiveFlags;
  ArchiveType m_Type;
};

}  // namespace BSA

#endif  // BSA_ARCHIVE_H
