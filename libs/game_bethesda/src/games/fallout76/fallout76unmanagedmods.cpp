#include "fallout76unmanagedmods.h"

Fallout76UnmangedMods::Fallout76UnmangedMods(const GameGamebryo* game)
    : GamebryoUnmangedMods(game)
{}

Fallout76UnmangedMods::~Fallout76UnmangedMods() {}

QStringList Fallout76UnmangedMods::mods(bool onlyOfficial) const
{
  QStringList result;

  QStringList pluginList   = game()->primaryPlugins();
  QStringList otherPlugins = game()->DLCPlugins();
  otherPlugins.append(game()->CCPlugins());
  for (QString plugin : otherPlugins) {
    pluginList.removeAll(plugin);
  }
  QDir dataDir(game()->dataDirectory());
  for (const QString& fileName : dataDir.entryList({"*.esp", "*.esl", "*.esm"})) {
    if (!pluginList.contains(fileName, Qt::CaseInsensitive)) {
      if (!onlyOfficial || pluginList.contains(fileName, Qt::CaseInsensitive)) {
        QFileInfo file(fileName);
        result.append(file.baseName());
      }
    }
  }

  return result;
}

QStringList Fallout76UnmangedMods::secondaryFiles(const QString& modName) const
{
  // file extension in FO76 is .ba2 instead of bsa
  QStringList archives;
  QDir dataDir = game()->dataDirectory();
  for (const QString& archiveName : dataDir.entryList({modName + "*.ba2"})) {
    archives.append(dataDir.absoluteFilePath(archiveName));
  }
  return archives;
}

QString Fallout76UnmangedMods::displayName(const QString& modName) const
{
  return modName;
}
