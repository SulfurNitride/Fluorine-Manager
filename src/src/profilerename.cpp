#include "profilerename.h"

#include <uibase/filesystemutilities.h>

#include <QCoreApplication>
#include <QDirIterator>
#include <QEvent>
#include <QFile>
#include <QFileInfo>

#include <cerrno>
#include <cstring>
#include <exception>
#include <optional>

#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace ProfileRename
{
namespace
{

QString cleanAbsolutePath(const QString& path)
{
  return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

Result failure(Status status, const QString& sourcePath, const QString& targetPath,
               const QString& error)
{
  return {status, sourcePath, targetPath, error, {}};
}

bool caseInsensitiveSiblingExists(const QDir& parent, const QString& sourceName,
                                  const QString& requestedName)
{
  QDirIterator entries(
      parent.absolutePath(),
      QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
      QDirIterator::NoIteratorFlags);
  while (entries.hasNext())
  {
    entries.next();
    const QString candidate = entries.fileName();
    if (candidate == sourceName)
    {
      continue;
    }
    if (candidate.compare(requestedName, Qt::CaseInsensitive) == 0)
    {
      return true;
    }
  }
  return false;
}

#ifdef Q_OS_LINUX
struct FileIdentity
{
  quint64 device = 0;
  quint64 inode = 0;
};

std::optional<FileIdentity> identityOf(const QString& path)
{
  struct stat value{};
  const QByteArray encoded = QFile::encodeName(path);
  if (::lstat(encoded.constData(), &value) != 0)
  {
    return std::nullopt;
  }
  return FileIdentity{static_cast<quint64>(value.st_dev),
                      static_cast<quint64>(value.st_ino)};
}

bool sameIdentity(const FileIdentity& left, const FileIdentity& right)
{
  return left.device == right.device && left.inode == right.inode;
}
#endif

enum class RenameStatus
{
  Success,
  DestinationExists,
  Failure,
  CommittedInvalid,
};

RenameStatus renameNoReplace(const QString& source, const QString& destination,
                             bool caseOnly, QString& error)
{
#ifdef Q_OS_LINUX
  const auto sourceIdentity = identityOf(source);
  if (!sourceIdentity)
  {
    error = QObject::tr("The source profile directory disappeared.");
    return RenameStatus::Failure;
  }

  // A case-insensitive filesystem can report the source itself at the new
  // spelling. QDir handles that one identity-preserving case; ordinary moves
  // use renameat2 so a racing destination can never be replaced.
  const auto destinationIdentity = identityOf(destination);
  const bool aliasesSource = caseOnly && destinationIdentity &&
                             sameIdentity(*sourceIdentity, *destinationIdentity);
  if (aliasesSource)
  {
    const QFileInfo sourceInfo(source);
    if (!QDir(sourceInfo.absolutePath())
             .rename(sourceInfo.fileName(), QFileInfo(destination).fileName()))
    {
      error = QObject::tr("The profile directory could not be renamed.");
      return RenameStatus::Failure;
    }
  }
  else
  {
#if defined(SYS_renameat2) && defined(RENAME_NOREPLACE)
    const QByteArray encodedSource = QFile::encodeName(source);
    const QByteArray encodedDestination = QFile::encodeName(destination);
    if (::syscall(SYS_renameat2, AT_FDCWD, encodedSource.constData(), AT_FDCWD,
                  encodedDestination.constData(), RENAME_NOREPLACE) != 0)
    {
      if (errno == EEXIST)
      {
        error = QObject::tr("The rename destination already exists.");
        return RenameStatus::DestinationExists;
      }
      error = QObject::tr("The profile directory could not be renamed: %1")
                  .arg(QString::fromLocal8Bit(std::strerror(errno)));
      return RenameStatus::Failure;
    }
#else
    error = QObject::tr("Atomic profile-directory rename is unavailable.");
    return RenameStatus::Failure;
#endif
  }

  const auto publishedIdentity = identityOf(destination);
  if (!publishedIdentity || !sameIdentity(*sourceIdentity, *publishedIdentity))
  {
    error = QObject::tr("The renamed profile directory changed during publication.");
    return RenameStatus::CommittedInvalid;
  }
  return RenameStatus::Success;
#else
  Q_UNUSED(caseOnly);
  if (QFileInfo::exists(destination) || QFileInfo(destination).isSymLink())
  {
    error = QObject::tr("The rename destination already exists.");
    return RenameStatus::DestinationExists;
  }
  const QFileInfo sourceInfo(source);
  if (!QDir(sourceInfo.absolutePath())
           .rename(sourceInfo.fileName(), QFileInfo(destination).fileName()))
  {
    error = QObject::tr("The profile directory could not be renamed.");
    return RenameStatus::Failure;
  }
  return RenameStatus::Success;
#endif
}

}  // namespace

Result apply(const QDir& directory, QSettings& currentSettings,
             const QString& requestedName)
{
  const QString sourcePath = cleanAbsolutePath(directory.absolutePath());
  const QFileInfo sourceInfo(sourcePath);
  const QString sourceName = sourceInfo.fileName();
  QDir parent(sourceInfo.absolutePath());
  const QString targetPath = cleanAbsolutePath(parent.absoluteFilePath(requestedName));

  QString normalizedName = requestedName;
  if (!MOBase::fixDirectoryName(normalizedName) || normalizedName != requestedName ||
      requestedName == QStringLiteral(".") || requestedName == QStringLiteral(".."))
  {
    return failure(Status::InvalidName, sourcePath, targetPath,
                   QObject::tr("Invalid profile name: %1").arg(requestedName));
  }

  if (sourceName == requestedName)
  {
    return {Status::NoChange, sourcePath, sourcePath, {}, {}};
  }

  if (!sourceInfo.exists() || !sourceInfo.isDir() || sourceInfo.isSymLink())
  {
    return failure(Status::SourceUnavailable, sourcePath, targetPath,
                   QObject::tr("The source profile directory is unavailable or unsafe."));
  }

  const QString expectedSettings =
      cleanAbsolutePath(QDir(sourcePath).absoluteFilePath("settings.ini"));
  if (cleanAbsolutePath(currentSettings.fileName()) != expectedSettings)
  {
    return failure(
        Status::SettingsMismatch, sourcePath, targetPath,
        QObject::tr("The profile settings backend is bound to another directory."));
  }

  const bool caseOnly = sourceName.compare(requestedName, Qt::CaseInsensitive) == 0;
  const QFileInfo targetInfo(targetPath);
#ifdef Q_OS_LINUX
  const auto sourceIdentity = identityOf(sourcePath);
  const auto targetIdentity = identityOf(targetPath);
  const bool targetIsSource = caseOnly && sourceIdentity && targetIdentity &&
                              sameIdentity(*sourceIdentity, *targetIdentity);
#else
  const bool targetIsSource = false;
#endif
  if ((!targetIsSource && (targetInfo.exists() || targetInfo.isSymLink())) ||
      caseInsensitiveSiblingExists(parent, sourceName, requestedName))
  {
    return failure(Status::DestinationExists, sourcePath, targetPath,
                   QObject::tr("A profile with the same name already exists."));
  }

  std::unique_ptr<QSettings> replacement;
  try
  {
    replacement = std::make_unique<QSettings>(
        QDir(targetPath).absoluteFilePath("settings.ini"), QSettings::IniFormat);
  }
  catch (const std::exception& e)
  {
    return failure(Status::RebindFailed, sourcePath, targetPath,
                   QObject::tr("The renamed profile settings could not be prepared: %1")
                       .arg(QString::fromUtf8(e.what())));
  }
  catch (...)
  {
    return failure(Status::RebindFailed, sourcePath, targetPath,
                   QObject::tr("The renamed profile settings could not be prepared."));
  }

  currentSettings.sync();
  if (currentSettings.status() != QSettings::NoError)
  {
    return failure(Status::SettingsSyncFailed, sourcePath, targetPath,
                   QObject::tr("The profile settings could not be saved before renaming."));
  }
  QCoreApplication::removePostedEvents(&currentSettings, QEvent::UpdateRequest);

  QString renameError;
  const RenameStatus renamed =
      renameNoReplace(sourcePath, targetPath, caseOnly, renameError);
  if (renamed == RenameStatus::DestinationExists)
  {
    return failure(Status::DestinationExists, sourcePath, targetPath,
                   QObject::tr("A profile with the same name already exists."));
  }
  if (renamed == RenameStatus::CommittedInvalid)
  {
    return failure(Status::RollbackFailed, sourcePath, targetPath, renameError);
  }
  if (renamed != RenameStatus::Success)
  {
    return failure(Status::RenameFailed, sourcePath, targetPath, renameError);
  }

  replacement->sync();
  if (replacement->status() != QSettings::NoError)
  {
    QString rollbackError;
    const RenameStatus rolledBack =
        renameNoReplace(targetPath, sourcePath, caseOnly, rollbackError);
    return failure(
        rolledBack == RenameStatus::Success ? Status::RebindFailed : Status::RollbackFailed,
        sourcePath, targetPath,
        rolledBack == RenameStatus::Success
            ? QObject::tr("The renamed profile settings could not be reopened.")
            : QObject::tr("The renamed profile settings could not be reopened, and the "
                          "directory rename could not be rolled back: %1")
                  .arg(rollbackError));
  }

  return {Status::Renamed, sourcePath, targetPath, {}, std::move(replacement)};
}

}  // namespace ProfileRename
