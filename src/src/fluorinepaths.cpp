#include "fluorinepaths.h"

#include <QDir>

QString fluorineDataDir()
{
  return QDir::homePath() + "/.local/share/fluorine";
}

QString fluorineVfsCacheDir()
{
  return fluorineDataDir() + "/vfs_cache";
}
