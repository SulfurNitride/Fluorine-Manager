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

#ifndef ARCHIVE_H
#define ARCHIVE_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#if defined(MO2_ARCHIVE_BUILD_STATIC)
#define DLLEXPORT
#elif defined(_WIN32)
  #if defined(MO2_ARCHIVE_BUILD_EXPORT)
  #define DLLEXPORT __declspec(dllexport)
  #else
  #define DLLEXPORT __declspec(dllimport)
  #endif
#else
  #if defined(MO2_ARCHIVE_BUILD_EXPORT)
  #define DLLEXPORT __attribute__((visibility("default")))
  #else
  #define DLLEXPORT
  #endif
#endif

class FileData
{
public:
  /**
   * @return the path of this entry in the archive (usually relative, unless the archive
   *   contains absolute path).
   */
  virtual std::wstring getArchiveFilePath() const = 0;

  /**
   * @return the size of this entry in bytes (uncompressed).
   */
  virtual uint64_t getSize() const = 0;

  /**
   * @brief Add the given filepath to the list of files to create from this
   *   entry when extracting.
   *
   * @param filepath The filepath to add, relative to the output folder.
   */
  virtual void addOutputFilePath(std::wstring const& filepath) = 0;

  /**
   * @brief Retrieve the list of filepaths to extract this entry to.
   *
   * @return the list of paths this entry should be extracted to, relative to the
   *   output folder.
   */
  virtual const std::vector<std::wstring>& getOutputFilePaths() const = 0;

  /**
   * @brief Clear the list of output file paths for this entry.
   */
  virtual void clearOutputFilePaths() = 0;

  /**
   * @return the CRC of this file.
   */
  virtual uint64_t getCRC() const = 0;

  /**
   * @return true if this entry is a directory, false otherwize.
   */
  virtual bool isDirectory() const = 0;

  virtual ~FileData() = default;
};

class Archive
{
public:  // Declarations
  enum class LogLevel
  {
    Debug,
    Info,
    Warning,
    Error
  };

  enum class ProgressType
  {

    // Indicates the 7z progression in the archive (related to reading the archive. When
    // extracting
    // a lot of files, this may reach 100% way before the extraction is complete since
    // most of the
    // time will be spend writing data and not reading it (use EXTRACTION in this case).
    // When
    // extracting few small files,  this may be useful for solid archives since most of
    // the time
    // will be spent in reading and decompressing the archive rather than in writing the
    // actual files.
    ARCHIVE,

    // Progress about extraction. If this reach 100%, it means that the extraction of
    // all files is
    // complete. The EXTRACTION progress may not start immediately, and might be kind of
    // chaotic when
    // extracting few files from an archive, but is much more representative of the
    // actual progress
    // than ARCHIVE.
    EXTRACTION
  };

  enum class FileChangeType
  {
    EXTRACTION_START,
    EXTRACTION_END
  };

  static constexpr int MAX_PASSWORD_LENGTH = 256;

  /**
   * List of callbacks:
   */
  using LogCallback        = std::function<void(LogLevel, std::wstring const& log)>;
  using ProgressCallback   = std::function<void(ProgressType, uint64_t, uint64_t)>;
  using PasswordCallback   = std::function<std::wstring()>;
  using FileChangeCallback = std::function<void(FileChangeType, std::wstring const&)>;
  using ErrorCallback      = std::function<void(std::wstring const&)>;

  /**
   *
   */
  enum class Error
  {
    ERROR_NONE,
    ERROR_EXTRACT_CANCELLED,
    ERROR_LIBRARY_NOT_FOUND,
    ERROR_LIBRARY_INVALID,
    ERROR_ARCHIVE_NOT_FOUND,
    ERROR_FAILED_TO_OPEN_ARCHIVE,
    ERROR_INVALID_ARCHIVE_FORMAT,
    ERROR_LIBRARY_ERROR,
    ERROR_ARCHIVE_INVALID,
    ERROR_OUT_OF_MEMORY
  };

public:  // Special member functions:
  virtual ~Archive() {}

public:
  /**
   * @brief Check if this Archive wrapper is in a valid state.
   *
   * A non-valid Archive instance usually means that the 7z DLLs could not be loaded
   * properly. Failures to open or extract archives do not invalidate the Archive, so
   * this should only be used to check if the Archive object has been initialized
   * properly.
   *
   * @return true if this instance is valid, false otherwise.
   */
  virtual bool isValid() const = 0;

  /**
   * @return retrieve the error-code of the last error that occurred.
   */
  virtual Error getLastError() const = 0;

  /**
   * @brief Set the callback used to log messages.
   *
   * To remove the callback, you can pass a default-constructed LogCallback object.
   *
   * @param logCallback The new callback to use for logging message.
   */
  virtual void setLogCallback(LogCallback logCallback) = 0;

  /**
   * @brief Open the given archive.
   *
   * @param archivePath Path to the archive to open.
   * @param passwordCallback Callback to use to ask user for password. This callback
   * must remain valid until extraction is complete since some types of archives only
   * requires password when extracting.
   *
   * @return true if the archive was open properly, false otherwise.
   */
  virtual bool open(std::wstring const& archivePath,
                    PasswordCallback passwordCallback) = 0;

  /**
   * @brief Close the currently opened archive.
   */
  virtual void close() = 0;

  /**
   * @return the list of files in the currently opened archive.
   */
  virtual const std::vector<FileData*>& getFileList() const = 0;

  /**
   * @brief Extract the content of the archive.
   *
   * This function uses the filenames from FileData to obtain the extraction paths of
   * file. All the callbacks are optional (you can specify default-constructed
   * std::function). Overloads with one or two callbacks are also provided.
   *
   * @param outputDirectory Path to the directory where the archive should be extracted.
   * If not empty, conflicting files will be replaced by the extracted ones.
   * @param progressCallback Function called to notify extraction progress.
   * @param fileChangeCallback Function called when the file currently being extracted
   * changes.
   * @param errorCallback Function called when an error occurs.
   *
   * @return true if the archive was extracted, false otherwise.
   */
  virtual bool extract(std::wstring const& outputDirectory,
                       ProgressCallback progressCallback,
                       FileChangeCallback fileChangeCallback,
                       ErrorCallback errorCallback) = 0;

  /**
   * @brief Cancel the current extraction process.
   */
  virtual void cancel() = 0;

  // A bunch of useful overloads (with one or two callbacks):
  bool extract(std::wstring const& outputDirectory, ErrorCallback errorCallback)
  {
    return extract(outputDirectory, {}, {}, errorCallback);
  }
  bool extract(std::wstring const& outputDirectory, ProgressCallback progressCallback)
  {
    return extract(outputDirectory, progressCallback, {}, {});
  }
  bool extract(std::wstring const& outputDirectory,
               FileChangeCallback fileChangeCallback)
  {
    return extract(outputDirectory, {}, fileChangeCallback, {});
  }
  bool extract(std::wstring const& outputDirectory, ProgressCallback progressCallback,
               ErrorCallback errorCallback)
  {
    return extract(outputDirectory, progressCallback, {}, errorCallback);
  }
  bool extract(std::wstring const& outputDirectory, ProgressCallback progressCallback,
               FileChangeCallback fileChangeCallback)
  {
    return extract(outputDirectory, progressCallback, fileChangeCallback, {});
  }
  bool extract(std::wstring const& outputDirectory,
               FileChangeCallback fileChangeCallback, ErrorCallback errorCallback)
  {
    return extract(outputDirectory, {}, fileChangeCallback, errorCallback);
  }
};

/**
 * @brief Factory function for archive-objects.
 *
 * @return a pointer to a new Archive object that can be used to manipulate archives.
 */
DLLEXPORT std::unique_ptr<Archive> CreateArchive();

#endif  // ARCHIVE_H
