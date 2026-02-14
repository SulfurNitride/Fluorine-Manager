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

#include <uibase/utility.h>
#include <uibase/log.h>
#include <uibase/report.h>
#include <QApplication>
#include <QBuffer>
#include <QCollator>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QLayout>
#include <QProcess>
#include <QRegularExpression>
#include <QScreen>
#include <QStandardPaths>
#include <QStringEncoder>
#include <QUrl>
#include <QUuid>
#include <QtDebug>
#include <cerrno>
#include <cstring>
#include <format>
#include <iostream>
#include <memory>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#define FO_RECYCLE 0x1003
#endif

namespace MOBase
{

bool removeDir(const QString& dirName)
{
  QDir dir(dirName);

  if (dir.exists()) {
    Q_FOREACH (QFileInfo info,
               dir.entryInfoList(QDir::NoDotAndDotDot | QDir::System | QDir::Hidden |
                                     QDir::AllDirs | QDir::Files,
                                 QDir::DirsFirst)) {
      // Never recurse into symlinked directories: in Flatpak they can point to
      // read-only runtime locations (e.g. /app), and we only want to remove the link.
      if (info.isSymLink()) {
        QFile file(info.absoluteFilePath());
        if (!file.remove()) {
          reportError(QObject::tr("removal of \"%1\" failed: %2")
                          .arg(info.absoluteFilePath())
                          .arg(file.errorString()));
          return false;
        }
      } else if (info.isDir()) {
        if (!removeDir(info.absoluteFilePath())) {
          return false;
        }
      } else {
        // On Linux, just make sure file is writable before removing
        QFile file(info.absoluteFilePath());
        file.setPermissions(file.permissions() | QFile::WriteUser);
        if (!file.remove()) {
          reportError(QObject::tr("removal of \"%1\" failed: %2")
                          .arg(info.absoluteFilePath())
                          .arg(file.errorString()));
          return false;
        }
      }
    }

    if (!dir.rmdir(dirName)) {
      reportError(QObject::tr("removal of \"%1\" failed").arg(dir.absolutePath()));
      return false;
    }
  } else {
    reportError(QObject::tr("\"%1\" doesn't exist (remove)").arg(dirName));
    return false;
  }

  return true;
}

bool copyDir(const QString& sourceName, const QString& destinationName, bool merge)
{
  QDir sourceDir(sourceName);
  if (!sourceDir.exists()) {
    return false;
  }
  QDir destDir(destinationName);
  if (!destDir.exists()) {
    destDir.mkdir(destinationName);
  } else if (!merge) {
    return false;
  }

  QStringList files = sourceDir.entryList(QDir::Files);
  foreach (QString fileName, files) {
    QString srcName  = sourceName + "/" + fileName;
    QString destName = destinationName + "/" + fileName;
    QFile::copy(srcName, destName);
  }

  files.clear();
  // we leave out symlinks because that could cause an endless recursion
  QStringList subDirs =
      sourceDir.entryList(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
  foreach (QString subDir, subDirs) {
    QString srcName  = sourceName + "/" + subDir;
    QString destName = destinationName + "/" + subDir;
    copyDir(srcName, destName, merge);
  }
  return true;
}

// Linux shell operations use QFile/QDir instead of SHFileOperation

static bool shellOpCopy(const QStringList& sourceNames,
                       const QStringList& destinationNames)
{
  // Multiple sources → single destination: treat destination as a directory
  if (destinationNames.count() == 1 && sourceNames.count() > 1) {
    QDir destDir(destinationNames[0]);
    if (!destDir.exists()) {
      destDir.mkpath(".");
    }
    for (const auto& src : sourceNames) {
      QFileInfo srcInfo(src);
      QString dest = destinationNames[0] + "/" + srcInfo.fileName();
      if (!QFile::copy(src, dest)) {
        return false;
      }
    }
    return true;
  }

  // 1:1 or N:N — direct file-to-file copy
  if (destinationNames.count() != sourceNames.count()) {
    return false;
  }

  for (int i = 0; i < sourceNames.count(); ++i) {
    QFileInfo destInfo(destinationNames[i]);
    if (!destInfo.dir().exists()) {
      destInfo.dir().mkpath(".");
    }
    if (!QFile::copy(sourceNames[i], destinationNames[i])) {
      return false;
    }
  }
  return true;
}

static bool shellOpMove(const QStringList& sourceNames,
                       const QStringList& destinationNames)
{
  // Multiple sources → single destination: treat destination as a directory
  if (destinationNames.count() == 1 && sourceNames.count() > 1) {
    QDir destDir(destinationNames[0]);
    if (!destDir.exists()) {
      destDir.mkpath(".");
    }
    for (const auto& src : sourceNames) {
      QFileInfo srcInfo(src);
      QString dest = destinationNames[0] + "/" + srcInfo.fileName();
      if (!QFile::rename(src, dest)) {
        if (!QFile::copy(src, dest) || !QFile::remove(src)) {
          return false;
        }
      }
    }
    return true;
  }

  // 1:1 or N:N — direct file-to-file move
  if (destinationNames.count() != sourceNames.count()) {
    return false;
  }

  for (int i = 0; i < sourceNames.count(); ++i) {
    QFileInfo destInfo(destinationNames[i]);
    if (!destInfo.dir().exists()) {
      destInfo.dir().mkpath(".");
    }
    if (!QFile::rename(sourceNames[i], destinationNames[i])) {
      if (!QFile::copy(sourceNames[i], destinationNames[i]) ||
          !QFile::remove(sourceNames[i])) {
        return false;
      }
    }
  }
  return true;
}

static bool shellOpDelete(const QStringList& fileNames, bool recycle)
{
  // On Linux, "recycle" moves to trash using Qt; otherwise just delete
  for (const auto& fileName : fileNames) {
    QFileInfo fi(fileName);
    if (fi.isSymLink()) {
      if (!QFile::remove(fileName)) {
        return false;
      }
    } else if (fi.isDir()) {
      if (!removeDir(fileName)) {
        return false;
      }
    } else {
      if (recycle) {
        if (!QFile::moveToTrash(fileName)) {
          return false;
        }
      } else {
        if (!QFile::remove(fileName)) {
          return false;
        }
      }
    }
  }
  return true;
}

bool shellCopy(const QStringList& sourceNames, const QStringList& destinationNames,
               QWidget* dialog)
{
  (void)dialog;
  return shellOpCopy(sourceNames, destinationNames);
}

bool shellCopy(const QString& sourceNames, const QString& destinationNames,
               bool yesToAll, QWidget* dialog)
{
  (void)yesToAll;
  (void)dialog;
  return shellOpCopy(QStringList() << sourceNames, QStringList() << destinationNames);
}

bool shellMove(const QStringList& sourceNames, const QStringList& destinationNames,
               QWidget* dialog)
{
  (void)dialog;
  return shellOpMove(sourceNames, destinationNames);
}

bool shellMove(const QString& sourceNames, const QString& destinationNames,
               bool yesToAll, QWidget* dialog)
{
  (void)yesToAll;
  (void)dialog;
  return shellOpMove(QStringList() << sourceNames, QStringList() << destinationNames);
}

bool shellRename(const QString& oldName, const QString& newName, bool yesToAll,
                 QWidget* dialog)
{
  (void)yesToAll;
  (void)dialog;
  return QFile::rename(oldName, newName);
}

bool shellDelete(const QStringList& fileNames, bool recycle, QWidget* dialog)
{
  (void)dialog;
  return shellOpDelete(fileNames, recycle);
}

namespace shell
{

  static QString g_urlHandler;

  Result::Result(bool success, DWORD error, QString message, HANDLE process)
      : m_success(success), m_error(error), m_message(std::move(message)),
        m_process(process)
  {
    if (m_message.isEmpty()) {
      m_message = QString::fromStdWString(formatSystemMessage(m_error));
    }
  }

  Result Result::makeFailure(DWORD error, QString message)
  {
    return Result(false, error, std::move(message), INVALID_HANDLE_VALUE);
  }

  Result Result::makeSuccess(HANDLE process)
  {
    return Result(true, ERROR_SUCCESS, {}, process);
  }

  bool Result::success() const
  {
    return m_success;
  }

  Result::operator bool() const
  {
    return m_success;
  }

  DWORD Result::error()
  {
    return m_error;
  }

  const QString& Result::message() const
  {
    return m_message;
  }

  HANDLE Result::processHandle() const
  {
    return m_process.get();
  }

  HANDLE Result::stealProcessHandle()
  {
    const auto h = m_process.release();
    m_process.reset(INVALID_HANDLE_VALUE);
    return h;
  }

  QString Result::toString() const
  {
    if (m_message.isEmpty()) {
      return QObject::tr("Error %1").arg(m_error);
    } else {
      return m_message;
    }
  }

  QString formatError(int i)
  {
    switch (i) {
    case 0:
      return "The operating system is out of memory or resources";
    case (int)ERROR_FILE_NOT_FOUND:
      return "The specified file was not found";
    case (int)ERROR_PATH_NOT_FOUND:
      return "The specified path was not found";
    case (int)ERROR_BAD_FORMAT:
      return "The .exe file is invalid (non-Win32 .exe or error in .exe image)";
    case SE_ERR_ACCESSDENIED:
      return "The operating system denied access to the specified file";
    case SE_ERR_ASSOCINCOMPLETE:
      return "The file name association is incomplete or invalid";
    case SE_ERR_DDEBUSY:
      return "The DDE transaction could not be completed because other DDE "
             "transactions were being processed";
    case SE_ERR_DDEFAIL:
      return "The DDE transaction failed";
    case SE_ERR_DDETIMEOUT:
      return "The DDE transaction could not be completed because the request "
             "timed out";
    case SE_ERR_DLLNOTFOUND:
      return "The specified DLL was not found";
    case SE_ERR_NOASSOC:
      return "There is no application associated with the given file name "
             "extension";
    case SE_ERR_OOM:
      return "There was not enough memory to complete the operation";
    case SE_ERR_SHARE:
      return "A sharing violation occurred";
    default:
      return QString("Unknown error %1").arg(i);
    }
  }

  Result ExploreDirectory(const QFileInfo& info)
  {
    const auto path = info.absoluteFilePath();
    // Use xdg-open on Linux
    if (QProcess::startDetached("xdg-open", {path})) {
      return Result::makeSuccess();
    }
    return Result::makeFailure(ERROR_FILE_NOT_FOUND, QObject::tr("Failed to open directory"));
  }

  Result ExploreFileInDirectory(const QFileInfo& info)
  {
    // Try to use the system file manager to highlight the file
    // dbus method for Nautilus/Files, fallback to xdg-open on parent dir
    const auto dir = info.absolutePath();
    if (QProcess::startDetached("xdg-open", {dir})) {
      return Result::makeSuccess();
    }
    return Result::makeFailure(ERROR_FILE_NOT_FOUND, QObject::tr("Failed to open file manager"));
  }

  Result Explore(const QFileInfo& info)
  {
    if (info.isFile()) {
      return ExploreFileInDirectory(info);
    } else if (info.isDir()) {
      return ExploreDirectory(info);
    } else {
      const auto parent = info.dir();
      if (parent.exists()) {
        return ExploreDirectory(QFileInfo(parent.absolutePath()));
      } else {
        return Result::makeFailure(ERROR_FILE_NOT_FOUND);
      }
    }
  }

  Result Explore(const QString& path)
  {
    return Explore(QFileInfo(path));
  }

  Result Explore(const QDir& dir)
  {
    return Explore(QFileInfo(dir.absolutePath()));
  }

  Result Open(const QString& path)
  {
    if (QProcess::startDetached("xdg-open", {path})) {
      return Result::makeSuccess();
    }
    return Result::makeFailure(ERROR_FILE_NOT_FOUND, QObject::tr("Failed to open file"));
  }

  Result Open(const QUrl& url)
  {
    log::debug("opening url '{}'", url.toString());

    if (g_urlHandler.isEmpty()) {
      if (QProcess::startDetached("xdg-open", {url.toString()})) {
        return Result::makeSuccess();
      }
      return Result::makeFailure(ERROR_FILE_NOT_FOUND, QObject::tr("Failed to open URL"));
    } else {
      // Custom URL handler
      QString cmd = g_urlHandler;
      cmd.replace("%1", url.toString());
      if (QProcess::startDetached("/bin/sh", {"-c", cmd})) {
        return Result::makeSuccess();
      }
      return Result::makeFailure(ERROR_FILE_NOT_FOUND,
        QObject::tr("You have an invalid custom browser command in the settings."));
    }
  }

  Result Execute(const QString& program, const QString& params)
  {
    QStringList args;
    if (!params.isEmpty()) {
      args = QProcess::splitCommand(params);
    }
    if (QProcess::startDetached(program, args)) {
      return Result::makeSuccess();
    }
    return Result::makeFailure(ERROR_FILE_NOT_FOUND, QObject::tr("Failed to execute program"));
  }

  void SetUrlHandler(const QString& cmd)
  {
    g_urlHandler = cmd;
  }

  Result Delete(const QFileInfo& path)
  {
    if (QFile::remove(path.absoluteFilePath())) {
      return Result::makeSuccess();
    }
    return Result::makeFailure(ERROR_ACCESS_DENIED);
  }

  Result Rename(const QFileInfo& src, const QFileInfo& dest)
  {
    return Rename(src, dest, true);
  }

  Result Rename(const QFileInfo& src, const QFileInfo& dest, bool copyAllowed)
  {
    if (QFile::rename(src.absoluteFilePath(), dest.absoluteFilePath())) {
      return Result::makeSuccess();
    }

    if (copyAllowed) {
      if (QFile::copy(src.absoluteFilePath(), dest.absoluteFilePath())) {
        QFile::remove(src.absoluteFilePath());
        return Result::makeSuccess();
      }
    }

    return Result::makeFailure(ERROR_ACCESS_DENIED);
  }

  Result CreateDirectories(const QDir& dir)
  {
    if (dir.mkpath(".")) {
      return Result::makeSuccess();
    }
    return Result::makeFailure(ERROR_ACCESS_DENIED,
      QObject::tr("Failed to create directory: %1").arg(dir.path()));
  }

  Result DeleteDirectoryRecursive(const QDir& dir)
  {
    if (shellOpDelete({dir.path()}, false)) {
      return Result::makeSuccess();
    }
    return Result::makeFailure(ERROR_ACCESS_DENIED);
  }

}  // namespace shell

bool moveFileRecursive(const QString& source, const QString& baseDir,
                       const QString& destination)
{
  QStringList pathComponents = destination.split("/");
  QString path               = baseDir;
  for (QStringList::Iterator iter = pathComponents.begin();
       iter != pathComponents.end() - 1; ++iter) {
    path.append("/").append(*iter);
    if (!QDir(path).exists() && !QDir().mkdir(path)) {
      reportError(QObject::tr("failed to create directory \"%1\"").arg(path));
      return false;
    }
  }

  QString destinationAbsolute = baseDir.mid(0).append("/").append(destination);
  if (!QFile::rename(source, destinationAbsolute)) {
    // move failed, try copy & delete
    if (!QFile::copy(source, destinationAbsolute)) {
      reportError(QObject::tr("failed to copy \"%1\" to \"%2\"")
                      .arg(source)
                      .arg(destinationAbsolute));
      return false;
    } else {
      QFile::remove(source);
    }
  }
  return true;
}

bool copyFileRecursive(const QString& source, const QString& baseDir,
                       const QString& destination)
{
  QStringList pathComponents = destination.split("/");
  QString path               = baseDir;
  for (QStringList::Iterator iter = pathComponents.begin();
       iter != pathComponents.end() - 1; ++iter) {
    path.append("/").append(*iter);
    if (!QDir(path).exists() && !QDir().mkdir(path)) {
      reportError(QObject::tr("failed to create directory \"%1\"").arg(path));
      return false;
    }
  }

  QString destinationAbsolute = baseDir.mid(0).append("/").append(destination);
  if (!QFile::copy(source, destinationAbsolute)) {
    reportError(QObject::tr("failed to copy \"%1\" to \"%2\"")
                    .arg(source)
                    .arg(destinationAbsolute));
    return false;
  }
  return true;
}

std::wstring ToWString(const QString& source)
{
  return source.toStdWString();
}

std::string ToString(const QString& source, bool utf8)
{
  QByteArray array8bit;
  if (utf8) {
    array8bit = source.toUtf8();
  } else {
    array8bit = source.toLocal8Bit();
  }
  return std::string(array8bit.constData());
}

QString ToQString(const std::string& source)
{
  return QString::fromStdString(source);
}

QString ToQString(const std::wstring& source)
{
  return QString::fromStdWString(source);
}

bool isWindowsDrivePath(const QString& path)
{
  static const QRegularExpression re(R"(^\s*[A-Za-z]:[\\/].*)");
  return re.match(path).hasMatch();
}

bool isWineZDrivePath(const QString& path)
{
  static const QRegularExpression re(R"(^\s*[Zz]:[\\/].*)");
  return re.match(path).hasMatch();
}

QString toWinePath(const QString& path)
{
#ifdef _WIN32
  return QDir::toNativeSeparators(path);
#else
  QString p = path.trimmed();
  if (p.isEmpty()) {
    return p;
  }

  if (isWindowsDrivePath(p)) {
    p.replace('/', '\\');
    return p;
  }

  // Expand "~" on Linux before mapping to Z:.
  if (p.startsWith("~")) {
    p.replace(0, 1, QDir::homePath());
  }

  if (!QDir::isAbsolutePath(p)) {
    p = QDir(p).absolutePath();
  }

  p = QDir::cleanPath(p);
  p.replace('/', '\\');
  return "Z:" + p;
#endif
}

QString fromWinePath(const QString& path)
{
#ifdef _WIN32
  return QDir::toNativeSeparators(path);
#else
  QString p = path.trimmed();
  if (p.isEmpty()) {
    return p;
  }

  if (!isWindowsDrivePath(p)) {
    return QDir::cleanPath(p);
  }

  // We only map Wine's Z: drive to host Linux absolute paths.
  if (!isWineZDrivePath(p)) {
    return p;
  }

  p = p.mid(2);  // strip "Z:"
  p.replace('\\', '/');
  if (!p.startsWith('/')) {
    p.prepend('/');
  }

  return QDir::cleanPath(p);
#endif
}

QString normalizePathForHost(const QString& path)
{
#ifdef _WIN32
  return QDir::toNativeSeparators(path);
#else
  if (isWineZDrivePath(path)) {
    return fromWinePath(path);
  }
  return QDir::cleanPath(path);
#endif
}

QString normalizePathForWine(const QString& path)
{
#ifdef _WIN32
  return QDir::toNativeSeparators(path);
#else
  if (isWindowsDrivePath(path)) {
    QString p = path;
    p.replace('/', '\\');
    return p;
  }
  return toWinePath(path);
#endif
}

#ifdef _WIN32
QString ToString(const SYSTEMTIME& time)
{
  char dateBuffer[100];
  char timeBuffer[100];
  int size = 100;
  GetDateFormatA(LOCALE_USER_DEFAULT, LOCALE_USE_CP_ACP, &time, nullptr, dateBuffer,
                 size);
  GetTimeFormatA(LOCALE_USER_DEFAULT, LOCALE_USE_CP_ACP, &time, nullptr, timeBuffer,
                 size);
  return QString::fromLocal8Bit(dateBuffer) + " " + QString::fromLocal8Bit(timeBuffer);
}
#endif

static int naturalCompareI(const QString& a, const QString& b)
{
  static QCollator c = [] {
    QCollator temp;
    temp.setNumericMode(true);
    temp.setCaseSensitivity(Qt::CaseInsensitive);
    return temp;
  }();

  return c.compare(a, b);
}

int naturalCompare(const QString& a, const QString& b, Qt::CaseSensitivity cs)
{
  if (cs == Qt::CaseInsensitive) {
    return naturalCompareI(a, b);
  }

  static QCollator c = [] {
    QCollator temp;
    temp.setNumericMode(true);
    return temp;
  }();

  return c.compare(a, b);
}

#ifdef _WIN32
struct CoTaskMemFreer
{
  void operator()(void* p) { ::CoTaskMemFree(p); }
};

template <class T>
using COMMemPtr = std::unique_ptr<T, CoTaskMemFreer>;

QString getOptionalKnownFolder(KNOWNFOLDERID id)
{
  COMMemPtr<wchar_t> path;
  {
    wchar_t* rawPath = nullptr;
    HRESULT res      = SHGetKnownFolderPath(id, 0, nullptr, &rawPath);
    if (FAILED(res)) {
      return {};
    }
    path.reset(rawPath);
  }
  return QString::fromWCharArray(path.get());
}

QDir getKnownFolder(KNOWNFOLDERID id, const QString& what)
{
  COMMemPtr<wchar_t> path;
  {
    wchar_t* rawPath = nullptr;
    HRESULT res      = SHGetKnownFolderPath(id, 0, nullptr, &rawPath);
    if (FAILED(res)) {
      log::error("failed to get known folder '{}', {}",
                 what.isEmpty() ? QUuid(id).toString() : what,
                 formatSystemMessage(res));
      throw std::runtime_error("couldn't get known folder path");
    }
    path.reset(rawPath);
  }
  return QString::fromWCharArray(path.get());
}
#endif

QString getDesktopDirectory()
{
#ifdef _WIN32
  return getKnownFolder(FOLDERID_Desktop, "desktop").absolutePath();
#else
  return QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
#endif
}

QString getStartMenuDirectory()
{
#ifdef _WIN32
  return getKnownFolder(FOLDERID_StartMenu, "start menu").absolutePath();
#else
  return QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
#endif
}

bool shellDeleteQuiet(const QString& fileName, QWidget* dialog)
{
  if (!QFile::remove(fileName)) {
    return shellDelete(QStringList(fileName), false, dialog);
  }
  return true;
}

QString readFileText(const QString& fileName, QString* encoding, bool* hadBOM)
{

  QFile textFile(fileName);
  if (!textFile.open(QIODevice::ReadOnly)) {
    return QString();
  }

  QByteArray buffer = textFile.readAll();
  return decodeTextData(buffer, encoding, hadBOM);
}

QString decodeTextData(const QByteArray& fileData, QString* encoding, bool* hadBOM)
{
  QStringConverter::Encoding codec = QStringConverter::Encoding::Utf8;
  QStringEncoder encoder(codec);
  QStringDecoder decoder(codec, QStringConverter::Flag::ConvertInitialBom);
  QString text = decoder.decode(fileData);

  // embedded nulls probably mean it was UTF-16 - they're rare/illegal in text files
  bool hasEmbeddedNulls = false;
  for (const auto& character : text) {
    if (character.isNull()) {
      hasEmbeddedNulls = true;
      break;
    }
  }

  // check reverse conversion
  if (hasEmbeddedNulls || encoder.encode(text) != fileData) {
    log::debug("conversion failed assuming local encoding");
    auto codecSearch = QStringConverter::encodingForData(fileData);
    if (codecSearch.has_value()) {
      codec   = codecSearch.value();
      decoder = QStringDecoder(codec, QStringConverter::Flag::ConvertInitialBom);
    } else {
      decoder = QStringDecoder(hasEmbeddedNulls ? QStringConverter::Encoding::Utf16
                                                : QStringConverter::Encoding::System);
    }
    text = decoder.decode(fileData);
  }

  if (encoding != nullptr) {
    *encoding = QStringConverter::nameForEncoding(codec);
  }

  if (!text.isEmpty() && text.startsWith(QChar::ByteOrderMark)) {
    text.remove(0, 1);

    if (hadBOM != nullptr) {
      *hadBOM = true;
    }
  } else if (hadBOM != nullptr) {
    *hadBOM = false;
  }

  return text;
}

void removeOldFiles(const QString& path, const QString& pattern, int numToKeep,
                    QDir::SortFlags sorting)
{
  QFileInfoList files =
      QDir(path).entryInfoList(QStringList(pattern), QDir::Files, sorting);

  if (files.count() > numToKeep) {
    QStringList deleteFiles;
    for (int i = 0; i < files.count() - numToKeep; ++i) {
      deleteFiles.append(files.at(i).absoluteFilePath());
    }

    if (!shellDelete(deleteFiles)) {
      log::warn("failed to remove log files");
    }
  }
}

QIcon iconForExecutable(const QString& filePath)
{
  static QHash<QString, QIcon> cache;

  const QFileInfo fi(filePath);
  if (!fi.exists()) {
    return QIcon(":/MO/gui/executable");
  }

  const QString cacheKey =
      fi.canonicalFilePath() + "|" + QString::number(fi.size()) + "|" +
      fi.lastModified().toString(Qt::ISODateWithMs);

  if (cache.contains(cacheKey)) {
    return cache.value(cacheKey);
  }

#ifdef _WIN32
  QIcon icon(":/MO/gui/executable");
  cache.insert(cacheKey, icon);
  return icon;
#else
  // Try to extract icon resources from PE executables via icoutils/wrestool.
  // If this fails, return a generic executable icon.
  QIcon icon;

  const QString wrestool = QStandardPaths::findExecutable("wrestool");
  if (!wrestool.isEmpty()) {
    const QString cacheRoot =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
        "/exe_icons";
    QDir().mkpath(cacheRoot);

    const QString outDirPath = cacheRoot + "/" + QString::number(qHash(cacheKey), 16);
    QDir outDir(outDirPath);
    if (!outDir.exists()) {
      QDir().mkpath(outDirPath);
    }

    auto findBestIcon = [&](const QString& dirPath) -> QIcon {
      QString bestFile;
      int bestArea = -1;

      QDirIterator it(dirPath,
                      QStringList() << "*.png"
                                    << "*.ico"
                                    << "*.bmp",
                      QDir::Files, QDirIterator::Subdirectories);
      while (it.hasNext()) {
        const QString path = it.next();
        QImageReader reader(path);
        const QSize size = reader.size();
        const int area   = size.width() * size.height();
        if (area > bestArea) {
          bestArea = area;
          bestFile = path;
        } else if (bestArea < 0) {
          // Unknown image size (e.g. multi-size ico); keep first viable file.
          bestFile = path;
        }
      }

      if (!bestFile.isEmpty()) {
        QIcon candidate(bestFile);
        if (!candidate.pixmap(24, 24).isNull() || !candidate.pixmap(32, 32).isNull()) {
          return candidate;
        }
      }

      return QIcon();
    };

    // Reuse previous extraction if present.
    icon = findBestIcon(outDirPath);

    if (icon.isNull()) {
      const QList<QStringList> tries = {
          {"-x", "--type=group_icon", "-o", outDirPath, fi.absoluteFilePath()},
          {"-x", "--type=14", "-o", outDirPath, fi.absoluteFilePath()},
          {"-x", "-t14", "-o", outDirPath, fi.absoluteFilePath()},
      };

      for (const auto& args : tries) {
        QProcess p;
        p.start(wrestool, args);
        if (!p.waitForFinished(4000)) {
          p.kill();
          continue;
        }
        if (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0) {
          icon = findBestIcon(outDirPath);
          if (!icon.isNull()) {
            break;
          }
        }
      }
    }
  }

  if (icon.isNull()) {
    icon = QIcon(":/MO/gui/executable");
  }

  cache.insert(cacheKey, icon);
  return icon;
#endif
}

QString getFileVersion(QString const& filepath)
{
  // On Linux, there is no Windows PE file version. Return empty.
  (void)filepath;
  return "";
}

QString getProductVersion(QString const& filepath)
{
  // On Linux, there is no Windows PE product version. Return empty.
  (void)filepath;
  return "";
}

void deleteChildWidgets(QWidget* w)
{
  auto* ly = w->layout();
  if (!ly) {
    return;
  }

  while (auto* item = ly->takeAt(0)) {
    delete item->widget();
    delete item;
  }
}

void trimWString(std::wstring& s)
{
  s.erase(std::remove_if(s.begin(), s.end(),
                         [](wint_t ch) {
                           return std::iswspace(ch);
                         }),
          s.end());
}

std::wstring formatMessage(DWORD id, const std::wstring& message)
{
  std::wstring s;

  std::wostringstream oss;
  oss << L"0x" << std::hex << id;

  if (message.empty()) {
    s = oss.str();
  } else {
    s += message + L" (" + oss.str() + L")";
  }

  return s;
}

std::wstring formatSystemMessage(DWORD id)
{
#ifdef _WIN32
  std::wstring getMessage(DWORD id, HMODULE mod);
  return formatMessage(id, getMessage(id, 0));
#else
  // On Linux, map common error codes to strings or use strerror for errno values
  if (id == 0) {
    return L"Success";
  }
  // If it looks like an errno value (small numbers), use strerror
  if (id < 200) {
    const char* msg = strerror(static_cast<int>(id));
    if (msg) {
      std::string s(msg);
      return std::wstring(s.begin(), s.end());
    }
  }
  return formatMessage(id, L"");
#endif
}

std::wstring formatNtMessage(NTSTATUS s)
{
#ifdef _WIN32
  const DWORD id = static_cast<DWORD>(s);
  std::wstring getMessage(DWORD id, HMODULE mod);
  return formatMessage(id, getMessage(id, ::GetModuleHandleW(L"ntdll.dll")));
#else
  return formatMessage(static_cast<DWORD>(s), L"NT status");
#endif
}

QString windowsErrorString(DWORD errorCode)
{
  return QString::fromStdWString(formatSystemMessage(errorCode));
}

QString localizedSize(unsigned long long bytes, const QString& B, const QString& KB,
                      const QString& MB, const QString& GB, const QString& TB)
{
  constexpr unsigned long long OneKB = 1024ull;
  constexpr unsigned long long OneMB = 1024ull * 1024;
  constexpr unsigned long long OneGB = 1024ull * 1024 * 1024;
  constexpr unsigned long long OneTB = 1024ull * 1024 * 1024 * 1024;

  auto makeNum = [&](int factor) {
    const double n = static_cast<double>(bytes) / std::pow(1024.0, factor);
    const double truncated =
        static_cast<double>(static_cast<unsigned long long>(n * 100)) / 100.0;
    return QString().setNum(truncated, 'f', 2);
  };

  if (bytes < OneKB) {
    return B.arg(bytes);
  } else if (bytes < OneMB) {
    return KB.arg(makeNum(1));
  } else if (bytes < OneGB) {
    return MB.arg(makeNum(2));
  } else if (bytes < OneTB) {
    return GB.arg(makeNum(3));
  } else {
    return TB.arg(makeNum(4));
  }
}

QDLLEXPORT QString localizedByteSize(unsigned long long bytes)
{
  return localizedSize(bytes, QObject::tr("%1 B"), QObject::tr("%1 KB"),
                       QObject::tr("%1 MB"), QObject::tr("%1 GB"),
                       QObject::tr("%1 TB"));
}

QDLLEXPORT QString localizedByteSpeed(unsigned long long bps)
{
  return localizedSize(bps, QObject::tr("%1 B/s"), QObject::tr("%1 KB/s"),
                       QObject::tr("%1 MB/s"), QObject::tr("%1 GB/s"),
                       QObject::tr("%1 TB/s"));
}

QDLLEXPORT QString localizedTimeRemaining(unsigned int remaining)
{
  QString Result;
  double interval;
  qint64 intval;

  // Hours
  interval = 60.0 * 60.0 * 1000.0;
  intval   = (qint64)trunc((double)remaining / interval);
  if (intval < 0)
    intval = 0;
  remaining -= static_cast<unsigned int>(trunc((double)intval * interval));
  qint64 hours = intval;

  // Minutes
  interval = 60.0 * 1000.0;
  intval   = (qint64)trunc((double)remaining / interval);
  if (intval < 0)
    intval = 0;
  remaining -= static_cast<unsigned int>(trunc((double)intval * interval));
  qint64 minutes = intval;

  // Seconds
  interval = 1000.0;
  intval   = (qint64)trunc((double)remaining / interval);
  if (intval < 0)
    intval = 0;
  remaining -= static_cast<unsigned int>(trunc((double)intval * interval));
  qint64 seconds = intval;

  char buffer[25];
  memset(buffer, 0, 25);

  if (hours > 0) {
    if (hours < 10)
      snprintf(buffer, sizeof(buffer), "0%lld", (long long)hours);
    else
      snprintf(buffer, sizeof(buffer), "%lld", (long long)hours);
    Result.append(QString("%1:").arg(buffer));
  }

  if (minutes > 0 || hours > 0) {
    if (minutes < 10 && hours > 0)
      snprintf(buffer, sizeof(buffer), "0%lld", (long long)minutes);
    else
      snprintf(buffer, sizeof(buffer), "%lld", (long long)minutes);
    Result.append(QString("%1:").arg(buffer));
  }

  if (seconds < 10 && (minutes > 0 || hours > 0))
    snprintf(buffer, sizeof(buffer), "0%lld", (long long)seconds);
  else
    snprintf(buffer, sizeof(buffer), "%lld", (long long)seconds);
  Result.append(QString("%1").arg(buffer));

  if (hours > 0)
    //: Time remaining hours
    Result.append(QApplication::translate("uibase", "h"));
  else if (minutes > 0)
    //: Time remaining minutes
    Result.append(QApplication::translate("uibase", "m"));
  else
    //: Time remaining seconds
    Result.append(QApplication::translate("uibase", "s"));

  return Result;
}

QDLLEXPORT void localizedByteSizeTests()
{
  auto f = [](unsigned long long n) {
    return localizedByteSize(n).toStdString();
  };

#define CHECK_EQ(a, b)                                                                 \
  if ((a) != (b)) {                                                                    \
    std::cerr << "failed: " << a << " == " << b << "\n";                               \
  }

  CHECK_EQ(f(0), "0 B");
  CHECK_EQ(f(1), "1 B");
  CHECK_EQ(f(999), "999 B");
  CHECK_EQ(f(1000), "1000 B");
  CHECK_EQ(f(1023), "1023 B");

  CHECK_EQ(f(1024), "1.00 KB");
  CHECK_EQ(f(2047), "1.99 KB");
  CHECK_EQ(f(2048), "2.00 KB");
  CHECK_EQ(f(1048575), "1023.99 KB");

  CHECK_EQ(f(1048576), "1.00 MB");
  CHECK_EQ(f(1073741823), "1023.99 MB");

  CHECK_EQ(f(1073741824), "1.00 GB");
  CHECK_EQ(f(1099511627775), "1023.99 GB");

  CHECK_EQ(f(1099511627776), "1.00 TB");
  CHECK_EQ(f(2759774185818), "2.51 TB");

#undef CHECK_EQ
}

TimeThis::TimeThis(const QString& what) : m_running(false)
{
  start(what);
}

TimeThis::~TimeThis()
{
  stop();
}

void TimeThis::start(const QString& what)
{
  stop();

  m_what    = what;
  m_start   = Clock::now();
  m_running = true;
}

void TimeThis::stop()
{
  using namespace std::chrono;

  if (!m_running) {
    return;
  }

  const auto end = Clock::now();
  const auto d   = duration_cast<milliseconds>(end - m_start).count();

  if (m_what.isEmpty()) {
    log::debug("timing: {} ms", d);
  } else {
    log::debug("timing: {} {} ms", m_what, d);
  }

  m_running = false;
}

}  // namespace MOBase
