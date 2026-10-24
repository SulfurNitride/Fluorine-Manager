#ifndef PREFIXSYMLINKS_H
#define PREFIXSYMLINKS_H

#include <cstdint>
#include <QString>

inline constexpr std::uint32_t kSkyrimSpecialEditionSteamAppId = 489830;

/// Ensure AppData/Local/Temp exists in the Wine prefix.
__attribute__((visibility("default"))) void ensureTempDirectory(const QString& prefixPath);

/// Move a legacy shared Skyrim SE AppData directory link out of the way and
/// create a private directory for the prefix. The original link is retained
/// under the prefix's .fluorine/legacy-links directory for rollback.
__attribute__((visibility("default"))) bool
ensureSkyrimSpecialEditionAppDataPrivate(const QString& prefixPath);

/// Detect all games and create symlinks from the given prefix to game prefixes.
/// With `linkSaveData` false no game folders are linked; stale links to shared
/// folders are still cleaned up.
__attribute__((visibility("default"))) void
createGameSymlinksAuto(const QString& prefixPath, bool linkSaveData = true);

#endif // PREFIXSYMLINKS_H
