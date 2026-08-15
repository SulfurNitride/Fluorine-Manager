/*
Copyright (C) 2026 Fluorine Manager contributors.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

#include "wineprofileinisync.h"

#include "winepluginlistsync.h"

#include <uibase/transactionalwritefile.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <optional>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace WineProfileIniSync {
namespace {

constexpr auto BackupSuffix = ".mo2linux_backup";

Result fail(QString error) { return {false, std::move(error)}; }

Result ok() { return {true, {}}; }

QStringList caseVariants(const QString &path) {
  const QFileInfo info(path);
  QDir directory(info.absolutePath());
  QStringList variants;
  for (const QString &entry :
       directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot |
                           QDir::Hidden | QDir::System)) {
    if (entry.compare(info.fileName(), Qt::CaseInsensitive) == 0) {
      variants.append(directory.filePath(entry));
    }
  }
  return variants;
}

bool safeMovableLeaf(const QString &path, QString &detail) {
#ifdef Q_OS_UNIX
  struct stat status;
  const QByteArray encoded = QFile::encodeName(path);
  if (::lstat(encoded.constData(), &status) != 0) {
    detail = QString::fromLocal8Bit(std::strerror(errno));
    return false;
  }
  if (!S_ISREG(status.st_mode) && !S_ISLNK(status.st_mode)) {
    detail = QStringLiteral("leaf is neither a regular file nor a symlink");
    return false;
  }
  return true;
#else
  const QFileInfo info(path);
  if ((!info.exists() && !info.isSymLink()) ||
      (!info.isFile() && !info.isSymLink())) {
    detail = QStringLiteral("leaf is neither a regular file nor a symlink");
    return false;
  }
  return true;
#endif
}

QString retirementSuffix(const QString &ownerId) {
  const QByteArray digest =
      QCryptographicHash::hash(ownerId.toUtf8(), QCryptographicHash::Sha256)
          .toHex()
          .left(24);
  return QStringLiteral(".fluorine-session-") + QString::fromLatin1(digest);
}

QStringList backupVariants(const QString &path) {
  return caseVariants(path + QString::fromLatin1(BackupSuffix));
}

struct Work {
  Deployment *deployment{nullptr};
  QStringList newestCandidates;
};

bool leafExists(const QString &path) {
#ifdef Q_OS_UNIX
  struct stat status;
  return ::lstat(QFile::encodeName(path).constData(), &status) == 0;
#else
  const QFileInfo info(path);
  return info.exists() || info.isSymLink();
#endif
}

QString retirementPath(const QString &live, const QString &suffix) {
  return live + suffix;
}

bool restoreMovedBackups(Deployment &deployment, QString &detail) {
  bool complete = true;
  for (auto it = deployment.globalBackups.crbegin();
       it != deployment.globalBackups.crend(); ++it) {
    const QString &live = it->first;
    const QString &backup = it->second;
    if (!leafExists(backup)) {
      if (!leafExists(live)) {
        detail = QStringLiteral("Global INI backup '%1' and live leaf '%2' "
                                "are both missing.")
                     .arg(backup, live);
        complete = false;
      }
      continue;
    }
    if (leafExists(live) || !QFile::rename(backup, live)) {
      detail = QStringLiteral("Could not restore global INI '%1'.").arg(live);
      complete = false;
    }
  }
  if (complete)
    deployment.globalBackups.clear();
  return complete;
}

} // namespace

Result deploy(const QString &profilePath, const QString &prefixPath,
              const QString &ownerId, Deployment &deployment) {
  deployment = {};
  deployment.profilePath =
      QDir::cleanPath(QFileInfo(profilePath).absoluteFilePath());
  deployment.prefixPath =
      QDir::cleanPath(QFileInfo(prefixPath).absoluteFilePath());
  if (ownerId.trimmed().isEmpty() || deployment.profilePath.isEmpty() ||
      deployment.prefixPath.isEmpty()) {
    return fail(QStringLiteral("Invalid profile-INI deployment identity."));
  }

  const auto source = WinePluginListSync::read(deployment.profilePath);
  if (!source.snapshot)
    return fail(source.error);
  const QFileInfo destinationInfo(deployment.prefixPath);
  if (!QDir().mkpath(destinationInfo.absolutePath()) ||
      QFileInfo(destinationInfo.absolutePath()).isSymLink()) {
    return fail(QStringLiteral("Could not prepare profile-INI destination."));
  }

  const QStringList variants = caseVariants(deployment.prefixPath);
  const QStringList staleBackups = backupVariants(deployment.prefixPath);
  if (!staleBackups.isEmpty()) {
    return fail(QStringLiteral("Refusing unresolved global INI backup '%1'.")
                    .arg(staleBackups.first()));
  }
  for (const QString &variant : variants) {
    QString detail;
    if (!safeMovableLeaf(variant, detail)) {
      return fail(QStringLiteral("Refusing unsafe global INI '%1': %2")
                      .arg(variant, detail));
    }
    const QString backup = variant + QString::fromLatin1(BackupSuffix);
    if (leafExists(backup)) {
      return fail(QStringLiteral("Refusing occupied global INI backup '%1'.")
                      .arg(backup));
    }
  }

  // Every possible failure is preflighted before the first move.  Should a
  // rename still fail (I/O error or a same-user race), roll back the exact
  // successful moves and retain their ownership if rollback itself fails.
  for (const QString &variant : variants) {
    const QString backup = variant + QString::fromLatin1(BackupSuffix);
    if (!QFile::rename(variant, backup)) {
      QString detail;
      restoreMovedBackups(deployment, detail);
      return fail(QStringLiteral("Could not back up global INI '%1'. %2")
                      .arg(variant, detail));
    }
    deployment.globalBackups.append({variant, backup});
  }

  if (!WinePluginListSync::isSameFile(deployment.profilePath,
                                      *source.snapshot)) {
    QString detail;
    restoreMovedBackups(deployment, detail);
    return fail(QStringLiteral("Profile INI changed before deployment. %1")
                    .arg(detail));
  }
  MOBase::TransactionalWriteFile transaction(deployment.prefixPath);
  QString error;
  if (!WinePluginListSync::publish(transaction, *source.snapshot, error)) {
    QString detail;
    restoreMovedBackups(deployment, detail);
    return fail(QStringLiteral("Could not deploy profile INI '%1': %2 %3")
                    .arg(deployment.prefixPath, error, detail));
  }
  deployment.deployedLeaves.append(deployment.prefixPath);

  const QString lowerName = destinationInfo.fileName().toLower();
  if (lowerName != destinationInfo.fileName()) {
    const QString lowerPath =
        QDir(destinationInfo.absolutePath()).filePath(lowerName);
    if (leafExists(lowerPath) ||
        !QFile::link(destinationInfo.fileName(), lowerPath)) {
      QString detail;
      if (QFile::remove(deployment.prefixPath))
        deployment.deployedLeaves.clear();
      restoreMovedBackups(deployment, detail);
      return fail(
          QStringLiteral("Could not create profile-INI case alias '%1'. %2")
              .arg(lowerPath, detail));
    }
    deployment.deployedLeaves.append(lowerPath);
  }
  deployment.complete = true;
  return ok();
}

Result restoreLegacyBackup(const QString &livePath, const QString &backupPath) {
  const auto backup = WinePluginListSync::read(backupPath);
  if (!backup.snapshot) {
    if (!leafExists(backupPath))
      return ok();
    return fail(QStringLiteral("Refusing unsafe INI backup '%1': %2")
                    .arg(backupPath, backup.error));
  }
  if (!WinePluginListSync::isSameFile(backupPath, *backup.snapshot)) {
    return fail(QStringLiteral("INI backup changed before restoration."));
  }
  if (leafExists(livePath)) {
    const auto live = WinePluginListSync::read(livePath);
    if (!live.snapshot)
      return fail(live.error);
    if (live.snapshot->contents != backup.snapshot->contents) {
      return fail(QStringLiteral("Live INI '%1' and legacy backup '%2' both "
                                 "contain data; preserving both.")
                      .arg(livePath, backupPath));
    }
    if (!WinePluginListSync::isSameFile(livePath, *live.snapshot) ||
        !WinePluginListSync::isSameFile(backupPath, *backup.snapshot) ||
        !QFile::remove(backupPath)) {
      return fail(QStringLiteral("Could not safely deduplicate INI backup '%1'.")
                      .arg(backupPath));
    }
    return ok();
  }
  MOBase::TransactionalWriteFile transaction(livePath);
  QString error;
  if (!WinePluginListSync::publish(transaction, *backup.snapshot, error)) {
    return fail(
        QStringLiteral("Could not atomically restore INI backup '%1': %2")
            .arg(backupPath, error));
  }
  if (!WinePluginListSync::isSameFile(backupPath, *backup.snapshot) ||
      !QFile::remove(backupPath)) {
    return fail(
        QStringLiteral("Restored '%1' but could not retire backup '%2'.")
            .arg(livePath, backupPath));
  }
  return ok();
}

Result finish(QList<Deployment> &deployments, const QString &ownerId,
              bool publishChanges, CleanupPhase &phase) {
  if (ownerId.trimmed().isEmpty()) {
    return fail(QStringLiteral("Empty profile-INI deployment owner."));
  }
  const QString suffix = retirementSuffix(ownerId);
  QList<Work> work;
  for (Deployment &deployment : deployments) {
    deployment.profilePath =
        QDir::cleanPath(QFileInfo(deployment.profilePath).absoluteFilePath());
    deployment.prefixPath =
        QDir::cleanPath(QFileInfo(deployment.prefixPath).absoluteFilePath());
    if (deployment.profilePath.isEmpty() || deployment.prefixPath.isEmpty()) {
      return fail(QStringLiteral("Invalid profile-INI deployment mapping."));
    }
    if (deployment.sessionLeaves.isEmpty()) {
      deployment.sessionLeaves = publishChanges && deployment.complete
                                     ? caseVariants(deployment.prefixPath)
                                     : deployment.deployedLeaves;
    }
    Work entry{&deployment, {}};
    if (publishChanges && phase == CleanupPhase::Prepared) {
      QDateTime newestTime;
      for (const QString &variant : deployment.sessionLeaves) {
        const QFileInfo info(variant);
        if (!leafExists(variant))
          continue;
        if (info.isSymLink() && !QFileInfo(info.symLinkTarget()).exists())
          continue;
        if (info.lastModified() > newestTime) {
          newestTime = info.lastModified();
          entry.newestCandidates = {variant};
        } else if (info.lastModified() == newestTime) {
          entry.newestCandidates.append(variant);
        }
      }
    }
    work.append(std::move(entry));
  }

  // Publish every profile destination first. Prefix leaves and backups remain
  // untouched until the complete set has succeeded, so a later failure can
  // safely retry without losing the only game-edited generation.
  if (phase == CleanupPhase::Prepared) {
    for (const Work &entry : work) {
      if (entry.newestCandidates.isEmpty())
        continue;
      std::optional<WinePluginListSync::Snapshot> sourceSnapshot;
      QString sourcePath;
      for (const QString &candidate : entry.newestCandidates) {
        const auto source = WinePluginListSync::read(candidate);
        if (!source.snapshot)
          return fail(source.error);
        if (!WinePluginListSync::isSameFile(candidate, *source.snapshot)) {
          return fail(
              QStringLiteral("Profile INI source changed before publication."));
        }
        if (!sourceSnapshot) {
          sourceSnapshot = source.snapshot;
          sourcePath = candidate;
        } else if (sourceSnapshot->contents != source.snapshot->contents) {
          return fail(QStringLiteral(
              "Equally recent profile INI variants contain different data; "
              "preserving every variant."));
        }
      }
      MOBase::TransactionalWriteFile transaction(entry.deployment->profilePath);
      QString error;
      if (!WinePluginListSync::isSameFile(sourcePath, *sourceSnapshot) ||
          !WinePluginListSync::publish(transaction, *sourceSnapshot, error)) {
        return fail(QStringLiteral("Could not publish profile INI '%1': %2")
                        .arg(entry.deployment->profilePath, error));
      }
    }
    phase = CleanupPhase::Published;
  }

  if (phase == CleanupPhase::Published) {
    for (const Work &entry : work) {
      for (const QString &live : entry.deployment->sessionLeaves) {
        const QString retirement = retirementPath(live, suffix);
        if (leafExists(retirement))
          continue;
        if (!leafExists(live))
          continue;
        QString detail;
        if (!safeMovableLeaf(live, detail) ||
            !QFile::rename(live, retirement)) {
          return fail(QStringLiteral("Could not retain deployed INI '%1': %2")
                          .arg(live, detail));
        }
      }
    }
    phase = CleanupPhase::SessionRetired;
  }

  if (phase == CleanupPhase::SessionRetired) {
    for (const Work &entry : work) {
      for (const auto &owned : entry.deployment->globalBackups) {
        const QString &live = owned.first;
        const QString &backup = owned.second;
        if (!leafExists(backup)) {
          if (leafExists(live))
            continue;
          return fail(
              QStringLiteral("Global INI backup '%1' is missing.").arg(backup));
        }
        QString detail;
        if (!safeMovableLeaf(backup, detail)) {
          return fail(
              QStringLiteral("Refusing unsafe global INI backup '%1': %2")
                  .arg(backup, detail));
        }
        if (leafExists(live) || !QFile::rename(backup, live)) {
          return fail(
              QStringLiteral("Could not restore global INI '%1'.").arg(live));
        }
      }
    }
    phase = CleanupPhase::GlobalsRestored;
  }

  // Retire session copies only after every mapping has restored its global
  // topology. Until then their presence is the durable proof that publication
  // already succeeded, preventing a retry from copying a restored global INI
  // back over the profile generation.
  if (phase == CleanupPhase::GlobalsRestored) {
    for (const Work &entry : work) {
      for (const QString &live : entry.deployment->sessionLeaves) {
        const QString retirement = retirementPath(live, suffix);
        if (!leafExists(retirement))
          continue;
        QString detail;
        if (!safeMovableLeaf(retirement, detail) ||
            !QFile::remove(retirement)) {
          return fail(QStringLiteral("Could not retire deployed INI '%1': %2")
                          .arg(retirement, detail));
        }
      }
    }
  }
  return ok();
}

} // namespace WineProfileIniSync
