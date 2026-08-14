#include "morrowindpluginlistwriter.h"

#include <uibase/transactionalwritefile.h>

#include <QFile>
#include <QFileInfo>
#include <QObject>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace MorrowindPluginListWriter
{
namespace
{

constexpr qint64 MaxIniBytes = 16 * 1024 * 1024;

struct Line
{
  qsizetype begin;
  qsizetype contentEnd;
  qsizetype end;
};

QList<Line> linesOf(QByteArrayView contents)
{
  QList<Line> lines;
  qsizetype begin = 0;
  while (begin < contents.size()) {
    qsizetype contentEnd = begin;
    while (contentEnd < contents.size() && contents[contentEnd] != '\r' &&
           contents[contentEnd] != '\n') {
      ++contentEnd;
    }
    qsizetype end = contentEnd;
    if (end < contents.size() && contents[end] == '\r') {
      ++end;
    }
    if (end < contents.size() && contents[end] == '\n') {
      ++end;
    }
    lines.append({begin, contentEnd, end});
    begin = end;
  }
  return lines;
}

bool isSection(QByteArrayView line)
{
  const QByteArray trimmed = QByteArray(line).trimmed();
  return trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']';
}

bool isGameFilesSection(QByteArrayView line)
{
  return QByteArray(line).trimmed().compare("[Game Files]", Qt::CaseInsensitive) ==
         0;
}

Result readSource(const QString& path, QByteArray& contents)
{
#ifdef Q_OS_UNIX
  const QByteArray encoded = QFile::encodeName(path);
  const int descriptor =
      ::open(encoded.constData(), O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
  if (descriptor < 0) {
    return {Status::ReadError,
            QObject::tr("Could not safely open Morrowind INI '%1'.").arg(path)};
  }

  struct stat status;
  if (::fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode) ||
      status.st_size < 0 || status.st_size > MaxIniBytes) {
    ::close(descriptor);
    return {Status::ReadError,
            QObject::tr("Refusing unsafe or oversized Morrowind INI '%1'.")
                .arg(path)};
  }

  QFile input;
  if (!input.open(descriptor, QIODevice::ReadOnly,
                  QFileDevice::AutoCloseHandle)) {
    ::close(descriptor);
    return {Status::ReadError, input.errorString()};
  }
  contents = input.read(status.st_size + 1);
  if (input.error() != QFileDevice::NoError || contents.size() != status.st_size) {
    return {Status::ReadError,
            input.error() == QFileDevice::NoError
                ? QObject::tr("Morrowind INI changed while it was read.")
                : input.errorString()};
  }
#else
  const QFileInfo info(path);
  if (info.isSymLink() || !info.exists() || !info.isFile() || info.size() < 0 ||
      info.size() > MaxIniBytes) {
    return {Status::ReadError,
            QObject::tr("Refusing unsafe or oversized Morrowind INI '%1'.")
                .arg(path)};
  }
  QFile input(path);
  if (!input.open(QIODevice::ReadOnly)) {
    return {Status::ReadError, input.errorString()};
  }
  contents = input.read(info.size() + 1);
  if (input.error() != QFileDevice::NoError || contents.size() != info.size()) {
    return {Status::ReadError, input.errorString()};
  }
#endif
  return {};
}

QByteArray serializedSection(const QList<QByteArray>& pluginNames)
{
  QByteArray section;
  for (qsizetype i = 0; i < pluginNames.size(); ++i) {
    section.append("GameFile");
    section.append(QByteArray::number(i));
    section.append('=');
    section.append(pluginNames[i]);
    section.append("\r\n");
  }
  return section;
}

}  // namespace

Result publish(const QString& path, const QList<QByteArray>& pluginNames)
{
  if (pluginNames.isEmpty()) {
    return {Status::NoPlugins,
            QObject::tr("The Morrowind plugin list would be empty.")};
  }

  MOBase::TransactionalWriteFile output(path);
  QByteArray source;
  if (const Result read = readSource(path, source);
      read.status != Status::Published) {
    return read;
  }

  const QList<Line> lines = linesOf(source);
  qsizetype sectionHeader = -1;
  qsizetype sectionEnd    = source.size();
  bool sectionEndFound    = false;
  for (qsizetype i = 0; i < lines.size(); ++i) {
    const auto line = QByteArrayView(source).sliced(
        lines[i].begin, lines[i].contentEnd - lines[i].begin);
    if (isGameFilesSection(line)) {
      if (sectionHeader != -1) {
        return {Status::InvalidFormat,
                QObject::tr("Morrowind INI contains multiple [Game Files] sections.")};
      }
      sectionHeader = i;
      continue;
    }
    if (sectionHeader != -1 && !sectionEndFound && isSection(line)) {
      sectionEnd = lines[i].begin;
      sectionEndFound = true;
    }
  }

  const QByteArray pluginSection = serializedSection(pluginNames);
  QByteArray replacement;
  if (sectionHeader == -1) {
    replacement = source;
    if (!replacement.isEmpty() && !replacement.endsWith('\n') &&
        !replacement.endsWith('\r')) {
      replacement.append("\r\n");
    }
    replacement.append("[Game Files]\r\n");
    replacement.append(pluginSection);
  } else {
    const Line header = lines[sectionHeader];
    replacement.reserve(source.size() + pluginSection.size());
    replacement.append(source.first(header.end));
    if (header.end == header.contentEnd) {
      replacement.append("\r\n");
    }
    replacement.append(pluginSection);
    replacement.append(source.sliced(sectionEnd));
  }

  if (!output.replaceWith(replacement)) {
    return {Status::WriteError, output.errorString()};
  }
  return {};
}

}  // namespace MorrowindPluginListWriter
