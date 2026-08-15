/*
Copyright (C) 2026 Fluorine Manager contributors.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

#ifndef WINEPLUGINPROJECTIONSYNC_H
#define WINEPLUGINPROJECTIONSYNC_H

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace WinePluginProjectionSync
{

struct Result
{
  bool success{false};
  QString error;
  QStringList recoveryFiles;

  explicit operator bool() const noexcept { return success; }
};

struct Target
{
  QString path;
  QList<QPair<QString, QString>> globalBackups;
  QStringList deployedLeaves;
  QStringList sessionLeaves;
  QList<QPair<QString, QString>> retiredLeaves;
  bool sessionFamilyCaptured{false};
};

enum class CleanupPhase
{
  Prepared,
  Reconciled,
  SessionsRetired,
  GlobalsRestored,
  Complete,
};

// Value-only launch receipt. Prefix originals are renamed to adjacent backup
// leaves before projection, so a manager crash retains both the original
// generation and the launch session under the prefix's persistent lease.
struct Deployment
{
  QString profilePath;
  QString ownerId;
  QByteArray baselineContents;
  QList<Target> targets;
  CleanupPhase phase{CleanupPhase::Prepared};
  bool projectionComplete{false};

  bool needsCleanup() const noexcept
  {
    return phase != CleanupPhase::Complete && !targets.isEmpty();
  }
};

// Resolves the physical Wine-prefix owner of a lexical plugin target. This
// intentionally follows a final AppData game-directory bridge, then requires
// an ordinary ancestor that directly owns drive_c.
[[nodiscard]] QString physicalPrefixRoot(const QString& targetPath);

// Preflights every destination before the first mutation, moves every original
// case-family leaf to an exact backup, then atomically projects the profile's
// raw bytes to every target. Present empty files are authoritative.
Result prepare(const QString& profilePath, const QStringList& targetPaths,
               const QString& ownerId, Deployment& deployment);

// Reconciles one unambiguous launch edit with the profile, or preserves it in
// an owner-specific recovery file when publication is disabled, suspicious,
// or conflicts with a concurrent profile edit. Prefix session leaves are then
// retired, exact originals restored, and the retirement copies removed.
Result finish(Deployment& deployment, bool publishChanges);

}  // namespace WinePluginProjectionSync

#endif  // WINEPLUGINPROJECTIONSYNC_H
