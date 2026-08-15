/*
Copyright (C) 2026 Fluorine Manager contributors.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

#include "winesavedeployment.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <cerrno>
#include <utility>

#ifdef Q_OS_UNIX
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef Q_OS_LINUX
#include <linux/fs.h>
#include <sys/syscall.h>
#endif
#endif

namespace WineSaveDeployment {
namespace {

enum class LeafType {
  Missing,
  RegularFile,
  Directory,
  Symlink,
  Other,
  Error,
};

struct FileIdentity {
  bool exists{false};
#ifdef Q_OS_UNIX
  dev_t device{};
  ino_t inode{};
  mode_t mode{};
  off_t size{};
  timespec modified{};
  timespec changed{};
#else
  bool symlink{false};
  bool regular{false};
  qint64 size{};
  QDateTime modified;
  QDateTime changed;
  QString canonicalPath;
#endif
};

struct TreeFile {
  QString relativePath;
  FileIdentity identity;
};

struct Slot {
  QString live;
  QString backup;
  QString marker;
  LeafType liveType{LeafType::Missing};
  LeafType backupType{LeafType::Missing};
  bool exactLink{false};
};

struct OwnerMarker {
  QString ownerId;
  QString profileRoot;
  QString livePath;
  QString intent;
  QString phase;
  QString retirement;
};

bool validRetirementPath(const Slot &slot, const QString &path) {
  if (path.isEmpty())
    return true;
  if (!QDir::isAbsolutePath(path))
    return false;

  const QFileInfo liveInfo(slot.live);
  const QFileInfo retirementInfo(path);
  if (QDir::cleanPath(retirementInfo.absolutePath()) !=
      QDir::cleanPath(liveInfo.absolutePath())) {
    return false;
  }

  const QString prefix =
      QStringLiteral(".mo2linux_synced_%1_").arg(liveInfo.fileName());
  if (!retirementInfo.fileName().startsWith(prefix))
    return false;
  const QString suffix = retirementInfo.fileName().mid(prefix.size());
  const QUuid generation(suffix);
  return !generation.isNull() &&
         generation.toString(QUuid::WithoutBraces)
                 .compare(suffix, Qt::CaseInsensitive) == 0;
}

Result fail(QString error) { return {false, std::move(error), false, false}; }

Result ok(QString warning = {}, bool topologyComplete = false) {
  return {true, std::move(warning), topologyComplete, false};
}

Result cleanupPending(QString error) {
  return {false, std::move(error), true, true};
}

#ifdef Q_OS_UNIX
timespec modificationTime(const struct stat &status) {
#ifdef Q_OS_DARWIN
  return status.st_mtimespec;
#else
  return status.st_mtim;
#endif
}

timespec changeTime(const struct stat &status) {
#ifdef Q_OS_DARWIN
  return status.st_ctimespec;
#else
  return status.st_ctim;
#endif
}

bool sameTime(const timespec &left, const timespec &right) {
  return left.tv_sec == right.tv_sec && left.tv_nsec == right.tv_nsec;
}

FileIdentity fromStat(const struct stat &status) {
  return {true,
          status.st_dev,
          status.st_ino,
          status.st_mode,
          status.st_size,
          modificationTime(status),
          changeTime(status)};
}
#endif

bool inspect(const QString &path, FileIdentity &identity, QString &error) {
#ifdef Q_OS_UNIX
  const QByteArray encoded = QFile::encodeName(path);
  struct stat status;
  if (::lstat(encoded.constData(), &status) == 0) {
    identity = fromStat(status);
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
  identity.exists = info.exists() || info.isSymLink();
  identity.symlink = info.isSymLink();
  identity.regular = info.isFile() && !info.isSymLink();
  identity.size = info.size();
  identity.modified = info.lastModified();
  identity.changed = info.metadataChangeTime();
  identity.canonicalPath = info.canonicalFilePath();
  return true;
#endif
}

LeafType leafType(const QString &path, QString &error) {
  FileIdentity identity;
  if (!inspect(path, identity, error)) {
    return LeafType::Error;
  }
  if (!identity.exists) {
    return LeafType::Missing;
  }
#ifdef Q_OS_UNIX
  if (S_ISLNK(identity.mode))
    return LeafType::Symlink;
  if (S_ISREG(identity.mode))
    return LeafType::RegularFile;
  if (S_ISDIR(identity.mode))
    return LeafType::Directory;
#else
  if (identity.symlink)
    return LeafType::Symlink;
  if (identity.regular)
    return LeafType::RegularFile;
  if (QFileInfo(path).isDir())
    return LeafType::Directory;
#endif
  return LeafType::Other;
}

bool sameIdentity(const FileIdentity &left, const FileIdentity &right) {
  if (left.exists != right.exists)
    return false;
  if (!left.exists)
    return true;
#ifdef Q_OS_UNIX
  return left.device == right.device && left.inode == right.inode &&
         (left.mode & S_IFMT) == (right.mode & S_IFMT) &&
         left.size == right.size && sameTime(left.modified, right.modified) &&
         sameTime(left.changed, right.changed);
#else
  return left.symlink == right.symlink && left.regular == right.regular &&
         left.size == right.size && left.modified == right.modified &&
         left.changed == right.changed &&
         left.canonicalPath == right.canonicalPath;
#endif
}

bool isRegular(const FileIdentity &identity) {
#ifdef Q_OS_UNIX
  return identity.exists && S_ISREG(identity.mode);
#else
  return identity.exists && identity.regular && !identity.symlink;
#endif
}

QString canonicalOrAbsolute(const QString &path) {
  const QFileInfo info(path);
  const QString canonical = info.canonicalFilePath();
  return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath()
                                             : canonical);
}

QString canonicalWithMissingTail(const QString &path) {
  QFileInfo current(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
  QStringList tail;
  while (!current.exists() && !current.isSymLink() &&
         !current.fileName().isEmpty()) {
    tail.prepend(current.fileName());
    current = QFileInfo(current.absolutePath());
  }

  QString resolved = current.canonicalFilePath();
  if (resolved.isEmpty())
    resolved = current.absoluteFilePath();
  for (const QString &component : tail) {
    resolved = QDir(resolved).filePath(component);
  }
  return QDir::cleanPath(resolved);
}

QString physicalLiveIdentity(const QString &exactLivePath) {
  const QFileInfo requested(
      QDir::cleanPath(QFileInfo(exactLivePath).absoluteFilePath()));
  const QString physicalParent =
      canonicalWithMissingTail(requested.absolutePath());
  return QDir(physicalParent).filePath(requested.fileName().toCaseFolded());
}

bool pathWithin(const QString &child, const QString &parent) {
  const QString relative = QDir(parent).relativeFilePath(child);
  return relative != QStringLiteral("..") &&
         !relative.startsWith(QStringLiteral("../")) &&
         !QDir::isAbsolutePath(relative);
}

Result ensurePrefixParent(const QString &prefixRoot, const QString &liveParent,
                          bool createMissing = true) {
  const QString prefix = canonicalOrAbsolute(prefixRoot);
  const QString parent =
      QDir::cleanPath(QFileInfo(liveParent).absoluteFilePath());
  if (!pathWithin(parent,
                  QDir::cleanPath(QFileInfo(prefixRoot).absoluteFilePath()))) {
    return fail(QStringLiteral("Wine save parent '%1' is outside prefix '%2'.")
                    .arg(parent, prefixRoot));
  }

  const QString relative =
      QDir(QDir::cleanPath(QFileInfo(prefixRoot).absoluteFilePath()))
          .relativeFilePath(parent);
  QString current = prefix;
  const QStringList components = relative.split('/', Qt::SkipEmptyParts);
  for (const QString &component : components) {
    if (component == QStringLiteral(".") || component == QStringLiteral("..")) {
      return fail(QStringLiteral("Unsafe Wine save path component '%1'.")
                      .arg(component));
    }
    current = QDir(current).filePath(component);
    QString detail;
    LeafType type = leafType(current, detail);
    if (type == LeafType::Missing) {
      if (!createMissing)
        return ok();
      if (!QDir().mkdir(current)) {
        return fail(QStringLiteral("Could not create Wine save directory '%1'.")
                        .arg(current));
      }
      type = leafType(current, detail);
    }
    if (type != LeafType::Directory) {
      return fail(
          QStringLiteral(
              "Refusing symlinked or non-directory Wine path component '%1'.")
              .arg(current));
    }
    const QString resolved = QFileInfo(current).canonicalFilePath();
    if (resolved.isEmpty() || !pathWithin(resolved, prefix)) {
      return fail(QStringLiteral("Wine save path component '%1' escapes '%2'.")
                      .arg(current, prefix));
    }
  }
  return ok();
}

Result ensureProfileRoot(const QString &profileRoot) {
  QString detail;
  LeafType type = leafType(profileRoot, detail);
  if (type == LeafType::Directory)
    return ok();
  if (type != LeafType::Missing) {
    return fail(
        QStringLiteral("Profile save root '%1' is not a real directory.")
            .arg(profileRoot));
  }
  const QString parent = QFileInfo(profileRoot).absolutePath();
  if (leafType(parent, detail) != LeafType::Directory ||
      !QDir().mkdir(profileRoot) ||
      leafType(profileRoot, detail) != LeafType::Directory) {
    return fail(
        QStringLiteral("Could not safely create profile save root '%1'.")
            .arg(profileRoot));
  }
  return ok();
}

Result validateRoots(const QString &prefixRoot, const QString &profileRoot,
                     const QString &exactLivePath) {
  QString detail;
  if (leafType(prefixRoot, detail) != LeafType::Directory) {
    return fail(
        QStringLiteral("Wine prefix root '%1' is not a real directory: %2")
            .arg(prefixRoot, detail));
  }
  Result result = ensureProfileRoot(profileRoot);
  if (!result)
    return result;

  result =
      ensurePrefixParent(prefixRoot, QFileInfo(exactLivePath).absolutePath());
  if (!result)
    return result;

  const QString prefix = canonicalOrAbsolute(prefixRoot);
  const QString parent = canonicalOrAbsolute(QFileInfo(exactLivePath).path());
  const QString profile = canonicalOrAbsolute(profileRoot);
  if (!pathWithin(parent, prefix)) {
    return fail(QStringLiteral("Wine save path '%1' escapes prefix '%2'.")
                    .arg(exactLivePath, prefixRoot));
  }
  if (pathWithin(profile, parent) || pathWithin(parent, profile)) {
    return fail(QStringLiteral("Wine and profile save roots overlap."));
  }
  return ok();
}

bool managedLink(const QString &path, const QString &profileRoot) {
  if (!QFileInfo(path).isSymLink())
    return false;
  const QString target = QFileInfo(path).canonicalFilePath();
  const QString profile = QFileInfo(profileRoot).canonicalFilePath();
  return !target.isEmpty() && !profile.isEmpty() && target == profile;
}

QList<Slot> entriesFor(const QString &exactLivePath) {
  const QFileInfo exactInfo(exactLivePath);
  const QString exact = QDir::cleanPath(exactInfo.absoluteFilePath());
  const QString lower =
      QDir(exactInfo.absolutePath()).filePath(exactInfo.fileName().toLower());
  QList<Slot> entries;
  const auto append = [&entries](const QString &live) {
    Slot slot;
    slot.live = QDir::cleanPath(live);
    slot.backup = backupPathFor(slot.live);
    const QFileInfo info(slot.live);
    slot.marker = QDir(info.absolutePath())
                      .filePath(QStringLiteral(".fluorine-save-owner-%1.json")
                                    .arg(info.fileName()));
    entries.append(std::move(slot));
  };
  append(exact);
  if (QDir::cleanPath(lower) != exact)
    append(lower);
  return entries;
}

Result loadMarker(const Slot &slot, OwnerMarker &marker, bool &present) {
  present = false;
  QString detail;
  const LeafType type = leafType(slot.marker, detail);
  if (type == LeafType::Missing)
    return ok();
  if (type != LeafType::RegularFile) {
    return fail(QStringLiteral("Refusing unsafe save owner marker '%1'.")
                    .arg(slot.marker));
  }
  QFile file(slot.marker);
  if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024) {
    return fail(QStringLiteral("Could not safely read save owner marker '%1'.")
                    .arg(slot.marker));
  }
  QJsonParseError parseError;
  const QJsonDocument document =
      QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    return fail(
        QStringLiteral("Invalid save owner marker '%1'.").arg(slot.marker));
  }
  const QJsonObject object = document.object();
  if (object.value(QStringLiteral("version")).toInt() != 1) {
    return fail(
        QStringLiteral("Unsupported save owner marker '%1'.").arg(slot.marker));
  }
  marker.ownerId = object.value(QStringLiteral("owner")).toString();
  marker.profileRoot =
      QDir::cleanPath(object.value(QStringLiteral("profile")).toString());
  marker.livePath =
      QDir::cleanPath(object.value(QStringLiteral("live")).toString());
  marker.intent = object.value(QStringLiteral("intent")).toString();
  marker.phase = object.value(QStringLiteral("phase")).toString();
  const QString retirementValue =
      object.value(QStringLiteral("retirement")).toString();
  marker.retirement =
      retirementValue.isEmpty() ? QString{} : QDir::cleanPath(retirementValue);
  if (marker.ownerId.isEmpty() || marker.profileRoot.isEmpty() ||
      marker.livePath.isEmpty() || marker.livePath != slot.live ||
      !QStringList{QStringLiteral("rollback"), QStringLiteral("publish")}
           .contains(marker.intent) ||
      !QStringList{QStringLiteral("preparing"), QStringLiteral("active"),
                   QStringLiteral("publishing"), QStringLiteral("published"),
                   QStringLiteral("retiring"), QStringLiteral("restoring"),
                   QStringLiteral("restored")}
           .contains(marker.phase) ||
      !validRetirementPath(slot, marker.retirement)) {
    return fail(QStringLiteral("Invalid save owner marker identity '%1'.")
                    .arg(slot.marker));
  }
  present = true;
  return ok();
}

Result storeMarker(const Slot &slot, const OwnerMarker &marker,
                   bool allowCreate) {
  if (marker.ownerId.trimmed().isEmpty()) {
    return fail(QStringLiteral("Save deployment owner is empty."));
  }
  OwnerMarker existing;
  bool present = false;
  Result result = loadMarker(slot, existing, present);
  if (!result)
    return result;
  if (present) {
    if (existing.ownerId != marker.ownerId ||
        existing.profileRoot != marker.profileRoot ||
        existing.livePath != marker.livePath) {
      return fail(
          QStringLiteral("Save owner marker '%1' belongs to another launch.")
              .arg(slot.marker));
    }
  } else if (!allowCreate) {
    return fail(
        QStringLiteral("Save owner marker '%1' disappeared.").arg(slot.marker));
  }

  const QJsonObject object{{QStringLiteral("version"), 1},
                           {QStringLiteral("owner"), marker.ownerId},
                           {QStringLiteral("profile"), marker.profileRoot},
                           {QStringLiteral("live"), marker.livePath},
                           {QStringLiteral("intent"), marker.intent},
                           {QStringLiteral("phase"), marker.phase},
                           {QStringLiteral("retirement"), marker.retirement}};
  const QByteArray bytes =
      QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
  QSaveFile file(slot.marker);
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly) ||
      !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
      file.write(bytes) != bytes.size() || !file.flush() || !file.commit()) {
    file.cancelWriting();
    return fail(QStringLiteral("Could not publish save owner marker '%1': %2")
                    .arg(slot.marker, file.errorString()));
  }
  return ok();
}

Result writeMarker(const Slot &slot, const QString &profileRoot,
                   const QString &ownerId, const QString &phase,
                   const QString &retirement = {}) {
  return storeMarker(slot,
                     OwnerMarker{ownerId, canonicalOrAbsolute(profileRoot),
                                 slot.live, QStringLiteral("rollback"), phase,
                                 retirement.isEmpty()
                                     ? QString{}
                                     : QDir::cleanPath(retirement)},
                     /*allowCreate=*/true);
}

Result updateMarkerIntent(const Slot &slot, OwnerMarker &marker,
                          const QString &intent) {
  marker.intent = intent;
  return storeMarker(slot, marker, /*allowCreate=*/false);
}

Result updateMarker(const Slot &slot, OwnerMarker &marker, const QString &phase,
                    const QString &retirement = {}) {
  marker.phase = phase;
  marker.retirement =
      retirement.isEmpty() ? QString{} : QDir::cleanPath(retirement);
  return storeMarker(slot, marker, /*allowCreate=*/false);
}

Result requireMarker(const Slot &slot, const QString &profileRoot,
                     const QString &ownerId) {
  OwnerMarker marker;
  bool present = false;
  Result result = loadMarker(slot, marker, present);
  if (!result)
    return result;
  if (!present || marker.ownerId != ownerId ||
      marker.profileRoot != canonicalOrAbsolute(profileRoot)) {
    return fail(QStringLiteral("Save state '%1' is not owned by this launch.")
                    .arg(slot.live));
  }
  return ok();
}

Result removeOwnedMarker(const Slot &slot, const QString &profileRoot,
                         const QString &ownerId) {
  Result result = requireMarker(slot, profileRoot, ownerId);
  if (!result)
    return result;
  if (!QFile::remove(slot.marker)) {
    return fail(QStringLiteral("Could not retire save owner marker '%1'.")
                    .arg(slot.marker));
  }
  return ok();
}

Result classify(QList<Slot> &entries, const QString &profileRoot) {
  for (Slot &slot : entries) {
    QString detail;
    slot.exactLink = false;
    slot.liveType = leafType(slot.live, detail);
    if (slot.liveType == LeafType::Error) {
      return fail(QStringLiteral("Could not inspect Wine save path '%1': %2")
                      .arg(slot.live, detail));
    }
    slot.backupType = leafType(slot.backup, detail);
    if (slot.backupType == LeafType::Error) {
      return fail(QStringLiteral("Could not inspect Wine save backup '%1': %2")
                      .arg(slot.backup, detail));
    }
    if (slot.backupType != LeafType::Missing &&
        slot.backupType != LeafType::Directory) {
      return fail(QStringLiteral("Refusing non-directory save backup '%1'.")
                      .arg(slot.backup));
    }
    if (slot.liveType == LeafType::Symlink) {
      slot.exactLink = managedLink(slot.live, profileRoot);
      if (!slot.exactLink) {
        return fail(
            QStringLiteral("Refusing foreign save link '%1'.").arg(slot.live));
      }
    } else if (slot.liveType != LeafType::Missing &&
               slot.liveType != LeafType::Directory) {
      return fail(QStringLiteral("Refusing non-directory Wine save leaf '%1'.")
                      .arg(slot.live));
    }
  }
  return ok();
}

QString uniqueRetirementPath(const QString &livePath) {
  const QFileInfo info(livePath);
  return QDir(info.absolutePath())
      .filePath(QStringLiteral(".mo2linux_synced_%1_%2")
                    .arg(info.fileName(),
                         QUuid::createUuid().toString(QUuid::WithoutBraces)));
}

Result safeParent(const QString &root, const QString &relativeFile) {
  const QStringList parts = relativeFile.split('/', Qt::SkipEmptyParts);
  QString current = QDir::cleanPath(root);
  for (qsizetype i = 0; i + 1 < parts.size(); ++i) {
    current = QDir(current).filePath(parts[i]);
    QString detail;
    const LeafType type = leafType(current, detail);
    if (type == LeafType::Missing) {
      if (!QDir().mkdir(current)) {
        return fail(QStringLiteral("Could not create save directory '%1'.")
                        .arg(current));
      }
    } else if (type != LeafType::Directory) {
      return fail(
          QStringLiteral("Refusing unsafe save directory '%1'.").arg(current));
    }
  }
  return ok();
}

Result inventory(const QString &root, QList<TreeFile> &files) {
  QString detail;
  if (leafType(root, detail) != LeafType::Directory) {
    return fail(
        QStringLiteral("Save source '%1' is not a real directory.").arg(root));
  }

  QDirIterator iterator(root,
                        QDir::AllEntries | QDir::Hidden | QDir::System |
                            QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    const QString path = iterator.next();
    const QString relative = QDir(root).relativeFilePath(path);
    FileIdentity identity;
    if (!inspect(path, identity, detail)) {
      return fail(QStringLiteral("Could not inspect save leaf '%1': %2")
                      .arg(path, detail));
    }
#ifdef Q_OS_UNIX
    if (S_ISDIR(identity.mode))
      continue;
    if (!S_ISREG(identity.mode)) {
#else
    if (QFileInfo(path).isDir() && !QFileInfo(path).isSymLink())
      continue;
    if (!isRegular(identity)) {
#endif
      return fail(
          QStringLiteral("Refusing non-regular save leaf '%1'.").arg(path));
    }
    files.append({relative, identity});
  }
  std::sort(files.begin(), files.end(),
            [](const TreeFile &left, const TreeFile &right) {
              return left.relativePath < right.relativePath;
            });
  return ok();
}

bool sameInventory(const QList<TreeFile> &left, const QList<TreeFile> &right) {
  if (left.size() != right.size())
    return false;
  for (qsizetype i = 0; i < left.size(); ++i) {
    if (left[i].relativePath != right[i].relativePath ||
        !sameIdentity(left[i].identity, right[i].identity)) {
      return false;
    }
  }
  return true;
}

Result copyFile(const QString &sourcePath, const FileIdentity &expectedSource,
                const QString &destinationPath) {
  const Result parent = safeParent(QFileInfo(destinationPath).absolutePath(),
                                   QFileInfo(destinationPath).fileName());
  if (!parent)
    return parent;

  QString detail;
  FileIdentity destinationBefore;
  if (!inspect(destinationPath, destinationBefore, detail)) {
    return fail(QStringLiteral("Could not inspect destination '%1': %2")
                    .arg(destinationPath, detail));
  }
  if (destinationBefore.exists && !isRegular(destinationBefore)) {
    return fail(QStringLiteral("Refusing non-regular save destination '%1'.")
                    .arg(destinationPath));
  }

  QFile source;
  source.setFileName(sourcePath);
#ifdef Q_OS_UNIX
  const QByteArray encoded = QFile::encodeName(sourcePath);
  const int descriptor = ::open(encoded.constData(),
                                O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
  if (descriptor < 0) {
    return fail(
        QStringLiteral("Could not open save source '%1': %2")
            .arg(sourcePath, QString::fromLocal8Bit(std::strerror(errno))));
  }
  struct stat sourceStatus;
  if (::fstat(descriptor, &sourceStatus) != 0 ||
      !S_ISREG(sourceStatus.st_mode) ||
      !sameIdentity(expectedSource, fromStat(sourceStatus))) {
    ::close(descriptor);
    return fail(QStringLiteral("Save source '%1' changed before publication.")
                    .arg(sourcePath));
  }
  if (!source.open(descriptor, QIODevice::ReadOnly,
                   QFileDevice::AutoCloseHandle)) {
    ::close(descriptor);
    return fail(
        QStringLiteral("Could not adopt save source '%1'.").arg(sourcePath));
  }
#else
  if (!source.open(QIODevice::ReadOnly)) {
    return fail(QStringLiteral("Could not open save source '%1': %2")
                    .arg(sourcePath, source.errorString()));
  }
#endif

  QSaveFile destination(destinationPath);
  destination.setDirectWriteFallback(false);
  if (!destination.open(QIODevice::WriteOnly)) {
    return fail(QStringLiteral("Could not create temporary save for '%1': %2")
                    .arg(destinationPath, destination.errorString()));
  }

  QByteArray buffer;
  buffer.resize(1024 * 1024);
  for (;;) {
    const qint64 count = source.read(buffer.data(), buffer.size());
    if (count < 0) {
      destination.cancelWriting();
      return fail(QStringLiteral("Could not read save source '%1': %2")
                      .arg(sourcePath, source.errorString()));
    }
    if (count == 0)
      break;
    qint64 offset = 0;
    while (offset < count) {
      const qint64 written =
          destination.write(buffer.constData() + offset, count - offset);
      if (written <= 0) {
        const QString error = destination.errorString();
        destination.cancelWriting();
        return fail(QStringLiteral("Could not write save destination '%1': %2")
                        .arg(destinationPath, error));
      }
      offset += written;
    }
  }

  if (!destination.flush()) {
    const QString error = destination.errorString();
    destination.cancelWriting();
    return fail(QStringLiteral("Could not flush save destination '%1': %2")
                    .arg(destinationPath, error));
  }

  FileIdentity sourceAfter;
#ifdef Q_OS_UNIX
  if (::fstat(source.handle(), &sourceStatus) != 0) {
    destination.cancelWriting();
    return fail(QStringLiteral("Could not revalidate save source '%1'.")
                    .arg(sourcePath));
  }
  sourceAfter = fromStat(sourceStatus);
#else
  if (!inspect(sourcePath, sourceAfter, detail)) {
    destination.cancelWriting();
    return fail(QStringLiteral("Could not revalidate save source '%1': %2")
                    .arg(sourcePath, detail));
  }
#endif
  FileIdentity destinationAfter;
  if (!sameIdentity(expectedSource, sourceAfter) ||
      !inspect(destinationPath, destinationAfter, detail) ||
      !sameIdentity(destinationBefore, destinationAfter)) {
    destination.cancelWriting();
    return fail(QStringLiteral(
        "Save source or destination changed during publication."));
  }

#ifdef Q_OS_UNIX
  const QDateTime sourceModified =
      QDateTime::fromSecsSinceEpoch(expectedSource.modified.tv_sec);
#else
  const QDateTime sourceModified = expectedSource.modified;
#endif
  if (sourceModified.isValid() &&
      !destination.setFileTime(sourceModified,
                               QFileDevice::FileModificationTime)) {
    const QString error = destination.errorString();
    destination.cancelWriting();
    return fail(QStringLiteral("Could not preserve save timestamp for '%1': %2")
                    .arg(destinationPath, error));
  }
  if (!destination.commit()) {
    return fail(QStringLiteral("Could not publish save destination '%1': %2")
                    .arg(destinationPath, destination.errorString()));
  }
  return ok();
}

Result publishTrees(const QStringList &sourceRoots,
                    const QString &destinationRoot, bool mirrorDeletions) {
  if (sourceRoots.isEmpty())
    return ok();
  const QString destination =
      QDir::cleanPath(QFileInfo(destinationRoot).absoluteFilePath());
  QString detail;
  const LeafType destinationType = leafType(destination, detail);
  if (destinationType != LeafType::Missing &&
      destinationType != LeafType::Directory) {
    return fail(QStringLiteral("Refusing unsafe profile save root '%1'.")
                    .arg(destination));
  }
  const QString destinationParent = QFileInfo(destination).absolutePath();
  if (leafType(destinationParent, detail) != LeafType::Directory) {
    return fail(
        QStringLiteral("Profile save parent '%1' is not a real directory.")
            .arg(destinationParent));
  }

  FileIdentity destinationBefore;
  if (!inspect(destination, destinationBefore, detail)) {
    return fail(QStringLiteral("Could not inspect profile save root '%1': %2")
                    .arg(destination, detail));
  }

  struct SourceSnapshot {
    QString root;
    QList<TreeFile> files;
  };
  QList<SourceSnapshot> sources;
  QMap<QString, QPair<QString, TreeFile>> merged;
  for (const QString &sourceRoot : sourceRoots) {
    const QString source =
        QDir::cleanPath(QFileInfo(sourceRoot).absoluteFilePath());
    if (source == destination || pathWithin(source, destination) ||
        pathWithin(destination, source)) {
      return fail(QStringLiteral("Save source and destination overlap."));
    }
    SourceSnapshot snapshot{source, {}};
    Result result = inventory(source, snapshot.files);
    if (!result)
      return result;
    for (const TreeFile &file : snapshot.files) {
      if (merged.contains(file.relativePath)) {
        return fail(
            QStringLiteral(
                "Case-variant save trees both contain '%1'; preserving both.")
                .arg(file.relativePath));
      }
      merged.insert(file.relativePath, {source, file});
    }
    sources.append(std::move(snapshot));
  }

  const QString stage =
      QDir(destinationParent)
          .filePath(
              QStringLiteral(".fluorine-save-stage-%1")
                  .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
  if (!QDir().mkdir(stage)) {
    return fail(
        QStringLiteral("Could not create profile save staging directory '%1'.")
            .arg(stage));
  }
  const auto discardStage = [&stage]() {
    return QDir(stage).removeRecursively();
  };

  if (destinationType == LeafType::Directory) {
    QList<TreeFile> destinationFiles;
    Result result = inventory(destination, destinationFiles);
    if (!result) {
      discardStage();
      return result;
    }
    if (!mirrorDeletions) {
      for (const TreeFile &file : destinationFiles) {
        result = safeParent(stage, file.relativePath);
        if (result) {
          result =
              copyFile(QDir(destination).filePath(file.relativePath),
                       file.identity, QDir(stage).filePath(file.relativePath));
        }
        if (!result) {
          discardStage();
          return result;
        }
      }
    }
  }

  for (auto it = merged.cbegin(); it != merged.cend(); ++it) {
    Result result = safeParent(stage, it.key());
    if (result) {
      result = copyFile(QDir(it->first).filePath(it.key()), it->second.identity,
                        QDir(stage).filePath(it.key()));
    }
    if (!result) {
      discardStage();
      return result;
    }
  }

  for (const SourceSnapshot &source : sources) {
    QList<TreeFile> after;
    Result result = inventory(source.root, after);
    if (!result || !sameInventory(source.files, after)) {
      discardStage();
      return result ? fail(QStringLiteral(
                          "Save source changed during tree publication."))
                    : result;
    }
  }

  FileIdentity destinationAfter;
  if (!inspect(destination, destinationAfter, detail) ||
      !sameIdentity(destinationBefore, destinationAfter)) {
    discardStage();
    return fail(QStringLiteral(
        "Profile save destination changed during tree publication."));
  }

  if (destinationType == LeafType::Missing) {
    if (!QDir().rename(stage, destination)) {
      discardStage();
      return fail(QStringLiteral("Could not publish profile save tree '%1'.")
                      .arg(destination));
    }
    return ok();
  }

#ifdef Q_OS_LINUX
  const QByteArray encodedStage = QFile::encodeName(stage);
  const QByteArray encodedDestination = QFile::encodeName(destination);
  if (::syscall(SYS_renameat2, AT_FDCWD, encodedStage.constData(), AT_FDCWD,
                encodedDestination.constData(), RENAME_EXCHANGE) != 0) {
    const QString error = QString::fromLocal8Bit(std::strerror(errno));
    discardStage();
    return fail(QStringLiteral(
                    "Could not atomically exchange profile save tree '%1': %2")
                    .arg(destination, error));
  }
#else
  const QString previous =
      QDir(destinationParent)
          .filePath(
              QStringLiteral(".fluorine-save-old-%1")
                  .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
  if (!QDir().rename(destination, previous) ||
      !QDir().rename(stage, destination)) {
    if (!QFileInfo::exists(destination) && QFileInfo::exists(previous)) {
      QDir().rename(previous, destination);
    }
    discardStage();
    return fail(QStringLiteral("Could not publish profile save tree '%1'.")
                    .arg(destination));
  }
  QDir old(previous);
  if (old.exists() && !old.removeRecursively()) {
    return ok(
        QStringLiteral(
            "Profile save publication succeeded; old generation '%1' remains.")
            .arg(previous));
  }
  return ok();
#endif

  QDir old(stage);
  if (old.exists() && !old.removeRecursively()) {
    return ok(
        QStringLiteral(
            "Profile save publication succeeded; old generation '%1' remains.")
            .arg(stage));
  }
  return ok();
}

Result markerGeneration(const QList<Slot> &entries, const QString &profileRoot,
                        QString &ownerId, bool &present) {
  present = false;
  const QString profile = canonicalOrAbsolute(profileRoot);
  QList<QPair<const Slot *, OwnerMarker>> markers;
  for (const Slot &slot : entries) {
    OwnerMarker marker;
    bool slotPresent = false;
    Result result = loadMarker(slot, marker, slotPresent);
    if (!result)
      return result;
    if (!slotPresent)
      continue;
    markers.append({&slot, marker});
    if (!present) {
      present = true;
      ownerId = marker.ownerId;
    } else if (ownerId != marker.ownerId) {
      return fail(QStringLiteral("Save deployment markers have mixed owners."));
    }
    if (marker.profileRoot != profile) {
      return fail(QStringLiteral("Interrupted save deployment belongs to "
                                 "another profile; preserving it."));
    }
  }
  if (!present || markers.size() == entries.size())
    return ok();

  // Markers are published for every slot before topology mutation begins.
  // Therefore a strict subset of same-profile preparing markers is a
  // crash-before-mutation state and can be retired without guessing about
  // user data.
  const bool safeIncomplete = std::all_of(
      markers.cbegin(), markers.cend(), [&profile](const auto &entry) {
        return entry.second.profileRoot == profile &&
               entry.second.intent == QStringLiteral("rollback") &&
               entry.second.phase == QStringLiteral("preparing");
      });
  if (safeIncomplete) {
    for (const auto &entry : markers) {
      const Result removed =
          removeOwnedMarker(*entry.first, profileRoot, entry.second.ownerId);
      if (!removed)
        return removed;
    }
    present = false;
    ownerId.clear();
    return ok();
  }

  // Marker retirement happens only after every slot has restored its global
  // topology and every invocation-owned retirement has been removed. A crash
  // between the two marker unlinks therefore leaves a strict subset of
  // restored markers. It is safe and necessary to finish that final unlink;
  // no source publication or topology mutation is repeated here.
  const bool partiallyRetired = std::all_of(
      markers.cbegin(), markers.cend(), [&profile](const auto &entry) {
        return entry.second.profileRoot == profile &&
               entry.second.phase == QStringLiteral("restored") &&
               entry.second.retirement.isEmpty();
      });
  if (partiallyRetired) {
    for (const Slot &slot : entries) {
      QString detail;
      const LeafType live = leafType(slot.live, detail);
      const LeafType backup = leafType(slot.backup, detail);
      if ((live != LeafType::Directory && live != LeafType::Missing) ||
          backup != LeafType::Missing) {
        return fail(QStringLiteral(
            "Incomplete save marker retirement has non-terminal topology."));
      }
    }
    for (const auto &entry : markers) {
      const Result removed =
          removeOwnedMarker(*entry.first, profileRoot, entry.second.ownerId);
      if (!removed)
        return cleanupPending(removed.error);
    }
    present = false;
    ownerId.clear();
    return ok({}, /*topologyComplete=*/true);
  }
  return fail(QStringLiteral("Save deployment marker set is incomplete."));
}

Result finishOwnedDeployment(QList<Slot> &entries, const QString &profileRoot,
                             const QString &ownerId, bool publishSession) {
  QString generationOwner;
  bool generationPresent = false;
  Result generation = markerGeneration(entries, profileRoot, generationOwner,
                                       generationPresent);
  if (!generation)
    return generation;
  if (!generationPresent) {
    if (generation.topologyComplete)
      return generation;
    return fail(
        QStringLiteral("Save deployment has no owned generation marker."));
  }
  if (generationOwner != ownerId) {
    return fail(QStringLiteral("Save topology belongs to another launch."));
  }

  QList<OwnerMarker> markers;
  for (const Slot &slot : entries) {
    OwnerMarker marker;
    bool present = false;
    Result result = loadMarker(slot, marker, present);
    if (!result)
      return result;
    if (!present || marker.ownerId != ownerId ||
        marker.profileRoot != canonicalOrAbsolute(profileRoot)) {
      return fail(QStringLiteral("Save state '%1' is not owned by launch '%2'.")
                      .arg(slot.live, ownerId));
    }
    markers.append(std::move(marker));
  }

  const auto phaseIs = [&markers](std::initializer_list<QString> phases) {
    return std::all_of(markers.cbegin(), markers.cend(),
                       [&phases](const auto &marker) {
                         return std::find(phases.begin(), phases.end(),
                                          marker.phase) != phases.end();
                       });
  };
  const bool publicationIntent = std::any_of(
      markers.cbegin(), markers.cend(), [](const OwnerMarker &marker) {
        return marker.intent == QStringLiteral("publish");
      });
  const bool publishGeneration = publishSession || publicationIntent;

  // Publication intent is durable before any source publication or teardown.
  // If writing the intent to one case-variant marker fails, a retry sees at
  // least one "publish" marker and completes the transition instead of
  // accidentally rolling the generation back.
  if (publishGeneration) {
    for (qsizetype index = 0; index < entries.size(); ++index) {
      if (markers[index].intent == QStringLiteral("rollback")) {
        Result result = updateMarkerIntent(entries[index], markers[index],
                                           QStringLiteral("publish"));
        if (!result)
          return result;
      }
    }
  }
  const bool teardownStarted = std::any_of(
      markers.cbegin(), markers.cend(), [](const OwnerMarker &marker) {
        return marker.phase == QStringLiteral("retiring") ||
               marker.phase == QStringLiteral("restoring") ||
               marker.phase == QStringLiteral("restored");
      });

  if (publishGeneration) {
    if (teardownStarted) {
      if (!phaseIs({QStringLiteral("published"), QStringLiteral("retiring"),
                    QStringLiteral("restoring"), QStringLiteral("restored")})) {
        return fail(QStringLiteral("Save deployment entered teardown before "
                                   "generation publication completed."));
      }
    } else if (!phaseIs({QStringLiteral("preparing")})) {
      if (!phaseIs({QStringLiteral("active"), QStringLiteral("publishing"),
                    QStringLiteral("published")})) {
        return fail(
            QStringLiteral("Save deployment has mixed publication phases."));
      }

      const bool publicationComplete = phaseIs({QStringLiteral("published")});
      if (!publicationComplete) {
        // Every source remains in place until the complete generation is
        // published and every marker says "published". A crash while moving
        // markers through "publishing" can therefore safely repeat the full
        // union; it can never mirror just the not-yet-retired subset.
        for (qsizetype index = 0; index < entries.size(); ++index) {
          if (markers[index].phase == QStringLiteral("active")) {
            Result result = updateMarker(entries[index], markers[index],
                                         QStringLiteral("publishing"));
            if (!result)
              return result;
          }
        }

        QStringList sources;
        for (const Slot &slot : entries) {
          QString detail;
          const LeafType live = leafType(slot.live, detail);
          if (live == LeafType::Directory) {
            sources.append(slot.live);
          } else if (live != LeafType::Symlink ||
                     !managedLink(slot.live, profileRoot)) {
            return fail(
                QStringLiteral("Owned save link '%1' is missing or foreign.")
                    .arg(slot.live));
          }
        }

        if (!sources.isEmpty()) {
          // A surviving managed link exposes the profile generation directly;
          // a case-variant real tree is only an overlay. Mirror deletions only
          // when Wine replaced every managed slot with a complete real tree.
          Result result = publishTrees(sources, profileRoot,
                                       sources.size() == entries.size());
          if (!result)
            return result;
        }
        for (qsizetype index = 0; index < entries.size(); ++index) {
          Result result = updateMarker(entries[index], markers[index],
                                       QStringLiteral("published"));
          if (!result)
            return result;
        }
      }
    }
  } else if (!phaseIs({QStringLiteral("preparing"), QStringLiteral("active"),
                       QStringLiteral("retiring"), QStringLiteral("restoring"),
                       QStringLiteral("restored")})) {
    return fail(QStringLiteral("Refusing to roll back a save generation that "
                               "may contain session output."));
  }

  QStringList warnings;
  for (qsizetype index = 0; index < entries.size(); ++index) {
    Slot &slot = entries[index];
    OwnerMarker &marker = markers[index];
    QString detail;

    if (marker.phase == QStringLiteral("preparing")) {
      const LeafType live = leafType(slot.live, detail);
      const LeafType backup = leafType(slot.backup, detail);
      if (live == LeafType::Symlink && managedLink(slot.live, profileRoot)) {
        if (!QFile::remove(slot.live)) {
          return fail(
              QStringLiteral("Could not remove prepared save link '%1'.")
                  .arg(slot.live));
        }
      } else if (live == LeafType::Directory && backup == LeafType::Missing) {
        Result result = updateMarker(slot, marker, QStringLiteral("restored"));
        if (!result)
          return result;
        continue;
      } else if (live == LeafType::Missing && backup == LeafType::Missing) {
        Result result = updateMarker(slot, marker, QStringLiteral("restored"));
        if (!result)
          return result;
        continue;
      } else if (live != LeafType::Missing) {
        return fail(QStringLiteral("Prepared save state '%1' changed before "
                                   "launch; preserving it.")
                        .arg(slot.live));
      }
      Result result = updateMarker(slot, marker, QStringLiteral("restoring"),
                                   marker.retirement);
      if (!result)
        return result;
    }

    if (marker.phase == QStringLiteral("active") ||
        marker.phase == QStringLiteral("published")) {
      const LeafType live = leafType(slot.live, detail);
      QString retirement;
      if (live == LeafType::Directory) {
        if (!publishGeneration) {
          return fail(QStringLiteral("Prepared save link '%1' was replaced "
                                     "before launch; preserving it.")
                          .arg(slot.live));
        }
        retirement = uniqueRetirementPath(slot.live);
      } else if (live != LeafType::Symlink ||
                 !managedLink(slot.live, profileRoot)) {
        return fail(
            QStringLiteral("Owned save state '%1' changed unexpectedly.")
                .arg(slot.live));
      }
      Result result =
          updateMarker(slot, marker, QStringLiteral("retiring"), retirement);
      if (!result)
        return result;
    }

    if (marker.phase == QStringLiteral("retiring")) {
      const LeafType live = leafType(slot.live, detail);
      if (live == LeafType::Symlink && managedLink(slot.live, profileRoot)) {
        if (!marker.retirement.isEmpty() || !QFile::remove(slot.live)) {
          return fail(QStringLiteral("Could not retire managed save link '%1'.")
                          .arg(slot.live));
        }
      } else if (live == LeafType::Directory && !marker.retirement.isEmpty()) {
        if (QFileInfo::exists(marker.retirement) ||
            QFileInfo(marker.retirement).isSymLink() ||
            !QDir().rename(slot.live, marker.retirement)) {
          return fail(QStringLiteral("Could not retain session saves '%1'.")
                          .arg(slot.live));
        }
      } else if (live != LeafType::Missing) {
        return fail(
            QStringLiteral("Could not classify retiring save state '%1'.")
                .arg(slot.live));
      }
      Result result = updateMarker(slot, marker, QStringLiteral("restoring"),
                                   marker.retirement);
      if (!result)
        return result;
    }

    if (marker.phase == QStringLiteral("restoring")) {
      const LeafType live = leafType(slot.live, detail);
      const LeafType backup = leafType(slot.backup, detail);
      if (live == LeafType::Missing && backup == LeafType::Directory) {
        if (!QDir().rename(slot.backup, slot.live)) {
          return fail(QStringLiteral("Could not restore global save tree '%1'.")
                          .arg(slot.live));
        }
      } else if (live != LeafType::Directory || backup != LeafType::Missing) {
        return fail(QStringLiteral("Ambiguous save restore state at '%1'.")
                        .arg(slot.live));
      }
      Result result = updateMarker(slot, marker, QStringLiteral("restored"),
                                   marker.retirement);
      if (!result)
        return result;
    }

    if (marker.phase != QStringLiteral("restored")) {
      return fail(QStringLiteral("Save slot '%1' did not reach restored state.")
                      .arg(slot.live));
    }
  }

  // Keep every marker until every slot is restored. This makes publication
  // and topology restoration generation-wide rather than a sequence of
  // independently retired aliases.
  for (qsizetype index = 0; index < entries.size(); ++index) {
    OwnerMarker &marker = markers[index];
    if (!marker.retirement.isEmpty()) {
      QString detail;
      const LeafType retirement = leafType(marker.retirement, detail);
      if (retirement == LeafType::Directory) {
        if (!QDir(marker.retirement).removeRecursively()) {
          return cleanupPending(
              QStringLiteral("Could not retire recovery copy '%1'.")
                  .arg(marker.retirement));
        }
      } else if (retirement != LeafType::Missing) {
        return cleanupPending(
            QStringLiteral("Unsafe recovery leaf '%1' was preserved.")
                .arg(marker.retirement));
      }
      Result result =
          updateMarker(entries[index], marker, QStringLiteral("restored"));
      if (!result)
        return cleanupPending(result.error);
    }
  }

  for (qsizetype index = 0; index < entries.size(); ++index) {
    Result result = removeOwnedMarker(entries[index], profileRoot, ownerId);
    if (!result)
      return cleanupPending(result.error);
  }
  return ok(warnings.join(QStringLiteral(" ")),
            /*topologyComplete=*/true);
}

} // namespace

QString backupPathFor(const QString &livePath) {
  const QFileInfo info(livePath);
  return QDir(info.absolutePath())
      .filePath(QStringLiteral(".mo2linux_backup_") + info.fileName());
}

bool samePhysicalDirectory(const QString &left, const QString &right) {
  const QString leftCanonical = QFileInfo(left).canonicalFilePath();
  const QString rightCanonical = QFileInfo(right).canonicalFilePath();
  return !leftCanonical.isEmpty() && !rightCanonical.isEmpty() &&
         leftCanonical == rightCanonical;
}

QStringList managedLivePaths(const QString &exactLivePath) {
  QStringList paths;
  for (const Slot &slot : entriesFor(exactLivePath)) {
    paths.append(slot.live);
  }
  return paths;
}

Result publishTree(const QString &sourceRoot, const QString &destinationRoot,
                   bool mirrorDeletions) {
  return publishTrees({sourceRoot}, destinationRoot, mirrorDeletions);
}

Result pendingDeployment(const QString &prefixRoot,
                         const QString &exactLivePath,
                         PendingDeployment &pending) {
  pending = {};
  QString detail;
  if (leafType(prefixRoot, detail) != LeafType::Directory) {
    return fail(QStringLiteral("Wine prefix root '%1' is not a real directory.")
                    .arg(prefixRoot));
  }
  Result result =
      ensurePrefixParent(prefixRoot, QFileInfo(exactLivePath).absolutePath(),
                         /*createMissing=*/false);
  if (!result)
    return result;

  const QList<Slot> entries = entriesFor(exactLivePath);
  for (const Slot &slot : entries) {
    OwnerMarker marker;
    bool present = false;
    result = loadMarker(slot, marker, present);
    if (!result)
      return result;
    if (!present)
      continue;
    if (!pending.present) {
      pending.present = true;
      pending.ownerId = marker.ownerId;
      pending.profileRoot = marker.profileRoot;
    } else if (pending.ownerId != marker.ownerId ||
               pending.profileRoot != marker.profileRoot) {
      return fail(
          QStringLiteral("Save deployment markers have mixed identities."));
    }
  }
  return ok();
}

Result deployLinks(const QString &prefixRoot, const QString &profileRoot,
                   const QString &exactLivePath, const QString &ownerId) {
  Result result = validateRoots(prefixRoot, profileRoot, exactLivePath);
  if (!result)
    return result;
  if (ownerId.trimmed().isEmpty())
    return fail(QStringLiteral("Empty save owner."));

  QList<Slot> entries = entriesFor(exactLivePath);
  result = classify(entries, profileRoot);
  if (!result)
    return result;

  QString previousOwner;
  bool markerPresent = false;
  result = markerGeneration(entries, profileRoot, previousOwner, markerPresent);
  if (!result)
    return result;
  if (markerPresent) {
    if (previousOwner != ownerId) {
      return fail(QStringLiteral("Interrupted save deployment belongs to "
                                 "another launch; preserving it."));
    }
    result = finishOwnedDeployment(entries, profileRoot, previousOwner,
                                   /*publishSession=*/true);
    if (!result)
      return result;
    result = classify(entries, profileRoot);
    if (!result)
      return result;
  }

  // Without a durable owner marker, live+backup could be partial output from
  // an old profile or a foreign directory using the historical name. Never
  // infer that it belongs to the selected profile.
  for (const Slot &slot : entries) {
    if (slot.liveType == LeafType::Directory &&
        slot.backupType == LeafType::Directory) {
      return fail(QStringLiteral("Unowned live and backup save trees coexist "
                                 "at '%1'; preserving both.")
                      .arg(slot.live));
    }
  }

  QList<Slot *> marked;
  for (Slot &slot : entries) {
    result =
        writeMarker(slot, profileRoot, ownerId, QStringLiteral("preparing"));
    if (!result) {
      bool cleanupRequired = false;
      for (Slot *created : marked) {
        const Result removed =
            removeOwnedMarker(*created, profileRoot, ownerId);
        cleanupRequired = cleanupRequired || !removed;
      }
      result.cleanupRequired = cleanupRequired;
      return result;
    }
    marked.append(&slot);
  }
  const auto ownedFailure = [](Result failure) {
    failure.cleanupRequired = true;
    return failure;
  };

  for (Slot &slot : entries) {
    if (slot.liveType == LeafType::Directory &&
        slot.backupType == LeafType::Missing) {
      if (!QDir().rename(slot.live, slot.backup)) {
        result = fail(QStringLiteral("Could not back up global saves '%1'.")
                          .arg(slot.live));
        const Result rollback = finishOwnedDeployment(
            entries, profileRoot, ownerId, /*publishSession=*/false);
        return rollback ? result : ownedFailure(rollback);
      }
    } else if ((slot.liveType == LeafType::Missing || slot.exactLink) &&
               slot.backupType == LeafType::Missing) {
      if (!QDir().mkdir(slot.backup)) {
        result = fail(
            QStringLiteral("Could not create empty global save backup '%1'.")
                .arg(slot.backup));
        const Result rollback = finishOwnedDeployment(
            entries, profileRoot, ownerId, /*publishSession=*/false);
        return rollback ? result : ownedFailure(rollback);
      }
    }
  }

  for (Slot &slot : entries) {
    if (slot.exactLink)
      continue;
    const bool linked = QFile::link(profileRoot, slot.live);
    if (!linked || !managedLink(slot.live, profileRoot)) {
      result = fail(QStringLiteral("Could not create managed save link '%1'.")
                        .arg(slot.live));
      const Result rollback =
          finishOwnedDeployment(entries, profileRoot, ownerId,
                                /*publishSession=*/false);
      return rollback ? result : ownedFailure(rollback);
    }
  }

  for (Slot &slot : entries) {
    result = writeMarker(slot, profileRoot, ownerId, QStringLiteral("active"));
    if (!result) {
      const Result rollback =
          finishOwnedDeployment(entries, profileRoot, ownerId,
                                /*publishSession=*/false);
      return rollback ? result : ownedFailure(rollback);
    }
  }
  return ok();
}

Result prepareBindTarget(const QString &prefixRoot, const QString &profileRoot,
                         const QString &exactLivePath, const QString &ownerId) {
  Result result = validateRoots(prefixRoot, profileRoot, exactLivePath);
  if (!result)
    return result;
  QList<Slot> entries = entriesFor(exactLivePath);
  result = classify(entries, profileRoot);
  if (!result)
    return result;

  QString previousOwner;
  bool markerPresent = false;
  result = markerGeneration(entries, profileRoot, previousOwner, markerPresent);
  if (!result)
    return result;
  if (markerPresent) {
    if (previousOwner != ownerId) {
      return fail(QStringLiteral("Interrupted save deployment belongs to "
                                 "another launch; preserving it."));
    }
    result = finishOwnedDeployment(entries, profileRoot, previousOwner,
                                   /*publishSession=*/true);
    if (!result)
      return result;
    result = classify(entries, profileRoot);
    if (!result)
      return result;
  }

  for (const Slot &slot : entries) {
    if (slot.liveType == LeafType::Directory &&
        slot.backupType == LeafType::Directory) {
      return fail(QStringLiteral("Wine save output and a global backup coexist "
                                 "at '%1'; preserving both.")
                      .arg(slot.live));
    }
  }

  const bool needsLegacyNormalization =
      std::any_of(entries.cbegin(), entries.cend(), [](const Slot &slot) {
        return slot.exactLink || slot.backupType == LeafType::Directory;
      });
  if (needsLegacyNormalization) {
    result = deployLinks(prefixRoot, profileRoot, exactLivePath, ownerId);
    if (!result)
      return result;
    result =
        synchronizeAndRestore(prefixRoot, profileRoot, exactLivePath, ownerId);
    if (!result)
      return result;
  }

  QString detail;
  for (const Slot &slot : entries) {
    const LeafType type = leafType(slot.live, detail);
    if (type == LeafType::Missing && !QDir().mkdir(slot.live)) {
      return fail(QStringLiteral("Could not create bind-mount target '%1'.")
                      .arg(slot.live));
    }
    if (leafType(slot.live, detail) != LeafType::Directory) {
      return fail(
          QStringLiteral("Bind-mount target '%1' is not a real directory.")
              .arg(slot.live));
    }
  }
  return ok();
}

Result synchronizeAndRestore(const QString &prefixRoot,
                             const QString &profileRoot,
                             const QString &exactLivePath,
                             const QString &ownerId) {
  Result result = validateRoots(prefixRoot, profileRoot, exactLivePath);
  if (!result)
    return result;
  QList<Slot> entries = entriesFor(exactLivePath);
  result = classify(entries, profileRoot);
  if (!result)
    return result;
  result = finishOwnedDeployment(entries, profileRoot, ownerId,
                                 /*publishSession=*/true);
  if (!result)
    result.cleanupRequired = true;
  return result;
}

Result rollbackLinks(const QString &prefixRoot, const QString &profileRoot,
                     const QString &exactLivePath, const QString &ownerId) {
  Result result = validateRoots(prefixRoot, profileRoot, exactLivePath);
  if (!result)
    return result;
  QList<Slot> entries = entriesFor(exactLivePath);
  result = classify(entries, profileRoot);
  if (!result)
    return result;
  QString existingOwner;
  bool markerPresent = false;
  result = markerGeneration(entries, profileRoot, existingOwner, markerPresent);
  if (!result)
    return result;
  if (!markerPresent) {
    const bool ownedTopologyWithoutMarker =
        std::any_of(entries.cbegin(), entries.cend(), [](const Slot &slot) {
          return slot.exactLink || slot.backupType == LeafType::Directory;
        });
    return ownedTopologyWithoutMarker
               ? fail(QStringLiteral("Managed save topology lost its owner "
                                     "marker; preserving it."))
               : ok({}, /*topologyComplete=*/true);
  }
  if (existingOwner != ownerId) {
    return fail(QStringLiteral("Save topology belongs to another launch."));
  }
  result = finishOwnedDeployment(entries, profileRoot, ownerId,
                                 /*publishSession=*/false);
  if (!result)
    result.cleanupRequired = true;
  return result;
}

QString leasePathFor(const QString &prefixRoot, const QString &exactLivePath) {
  Q_UNUSED(exactLivePath);
  const QString canonicalPrefix = QFileInfo(prefixRoot).canonicalFilePath();
  const QString identity = canonicalPrefix.isEmpty()
                               ? canonicalOrAbsolute(prefixRoot)
                               : canonicalPrefix;
  const QByteArray digest =
      QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256)
          .toHex()
          .left(24);
  QFileInfo prefixInfo(identity);
  QDir lockDirectory = prefixInfo.dir();
  // A configured Proton layout points at <compatdata>/pfx, while deleting the
  // runtime removes the entire compatdata directory. Put that admission lock
  // one level above the complete deletion boundary. Plain/direct prefixes use
  // their ordinary parent directory.
  if (prefixInfo.fileName() == QStringLiteral("pfx")) {
    lockDirectory.cdUp();
  }
  // The lock must survive delete/recreate of every tree it protects. Keeping
  // it outside the deletion boundary also prevents an open QLockFile from
  // becoming an unlinked inode while a second process acquires a replacement
  // pathname.
  return lockDirectory.filePath(
      QStringLiteral(".fluorine-save-prefix-%1.lock")
          .arg(QString::fromLatin1(digest)));
}

QString sessionLeasePathFor(const QString &prefixRoot,
                            const QString &exactLivePath) {
  Q_UNUSED(exactLivePath);
  const QString canonicalPrefix = QFileInfo(prefixRoot).canonicalFilePath();
  return QDir(canonicalPrefix.isEmpty() ? canonicalOrAbsolute(prefixRoot)
                                        : canonicalPrefix)
      .filePath(QStringLiteral(".fluorine-save-prefix.lock.session.json"));
}

bool hasPersistedSessionLease(const QString &prefixRoot) {
  const QDir prefix(prefixRoot);
  const QStringList entries = prefix.entryList(
      {QStringLiteral(".fluorine-save-prefix.lock.session.json"),
       QStringLiteral(".fluorine-save-lease-*.lock.session.json")},
      QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
  return !entries.isEmpty();
}

Result beginSessionLease(const QString &prefixRoot,
                         const QString &exactLivePath, const QString &ownerId) {
  if (ownerId.trimmed().isEmpty()) {
    return fail(QStringLiteral("Empty save-session owner."));
  }
  if (hasPersistedSessionLease(prefixRoot)) {
    return fail(QStringLiteral("Wine prefix has a persisted save-session "
                               "owner; refusing to steal it."));
  }
  const QString path = sessionLeasePathFor(prefixRoot, exactLivePath);
  QString detail;
  if (leafType(path, detail) != LeafType::Missing) {
    return fail(QStringLiteral(
        "Wine save path has a persisted launch owner; refusing to steal it."));
  }
  const QJsonObject object{
      {QStringLiteral("version"), 1},
      {QStringLiteral("owner"), ownerId},
      {QStringLiteral("live"), physicalLiveIdentity(exactLivePath)}};
  const QByteArray bytes =
      QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
  QSaveFile file(path);
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly) ||
      !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
      file.write(bytes) != bytes.size() || !file.flush() || !file.commit()) {
    const QString error = file.errorString();
    file.cancelWriting();
    return fail(QStringLiteral("Could not publish save-session owner '%1': %2")
                    .arg(path, error));
  }
  return ok();
}

Result endSessionLease(const QString &prefixRoot, const QString &exactLivePath,
                       const QString &ownerId) {
  const QString path = sessionLeasePathFor(prefixRoot, exactLivePath);
  QString detail;
  const LeafType type = leafType(path, detail);
  if (type == LeafType::Missing)
    return ok();
  if (type != LeafType::RegularFile) {
    return fail(QStringLiteral("Save-session owner '%1' is missing or unsafe.")
                    .arg(path));
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024) {
    return fail(QStringLiteral("Could not safely read save-session owner '%1'.")
                    .arg(path));
  }
  QJsonParseError error;
  const QJsonDocument document =
      QJsonDocument::fromJson(file.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    return fail(QStringLiteral("Invalid save-session owner '%1'.").arg(path));
  }
  const QJsonObject object = document.object();
  if (object.value(QStringLiteral("version")).toInt() != 1 ||
      object.value(QStringLiteral("owner")).toString() != ownerId ||
      QDir::cleanPath(object.value(QStringLiteral("live")).toString()) !=
          physicalLiveIdentity(exactLivePath)) {
    return fail(
        QStringLiteral("Save-session owner belongs to another launch."));
  }
  file.close();
  if (!QFile::remove(path)) {
    return fail(
        QStringLiteral("Could not retire save-session owner '%1'.").arg(path));
  }
  return ok();
}

} // namespace WineSaveDeployment
