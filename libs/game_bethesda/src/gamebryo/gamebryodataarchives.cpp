#include "gamebryodataarchives.h"

#include <registry.h>
#include <utility.h>

#include "gamegamebryo.h"

GamebryoDataArchives::GamebryoDataArchives(const GameGamebryo* game) : m_Game{game} {}

QDir GamebryoDataArchives::gameDirectory() const
{
  return QDir(m_Game->gameDirectory()).absolutePath();
}

QDir GamebryoDataArchives::localGameDirectory() const
{
  return QDir(m_Game->myGamesPath()).absolutePath();
}

QStringList GamebryoDataArchives::getArchivesFromKey(const QString& iniFile,
                                                     const QString& key,
                                                     const int size) const
{
  QStringList result;

  QString value = GameGamebryo::readIniValue(iniFile, "Archive", key, "");
  if (!value.isEmpty()) {
    result.append(value.split(','));
  }

  for (int i = 0; i < result.count(); ++i) {
    result[i] = result[i].trimmed();
  }
  return result;
}

void GamebryoDataArchives::setArchivesToKey(const QString& iniFile, const QString& key,
                                            const QString& value)
{
  if (!MOBase::WriteRegistryValue("Archive", key, value, iniFile)) {
    qWarning("failed to set archives in \"%s\"", qUtf8Printable(iniFile));
  }
}

void GamebryoDataArchives::addArchive(MOBase::IProfile* profile, int index,
                                      const QString& archiveName)
{
  QStringList current = archives(profile);
  if (current.contains(archiveName, Qt::CaseInsensitive)) {
    return;
  }

  current.insert(index != INT_MAX ? index : current.size(), archiveName);

  writeArchiveList(profile, current);
}

void GamebryoDataArchives::removeArchive(MOBase::IProfile* profile,
                                         const QString& archiveName)
{
  QStringList current = archives(profile);
  if (!current.contains(archiveName, Qt::CaseInsensitive)) {
    return;
  }
  current.removeAll(archiveName);

  writeArchiveList(profile, current);
}
