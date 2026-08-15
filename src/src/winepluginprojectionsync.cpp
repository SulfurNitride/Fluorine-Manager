/*
Copyright (C) 2026 Fluorine Manager contributors.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

#include "winepluginprojectionsync.h"

#include "winepluginlistsync.h"

#include <uibase/transactionalwritefile.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <optional>
#include <utility>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace WinePluginProjectionSync
{
namespace
{

constexpr auto BackupSuffix = ".mo2linux_plugin_backup";

Result fail(QString error, QStringList recoveryFiles = {})
{
  return {false, std::move(error), std::move(recoveryFiles)};
}

Result ok(QStringList recoveryFiles = {})
{
  return {true, {}, std::move(recoveryFiles)};
}

bool leafExists(const QString& path)
{
#ifdef Q_OS_UNIX
  struct stat status;
  return ::lstat(QFile::encodeName(path).constData(), &status) == 0;
#else
  const QFileInfo info(path);
  return info.exists() || info.isSymLink();
#endif
}

bool safeMovableLeaf(const QString& path, QString& error)
{
#ifdef Q_OS_UNIX
  struct stat status;
  if (::lstat(QFile::encodeName(path).constData(), &status) != 0) {
    error = QString::fromLocal8Bit(std::strerror(errno));
    return false;
  }
  if (!S_ISREG(status.st_mode) && !S_ISLNK(status.st_mode)) {
    error = QStringLiteral("leaf is neither a regular file nor a symlink");
    return false;
  }
#else
  const QFileInfo info(path);
  if ((!info.exists() && !info.isSymLink()) ||
      (!info.isFile() && !info.isSymLink())) {
    error = QStringLiteral("leaf is neither a regular file nor a symlink");
    return false;
  }
#endif
  return true;
}

QStringList caseVariants(const QString& path)
{
  const QFileInfo info(path);
  QDir directory(info.absolutePath());
  QStringList variants;
  for (const QString& entry : directory.entryList(
           QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
           QDir::Name)) {
    if (entry.compare(info.fileName(), Qt::CaseInsensitive) == 0) {
      variants.append(directory.filePath(entry));
    }
  }
  return variants;
}

QString ownerSuffix(const QString& ownerId)
{
  const QByteArray digest =
      QCryptographicHash::hash(ownerId.toUtf8(), QCryptographicHash::Sha256)
          .toHex()
          .left(24);
  return QStringLiteral(".fluorine-plugin-session-") +
         QString::fromLatin1(digest);
}

QString retirementPath(const QString& live, const QString& ownerId)
{
  return live + ownerSuffix(ownerId);
}

bool restoreBackups(Deployment& deployment, QString& detail)
{
  bool complete = true;
  for (auto targetIt = deployment.targets.rbegin();
       targetIt != deployment.targets.rend(); ++targetIt) {
    for (qsizetype index = targetIt->globalBackups.size(); index-- > 0;) {
      const QString live = targetIt->globalBackups.at(index).first;
      const QString backup = targetIt->globalBackups.at(index).second;
      if (!leafExists(backup)) {
        if (!leafExists(live)) {
          detail = QStringLiteral("Plugin-list backup '%1' and live leaf '%2' "
                                  "are both missing.")
                       .arg(backup, live);
          complete = false;
        } else {
          targetIt->globalBackups.removeAt(index);
        }
        continue;
      }
      if (leafExists(live) || !QFile::rename(backup, live)) {
        detail = QStringLiteral("Could not restore plugin-list leaf '%1'.")
                     .arg(live);
        complete = false;
      } else {
        targetIt->globalBackups.removeAt(index);
      }
    }
  }
  return complete;
}

bool removeDeployedLeaves(Deployment& deployment, QString& detail)
{
  bool complete = true;
  for (auto targetIt = deployment.targets.rbegin();
       targetIt != deployment.targets.rend(); ++targetIt) {
    for (qsizetype index = targetIt->deployedLeaves.size(); index-- > 0;) {
      const QString leaf = targetIt->deployedLeaves.at(index);
      if (!leafExists(leaf)) {
        targetIt->deployedLeaves.removeAt(index);
        continue;
      }
      const auto current = WinePluginListSync::read(leaf);
      if (!current.snapshot ||
          current.snapshot->contents != deployment.baselineContents ||
          !WinePluginListSync::isSameFile(leaf, *current.snapshot) ||
          !QFile::remove(leaf)) {
        detail = QStringLiteral("Could not safely retire projected plugin list "
                                "'%1'.")
                     .arg(leaf);
        complete = false;
      } else {
        targetIt->deployedLeaves.removeAt(index);
      }
    }
  }
  return complete;
}

bool rollbackPreparation(Deployment& deployment, QString& detail)
{
  const bool leavesRemoved = removeDeployedLeaves(deployment, detail);
  const bool backupsRestored = restoreBackups(deployment, detail);
  if (leavesRemoved && backupsRestored) {
    deployment.targets.clear();
    deployment.phase = CleanupPhase::Complete;
  }
  return leavesRemoved && backupsRestored;
}

QString recoveryPath(const Deployment& deployment, QByteArrayView contents,
                     int ordinal)
{
  const QByteArray owner =
      QCryptographicHash::hash(deployment.ownerId.toUtf8(),
                               QCryptographicHash::Sha256)
          .toHex()
          .left(12);
  const QByteArray generation =
      QCryptographicHash::hash(contents, QCryptographicHash::Sha256)
          .toHex()
          .left(12);
  return QStringLiteral("%1.fluorine-recovery-%2-%3-%4")
      .arg(deployment.profilePath, QString::fromLatin1(owner),
           QString::fromLatin1(generation))
      .arg(ordinal);
}

bool preserveCandidate(const Deployment& deployment, QByteArrayView contents,
                       int ordinal, QString& path, QString& error)
{
  path = recoveryPath(deployment, contents, ordinal);
  MOBase::TransactionalWriteFile transaction(path);
  QByteArray existing;
  bool present = false;
  if (!transaction.readOriginal(existing, present)) {
    error = transaction.errorString();
    return false;
  }
  if (present) {
    if (existing == contents) {
      return true;
    }
    error = QStringLiteral("Recovery destination '%1' contains a different "
                           "generation.")
                .arg(path);
    return false;
  }
  if (!transaction.replaceWith(contents)) {
    error = transaction.errorString();
    return false;
  }
  return true;
}

}  // namespace

QString physicalPrefixRoot(const QString& targetPath)
{
  QFileInfo current(QFileInfo(targetPath).absolutePath());
  while (!current.exists() && !current.isSymLink()) {
    const QDir parent = current.dir();
    if (parent.absolutePath() == current.absoluteFilePath()) {
      return {};
    }
    current = QFileInfo(parent.absolutePath());
  }
  if (current.isSymLink()) {
    const QString canonical = current.canonicalFilePath();
    if (canonical.isEmpty()) {
      return {};
    }
    current = QFileInfo(canonical);
  }
  QString candidate = current.isDir() ? current.canonicalFilePath()
                                      : current.dir().canonicalPath();
  while (!candidate.isEmpty()) {
    const QFileInfo drive(QDir(candidate).filePath(QStringLiteral("drive_c")));
    if (drive.isDir() && !drive.isSymLink()) {
      return QFileInfo(candidate).canonicalFilePath();
    }
    const QString next = QFileInfo(candidate).dir().canonicalPath();
    if (next.isEmpty() || next == candidate) {
      break;
    }
    candidate = next;
  }
  return {};
}

Result prepare(const QString& profilePath, const QStringList& targetPaths,
               const QString& ownerId, Deployment& deployment)
{
  deployment = {};
  deployment.profilePath =
      QDir::cleanPath(QFileInfo(profilePath).absoluteFilePath());
  deployment.ownerId = ownerId;
  if (deployment.profilePath.isEmpty() || ownerId.trimmed().isEmpty() ||
      targetPaths.isEmpty()) {
    return fail(QStringLiteral("Invalid plugin-projection identity."));
  }

  const auto profile = WinePluginListSync::read(deployment.profilePath);
  if (!profile.snapshot) {
    return fail(profile.error);
  }
  deployment.baselineContents = profile.snapshot->contents;

  QStringList normalizedTargets;
  QStringList targetFamilyKeys;
  for (const QString& requested : targetPaths) {
    const QString normalized =
        QDir::cleanPath(QFileInfo(requested).absoluteFilePath());
    if (normalized.isEmpty()) {
      continue;
    }
    const QFileInfo info(normalized);
    const QString canonicalParent = info.dir().canonicalPath();
    const QString familyKey =
        (canonicalParent.isEmpty() ? info.absolutePath() : canonicalParent) +
        QChar::Null + info.fileName().toCaseFolded();
    if (targetFamilyKeys.contains(familyKey)) {
      continue;
    }
    normalizedTargets.append(normalized);
    targetFamilyKeys.append(familyKey);
  }
  if (normalizedTargets.isEmpty()) {
    return fail(QStringLiteral("No distinct plugin-projection targets."));
  }

  // Complete preflight: no prefix leaf is moved until every target family,
  // backup slot, parent, and profile generation has been authenticated.
  QList<Target> plannedTargets;
  for (const QString& path : normalizedTargets) {
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath()) ||
        !QFileInfo(info.absolutePath()).isDir()) {
      return fail(QStringLiteral("Could not prepare plugin-list destination '%1'.")
                      .arg(path));
    }
    const auto family = WinePluginListSync::readUniqueFamily(path);
    if (!family.error.isEmpty()) {
      return fail(family.error);
    }
    Target target;
    target.path = path;
    const QString projectedRetirement = retirementPath(path, ownerId);
    if (leafExists(projectedRetirement)) {
      return fail(QStringLiteral("Refusing occupied plugin-list retirement "
                                 "leaf '%1'.")
                      .arg(projectedRetirement));
    }
    for (const QString& variant : caseVariants(path)) {
      QString detail;
      if (!safeMovableLeaf(variant, detail)) {
        return fail(QStringLiteral("Refusing unsafe plugin-list leaf '%1': %2")
                        .arg(variant, detail));
      }
      const QString backup = variant + QString::fromLatin1(BackupSuffix);
      if (leafExists(backup)) {
        return fail(QStringLiteral("Refusing occupied plugin-list backup '%1'.")
                        .arg(backup));
      }
      target.globalBackups.append({variant, backup});
    }
    plannedTargets.append(std::move(target));
  }
  if (!WinePluginListSync::isSameFile(deployment.profilePath,
                                      *profile.snapshot)) {
    return fail(QStringLiteral("Profile plugin list changed during preflight."));
  }
  deployment.targets = std::move(plannedTargets);

  for (Target& target : deployment.targets) {
    for (const auto& original : target.globalBackups) {
      if (!QFile::rename(original.first, original.second)) {
        QString detail;
        rollbackPreparation(deployment, detail);
        return fail(QStringLiteral("Could not back up plugin-list leaf '%1'. %2")
                        .arg(original.first, detail));
      }
    }
    MOBase::TransactionalWriteFile transaction(target.path);
    QString publicationError;
    if (!WinePluginListSync::publish(transaction, *profile.snapshot,
                                     publicationError)) {
      QString detail;
      rollbackPreparation(deployment, detail);
      return fail(QStringLiteral("Could not deploy plugin list '%1': %2 %3")
                      .arg(target.path, publicationError, detail));
    }
    target.deployedLeaves.append(target.path);
  }
  deployment.phase = CleanupPhase::Prepared;
  deployment.projectionComplete = true;
  return ok();
}

Result finish(Deployment& deployment, bool publishChanges)
{
  if (deployment.targets.isEmpty() || deployment.profilePath.isEmpty() ||
      deployment.ownerId.trimmed().isEmpty()) {
    return fail(QStringLiteral("Invalid plugin-projection receipt."));
  }
  if (!deployment.projectionComplete) {
    QString detail;
    if (!rollbackPreparation(deployment, detail)) {
      return fail(detail.isEmpty()
                      ? QStringLiteral("Plugin projection rollback is incomplete.")
                      : detail);
    }
    return ok();
  }
  QStringList recoveryFiles;

  if (deployment.phase == CleanupPhase::Prepared) {
    QList<QByteArray> candidates;
    for (const Target& target : deployment.targets) {
      const auto family = WinePluginListSync::readUniqueFamily(target.path);
      if (!family.error.isEmpty()) {
        return fail(family.error);
      }
      if (!family.snapshot ||
          family.snapshot->contents == deployment.baselineContents) {
        continue;
      }
      if (std::none_of(candidates.cbegin(), candidates.cend(),
                       [&family](const QByteArray& existing) {
                         return existing == family.snapshot->contents;
                       })) {
        candidates.append(family.snapshot->contents);
      }
    }

    std::optional<WinePluginListSync::Snapshot> profileSnapshot;
    if (publishChanges && !candidates.isEmpty()) {
      const auto profile = WinePluginListSync::read(deployment.profilePath);
      if (profile.snapshot) {
        profileSnapshot = std::move(profile.snapshot);
      }
    }
    for (int index = 0; index < candidates.size(); ++index) {
      const QByteArray& candidate = candidates.at(index);
      const bool suspicious = WinePluginListSync::isSuspiciousActiveDrop(
          WinePluginListSync::countStarred(deployment.baselineContents),
          WinePluginListSync::countStarred(candidate));
      const bool unambiguous = candidates.size() == 1;
      const bool profileUnchanged =
          profileSnapshot &&
          profileSnapshot->contents == deployment.baselineContents;
      const bool profileAlreadyMatches =
          profileSnapshot && profileSnapshot->contents == candidate;
      if (publishChanges && unambiguous && !suspicious &&
          (profileUnchanged || profileAlreadyMatches)) {
        if (profileUnchanged) {
          MOBase::TransactionalWriteFile transaction(deployment.profilePath);
          QByteArray current;
          bool present = false;
          if (!transaction.readOriginal(current, present) || !present ||
              current != deployment.baselineContents ||
              !transaction.replaceWith(candidate)) {
            return fail(QStringLiteral("Profile plugin list changed before launch "
                                       "output could be published: %1")
                            .arg(transaction.errorString()));
          }
        }
        continue;
      }

      QString recovery;
      QString error;
      if (!preserveCandidate(deployment, candidate, index, recovery, error)) {
        return fail(QStringLiteral("Could not preserve launch plugin-list "
                                   "generation: %1")
                        .arg(error),
                    recoveryFiles);
      }
      recoveryFiles.append(recovery);
    }
    deployment.phase = CleanupPhase::Reconciled;
  }

  if (deployment.phase == CleanupPhase::Reconciled) {
    for (Target& target : deployment.targets) {
      if (!target.sessionFamilyCaptured) {
        target.sessionLeaves = caseVariants(target.path);
        target.sessionFamilyCaptured = true;
      }
      for (const QString& live : target.sessionLeaves) {
        const QString retirement = retirementPath(live, deployment.ownerId);
        const auto retired = std::find_if(
            target.retiredLeaves.cbegin(), target.retiredLeaves.cend(),
            [&live, &retirement](const QPair<QString, QString>& entry) {
              return entry.first == live && entry.second == retirement;
            });
        if (retired != target.retiredLeaves.cend()) {
          if (leafExists(live) || !leafExists(retirement)) {
            return fail(QStringLiteral("Recorded plugin-list retirement for "
                                       "'%1' is inconsistent.")
                            .arg(live),
                        recoveryFiles);
          }
          continue;
        }
        if (leafExists(retirement)) {
          return fail(QStringLiteral("Refusing occupied plugin-list retirement "
                                     "leaf '%1'.")
                          .arg(retirement),
                      recoveryFiles);
        }
        if (!leafExists(live)) {
          continue;
        }
        QString detail;
        if (!safeMovableLeaf(live, detail) || !QFile::rename(live, retirement)) {
          return fail(QStringLiteral("Could not retain launch plugin list '%1': "
                                     "%2")
                          .arg(live, detail),
                      recoveryFiles);
        }
        target.retiredLeaves.append({live, retirement});
      }
    }
    deployment.phase = CleanupPhase::SessionsRetired;
  }

  if (deployment.phase == CleanupPhase::SessionsRetired) {
    QString detail;
    if (!restoreBackups(deployment, detail)) {
      return fail(detail, recoveryFiles);
    }
    deployment.phase = CleanupPhase::GlobalsRestored;
  }

  if (deployment.phase == CleanupPhase::GlobalsRestored) {
    for (Target& target : deployment.targets) {
      for (qsizetype index = target.retiredLeaves.size(); index-- > 0;) {
        const QString retirement = target.retiredLeaves.at(index).second;
        if (!leafExists(retirement)) {
          target.retiredLeaves.removeAt(index);
          continue;
        }
        QString detail;
        if (!safeMovableLeaf(retirement, detail) ||
            !QFile::remove(retirement)) {
          return fail(QStringLiteral("Could not retire launch plugin list '%1': "
                                     "%2")
                          .arg(retirement, detail),
                      recoveryFiles);
        }
        target.retiredLeaves.removeAt(index);
      }
      target.sessionLeaves.clear();
      target.sessionFamilyCaptured = false;
      target.deployedLeaves.clear();
    }
    deployment.phase = CleanupPhase::Complete;
  }

  return ok(recoveryFiles);
}

}  // namespace WinePluginProjectionSync
