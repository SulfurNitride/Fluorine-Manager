#ifndef FLUORINEPATHS_H
#define FLUORINEPATHS_H

#include <QString>

/// Returns the Fluorine data directory: ~/.local/share/fluorine
QString fluorineDataDir();

/// Returns the VFS scan-cache directory: ~/.local/share/fluorine/vfs_cache
/// Created on demand by the cache writer.
QString fluorineVfsCacheDir();

#endif  // FLUORINEPATHS_H
