#include "profilemodlistrename.h"

#include <uibase/transactionalwritefile.h>

#include <QFile>
#include <QFileInfo>
#include <QObject>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ProfileModlistRename
{

Result apply(const QString& path, const QString& oldName, const QString& newName)
{
  constexpr qint64 MaxModListBytes = 16 * 1024 * 1024;
  MOBase::TransactionalWriteFile output(path);
  qint64 expectedSize              = 0;
#ifdef Q_OS_UNIX
  const QByteArray encodedPath = QFile::encodeName(path);
  const int descriptor =
      ::open(encodedPath.constData(), O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
  if (descriptor < 0) {
    return {Status::ReadError, 0,
            QObject::tr("Could not safely open mod list '%1'.").arg(path)};
  }

  struct stat status;
  if (::fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode) ||
      status.st_size < 0 || status.st_size > MaxModListBytes) {
    ::close(descriptor);
    return {Status::ReadError, 0,
            QObject::tr("Refusing unsafe or oversized mod list '%1'.").arg(path)};
  }

  QFile input;
  if (!input.open(descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
    ::close(descriptor);
    return {Status::ReadError, 0, input.errorString()};
  }
  expectedSize = status.st_size;
#else
  const QFileInfo info(path);
  if (info.isSymLink() || !info.exists() || !info.isFile()) {
    return {Status::ReadError, 0,
            QObject::tr("Refusing non-regular mod list '%1'.").arg(path)};
  }

  QFile input(path);
  if (!input.open(QIODevice::ReadOnly)) {
    return {Status::ReadError, 0, input.errorString()};
  }
  expectedSize = input.size();
  if (expectedSize < 0 || expectedSize > MaxModListBytes) {
    return {Status::ReadError, 0,
            QObject::tr("Refusing oversized mod list '%1'.").arg(path)};
  }
#endif

  QByteArray source = input.read(expectedSize + 1);
  if (input.error() != QFileDevice::NoError || source.size() != expectedSize) {
    const QString detail = input.error() == QFileDevice::NoError
                               ? QObject::tr("The file changed while it was read.")
                               : input.errorString();
    return {Status::ReadError, 0, detail};
  }
  input.close();

  source.replace("\r\n", "\n");
  source.replace('\r', '\n');

  QByteArray replacement;
  int renamed = 0;
  const QList<QByteArray> lines = source.split('\n');
  for (const QByteArray& line : lines) {
    if (line.trimmed().isEmpty()) {
      continue;
    }

    const char spec = line.front();
    if (spec == '#') {
      replacement.append(line);
      replacement.append("\r\n");
      continue;
    }

    QString modName = QString::fromUtf8(line.sliced(1)).trimmed();
    if (modName.isEmpty()) {
      continue;
    }

    replacement.append(spec);
    if (modName == oldName) {
      modName = newName;
      ++renamed;
    }
    replacement.append(modName.toUtf8());
    replacement.append("\r\n");
  }

  if (renamed == 0) {
    return {};
  }

  if (!output.replaceWith(replacement)) {
    return {Status::WriteError, 0, output.errorString()};
  }

  return {Status::Changed, renamed, {}};
}

}  // namespace ProfileModlistRename
