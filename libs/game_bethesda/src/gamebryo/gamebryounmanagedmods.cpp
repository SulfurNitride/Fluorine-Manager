#include "gamebryounmanagedmods.h"
#include "gamegamebryo.h"
#include <pluginsetting.h>

GamebryoUnmangedMods::GamebryoUnmangedMods(const GameGamebryo* game) : m_Game(game) {}

GamebryoUnmangedMods::~GamebryoUnmangedMods() {}

QStringList GamebryoUnmangedMods::mods(bool onlyOfficial) const
{
  QStringList result;

  QStringList dlcPlugins  = m_Game->DLCPlugins();
  QStringList mainPlugins = m_Game->primaryPlugins();

  QDir dataDir(m_Game->dataDirectory());
  for (const QString& fileName : dataDir.entryList({"*.esp", "*.esl", "*.esm"})) {
    if (!mainPlugins.contains(fileName, Qt::CaseInsensitive) &&
        (!onlyOfficial || dlcPlugins.contains(fileName, Qt::CaseInsensitive))) {
      result.append(fileName.chopped(4));  // trims the extension off
    }
  }

  return result;
}

QString GamebryoUnmangedMods::displayName(const QString& modName) const
{
  return modName;
}

QFileInfo GamebryoUnmangedMods::referenceFile(const QString& modName) const
{
  QFileInfoList files =
      m_Game->dataDirectory().entryInfoList(QStringList() << modName + ".es*");
  if (files.size() > 0) {
    return files.at(0);
  } else {
    return QFileInfo();
  }
}

QStringList GamebryoUnmangedMods::secondaryFiles(const QString& modName) const
{
  QStringList archives;
  QDir dataDir = m_Game->dataDirectory();
  for (const QString& archiveName : dataDir.entryList({modName + "*.bsa"})) {
    archives.append(dataDir.absoluteFilePath(archiveName));
  }
  return archives;
}
