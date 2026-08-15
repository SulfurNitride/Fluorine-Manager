#include "instanceunregister.h"
#include "instancepathidentity.h"

#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QObject>
#include <QStandardPaths>

#include <cerrno>
#include <cstring>
#include <mutex>
#include <utility>

#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace InstanceUnregister
{
namespace
{

DisableResult failure(DisableStatus status, const QString& source,
                      const QString& disabled, const QString& error)
{
  return {status, source, disabled, error};
}

QString cleanAbsolutePath(const QString& path)
{
  return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

#ifdef Q_OS_LINUX
bool sameIdentity(const struct stat& left, const struct stat& right)
{
  return left.st_dev == right.st_dev && left.st_ino == right.st_ino;
}

bool liveDirectoryMatches(int descriptor, const QString& path)
{
  struct stat retained{};
  struct stat live{};
  const QByteArray encoded = QFile::encodeName(path);
  return ::fstat(descriptor, &retained) == 0 &&
         ::stat(encoded.constData(), &live) == 0 && S_ISDIR(live.st_mode) &&
         sameIdentity(retained, live);
}
#endif

QString portableRegistryLockPath(const QSettings& settings)
{
  const QByteArray identity =
      QCryptographicHash::hash(settings.fileName().toUtf8(),
                               QCryptographicHash::Sha256)
          .toHex()
          .left(24);
  const QString root = QDir(
      QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
                           .filePath(QStringLiteral("Fluorine"));
  return QDir(root).filePath(
      QStringLiteral(".portable-instances-%1.lock")
          .arg(QString::fromLatin1(identity)));
}

RegistryResult registryFailure(QString error,
                               RegistryStatus status = RegistryStatus::Failed)
{
  return {status, std::move(error)};
}

RegistryResult mutatePortableRegistry(QSettings& settings,
                                      const QString& removePath,
                                      const QString& addPath,
                                      bool requireRemoved,
                                      const RegistryReadHook& afterRead)
{
  // QLockFile serializes peer processes. Serialize callers in this process as
  // well so they wait for the same read/modify/write generation instead of
  // spuriously reporting the process's own lock as busy.
  static std::mutex processRegistryMutex;
  const std::lock_guard processRegistryLock(processRegistryMutex);

  const QString normalizedRemove =
      removePath.trimmed().isEmpty() ? QString{} : cleanAbsolutePath(removePath);
  const QString normalizedAdd =
      addPath.trimmed().isEmpty() ? QString{} : cleanAbsolutePath(addPath);
  if (normalizedRemove.isEmpty() && normalizedAdd.isEmpty()) {
    return registryFailure(
        QObject::tr("The portable instance registry mutation is empty."));
  }

  const QString lockPath = portableRegistryLockPath(settings);
  const QFileInfo lockInfo(lockPath);
  if (lockInfo.absolutePath().isEmpty() ||
      !QDir().mkpath(lockInfo.absolutePath())) {
    return registryFailure(
        QObject::tr("The portable-instance registry lock could not be created."));
  }
  QLockFile lock(lockPath);
  lock.setStaleLockTime(0);
  if (!lock.tryLock(5000) &&
      (!lock.removeStaleLockFile() || !lock.tryLock(5000))) {
    return registryFailure(
        QObject::tr("The portable-instance registry is busy in another process."));
  }

  settings.sync();
  if (settings.status() != QSettings::NoError) {
    return registryFailure(
        QObject::tr("The portable-instance registry could not be read."));
  }

  constexpr auto Key = "PortableInstances";
  const bool originalPresent = settings.contains(Key);
  const QVariant originalValue = settings.value(Key);
  const QStringList original = originalValue.toStringList();
  QStringList desired = original;
  qsizetype removed = 0;
  if (!normalizedRemove.isEmpty()) {
    for (auto it = desired.begin(); it != desired.end();) {
      if (instance_path::sameDirectoryOrPath(*it, normalizedRemove)) {
        it = desired.erase(it);
        ++removed;
      } else {
        ++it;
      }
    }
  }
  if (requireRemoved && removed == 0) {
    return registryFailure(
        QObject::tr("The original portable instance is no longer registered."));
  }
  if (!normalizedAdd.isEmpty()) {
    for (auto it = desired.begin(); it != desired.end();) {
      if (instance_path::sameDirectoryOrPath(*it, normalizedAdd)) {
        it = desired.erase(it);
      } else {
        ++it;
      }
    }
    desired.append(normalizedAdd);
  }
  if (afterRead) {
    afterRead();
  }

  settings.setValue(Key, desired);
  settings.sync();
  if (settings.status() != QSettings::NoError) {
    // Never allow the backend destructor to retry the rejected generation.
    // The caller must retain this backend when RollbackFailed is returned.
    if (originalPresent) {
      settings.setValue(Key, originalValue);
    } else {
      settings.remove(Key);
    }
    return registryFailure(
        QObject::tr("The portable-instance registry could not be saved; its "
                    "backend remains retained until restart."),
        RegistryStatus::RollbackFailed);
  }

  settings.sync();
  const QStringList persisted = settings.value(Key).toStringList();
  if (settings.status() == QSettings::NoError && persisted == desired) {
    return {RegistryStatus::Saved, {}};
  }

  if (originalPresent) {
    settings.setValue(Key, originalValue);
  } else {
    settings.remove(Key);
  }
  settings.sync();
  const bool restored =
      settings.status() == QSettings::NoError &&
      settings.contains(Key) == originalPresent &&
      (!originalPresent || settings.value(Key) == originalValue);
  return registryFailure(
      restored
          ? QObject::tr("The portable-instance registry verification failed; "
                        "the original value was restored.")
          : QObject::tr("The portable-instance registry and its rollback could "
                        "not be verified; the backend remains retained until "
                        "restart."),
      restored ? RegistryStatus::Failed : RegistryStatus::RollbackFailed);
}

}  // namespace

DisableResult disableGlobalIni(const QString& iniPath)
{
  const QString source = cleanAbsolutePath(iniPath);
  const QString disabled = source + QStringLiteral(".disabled");
  if (iniPath.trimmed().isEmpty() || source.isEmpty()) {
    return failure(DisableStatus::SourceUnavailable, source, disabled,
                   QObject::tr("The instance INI path is empty."));
  }

#ifdef Q_OS_LINUX
  const QFileInfo sourceInfo(source);
  const QString parentPath = sourceInfo.absolutePath();
  const QByteArray encodedParent = QFile::encodeName(parentPath);
  const int parent =
      ::open(encodedParent.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC |
                                            O_NOFOLLOW);
  if (parent < 0) {
    return failure(DisableStatus::SourceUnavailable, source, disabled,
                   QObject::tr("The instance INI directory is unavailable: %1")
                       .arg(QString::fromLocal8Bit(std::strerror(errno))));
  }

  const QByteArray sourceName = QFile::encodeName(sourceInfo.fileName());
  const QByteArray disabledName =
      QFile::encodeName(QFileInfo(disabled).fileName());
  struct stat sourceIdentity{};
  if (::fstatat(parent, sourceName.constData(), &sourceIdentity,
                AT_SYMLINK_NOFOLLOW) != 0) {
    const QString detail = QString::fromLocal8Bit(std::strerror(errno));
    ::close(parent);
    return failure(DisableStatus::SourceUnavailable, source, disabled,
                   QObject::tr("The instance INI is unavailable: %1").arg(detail));
  }
  if (!S_ISREG(sourceIdentity.st_mode)) {
    ::close(parent);
    return failure(DisableStatus::SourceUnsafe, source, disabled,
                   QObject::tr("The instance INI is not a regular file."));
  }

  struct stat destinationIdentity{};
  if (::fstatat(parent, disabledName.constData(), &destinationIdentity,
                AT_SYMLINK_NOFOLLOW) == 0) {
    ::close(parent);
    return failure(DisableStatus::DestinationExists, source, disabled,
                   QObject::tr("The disabled INI destination already exists."));
  }
  if (errno != ENOENT) {
    const QString detail = QString::fromLocal8Bit(std::strerror(errno));
    ::close(parent);
    return failure(DisableStatus::RenameFailed, source, disabled,
                   QObject::tr("The disabled INI destination could not be "
                               "verified: %1")
                       .arg(detail));
  }

#if defined(SYS_renameat2) && defined(RENAME_NOREPLACE)
  if (::syscall(SYS_renameat2, parent, sourceName.constData(), parent,
                disabledName.constData(), RENAME_NOREPLACE) != 0) {
    const int failureCode = errno;
    const QString detail = QString::fromLocal8Bit(std::strerror(failureCode));
    ::close(parent);
    return failure(failureCode == EEXIST ? DisableStatus::DestinationExists
                                        : DisableStatus::RenameFailed,
                   source, disabled,
                   failureCode == EEXIST
                       ? QObject::tr("The disabled INI destination already exists.")
                       : QObject::tr("The instance INI could not be disabled: %1")
                             .arg(detail));
  }
#else
  ::close(parent);
  return failure(DisableStatus::RenameFailed, source, disabled,
                 QObject::tr("Atomic no-replace INI rename is unavailable."));
#endif

  struct stat published{};
  struct stat staleSource{};
  const bool publishedMatches =
      ::fstatat(parent, disabledName.constData(), &published,
                AT_SYMLINK_NOFOLLOW) == 0 &&
      S_ISREG(published.st_mode) && sameIdentity(sourceIdentity, published);
  const bool sourceGone =
      ::fstatat(parent, sourceName.constData(), &staleSource,
                AT_SYMLINK_NOFOLLOW) != 0 &&
      errno == ENOENT;
  const bool parentMatches = liveDirectoryMatches(parent, parentPath);
  const bool directorySynchronized = ::fsync(parent) == 0;
  ::close(parent);
  if (!publishedMatches || !sourceGone || !parentMatches ||
      !directorySynchronized) {
    return failure(
        DisableStatus::PublicationUncertain, source, disabled,
        QObject::tr("The instance INI changed while it was being disabled; "
                    "restart and inspect both paths before retrying."));
  }
#else
  const QFileInfo sourceInfo(source);
  const QFileInfo disabledInfo(disabled);
  if (!sourceInfo.exists() || sourceInfo.isSymLink()) {
    return failure(DisableStatus::SourceUnavailable, source, disabled,
                   QObject::tr("The instance INI is unavailable or unsafe."));
  }
  if (!sourceInfo.isFile()) {
    return failure(DisableStatus::SourceUnsafe, source, disabled,
                   QObject::tr("The instance INI is not a regular file."));
  }
  if (disabledInfo.exists() || disabledInfo.isSymLink()) {
    return failure(DisableStatus::DestinationExists, source, disabled,
                   QObject::tr("The disabled INI destination already exists."));
  }
  if (!QDir(sourceInfo.absolutePath())
           .rename(sourceInfo.fileName(), disabledInfo.fileName())) {
    return failure(DisableStatus::RenameFailed, source, disabled,
                   QObject::tr("The instance INI could not be disabled."));
  }
  if (QFileInfo::exists(source) || QFileInfo(source).isSymLink() ||
      !QFileInfo(disabled).isFile() || QFileInfo(disabled).isSymLink()) {
    return failure(DisableStatus::PublicationUncertain, source, disabled,
                   QObject::tr("The disabled INI could not be verified."));
  }
#endif

  return {DisableStatus::Disabled, source, disabled, {}};
}

RegistryResult updatePortableRegistration(
    QSettings& settings, const QString& path, bool registered,
    RegistryReadHook afterReadForTesting)
{
  if (path.trimmed().isEmpty()) {
    return registryFailure(QObject::tr("The portable instance path is empty."));
  }
  return mutatePortableRegistry(
      settings, registered ? QString{} : path,
      registered ? path : QString{}, /*requireRemoved=*/false,
      afterReadForTesting);
}

RegistryResult replacePortableRegistration(
    QSettings& settings, const QString& oldPath, const QString& newPath,
    RegistryReadHook afterReadForTesting)
{
  if (oldPath.trimmed().isEmpty() || newPath.trimmed().isEmpty()) {
    return registryFailure(
        QObject::tr("The portable instance rename paths are incomplete."));
  }
  return mutatePortableRegistry(settings, oldPath, newPath,
                                /*requireRemoved=*/true,
                                afterReadForTesting);
}

}  // namespace InstanceUnregister
