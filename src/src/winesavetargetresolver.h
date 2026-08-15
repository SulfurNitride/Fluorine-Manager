/*
Copyright (C) 2026 Fluorine Manager contributors.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

#ifndef WINESAVETARGETRESOLVER_H
#define WINESAVETARGETRESOLVER_H

#include <QString>
#include <QStringList>

#include <uibase/filemapping.h>

namespace WineSaveTargetResolver {

enum class Kind {
  PrefixRouted,
  FixedGameDirectory,
  VfsOwned,
};

struct Plan {
  bool success{false};
  Kind kind{Kind::PrefixRouted};
  QString livePath;
  QString topologyRoot;
  QString error;

  explicit operator bool() const noexcept { return success; }
};

// Resolves the exact profile-save mapping before falling back to the ordinary
// Bethesda Wine-user layout. A matching mapping with an unsupported target is
// an error unless it is the plugin-authorized exact save target, in which case
// the launch VFS remains its sole owner. Silently inventing a different
// destination would create two save owners.
Plan resolve(const QString &prefixDriveRoot, const QString &prefixUserRoot,
             const QString &prefixMyGamesRoot, const QString &fallbackGameName,
             const QString &gameRoot, const QString &gameSavesPath,
             const QString &profileSavesPath, bool allowFixedGameDirectory,
             const MappingType &mappings);

// True only for the exact profile-save mapping whose destination is the
// authenticated save directory inside the game root. Preview/VFS mapping
// construction must omit this mapping on Linux because launch preparation is
// the sole physical owner of fixed-directory saves.
bool isFixedGameDirectorySaveMapping(const Mapping &mapping,
                                     const QString &gameRoot,
                                     const QString &gameSavesPath,
                                     const QString &profileSavesPath);

// Removes mappings whose physical destination is owned by a fixed-directory
// launch transaction. Source paths are deliberately ignored because plugin
// file mappers can consult a different UI profile during an override launch.
MappingType filterFixedMappings(const MappingType &mappings,
                                const QString &liveSavePath,
                                const QString &configurationRoot,
                                const QStringList &iniNames);

// Returns unresolved fixed-directory INI backup/retirement leaves. They are
// launch-owned generations from an interrupted older session and must block
// every new launch mode until explicitly recovered.
QStringList legacyIniRecoveryLeaves(const QString &configurationRoot,
                                    const QStringList &iniNames);

// Morrowind's historical Linux profile callback renamed Saves to _Saves
// outside a launch transaction. Under the authenticated game-root lock, the
// only unambiguous legacy state is an absent live leaf plus a real _Saves
// directory. Restoring that rename is atomic and leaves the new deployment
// journal to own every subsequent transition.
Plan restoreLegacyBackup(const QString &topologyRoot, const QString &livePath,
                         const QString &legacyBackupPath,
                         const QString &managedProfileRoot = {},
                         bool allowManagedLinks = false);

} // namespace WineSaveTargetResolver

#endif // WINESAVETARGETRESOLVER_H
