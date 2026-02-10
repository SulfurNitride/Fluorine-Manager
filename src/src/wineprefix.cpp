#include "wineprefix.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <log.h>

namespace
{
constexpr const char* BackupSavesUpper = ".mo2linux_backup_Saves";
constexpr const char* BackupSavesLower = ".mo2linux_backup_saves";
constexpr const char* BackupIniSuffix  = ".mo2linux_backup";

bool copyFileWithParents(const QString& source, const QString& destination)
{
  const QFileInfo destinationInfo(destination);
  if (!QDir().mkpath(destinationInfo.dir().absolutePath())) {
    return false;
  }

  if (QFile::exists(destination) && !QFile::remove(destination)) {
    return false;
  }

  return QFile::copy(source, destination);
}

bool copyTreeContents(const QString& sourceRoot, const QString& destinationRoot)
{
  QDirIterator it(sourceRoot, QDir::Files, QDirIterator::Subdirectories);

  while (it.hasNext()) {
    const QString source = it.next();
    const QString relativePath = QDir(sourceRoot).relativeFilePath(source);
    const QString destination = QDir(destinationRoot).filePath(relativePath);

    if (!copyFileWithParents(source, destination)) {
      return false;
    }
  }

  return true;
}

bool restoreBackedUpSaves(const QString& liveUpper, const QString& liveLower,
                          const QString& backupUpper, const QString& backupLower)
{
  if (QDir(liveUpper).exists() && !QDir(liveUpper).removeRecursively()) {
    return false;
  }
  if (QDir(liveLower).exists() && !QDir(liveLower).removeRecursively()) {
    return false;
  }

  if (QDir(backupUpper).exists() && !QDir().rename(backupUpper, liveUpper)) {
    return false;
  }
  if (QDir(backupLower).exists() && !QDir().rename(backupLower, liveLower)) {
    return false;
  }

  return true;
}

bool restoreBackedUpIni(const QString& liveIni, const QString& backupIni)
{
  if (!QFile::exists(backupIni)) {
    return true;
  }

  if (QFile::exists(liveIni) && !QFile::remove(liveIni)) {
    return false;
  }

  return QFile::rename(backupIni, liveIni);
}
}  // namespace

WinePrefix::WinePrefix(const QString& prefixPath)
    : m_prefixPath(QDir::cleanPath(prefixPath))
{}

bool WinePrefix::isValid() const
{
  return QDir(driveC()).exists();
}

QString WinePrefix::driveC() const
{
  return QDir(m_prefixPath).filePath("drive_c");
}

QString WinePrefix::documentsPath() const
{
  return QDir(driveC()).filePath("users/steamuser/Documents");
}

QString WinePrefix::myGamesPath() const
{
  return QDir(documentsPath()).filePath("My Games");
}

QString WinePrefix::appdataLocal() const
{
  return QDir(driveC()).filePath("users/steamuser/AppData/Local");
}

bool WinePrefix::deployPlugins(const QStringList& plugins, const QString& dataDir) const
{
  if (!isValid()) {
    return false;
  }

  const QString pluginsDir = QDir(appdataLocal()).filePath(dataDir);
  if (!QDir().mkpath(pluginsDir)) {
    return false;
  }

  QFile pluginsFile(QDir(pluginsDir).filePath("Plugins.txt"));
  if (!pluginsFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
    return false;
  }

  QTextStream pluginsStream(&pluginsFile);
  for (const QString& plugin : plugins) {
    pluginsStream << plugin << "\r\n";
  }
  pluginsFile.close();

  QFile loadOrderFile(QDir(pluginsDir).filePath("loadorder.txt"));
  if (!loadOrderFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
    return false;
  }

  QTextStream loadOrderStream(&loadOrderFile);
  for (const QString& plugin : plugins) {
    QString line = plugin;
    if (line.startsWith('*')) {
      line.remove(0, 1);
    }

    loadOrderStream << line << "\r\n";
  }

  return true;
}

bool WinePrefix::deployProfileIni(const QString& sourceIniPath,
                                  const QString& targetIniPath) const
{
  const QFileInfo iniInfo(sourceIniPath);
  if (!iniInfo.exists() || !iniInfo.isFile()) {
    return false;
  }

  const QString destination = QDir::cleanPath(targetIniPath);
  const QString backup      = destination + BackupIniSuffix;

  if (!restoreBackedUpIni(destination, backup)) {
    return false;
  }

  if (QFile::exists(destination) && !QFile::rename(destination, backup)) {
    return false;
  }

  return copyFileWithParents(iniInfo.absoluteFilePath(), destination);
}

bool WinePrefix::deployProfileSaves(const QString& profileSaveDir, const QString& gameName,
                                    const QString& saveRelativePath,
                                    bool clearDestination) const
{
  if (!isValid()) {
    return false;
  }

  const QString gameRoot = QDir(myGamesPath()).filePath(gameName);
  const QString normalizedSavePath =
      QString(saveRelativePath).replace('\\', '/').trimmed();
  const QString effectiveSavePath = normalizedSavePath.isEmpty() ? "Saves" : normalizedSavePath;
  const QString destinationSavesDirUpper = QDir(gameRoot).filePath(effectiveSavePath);
  const QString destinationSavesDirLower =
      QDir(gameRoot).filePath(effectiveSavePath.toLower());
  const QString backupUpper = QDir(gameRoot).filePath(BackupSavesUpper);
  const QString backupLower = QDir(gameRoot).filePath(BackupSavesLower);

  if (clearDestination) {
    // Recover from any stale backup left by an interrupted run.
    if ((QDir(backupUpper).exists() || QDir(backupLower).exists()) &&
        !restoreBackedUpSaves(destinationSavesDirUpper, destinationSavesDirLower,
                              backupUpper, backupLower)) {
      return false;
    }

    if (QDir(destinationSavesDirUpper).exists() &&
        !QDir().rename(destinationSavesDirUpper, backupUpper)) {
      return false;
    }
    if (QDir(destinationSavesDirLower).exists() &&
        !QDir().rename(destinationSavesDirLower, backupLower)) {
      return false;
    }
  }

  if (!QDir().mkpath(destinationSavesDirUpper)) {
    return false;
  }

  if (!QDir(profileSaveDir).exists()) {
    return true;
  }

  return copyTreeContents(profileSaveDir, destinationSavesDirUpper);
}

bool WinePrefix::syncSavesBack(const QString& profileSaveDir, const QString& gameName,
                               const QString& saveRelativePath) const
{
  if (!isValid()) {
    return false;
  }

  const QString gameRoot = QDir(myGamesPath()).filePath(gameName);
  const QString normalizedSavePath =
      QString(saveRelativePath).replace('\\', '/').trimmed();
  const QString effectiveSavePath = normalizedSavePath.isEmpty() ? "Saves" : normalizedSavePath;
  const QString upperSaves = QDir(gameRoot).filePath(effectiveSavePath);
  const QString lowerSaves = QDir(gameRoot).filePath(effectiveSavePath.toLower());

  QString sourceSavesDir;
  if (QDir(upperSaves).exists()) {
    sourceSavesDir = upperSaves;
  } else if (QDir(lowerSaves).exists()) {
    sourceSavesDir = lowerSaves;
  } else {
    return true;
  }

  if (!QDir().mkpath(profileSaveDir)) {
    return false;
  }

  const bool copied = copyTreeContents(sourceSavesDir, profileSaveDir);
  if (!copied) {
    MOBase::log::warn("Failed syncing saves from '{}' to '{}'", sourceSavesDir,
                      profileSaveDir);
  }

  const QString backupUpper = QDir(gameRoot).filePath(BackupSavesUpper);
  const QString backupLower = QDir(gameRoot).filePath(BackupSavesLower);
  if (!restoreBackedUpSaves(upperSaves, lowerSaves, backupUpper, backupLower)) {
    MOBase::log::warn("Failed restoring backed up global saves in '{}'", gameRoot);
    return false;
  }

  return copied;
}

bool WinePrefix::syncProfileInisBack(
    const QList<QPair<QString, QString>>& iniMappings) const
{
  bool allCopied = true;
  for (const auto& mapping : iniMappings) {
    const QString profileIniPath = QDir::cleanPath(mapping.first);
    const QString prefixIniPath  = QDir::cleanPath(mapping.second);
    const QString backupIniPath  = prefixIniPath + BackupIniSuffix;

    if (!QFileInfo::exists(prefixIniPath)) {
      if (!restoreBackedUpIni(prefixIniPath, backupIniPath)) {
        allCopied = false;
      }
      continue;
    }

    if (!copyFileWithParents(prefixIniPath, profileIniPath)) {
      allCopied = false;
    }

    if (!restoreBackedUpIni(prefixIniPath, backupIniPath)) {
      allCopied = false;
    }
  }

  return allCopied;
}
