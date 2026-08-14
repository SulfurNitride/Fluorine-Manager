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
#include <utility>

#ifdef Q_OS_UNIX
#include <cstring>
#include <sys/stat.h>
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
  ino_t inode  = 0;
  mode_t mode  = 0;
  off_t size   = 0;
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
  if (::lstat(encoded.constData(), &status) == 0) {
    identity.exists = true;
    identity.device = status.st_dev;
    identity.inode  = status.st_ino;
    identity.mode   = status.st_mode;
    identity.size   = status.st_size;
    identity.modified = modificationTime(status);
    identity.changed  = changeTime(status);
    return true;
  }

  if (errno == ENOENT) {
    identity = {};
    return true;
  }

  error = QString::fromLocal8Bit(std::strerror(errno));
  return false;
#else
  const QFileInfo info(path);
  identity.exists        = info.exists() || info.isSymLink();
  identity.symlink       = info.isSymLink();
  identity.regular       = info.isFile();
  identity.canonicalPath = info.canonicalFilePath();
  identity.size          = info.size();
  identity.modified      = info.lastModified();
  identity.changed       = info.metadataChangeTime();
  return true;
#endif
}

bool sameIdentity(const LeafIdentity& lhs, const LeafIdentity& rhs)
{
  if (lhs.exists != rhs.exists) {
    return false;
  }
  if (!lhs.exists) {
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

QString canonicalDirectory(const QString& path)
{
  QDir directory(path);
  const QString canonical = directory.canonicalPath();
  return canonical.isEmpty() ? QDir::cleanPath(directory.absolutePath()) : canonical;
}

}  // namespace

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
    if (requestedPath.isEmpty()) {
      preparationError = QObject::tr("The destination path is empty.");
      return;
    }

    QString inspectError;
    if (!inspectLeaf(requestedPath, requestedIdentity, inspectError)) {
      preparationError = QObject::tr("Could not inspect '%1': %2")
                             .arg(requestedPath, inspectError);
      return;
    }

    effectivePath     = requestedPath;
    effectiveIdentity = requestedIdentity;
    if (isSymlink(requestedIdentity)) {
      const QFileInfo aliasInfo(requestedPath);
      const QString targetPath = aliasInfo.symLinkTarget();
      if (targetPath.isEmpty()) {
        preparationError =
            QObject::tr("Refusing dangling symbolic link '%1'.").arg(requestedPath);
        return;
      }

      LeafIdentity targetIdentity;
      if (!inspectLeaf(targetPath, targetIdentity, inspectError)) {
        preparationError =
            QObject::tr("Could not inspect symbolic-link target '%1': %2")
                .arg(targetPath, inspectError);
        return;
      }

      const QFileInfo targetInfo(targetPath);
      const bool sameDirectory =
          canonicalDirectory(aliasInfo.absolutePath()) ==
          canonicalDirectory(targetInfo.absolutePath());
      const bool sameName = aliasInfo.fileName().compare(
                                targetInfo.fileName(), Qt::CaseInsensitive) == 0;

      if (!targetIdentity.exists || isSymlink(targetIdentity) ||
          !isRegular(targetIdentity) || !sameDirectory || !sameName) {
        preparationError =
            QObject::tr("Refusing unsupported symbolic-link destination '%1'.")
                .arg(requestedPath);
        return;
      }

      effectivePath     = targetPath;
      effectiveIdentity = targetIdentity;
    } else if (requestedIdentity.exists && !isRegular(requestedIdentity)) {
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
  QDateTime modificationTime;
  bool attempted = false;
};

TransactionalWriteFile::TransactionalWriteFile(QString fileName)
    : d(std::make_unique<Impl>(std::move(fileName)))
{}

TransactionalWriteFile::~TransactionalWriteFile() noexcept = default;

bool TransactionalWriteFile::setModificationTime(
    const QDateTime& modificationTime)
{
  if (d->attempted) {
    return d->fail(QObject::tr("The file transaction has already been used."));
  }
  if (!modificationTime.isValid()) {
    return d->fail(QObject::tr("The requested modification time is invalid."));
  }
  d->modificationTime = modificationTime;
  d->error.clear();
  return true;
}

bool TransactionalWriteFile::replaceWith(QByteArrayView contents)
{
  if (d->attempted) {
    return d->fail(QObject::tr("The file transaction has already been used."));
  }
  d->attempted = true;
  d->error.clear();

  if (!d->preparationError.isEmpty()) {
    return d->fail(d->preparationError);
  }

  QSaveFile file(d->effectivePath);
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly)) {
    return d->fail(QObject::tr("Could not create a temporary file for '%1': %2")
                       .arg(d->requestedPath, file.errorString()));
  }

  qsizetype offset = 0;
  while (offset < contents.size()) {
    const qint64 written =
        file.write(contents.data() + offset, contents.size() - offset);
    if (written <= 0) {
      const QString detail = file.errorString();
      file.cancelWriting();
      return d->fail(QObject::tr("Could not write '%1': %2")
                         .arg(d->requestedPath, detail));
    }
    offset += written;
  }

  if (!file.flush()) {
    const QString detail = file.errorString();
    file.cancelWriting();
    return d->fail(QObject::tr("Could not flush '%1': %2")
                       .arg(d->requestedPath, detail));
  }

  if (d->modificationTime.isValid() &&
      !file.setFileTime(d->modificationTime, QFileDevice::FileModificationTime)) {
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
      !sameIdentity(d->effectiveIdentity, currentEffective)) {
    file.cancelWriting();
    return d->fail(QObject::tr("Destination '%1' changed during publication.")
                       .arg(d->requestedPath));
  }

  if (!file.commit()) {
    return d->fail(QObject::tr("Could not replace '%1': %2")
                       .arg(d->requestedPath, file.errorString()));
  }

  return true;
}

QString TransactionalWriteFile::errorString() const
{
  return d->error;
}

}  // namespace MOBase
