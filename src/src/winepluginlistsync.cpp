#include "winepluginlistsync.h"

#include <uibase/transactionalwritefile.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace WinePluginListSync
{
namespace
{

constexpr qint64 MaxPluginListBytes = 8 * 1024 * 1024;

bool resolveRegularCaseAlias(const QString& requested, QString& effective)
{
  const QFileInfo requestedInfo(requested);
  if (!requestedInfo.isSymLink()) {
    if (!requestedInfo.exists() || !requestedInfo.isFile()) {
      return false;
    }
    effective = requested;
    return true;
  }

  const QString target = requestedInfo.symLinkTarget();
  const QFileInfo targetInfo(target);
  const QString requestedDirectory =
      QDir(requestedInfo.absolutePath()).canonicalPath();
  const QString targetDirectory = QDir(targetInfo.absolutePath()).canonicalPath();
  if (target.isEmpty() || targetInfo.isSymLink() || !targetInfo.exists() ||
      !targetInfo.isFile() || requestedDirectory.isEmpty() ||
      requestedDirectory != targetDirectory ||
      requestedInfo.fileName().compare(targetInfo.fileName(), Qt::CaseInsensitive) !=
          0) {
    return false;
  }

  effective = target;
  return true;
}

#ifdef Q_OS_UNIX
QString identityOf(const struct stat& status)
{
#ifdef Q_OS_DARWIN
  const auto modified = status.st_mtimespec;
  const auto changed  = status.st_ctimespec;
#else
  const auto modified = status.st_mtim;
  const auto changed  = status.st_ctim;
#endif
  return QStringLiteral("%1:%2:%3:%4:%5:%6:%7")
      .arg(static_cast<qulonglong>(status.st_dev))
      .arg(static_cast<qulonglong>(status.st_ino))
      .arg(static_cast<qlonglong>(status.st_size))
      .arg(static_cast<qlonglong>(modified.tv_sec))
      .arg(static_cast<qlonglong>(modified.tv_nsec))
      .arg(static_cast<qlonglong>(changed.tv_sec))
      .arg(static_cast<qlonglong>(changed.tv_nsec));
}

QDateTime modificationTimeOf(const struct stat& status)
{
#ifdef Q_OS_DARWIN
  const auto time = status.st_mtimespec;
#else
  const auto time = status.st_mtim;
#endif
  return QDateTime::fromMSecsSinceEpoch(
      static_cast<qint64>(time.tv_sec) * 1000 + time.tv_nsec / 1000000);
}

bool sameGeneration(const struct stat& lhs, const struct stat& rhs)
{
#ifdef Q_OS_DARWIN
  const auto lhsModified = lhs.st_mtimespec;
  const auto rhsModified = rhs.st_mtimespec;
  const auto lhsChanged  = lhs.st_ctimespec;
  const auto rhsChanged  = rhs.st_ctimespec;
#else
  const auto lhsModified = lhs.st_mtim;
  const auto rhsModified = rhs.st_mtim;
  const auto lhsChanged  = lhs.st_ctim;
  const auto rhsChanged  = rhs.st_ctim;
#endif
  return lhs.st_dev == rhs.st_dev && lhs.st_ino == rhs.st_ino &&
         lhs.st_size == rhs.st_size &&
         lhsModified.tv_sec == rhsModified.tv_sec &&
         lhsModified.tv_nsec == rhsModified.tv_nsec &&
         lhsChanged.tv_sec == rhsChanged.tv_sec &&
         lhsChanged.tv_nsec == rhsChanged.tv_nsec;
}
#endif

}  // namespace

ReadResult read(const QString& path)
{
  QString effective;
  if (!resolveRegularCaseAlias(path, effective)) {
    return {{}, QObject::tr("Refusing unsafe plugin-list source '%1'.").arg(path)};
  }

#ifdef Q_OS_UNIX
  const QByteArray encoded = QFile::encodeName(effective);
  const int descriptor =
      ::open(encoded.constData(), O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
  if (descriptor < 0) {
    return {{}, QObject::tr("Could not open plugin list '%1': %2")
                    .arg(path, QString::fromLocal8Bit(std::strerror(errno)))};
  }

  struct stat status;
  if (::fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode) ||
      status.st_size < 0 || status.st_size > MaxPluginListBytes) {
    ::close(descriptor);
    return {{}, QObject::tr("Refusing unsafe or oversized plugin list '%1'.")
                    .arg(path)};
  }

  QFile input;
  if (!input.open(descriptor, QIODevice::ReadOnly,
                  QFileDevice::AutoCloseHandle)) {
    ::close(descriptor);
    return {{}, input.errorString()};
  }
  QByteArray contents = input.read(status.st_size + 1);
  if (input.error() != QFileDevice::NoError || contents.size() != status.st_size) {
    return {{}, input.error() == QFileDevice::NoError
                    ? QObject::tr("Plugin list changed while it was read.")
                    : input.errorString()};
  }
  struct stat currentStatus;
  if (::fstat(descriptor, &currentStatus) != 0 ||
      !sameGeneration(status, currentStatus)) {
    return {{}, QObject::tr("Plugin list changed while it was read.")};
  }
  return {Snapshot{std::move(contents), modificationTimeOf(status),
                   identityOf(status)},
          {}};
#else
  const QFileInfo info(effective);
  if (info.size() < 0 || info.size() > MaxPluginListBytes) {
    return {{}, QObject::tr("Refusing oversized plugin list '%1'.").arg(path)};
  }
  QFile input(effective);
  if (!input.open(QIODevice::ReadOnly)) {
    return {{}, input.errorString()};
  }
  QByteArray contents = input.read(info.size() + 1);
  if (input.error() != QFileDevice::NoError || contents.size() != info.size()) {
    return {{}, input.errorString()};
  }
  return {Snapshot{std::move(contents), info.lastModified(),
                   info.canonicalFilePath()},
          {}};
#endif
}

int countStarred(QByteArrayView contents)
{
  int count = 0;
  qsizetype offset = 0;
  while (offset < contents.size()) {
    qsizetype end = offset;
    while (end < contents.size() && contents[end] != '\n') {
      ++end;
    }
    const QByteArrayView line = contents.sliced(offset, end - offset);
    qsizetype i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
      ++i;
    }
    if (i < line.size() && line[i] == '*') {
      ++count;
    }
    if (end == contents.size()) {
      break;
    }
    offset = end + 1;
  }
  return count;
}

bool isSuspiciousActiveDrop(int profileCount, int candidateCount)
{
  if (profileCount <= 10 || candidateCount < 0) {
    return false;
  }
  const int absoluteDrop = profileCount - candidateCount;
  const double relativeDrop =
      static_cast<double>(absoluteDrop) / static_cast<double>(profileCount);
  return absoluteDrop > 10 && relativeDrop > 0.30;
}

bool isSameFile(const QString& path, const Snapshot& snapshot)
{
  QString effective;
  if (!resolveRegularCaseAlias(path, effective)) {
    return false;
  }
#ifdef Q_OS_UNIX
  struct stat status;
  const QByteArray encoded = QFile::encodeName(effective);
  return ::lstat(encoded.constData(), &status) == 0 && S_ISREG(status.st_mode) &&
         identityOf(status) == snapshot.identity;
#else
  return QFileInfo(effective).canonicalFilePath() == snapshot.identity;
#endif
}

bool publish(MOBase::TransactionalWriteFile& transaction,
             const Snapshot& snapshot, QString& error)
{
  if (!snapshot.modificationTime.isValid() ||
      !transaction.setModificationTime(snapshot.modificationTime) ||
      !transaction.replaceWith(snapshot.contents)) {
    error = transaction.errorString();
    return false;
  }
  return true;
}

}  // namespace WinePluginListSync
