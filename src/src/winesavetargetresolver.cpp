/*
Copyright (C) 2026 Fluorine Manager contributors.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

#include "winesavetargetresolver.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <filesystem>
#include <optional>

namespace fs = std::filesystem;

namespace {

WineSaveTargetResolver::Plan failure(const QString &error) {
  return {.error = error};
}

QString absoluteClean(const QString &path) {
  return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString canonicalWithMissingTail(const QString &path) {
  QFileInfo current(absoluteClean(path));
  QStringList tail;
  while (!current.exists() && !current.isSymLink() &&
         !current.fileName().isEmpty()) {
    tail.prepend(current.fileName());
    current = QFileInfo(current.absolutePath());
  }

  QString resolved = current.canonicalFilePath();
  if (resolved.isEmpty()) {
    resolved = current.absoluteFilePath();
  }
  for (const QString &component : tail) {
    resolved = QDir(resolved).filePath(component);
  }
  return QDir::cleanPath(resolved);
}

QString physicalLeafSlot(const QString &path) {
  const QFileInfo info(absoluteClean(path));
  return QDir(canonicalWithMissingTail(info.absolutePath()))
      .filePath(info.fileName());
}

bool within(const QString &child, const QString &parent) {
  const QString relative = QDir(parent).relativeFilePath(child);
  return relative != QStringLiteral("..") &&
         !relative.startsWith(QStringLiteral("../")) &&
         !QDir::isAbsolutePath(relative);
}

bool samePath(const QString &left, const QString &right) {
  return canonicalWithMissingTail(left) == canonicalWithMissingTail(right);
}

QString translatedWineUserPath(const QString &source,
                               const QString &prefixUserRoot) {
  static const QRegularExpression userDirectory(
      QStringLiteral("(?:^|/)drive_c/users/[^/]+/(.+)$"),
      QRegularExpression::CaseInsensitiveOption);
  const auto match = userDirectory.match(QDir::fromNativeSeparators(source));
  return match.hasMatch() ? QDir(prefixUserRoot).filePath(match.captured(1))
                          : QString{};
}

bool sameOrChildPhysically(const QString &candidate, const QString &root) {
  const QString physicalCandidate = canonicalWithMissingTail(candidate);
  const QString physicalRoot = canonicalWithMissingTail(root);
  return physicalCandidate == physicalRoot ||
         within(physicalCandidate, physicalRoot);
}

bool sameCaseFamilyLeaf(const QString &candidate, const QString &target) {
  return canonicalWithMissingTail(QFileInfo(candidate).absolutePath()) ==
             canonicalWithMissingTail(QFileInfo(target).absolutePath()) &&
         QFileInfo(candidate).fileName().compare(QFileInfo(target).fileName(),
                                                 Qt::CaseInsensitive) == 0;
}

QStringList caseFamily(const QString &path) {
  const QFileInfo info(path);
  QDir parent(info.absolutePath());
  QStringList result;
  for (const QString &entry :
       parent.entryList(QDir::AllEntries | QDir::Hidden | QDir::System |
                        QDir::NoDotAndDotDot)) {
    if (entry.compare(info.fileName(), Qt::CaseInsensitive) == 0) {
      result.append(parent.filePath(entry));
    }
  }
  return result;
}

fs::file_status safeSymlinkStatus(const fs::path &path,
                                  std::error_code &error) {
  fs::file_status status = fs::symlink_status(path, error);
  if (error == std::errc::no_such_file_or_directory) {
    error.clear();
    status = fs::file_status(fs::file_type::not_found);
  }
  return status;
}

} // namespace

namespace WineSaveTargetResolver {

Plan resolve(const QString &prefixDriveRoot, const QString &prefixUserRoot,
             const QString &prefixMyGamesRoot, const QString &fallbackGameName,
             const QString &gameRoot, const QString &gameSavesPath,
             const QString &profileSavesPath, bool allowFixedGameDirectory,
             const MappingType &mappings) {
  const QString physicalGameRoot = canonicalWithMissingTail(gameRoot);
  const QString physicalGameSaves = physicalLeafSlot(gameSavesPath);
  std::optional<Plan> mappedPlan;

  for (const Mapping &mapping : mappings) {
    if (!mapping.isDirectory || !samePath(mapping.source, profileSavesPath)) {
      continue;
    }

    Plan candidate;
    if (allowFixedGameDirectory &&
        isFixedGameDirectorySaveMapping(mapping, gameRoot, gameSavesPath,
                                        profileSavesPath)) {
      if (!within(physicalGameSaves, physicalGameRoot)) {
        return failure(
            QStringLiteral(
                "Fixed game save directory '%1' escapes game root '%2'.")
                .arg(gameSavesPath, gameRoot));
      }
      candidate = {.success = true,
                   .kind = Kind::FixedGameDirectory,
                   .livePath = physicalGameSaves,
                   .topologyRoot = physicalGameRoot};
    } else {
      const QString translated =
          translatedWineUserPath(mapping.destination, prefixUserRoot);
      if (!translated.isEmpty()) {
        candidate = {.success = true,
                     .kind = Kind::PrefixRouted,
                     .livePath = QDir::cleanPath(translated),
                     .topologyRoot = canonicalWithMissingTail(prefixDriveRoot)};
      } else if (allowFixedGameDirectory) {
        return failure(
            QStringLiteral(
                "Profile save mapping targets unsupported directory '%1'.")
                .arg(mapping.destination));
      } else {
        continue;
      }
    }

    if (mappedPlan.has_value() &&
        (mappedPlan->kind != candidate.kind ||
         mappedPlan->livePath != candidate.livePath ||
         mappedPlan->topologyRoot != candidate.topologyRoot)) {
      return failure(QStringLiteral(
          "Profile save mappings resolve to multiple physical destinations."));
    }
    mappedPlan = std::move(candidate);
  }

  if (mappedPlan.has_value()) {
    return *mappedPlan;
  }

  const QString translated =
      translatedWineUserPath(gameSavesPath, prefixUserRoot);
  if (!translated.isEmpty()) {
    return {.success = true,
            .kind = Kind::PrefixRouted,
            .livePath = QDir::cleanPath(translated),
            .topologyRoot = canonicalWithMissingTail(prefixDriveRoot)};
  }

  return {.success = true,
          .kind = Kind::PrefixRouted,
          .livePath =
              QDir(prefixMyGamesRoot)
                  .filePath(fallbackGameName + QStringLiteral("/Saves")),
          .topologyRoot = canonicalWithMissingTail(prefixDriveRoot)};
}

bool isFixedGameDirectorySaveMapping(const Mapping &mapping,
                                     const QString &gameRoot,
                                     const QString &gameSavesPath,
                                     const QString &profileSavesPath) {
  if (!mapping.isDirectory || !samePath(mapping.source, profileSavesPath) ||
      physicalLeafSlot(mapping.destination) !=
          physicalLeafSlot(gameSavesPath)) {
    return false;
  }
  const QString physicalRoot = canonicalWithMissingTail(gameRoot);
  const QString physicalSaves = physicalLeafSlot(gameSavesPath);
  return !physicalRoot.isEmpty() && !physicalSaves.isEmpty() &&
         within(physicalSaves, physicalRoot);
}

MappingType filterFixedMappings(const MappingType &mappings,
                                const QString &liveSavePath,
                                const QString &configurationRoot,
                                const QStringList &iniNames) {
  QStringList liveSlots;
  if (!liveSavePath.isEmpty()) {
    liveSlots.append(liveSavePath);
    const QFileInfo liveInfo(liveSavePath);
    const QString lowerSlot =
        QDir(liveInfo.absolutePath()).filePath(liveInfo.fileName().toLower());
    if (lowerSlot != liveSavePath) {
      liveSlots.append(lowerSlot);
    }
  }

  MappingType filtered;
  filtered.reserve(mappings.size());
  for (const Mapping &mapping : mappings) {
    const bool saveOwned = std::any_of(
        liveSlots.cbegin(), liveSlots.cend(), [&](const QString &slot) {
          return sameOrChildPhysically(mapping.destination, slot);
        });
    bool owned = saveOwned;
    if (!owned && !mapping.isDirectory) {
      for (const QString &iniName : iniNames) {
        const QString target =
            QDir(configurationRoot).filePath(QFileInfo(iniName).fileName());
        if (sameCaseFamilyLeaf(mapping.destination, target)) {
          owned = true;
          break;
        }
      }
    }
    if (!owned) {
      filtered.push_back(mapping);
    }
  }
  return filtered;
}

QStringList legacyIniRecoveryLeaves(const QString &configurationRoot,
                                    const QStringList &iniNames) {
  QDir root(configurationRoot);
  QStringList recovery;
  for (const QString &entry :
       root.entryList(QDir::AllEntries | QDir::Hidden | QDir::System |
                      QDir::NoDotAndDotDot)) {
    for (const QString &iniName : iniNames) {
      const QString leaf = QFileInfo(iniName).fileName();
      const QString backup = leaf + QStringLiteral(".mo2linux_backup");
      const QString retirementPrefix =
          leaf + QStringLiteral(".fluorine-session-");
      if (entry.compare(backup, Qt::CaseInsensitive) == 0 ||
          entry.startsWith(retirementPrefix, Qt::CaseInsensitive)) {
        recovery.append(root.filePath(entry));
        break;
      }
    }
  }
  return recovery;
}

Plan restoreLegacyBackup(const QString &topologyRoot, const QString &livePath,
                         const QString &legacyBackupPath,
                         const QString &managedProfileRoot,
                         bool allowManagedLinks) {
  const QString physicalRoot = canonicalWithMissingTail(topologyRoot);
  const QString liveParent =
      canonicalWithMissingTail(QFileInfo(livePath).absolutePath());
  const QString backupParent =
      canonicalWithMissingTail(QFileInfo(legacyBackupPath).absolutePath());
  if (liveParent != physicalRoot || backupParent != physicalRoot) {
    return failure(
        QStringLiteral("Legacy save paths are not direct children of '%1'.")
            .arg(topologyRoot));
  }

  const QStringList backupFamily = caseFamily(legacyBackupPath);
  if (backupFamily.isEmpty()) {
    const QStringList liveFamily = caseFamily(livePath);
    const QFileInfo liveInfo(livePath);
    const QString exactName = liveInfo.fileName();
    const QString lowerName = exactName.toLower();
    for (const QString &variant : liveFamily) {
      const QString name = QFileInfo(variant).fileName();
      if (name != exactName && name != lowerName) {
        return failure(
            QStringLiteral("Unowned mixed-case save variant '%1' is ambiguous.")
                .arg(variant));
      }
      std::error_code statusError;
      const fs::file_status status = safeSymlinkStatus(
          fs::path(QFile::encodeName(variant).constData()), statusError);
      const bool authenticatedLink =
          !statusError && status.type() == fs::file_type::symlink &&
          allowManagedLinks &&
          !QFileInfo(managedProfileRoot).canonicalFilePath().isEmpty() &&
          QFileInfo(variant).canonicalFilePath() ==
              QFileInfo(managedProfileRoot).canonicalFilePath();
      if (statusError ||
          (status.type() != fs::file_type::directory && !authenticatedLink)) {
        return failure(
            QStringLiteral("Unowned save variant '%1' is not a real directory.")
                .arg(variant));
      }
    }
    return {.success = true};
  }
  if (backupFamily.size() != 1 ||
      absoluteClean(backupFamily.front()) != absoluteClean(legacyBackupPath)) {
    return failure(QStringLiteral("Legacy backup case family is ambiguous: %1.")
                       .arg(backupFamily.join(QStringLiteral(", "))));
  }

  const QStringList liveFamily = caseFamily(livePath);
  if (!liveFamily.isEmpty()) {
    return failure(
        QStringLiteral("Legacy backup coexists with live save variant(s): %1.")
            .arg(liveFamily.join(QStringLiteral(", "))));
  }

  std::error_code error;
  const fs::path live(QFile::encodeName(livePath).constData());
  const fs::path backup(QFile::encodeName(legacyBackupPath).constData());
  const fs::file_status backupStatus = safeSymlinkStatus(backup, error);
  if (error) {
    return failure(
        QStringLiteral("Could not inspect legacy save backup '%1': %2")
            .arg(legacyBackupPath, QString::fromStdString(error.message())));
  }
  if (backupStatus.type() != fs::file_type::directory) {
    return failure(
        QStringLiteral("Legacy save backup '%1' is not a real directory.")
            .arg(legacyBackupPath));
  }

  const fs::file_status liveStatus = safeSymlinkStatus(live, error);
  if (error) {
    return failure(QStringLiteral("Could not inspect live save path '%1': %2")
                       .arg(livePath, QString::fromStdString(error.message())));
  }
  if (liveStatus.type() != fs::file_type::not_found) {
    return failure(QStringLiteral("Legacy and live save paths coexist; "
                                  "preserving both '%1' and '%2'.")
                       .arg(livePath, legacyBackupPath));
  }

  fs::rename(backup, live, error);
  if (error) {
    return failure(
        QStringLiteral("Could not restore legacy save backup '%1': %2")
            .arg(legacyBackupPath, QString::fromStdString(error.message())));
  }
  return {.success = true};
}

} // namespace WineSaveTargetResolver
