/*
Copyright (C) 2026 Fluorine Manager contributors.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

#ifndef WINESAVEDEPLOYMENT_H
#define WINESAVEDEPLOYMENT_H

#include <QString>
#include <QStringList>

namespace WineSaveDeployment {

struct Result {
  bool success{false};
  QString error;
  // True once the Wine paths again expose the vanilla/global generation.
  // Cleanup of an invocation-owned marker or retirement may still require a
  // retry without keeping the game routed to the profile path.
  bool topologyComplete{false};
  bool cleanupRequired{false};

  explicit operator bool() const noexcept { return success; }
};

struct PendingDeployment {
  bool present{false};
  QString ownerId;
  QString profileRoot;
};

// Read-only discovery used before every Wine launch. A persisted deployment
// is never silently adopted by a different launch generation.
Result pendingDeployment(const QString &prefixRoot,
                         const QString &exactLivePath,
                         PendingDeployment &pending);

// Publishes a complete source tree through a sibling staging directory and an
// atomic directory exchange. Source and the previous destination generation
// remain authoritative on every pre-publication failure.
Result publishTree(const QString &sourceRoot, const QString &destinationRoot,
                   bool mirrorDeletions);

// Replaces the exact and lowercase Wine save paths with links to profileRoot.
// Existing real directories are global state and are renamed to the historical
// .mo2linux_backup_<leaf> slots. ownerId is persisted before the links become
// visible; an unmarked live+backup state is never claimed automatically.
Result deployLinks(const QString &prefixRoot, const QString &profileRoot,
                   const QString &exactLivePath, const QString &ownerId);

// Prepares an ordinary real directory for a per-launch bind mount. It never
// removes a foreign link or chooses between a live real tree and a backup.
Result prepareBindTarget(const QString &prefixRoot, const QString &profileRoot,
                         const QString &exactLivePath, const QString &ownerId);

// Completes a link/legacy deployment. Exact links to profileRoot are removed,
// interrupted real session trees are published first, and global backups are
// restored only after publication succeeds.
Result synchronizeAndRestore(const QString &prefixRoot,
                             const QString &profileRoot,
                             const QString &exactLivePath,
                             const QString &ownerId);

// Rolls back a prepared link deployment before a child process exists. A
// durable rollback intent never publishes a replaced real tree. If bind-target
// normalization had already durably switched the generation to publication,
// this resumes that exact cleanup transaction instead of reinterpreting it.
Result rollbackLinks(const QString &prefixRoot, const QString &profileRoot,
                     const QString &exactLivePath, const QString &ownerId);

// Startup has no profile identity. It may restore a backup only when the live
// leaf is absent; every coexistence/foreign-type state is preserved for a
// later profile-aware deployment or manual recovery.
QString backupPathFor(const QString &livePath);
bool samePhysicalDirectory(const QString &left, const QString &right);
QStringList managedLivePaths(const QString &exactLivePath);
QString leasePathFor(const QString &prefixRoot, const QString &exactLivePath);
QString sessionLeasePathFor(const QString &prefixRoot,
                            const QString &exactLivePath);
bool hasPersistedSessionLease(const QString &prefixRoot);
Result beginSessionLease(const QString &prefixRoot,
                         const QString &exactLivePath, const QString &ownerId);
Result endSessionLease(const QString &prefixRoot, const QString &exactLivePath,
                       const QString &ownerId);

} // namespace WineSaveDeployment

#endif // WINESAVEDEPLOYMENT_H
