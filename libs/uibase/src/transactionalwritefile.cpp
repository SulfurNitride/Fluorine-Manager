/*
Copyright (C) 2026 Fluorine Manager contributors.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

#include <uibase/transactionalwritefile.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSaveFile>

#include <cerrno>
#include <optional>
#include <utility>

#ifdef Q_OS_UNIX
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace MOBase
{

namespace
{

struct LeafIdentity
{
  bool exists = false;
#ifdef Q_OS_UNIX
  dev_t device = 0;
  ino_t inode = 0;
  mode_t mode = 0;
  off_t size = 0;
  timespec modified{};
  timespec changed{};
#else
  bool symlink = false;
  bool regular = false;
  QString canonicalPath;
  qint64 size = 0;
  QDateTime modified;
  QDateTime changed;
#endif
};

#ifdef Q_OS_UNIX
timespec modificationTime(const struct stat& status)
{
#ifdef Q_OS_DARWIN
  return status.st_mtimespec;
#else
  return status.st_mtim;
#endif
}

timespec changeTime(const struct stat& status)
{
#ifdef Q_OS_DARWIN
  return status.st_ctimespec;
#else
  return status.st_ctim;
#endif
}

bool sameTime(const timespec& lhs, const timespec& rhs)
{
  return lhs.tv_sec == rhs.tv_sec && lhs.tv_nsec == rhs.tv_nsec;
}
#endif

bool inspectLeaf(const QString& path, LeafIdentity& identity, QString& error)
{
#ifdef Q_OS_UNIX
  struct stat status;
  const QByteArray encoded = QFile::encodeName(path);
  if (::lstat(encoded.constData(), &status) == 0)
  {
    identity.exists = true;
    identity.device = status.st_dev;
    identity.inode = status.st_ino;
    identity.mode = status.st_mode;
    identity.size = status.st_size;
    identity.modified = modificationTime(status);
    identity.changed = changeTime(status);
    return true;
  }

  if (errno == ENOENT)
  {
    identity = {};
    return true;
  }

  error = QString::fromLocal8Bit(std::strerror(errno));
  return false;
#else
  const QFileInfo info(path);
  identity.exists = info.exists() || info.isSymLink();
  identity.symlink = info.isSymLink();
  identity.regular = info.isFile();
  identity.canonicalPath = info.canonicalFilePath();
  identity.size = info.size();
  identity.modified = info.lastModified();
  identity.changed = info.metadataChangeTime();
  return true;
#endif
}

#ifdef Q_OS_UNIX
bool inspectDescriptor(int descriptor, LeafIdentity& identity, QString& error)
{
  struct stat status;
  if (::fstat(descriptor, &status) != 0)
  {
    error = QString::fromLocal8Bit(std::strerror(errno));
    return false;
  }
  identity.exists = true;
  identity.device = status.st_dev;
  identity.inode = status.st_ino;
  identity.mode = status.st_mode;
  identity.size = status.st_size;
  identity.modified = modificationTime(status);
  identity.changed = changeTime(status);
  return true;
}
#endif

bool sameIdentity(const LeafIdentity& lhs, const LeafIdentity& rhs)
{
  if (lhs.exists != rhs.exists)
  {
    return false;
  }
  if (!lhs.exists)
  {
    return true;
  }
#ifdef Q_OS_UNIX
  return lhs.device == rhs.device && lhs.inode == rhs.inode &&
         (lhs.mode & S_IFMT) == (rhs.mode & S_IFMT) && lhs.size == rhs.size &&
         sameTime(lhs.modified, rhs.modified) && sameTime(lhs.changed, rhs.changed);
#else
  return lhs.symlink == rhs.symlink && lhs.regular == rhs.regular &&
         lhs.canonicalPath == rhs.canonicalPath && lhs.size == rhs.size &&
         lhs.modified == rhs.modified && lhs.changed == rhs.changed;
#endif
}

bool sameContentGeneration(const LeafIdentity& lhs, const LeafIdentity& rhs)
{
  if (lhs.exists != rhs.exists)
  {
    return false;
  }
  if (!lhs.exists)
  {
    return true;
  }
#ifdef Q_OS_UNIX
  return lhs.device == rhs.device && lhs.inode == rhs.inode &&
         (lhs.mode & S_IFMT) == (rhs.mode & S_IFMT) && lhs.size == rhs.size &&
         sameTime(lhs.modified, rhs.modified);
#else
  return lhs.symlink == rhs.symlink && lhs.regular == rhs.regular &&
         lhs.canonicalPath == rhs.canonicalPath && lhs.size == rhs.size &&
         lhs.modified == rhs.modified;
#endif
}

bool isSymlink(const LeafIdentity& identity)
{
#ifdef Q_OS_UNIX
  return identity.exists && S_ISLNK(identity.mode);
#else
  return identity.exists && identity.symlink;
#endif
}

bool isRegular(const LeafIdentity& identity)
{
#ifdef Q_OS_UNIX
  return identity.exists && S_ISREG(identity.mode);
#else
  return identity.exists && identity.regular && !identity.symlink;
#endif
}

QFileDevice::Permissions storedPermissions(const QString& path,
                                           const LeafIdentity& identity)
{
#ifdef Q_OS_UNIX
  Q_UNUSED(path);
  QFileDevice::Permissions permissions;
  if (identity.mode & S_IRUSR)
    permissions |= QFileDevice::ReadOwner | QFileDevice::ReadUser;
  if (identity.mode & S_IWUSR)
    permissions |= QFileDevice::WriteOwner | QFileDevice::WriteUser;
  if (identity.mode & S_IXUSR)
    permissions |= QFileDevice::ExeOwner | QFileDevice::ExeUser;
  if (identity.mode & S_IRGRP)
    permissions |= QFileDevice::ReadGroup;
  if (identity.mode & S_IWGRP)
    permissions |= QFileDevice::WriteGroup;
  if (identity.mode & S_IXGRP)
    permissions |= QFileDevice::ExeGroup;
  if (identity.mode & S_IROTH)
    permissions |= QFileDevice::ReadOther;
  if (identity.mode & S_IWOTH)
    permissions |= QFileDevice::WriteOther;
  if (identity.mode & S_IXOTH)
    permissions |= QFileDevice::ExeOther;
  return permissions;
#else
  Q_UNUSED(identity);
  return QFileInfo(path).permissions();
#endif
}

QString canonicalDirectory(const QString& path)
{
  QDir directory(path);
  const QString canonical = directory.canonicalPath();
  return canonical.isEmpty() ? QDir::cleanPath(directory.absolutePath()) : canonical;
}

} // namespace

class TransactionalWriteFile::Impl
{
public:
  explicit Impl(QString path) : requestedPath(std::move(path)) { prepare(); }

  bool fail(QString message)
  {
    error = std::move(message);
    return false;
  }

  void prepare()
  {
    if (requestedPath.isEmpty())
    {
      preparationError = QObject::tr("The destination path is empty.");
      return;
    }

    QString inspectError;
    if (!inspectLeaf(requestedPath, requestedIdentity, inspectError))
    {
      preparationError =
          QObject::tr("Could not inspect '%1': %2").arg(requestedPath, inspectError);
      return;
    }

    effectivePath = requestedPath;
    effectiveIdentity = requestedIdentity;
    if (isSymlink(requestedIdentity))
    {
      const QFileInfo aliasInfo(requestedPath);
      const QString targetPath = aliasInfo.symLinkTarget();
      if (targetPath.isEmpty())
      {
        preparationError =
            QObject::tr("Refusing dangling symbolic link '%1'.").arg(requestedPath);
        return;
      }

      LeafIdentity targetIdentity;
      if (!inspectLeaf(targetPath, targetIdentity, inspectError))
      {
        preparationError =
            QObject::tr("Could not inspect symbolic-link target '%1': %2")
                .arg(targetPath, inspectError);
        return;
      }

      const QFileInfo targetInfo(targetPath);
      const bool sameDirectory = canonicalDirectory(aliasInfo.absolutePath()) ==
                                 canonicalDirectory(targetInfo.absolutePath());
      const bool sameName =
          aliasInfo.fileName().compare(targetInfo.fileName(), Qt::CaseInsensitive) == 0;

      if (!targetIdentity.exists || isSymlink(targetIdentity) ||
          !isRegular(targetIdentity) || !sameDirectory || !sameName)
      {
        preparationError =
            QObject::tr("Refusing unsupported symbolic-link destination '%1'.")
                .arg(requestedPath);
        return;
      }

      effectivePath = targetPath;
      effectiveIdentity = targetIdentity;
    }
    else if (requestedIdentity.exists && !isRegular(requestedIdentity))
    {
      preparationError =
          QObject::tr("Refusing non-regular destination '%1'.").arg(requestedPath);
    }
  }

  QString requestedPath;
  QString effectivePath;
  LeafIdentity requestedIdentity;
  LeafIdentity effectiveIdentity;
  QString preparationError;
  QString error;
  std::optional<QFileDevice::Permissions> permissions;
  QDateTime modificationTime;
  bool attempted = false;
};

TransactionalWriteFile::TransactionalWriteFile(QString fileName)
    : d(std::make_unique<Impl>(std::move(fileName)))
{
}

TransactionalWriteFile::~TransactionalWriteFile() noexcept = default;

bool TransactionalWriteFile::readOriginal(QByteArray& contents, bool& present)
{
  contents.clear();
  present = false;
  d->error.clear();

  if (d->attempted)
  {
    return d->fail(QObject::tr("The file transaction has already been used."));
  }
  if (!d->preparationError.isEmpty())
  {
    return d->fail(d->preparationError);
  }

  LeafIdentity currentRequested;
  LeafIdentity currentEffective;
  QString inspectError;
  if (!inspectLeaf(d->requestedPath, currentRequested, inspectError) ||
      !inspectLeaf(d->effectivePath, currentEffective, inspectError) ||
      !sameIdentity(d->requestedIdentity, currentRequested) ||
      !sameIdentity(d->effectiveIdentity, currentEffective))
  {
    return d->fail(QObject::tr("Destination '%1' changed before it was read.")
                       .arg(d->requestedPath));
  }

  if (!d->effectiveIdentity.exists)
  {
    return true;
  }

#ifdef Q_OS_UNIX
  const QByteArray encoded = QFile::encodeName(d->effectivePath);
  const int descriptor =
      ::open(encoded.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  if (descriptor < 0)
  {
    return d->fail(
        QObject::tr("Could not safely open '%1': %2")
            .arg(d->requestedPath, QString::fromLocal8Bit(std::strerror(errno))));
  }

  LeafIdentity openedIdentity;
  if (!inspectDescriptor(descriptor, openedIdentity, inspectError) ||
      !sameIdentity(d->effectiveIdentity, openedIdentity))
  {
    ::close(descriptor);
    return d->fail(QObject::tr("Destination '%1' changed while it was opened.")
                       .arg(d->requestedPath));
  }

  QFile file;
  if (!file.open(descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle))
  {
    ::close(descriptor);
    return d->fail(QObject::tr("Could not read '%1': %2")
                       .arg(d->requestedPath, file.errorString()));
  }
#else
  QFile file(d->effectivePath);
  if (!file.open(QIODevice::ReadOnly))
  {
    return d->fail(QObject::tr("Could not read '%1': %2")
                       .arg(d->requestedPath, file.errorString()));
  }
#endif

  contents = file.readAll();
  if (file.error() != QFileDevice::NoError)
  {
    return d->fail(QObject::tr("Could not read '%1': %2")
                       .arg(d->requestedPath, file.errorString()));
  }

#ifdef Q_OS_UNIX
  LeafIdentity finishedIdentity;
  if (!inspectDescriptor(file.handle(), finishedIdentity, inspectError) ||
      !sameIdentity(d->effectiveIdentity, finishedIdentity))
  {
    return d->fail(QObject::tr("Destination '%1' changed while it was read.")
                       .arg(d->requestedPath));
  }
#else
  LeafIdentity finishedIdentity;
  if (!inspectLeaf(d->effectivePath, finishedIdentity, inspectError) ||
      !sameIdentity(d->effectiveIdentity, finishedIdentity))
  {
    return d->fail(QObject::tr("Destination '%1' changed while it was read.")
                       .arg(d->requestedPath));
  }
#endif

  present = true;
  return true;
}

bool TransactionalWriteFile::readPermissions(
    QFileDevice::Permissions& permissions)
{
  permissions = {};
  d->error.clear();
  if (d->attempted)
  {
    return d->fail(QObject::tr("The file transaction has already been used."));
  }
  if (!d->preparationError.isEmpty())
  {
    return d->fail(d->preparationError);
  }

  LeafIdentity currentRequested;
  LeafIdentity currentEffective;
  QString inspectError;
  if (!inspectLeaf(d->requestedPath, currentRequested, inspectError) ||
      !inspectLeaf(d->effectivePath, currentEffective, inspectError) ||
      !sameIdentity(d->requestedIdentity, currentRequested) ||
      !sameIdentity(d->effectiveIdentity, currentEffective))
  {
    return d->fail(QObject::tr("Destination '%1' changed before its permissions "
                               "were read.")
                       .arg(d->requestedPath));
  }
  if (d->effectiveIdentity.exists)
  {
    permissions = storedPermissions(d->effectivePath, d->effectiveIdentity);
  }
  return true;
}

bool TransactionalWriteFile::setModificationTime(const QDateTime& modificationTime)
{
  if (d->attempted)
  {
    return d->fail(QObject::tr("The file transaction has already been used."));
  }
  if (!modificationTime.isValid())
  {
    return d->fail(QObject::tr("The requested modification time is invalid."));
  }
  d->modificationTime = modificationTime;
  d->error.clear();
  return true;
}

bool TransactionalWriteFile::setPermissions(QFileDevice::Permissions permissions)
{
  if (d->attempted)
  {
    return d->fail(QObject::tr("The file transaction has already been used."));
  }
  d->permissions = permissions;
  d->error.clear();
  return true;
}

bool TransactionalWriteFile::replaceWith(QByteArrayView contents)
{
  if (d->attempted)
  {
    return d->fail(QObject::tr("The file transaction has already been used."));
  }
  d->attempted = true;
  d->error.clear();

  if (!d->preparationError.isEmpty())
  {
    return d->fail(d->preparationError);
  }

  // QSaveFile refuses to open an existing destination that is not writable by
  // the current process, even though publishing an adjacent temporary file is
  // otherwise permitted by the parent directory. A caller that explicitly
  // supplies final permissions has authenticated the old generation and may
  // replace such a leaf. Make only that already-opened inode writable for the
  // duration of QSaveFile::open(), then restore it before writing any bytes.
  // The temporary generation receives the caller's requested final mode below.
#ifdef Q_OS_UNIX
  int permissionDescriptor = -1;
  mode_t originalMode = 0;
#else
  bool permissionsTemporarilyChanged = false;
  QFileDevice::Permissions originalMode;
#endif

  const bool needsWritableOpen =
      d->permissions.has_value() && d->effectiveIdentity.exists &&
      !QFileInfo(d->effectivePath).isWritable();
  if (needsWritableOpen)
  {
#ifdef Q_OS_UNIX
    const QByteArray encoded = QFile::encodeName(d->effectivePath);
    permissionDescriptor =
        ::open(encoded.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (permissionDescriptor < 0)
    {
      return d->fail(
          QObject::tr("Could not safely open read-only destination '%1': %2")
              .arg(d->requestedPath,
                   QString::fromLocal8Bit(std::strerror(errno))));
    }

    LeafIdentity openedIdentity;
    QString permissionError;
    if (!inspectDescriptor(permissionDescriptor, openedIdentity, permissionError) ||
        !sameIdentity(d->effectiveIdentity, openedIdentity))
    {
      ::close(permissionDescriptor);
      return d->fail(
          QObject::tr("Read-only destination '%1' changed while it was opened.")
              .arg(d->requestedPath));
    }

    originalMode = openedIdentity.mode & 07777;
    if (::fchmod(permissionDescriptor, originalMode | S_IWUSR) != 0)
    {
      const QString detail = QString::fromLocal8Bit(std::strerror(errno));
      ::close(permissionDescriptor);
      return d->fail(
          QObject::tr("Could not temporarily enable publication for '%1': %2")
              .arg(d->requestedPath, detail));
    }

    LeafIdentity writableIdentity;
    if (!inspectDescriptor(permissionDescriptor, writableIdentity, permissionError) ||
        !sameContentGeneration(openedIdentity, writableIdentity))
    {
      ::fchmod(permissionDescriptor, originalMode);
      ::close(permissionDescriptor);
      return d->fail(
          QObject::tr("Read-only destination '%1' changed while publication was "
                      "enabled.")
              .arg(d->requestedPath));
    }
    d->effectiveIdentity = writableIdentity;
    if (!isSymlink(d->requestedIdentity))
    {
      d->requestedIdentity = writableIdentity;
    }
#else
    originalMode = storedPermissions(d->effectivePath, d->effectiveIdentity);
    if (!QFile::setPermissions(
            d->effectivePath,
            originalMode | QFileDevice::WriteOwner | QFileDevice::WriteUser))
    {
      return d->fail(
          QObject::tr("Could not temporarily enable publication for '%1'.")
              .arg(d->requestedPath));
    }
    permissionsTemporarilyChanged = true;

    LeafIdentity writableIdentity;
    QString permissionError;
    if (!inspectLeaf(d->effectivePath, writableIdentity, permissionError) ||
        !sameContentGeneration(d->effectiveIdentity, writableIdentity))
    {
      QFile::setPermissions(d->effectivePath, originalMode);
      return d->fail(
          QObject::tr("Read-only destination '%1' changed while publication was "
                      "enabled.")
              .arg(d->requestedPath));
    }
    d->effectiveIdentity = writableIdentity;
    if (!isSymlink(d->requestedIdentity))
    {
      d->requestedIdentity = writableIdentity;
    }
#endif
  }

  QSaveFile file(d->effectivePath);
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly))
  {
    const QString detail = file.errorString();
#ifdef Q_OS_UNIX
    if (permissionDescriptor >= 0)
    {
      ::fchmod(permissionDescriptor, originalMode);
      ::close(permissionDescriptor);
    }
#else
    if (permissionsTemporarilyChanged)
    {
      QFile::setPermissions(d->effectivePath, originalMode);
    }
#endif
    return d->fail(QObject::tr("Could not create a temporary file for '%1': %2")
                       .arg(d->requestedPath, detail));
  }

  if (needsWritableOpen)
  {
    LeafIdentity restoredIdentity;
    LeafIdentity currentRequested;
    LeafIdentity currentEffective;
    QString permissionError;
#ifdef Q_OS_UNIX
    const bool restored = ::fchmod(permissionDescriptor, originalMode) == 0 &&
                          inspectDescriptor(permissionDescriptor, restoredIdentity,
                                            permissionError);
    ::close(permissionDescriptor);
    permissionDescriptor = -1;
#else
    const bool restored = QFile::setPermissions(d->effectivePath, originalMode) &&
                          inspectLeaf(d->effectivePath, restoredIdentity,
                                      permissionError);
    permissionsTemporarilyChanged = false;
#endif
    if (!restored ||
        !sameContentGeneration(d->effectiveIdentity, restoredIdentity) ||
        !inspectLeaf(d->requestedPath, currentRequested, permissionError) ||
        !inspectLeaf(d->effectivePath, currentEffective, permissionError) ||
        !sameContentGeneration(restoredIdentity, currentEffective) ||
        (isSymlink(d->requestedIdentity)
             ? !sameIdentity(d->requestedIdentity, currentRequested)
             : !sameContentGeneration(restoredIdentity, currentRequested)))
    {
      file.cancelWriting();
      return d->fail(
          QObject::tr("Could not safely restore permissions for '%1'.")
              .arg(d->requestedPath));
    }
    d->effectiveIdentity = currentEffective;
    d->requestedIdentity = currentRequested;
  }

  if (d->permissions.has_value() && !file.setPermissions(*d->permissions))
  {
    const QString detail = file.errorString();
    file.cancelWriting();
    return d->fail(QObject::tr("Could not set permissions for '%1': %2")
                       .arg(d->requestedPath, detail));
  }

  qsizetype offset = 0;
  while (offset < contents.size())
  {
    const qint64 written =
        file.write(contents.data() + offset, contents.size() - offset);
    if (written <= 0)
    {
      const QString detail = file.errorString();
      file.cancelWriting();
      return d->fail(
          QObject::tr("Could not write '%1': %2").arg(d->requestedPath, detail));
    }
    offset += written;
  }

  if (!file.flush())
  {
    const QString detail = file.errorString();
    file.cancelWriting();
    return d->fail(
        QObject::tr("Could not flush '%1': %2").arg(d->requestedPath, detail));
  }

  if (d->modificationTime.isValid() &&
      !file.setFileTime(d->modificationTime, QFileDevice::FileModificationTime))
  {
    const QString detail = file.errorString();
    file.cancelWriting();
    return d->fail(QObject::tr("Could not set the modification time for '%1': %2")
                       .arg(d->requestedPath, detail));
  }

  LeafIdentity currentRequested;
  LeafIdentity currentEffective;
  QString inspectError;
  if (!inspectLeaf(d->requestedPath, currentRequested, inspectError) ||
      !inspectLeaf(d->effectivePath, currentEffective, inspectError) ||
      !sameIdentity(d->requestedIdentity, currentRequested) ||
      !sameIdentity(d->effectiveIdentity, currentEffective))
  {
    file.cancelWriting();
    return d->fail(QObject::tr("Destination '%1' changed during publication.")
                       .arg(d->requestedPath));
  }

  if (!file.commit())
  {
    return d->fail(QObject::tr("Could not replace '%1': %2")
                       .arg(d->requestedPath, file.errorString()));
  }

  return true;
}

QString TransactionalWriteFile::errorString() const
{
  return d->error;
}

} // namespace MOBase
