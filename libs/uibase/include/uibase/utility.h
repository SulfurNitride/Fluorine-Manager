/*
Mod Organizer shared UI functionality

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

#ifndef MO_UIBASE_UTILITY_INCLUDED
#define MO_UIBASE_UTILITY_INCLUDED

#include <QDir>
#include <QIcon>
#include <QList>
#include <QString>
#include <QTextStream>
#include <QUrl>
#include <QVariant>
#include <algorithm>
#include <set>
#include <vector>
#include <cstdint>
#include <fstream>

#ifdef _WIN32
#include <ShlObj.h>
#include <Windows.h>
#else
// POSIX compatibility types
#include "windows_compat.h"
#endif

#include "dllimport.h"
#include "exceptions.h"

namespace MOBase
{

/**
 * @brief remove the specified directory including all sub-directories
 *
 * @param dirName name of the directory to delete
 * @return true on success. in case of an error, "removeDir" itself displays an error
 *message
 **/
QDLLEXPORT bool removeDir(const QString& dirName);

/**
 * @brief copy a directory recursively
 * @param sourceName name of the directory to copy
 * @param destinationName name of the target directory
 * @param merge if true, the destination directory is allowed to exist, files will then
 *              be added to that directory. If false, the call will fail in that case
 * @return true if files were copied. This doesn't necessary mean ALL files were copied
 * @note symbolic links are not followed to prevent endless recursion
 */
QDLLEXPORT bool copyDir(const QString& sourceName, const QString& destinationName,
                        bool merge);

/**
 * @brief move a file, creating subdirectories as needed
 * @param source source file name
 * @param destination destination file name
 * @return true if the file was successfully copied
 */
QDLLEXPORT bool moveFileRecursive(const QString& source, const QString& baseDir,
                                  const QString& destination);

/**
 * @brief copy a file, creating subdirectories as needed
 * @param source source file name
 * @param destination destination file name
 * @return true if the file was successfully copied
 */
QDLLEXPORT bool copyFileRecursive(const QString& source, const QString& baseDir,
                                  const QString& destination);

/**
 * @brief copy one or multiple files using a shell operation (this will ask the user for
 *confirmation on overwrite or elevation requirement)
 * @param sourceNames names of files to be copied. This can include wildcards
 * @param destinationNames names of the files in the destination location or the
 *destination directory to copy to. There has to be one destination name for each source
 *name or a single directory
 * @param dialog a dialog to be the parent of possible confirmation dialogs
 * @return true on success, false on error
 **/
QDLLEXPORT bool shellCopy(const QStringList& sourceNames,
                          const QStringList& destinationNames,
                          QWidget* dialog = nullptr);

QDLLEXPORT bool shellCopy(const QString& sourceNames, const QString& destinationNames,
                          bool yesToAll = false, QWidget* dialog = nullptr);

QDLLEXPORT bool shellMove(const QStringList& sourceNames,
                          const QStringList& destinationNames,
                          QWidget* dialog = nullptr);

QDLLEXPORT bool shellMove(const QString& sourceNames, const QString& destinationNames,
                          bool yesToAll = false, QWidget* dialog = nullptr);

QDLLEXPORT bool shellRename(const QString& oldName, const QString& newName,
                            bool yesToAll = false, QWidget* dialog = nullptr);

QDLLEXPORT bool shellDelete(const QStringList& fileNames, bool recycle = false,
                            QWidget* dialog = nullptr);

QDLLEXPORT bool shellDeleteQuiet(const QString& fileName, QWidget* dialog = nullptr);

namespace shell
{
  namespace details
  {
    // used by HandlePtr on Windows, stub on Linux
    struct HandleCloser
    {
      using pointer = HANDLE;

      void operator()(HANDLE h)
      {
#ifdef _WIN32
        if (h != INVALID_HANDLE_VALUE) {
          ::CloseHandle(h);
        }
#else
        (void)h;
#endif
      }
    };

    using HandlePtr = std::unique_ptr<HANDLE, HandleCloser>;
  }  // namespace details

  // returned by the various shell functions
  class QDLLEXPORT Result
  {
  public:
    Result(bool success, DWORD error, QString message, HANDLE process);

    // non-copyable
    Result(const Result&)            = delete;
    Result& operator=(const Result&) = delete;
    Result(Result&&)                 = default;
    Result& operator=(Result&&)      = default;

    static Result makeFailure(DWORD error, QString message = {});
    static Result makeSuccess(HANDLE process = INVALID_HANDLE_VALUE);

    bool success() const;
    explicit operator bool() const;

    DWORD error();

    const QString& message() const;

    HANDLE processHandle() const;

    HANDLE stealProcessHandle();

    QString toString() const;

  private:
    bool m_success;
    DWORD m_error;
    QString m_message;
    details::HandlePtr m_process;
  };

  QDLLEXPORT QString formatError(int i);

  QDLLEXPORT Result Explore(const QFileInfo& info);
  QDLLEXPORT Result Explore(const QString& path);
  QDLLEXPORT Result Explore(const QDir& dir);

  QDLLEXPORT Result Open(const QString& path);
  QDLLEXPORT Result Open(const QUrl& url);

  QDLLEXPORT Result Execute(const QString& program, const QString& params = {});

  QDLLEXPORT Result Delete(const QFileInfo& path);

  QDLLEXPORT Result Rename(const QFileInfo& src, const QFileInfo& dest);
  QDLLEXPORT Result Rename(const QFileInfo& src, const QFileInfo& dest,
                           bool copyAllowed);

  QDLLEXPORT Result CreateDirectories(const QDir& dir);
  QDLLEXPORT Result DeleteDirectoryRecursive(const QDir& dir);

  QDLLEXPORT void SetUrlHandler(const QString& cmd);
}  // namespace shell

template <typename T>
QString VectorJoin(const std::vector<T>& value, const QString& separator,
                   size_t maximum = UINT_MAX)
{
  QString result;
  if (value.size() != 0) {
    QTextStream stream(&result);
    stream << value[0];
    for (unsigned int i = 1; i < (std::min)(value.size(), maximum); ++i) {
      stream << separator << value[i];
    }
    if (maximum < value.size()) {
      stream << separator << "...";
    }
  }
  return result;
}

template <typename T>
QString SetJoin(const std::set<T>& value, const QString& separator,
                size_t maximum = UINT_MAX)
{
  QString result;
  typename std::set<T>::const_iterator iter = value.begin();
  if (iter != value.end()) {
    QTextStream stream(&result);
    stream << *iter;
    ++iter;
    unsigned int pos = 1;
    for (; iter != value.end() && pos < maximum; ++iter) {
      stream << separator << *iter;
    }
    if (maximum < value.size()) {
      stream << separator << "...";
    }
  }
  return result;
}

template <typename T>
QList<T> ConvertList(const QVariantList& variants)
{
  QList<T> result;
  for (const QVariant& var : variants) {
    if (!var.canConvert<T>()) {
      throw Exception("invalid variant type");
    }
    result.append(var.value<T>());
  }
}

QDLLEXPORT std::wstring ToWString(const QString& source);
QDLLEXPORT std::string ToString(const QString& source, bool utf8 = true);
QDLLEXPORT QString ToQString(const std::string& source);
QDLLEXPORT QString ToQString(const std::wstring& source);

#ifdef _WIN32
QDLLEXPORT QString ToString(const SYSTEMTIME& time);
#endif

QDLLEXPORT int naturalCompare(const QString& a, const QString& b,
                              Qt::CaseSensitivity cs = Qt::CaseInsensitive);

class QDLLEXPORT NaturalSort
{
public:
  NaturalSort(Qt::CaseSensitivity cs = Qt::CaseInsensitive) : m_cs(cs) {}

  bool operator()(const QString& a, const QString& b)
  {
    return (naturalCompare(a, b, m_cs) < 0);
  }

private:
  Qt::CaseSensitivity m_cs;
};

#ifdef _WIN32
QDLLEXPORT QDir getKnownFolder(KNOWNFOLDERID id, const QString& what = {});
QDLLEXPORT QString getOptionalKnownFolder(KNOWNFOLDERID id);
#endif

QDLLEXPORT QString getDesktopDirectory();
QDLLEXPORT QString getStartMenuDirectory();

QDLLEXPORT QString readFileText(const QString& fileName, QString* encoding = nullptr,
                                bool* hadBOM = nullptr);

QDLLEXPORT QString decodeTextData(const QByteArray& fileData,
                                  QString* encoding = nullptr, bool* hadBOM = nullptr);

QDLLEXPORT void removeOldFiles(const QString& path, const QString& pattern,
                               int numToKeep, QDir::SortFlags sorting = QDir::Time);

QDLLEXPORT QIcon iconForExecutable(const QString& filePath);

QDLLEXPORT QString getFileVersion(QString const& filepath);
QDLLEXPORT QString getProductVersion(QString const& program);

QDLLEXPORT bool isWindowsDrivePath(const QString& path);
QDLLEXPORT bool isWineZDrivePath(const QString& path);
QDLLEXPORT QString toWinePath(const QString& path);
QDLLEXPORT QString fromWinePath(const QString& path);
QDLLEXPORT QString normalizePathForHost(const QString& path);
QDLLEXPORT QString normalizePathForWine(const QString& path);

QDLLEXPORT void deleteChildWidgets(QWidget* w);

template <typename T>
bool isOneOf(const T& val, const std::initializer_list<T>& list)
{
  return std::find(list.begin(), list.end(), val) != list.end();
}

QDLLEXPORT std::wstring formatSystemMessage(DWORD id);
QDLLEXPORT std::wstring formatNtMessage(NTSTATUS s);

inline std::wstring formatSystemMessage(HRESULT hr)
{
  return formatSystemMessage(static_cast<DWORD>(hr));
}

QDLLEXPORT QString windowsErrorString(DWORD errorCode);

QDLLEXPORT QString localizedByteSize(unsigned long long bytes);
QDLLEXPORT QString localizedByteSpeed(unsigned long long bytesPerSecond);

QDLLEXPORT QString localizedTimeRemaining(unsigned int msecs);

template <class F>
class Guard
{
public:
  Guard() : m_call(false) {}

  Guard(F f) : m_f(f), m_call(true) {}

  Guard(Guard&& g) : m_f(std::move(g.m_f)) { g.m_call = false; }

  ~Guard()
  {
    if (m_call)
      m_f();
  }

  Guard& operator=(Guard&& g)
  {
    m_f      = std::move(g.m_f);
    g.m_call = false;
    return *this;
  }

  void kill() { m_call = false; }

  Guard(const Guard&)            = delete;
  Guard& operator=(const Guard&) = delete;

private:
  F m_f;
  bool m_call;
};

class QDLLEXPORT TimeThis
{
public:
  TimeThis(const QString& what = {});
  ~TimeThis();

  TimeThis(const TimeThis&)            = delete;
  TimeThis& operator=(const TimeThis&) = delete;

  void start(const QString& what = {});
  void stop();

private:
  using Clock = std::chrono::high_resolution_clock;

  QString m_what;
  Clock::time_point m_start;
  bool m_running;
};

template <class F>
bool forEachLineInFile(const QString& filePath, F&& f)
{
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }

  QByteArray data = file.readAll();
  file.close();

  const char* lineStart = data.constData();
  const char* p         = lineStart;
  const char* end       = data.constData() + data.size();

  while (p < end) {
    // skip all newline characters
    while ((p < end) && (*p == '\n' || *p == '\r')) {
      ++p;
    }

    // line starts here
    lineStart = p;

    // find end of line
    while ((p < end) && *p != '\n' && *p != '\r') {
      ++p;
    }

    if (p != lineStart) {
      // skip whitespace at beginning of line, don't go past end of line
      while (std::isspace(*lineStart) && lineStart < p) {
        ++lineStart;
      }

      // skip comments
      if (*lineStart != '#') {
        // skip line if it only had whitespace
        if (lineStart < p) {
          // skip white at end of line
          const char* lineEnd = p - 1;
          while (std::isspace(*lineEnd) && lineEnd > lineStart) {
            --lineEnd;
          }
          ++lineEnd;

          f(QString::fromUtf8(lineStart, lineEnd - lineStart));
        }
      }
    }
  }

  return true;
}

}  // namespace MOBase

#endif  // MO_UIBASE_UTILITY_INCLUDED
