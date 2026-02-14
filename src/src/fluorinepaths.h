#ifndef FLUORINEPATHS_H
#define FLUORINEPATHS_H

#include <QString>

/// Returns the shared Fluorine data directory: ~/.local/share/fluorine
/// Uses $HOME directly to bypass Flatpak's XDG_DATA_HOME remapping
/// (the Flatpak has --filesystem=home).
QString fluorineDataDir();

/// One-time migration from the old ~/.var/app/com.fluorine.manager/ path
/// to ~/.local/share/fluorine/. Call before initLogging().
void fluorineMigrateDataDir();

#endif  // FLUORINEPATHS_H
