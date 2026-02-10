#include "gamebryodataarchives.h"

#include <Windows.h>

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
  wchar_t* buffer = new wchar_t[size];
  QStringList result;
  std::wstring iniFileW = QDir::toNativeSeparators(iniFile).toStdWString();

  // epic ms fail: GetPrivateProfileString uses errno (for whatever reason) to signal a
  // fail since the return value has a different meaning (number of bytes copied).
  // HOWEVER, it will not set errno to 0 if NO error occured
  errno = 0;

  if (::GetPrivateProfileStringW(L"Archive", key.toStdWString().c_str(), L"", buffer,
                                 size, iniFileW.c_str()) != 0) {
    result.append(QString::fromStdWString(buffer).split(','));
  }

  for (int i = 0; i < result.count(); ++i) {
    result[i] = result[i].trimmed();
  }
  delete[] buffer;
  return result;
}

void GamebryoDataArchives::setArchivesToKey(const QString& iniFile, const QString& key,
                                            const QString& value)
{
  if (!MOBase::WriteRegistryValue(L"Archive", key.toStdWString().c_str(),
                                  value.toStdWString().c_str(),
                                  iniFile.toStdWString().c_str())) {
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
