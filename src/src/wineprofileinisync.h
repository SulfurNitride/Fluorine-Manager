/*
Copyright (C) 2026 Fluorine Manager contributors.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

#ifndef WINEPROFILEINISYNC_H
#define WINEPROFILEINISYNC_H

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace WineProfileIniSync {

struct Result {
  bool success{false};
  QString error;

  explicit operator bool() const noexcept { return success; }
};

// Exact filesystem ownership accumulated while one profile INI is deployed.
// Callers retain this value through mandatory launch cleanup.  In particular,
// cleanup never rediscovers an arbitrary case-insensitive leaf and assumes it
// belongs to the launch.
struct Deployment {
  QString profilePath;
  QString prefixPath;
  QList<QPair<QString, QString>> globalBackups;
  QStringList deployedLeaves;
  QStringList sessionLeaves;
  bool complete{false};

  bool needsCleanup() const noexcept {
    return !globalBackups.isEmpty() || !deployedLeaves.isEmpty();
  }
};

enum class CleanupPhase {
  Prepared,
  Published,
  SessionRetired,
  GlobalsRestored,
};

// Replaces one case-insensitive prefix INI family while recording every leaf
// actually moved or created.  A failure either rolls back completely or leaves
// enough ownership in deployment for finish(..., false, ...) to do so.
Result deploy(const QString &profilePath, const QString &prefixPath,
              const QString &ownerId, Deployment &deployment);

// Publishes a historical global INI backup over its live leaf atomically, then
// retires the unchanged backup.  Failure always leaves the backup available.
Result restoreLegacyBackup(const QString &livePath, const QString &backupPath);

// Completes the exact profile->prefix INI mappings owned by one launch.
// When publishChanges is true, every readable game-edited INI is atomically
// published to its profile destination before any deployed prefix leaf is
// retired. Global backups are then restored. A deterministic owner-specific
// retirement leaf makes every cleanup boundary retryable.
Result finish(QList<Deployment> &deployments, const QString &ownerId,
              bool publishChanges, CleanupPhase &phase);

} // namespace WineProfileIniSync

#endif // WINEPROFILEINISYNC_H
