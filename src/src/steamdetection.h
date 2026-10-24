#ifndef STEAMDETECTION_H
#define STEAMDETECTION_H

#include <QString>
#include <QStringList>
#include <QVector>

/// Oldest Proton major version Fluorine supports.
constexpr int kMinimumSupportedProtonMajor = 11;

/// Information about an installed Proton version.
struct SteamProtonInfo {
  QString name;
  QString config_name;
  QString path;
  bool is_steam_proton  = false;
  bool is_experimental  = false;

  /// Get the path to the wine binary (files/bin/wine or dist/bin/wine).
  QString wineBinary() const;

  /// Get the path to the wineserver binary.
  QString wineserverBinary() const;

  /// Get the bin directory containing wine executables.
  QString binDir() const;
};

/// Find the Steam installation path (returns empty string if not found).
__attribute__((visibility("default"))) QString findSteamPath();

/// Find all Steam library root paths, including secondary libraries.
__attribute__((visibility("default"))) QStringList findSteamLibraryPaths();

/// Find supported Proton versions in explicit Steam libraries and additional
/// compatibility-tool directories. Exposed separately for deterministic tests.
/// Versions older than kMinimumSupportedProtonMajor are dropped unless
/// `allowUnsupported` is set.
__attribute__((visibility("default"))) QVector<SteamProtonInfo>
findProtonsForPaths(const QStringList& steamLibraries,
                    const QStringList& compatibilityToolDirs,
                    bool allowUnsupported = false);

/// True if the Proton is at least kMinimumSupportedProtonMajor (or unversioned).
__attribute__((visibility("default"))) bool
isSupportedProton(const SteamProtonInfo& proton);

/// Find all installed Proton versions, including Steam, system, and Heroic
/// installations (sorted newest first).
__attribute__((visibility("default"))) QVector<SteamProtonInfo>
findSteamProtons(bool allowUnsupported = false);

#endif // STEAMDETECTION_H
