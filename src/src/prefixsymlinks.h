#ifndef PREFIXSYMLINKS_H
#define PREFIXSYMLINKS_H

#include <QString>

/// Ensure AppData/Local/Temp exists in the Wine prefix.
__attribute__((visibility("default"))) void
ensureTempDirectory(const QString &prefixPath);

/// Checked variant used by prefix setup. The legacy void wrapper remains
/// ABI-compatible.
__attribute__((visibility("default"))) bool
ensureTempDirectoryChecked(const QString &prefixPath, QString *error = nullptr);

/// Detect all games and create symlinks from the given prefix to game prefixes.
__attribute__((visibility("default"))) void
createGameSymlinksAuto(const QString &prefixPath);

/// Checked variant used by prefix setup. Existing conflicting links are
/// preserved.
__attribute__((visibility("default"))) bool
createGameSymlinksAutoChecked(const QString &prefixPath,
                              QString *error = nullptr);

#endif // PREFIXSYMLINKS_H
