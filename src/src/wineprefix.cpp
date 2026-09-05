#include "wineprefix.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QSet>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <log.h>
#include <uibase/filesystemutilities.h>

#include <sys/stat.h>
#include <utime.h>

namespace
{
constexpr const char* BackupIniSuffix  = ".mo2linux_backup";

// Copy a file, preserving its modification time so that games see the
// original save timestamps rather than "now".
bool copyFileWithParents(const QString& source, const QString& destination)
{
  const QFileInfo destinationInfo(destination);
  if (!QDir().mkpath(destinationInfo.dir().absolutePath())) {
    return false;
  }

  if (QFile::exists(destination) && !QFile::remove(destination)) {
    return false;
  }

  if (!QFile::copy(source, destination)) {
    return false;
  }

  // QFile::copy() does not preserve timestamps. Restore the source file's
  // mtime so that games display the correct save date.
  struct stat srcStat;
  if (stat(source.toUtf8().constData(), &srcStat) == 0) {
    struct utimbuf times;
    times.actime  = srcStat.st_atime;
    times.modtime = srcStat.st_mtime;
    utime(destination.toUtf8().constData(), &times);
  }

  return true;
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

// Mirror deletions from source into destination: remove any file in the
// destination tree that does not exist at the matching relative path in
// source.  Then prune resulting empty directories.  Used to propagate
// in-game save deletions from the prefix back to the profile.
bool mirrorDeletions(const QString& sourceRoot, const QString& destinationRoot)
{
  if (!QDir(destinationRoot).exists()) {
    return true;
  }

  const QDir srcDir(sourceRoot);
  QDirIterator it(destinationRoot, QDir::Files | QDir::Hidden | QDir::System,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString destFile = it.next();
    const QString relativePath = QDir(destinationRoot).relativeFilePath(destFile);
    const QString sourceFile = srcDir.filePath(relativePath);
    if (!QFile::exists(sourceFile)) {
      if (!QFile::remove(destFile)) {
        MOBase::log::warn("mirrorDeletions: failed to remove '{}'", destFile);
      } else {
        MOBase::log::debug("mirrorDeletions: removed '{}'", destFile);
      }
    }
  }

  // Prune empty directories bottom-up.
  QStringList dirs;
  QDirIterator dirIt(destinationRoot,
                     QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                     QDirIterator::Subdirectories);
  while (dirIt.hasNext()) {
    dirs.append(dirIt.next());
  }
  std::sort(dirs.begin(), dirs.end(),
            [](const QString& a, const QString& b) { return a.length() > b.length(); });
  for (const QString& d : dirs) {
    QDir qd(d);
    if (qd.isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)) {
      qd.rmdir(".");
    }
  }

  return true;
}

bool restoreBackedUpSaves(const QString& liveUpper, const QString& liveLower,
                          const QString& backupUpper, const QString& backupLower)
{
  if (QDir(liveUpper).exists() && !QDir(liveUpper).removeRecursively()) {
    MOBase::log::warn("restoreBackedUpSaves: failed to remove '{}'", liveUpper);
    return false;
  }
  if (QDir(liveLower).exists() && !QDir(liveLower).removeRecursively()) {
    MOBase::log::warn("restoreBackedUpSaves: failed to remove '{}'", liveLower);
    return false;
  }

  if (QDir(backupUpper).exists() && !QDir().rename(backupUpper, liveUpper)) {
    MOBase::log::warn("restoreBackedUpSaves: failed to rename '{}' -> '{}'",
                      backupUpper, liveUpper);
    return false;
  }
  if (QDir(backupLower).exists() && !QDir().rename(backupLower, liveLower)) {
    MOBase::log::warn("restoreBackedUpSaves: failed to rename '{}' -> '{}'",
                      backupLower, liveLower);
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

// Find all files in the same directory that match the filename case-insensitively.
// E.g. for "skyrimprefs.ini" returns {"skyrimprefs.ini", "SkyrimPrefs.ini"} if both exist.
QStringList findCaseVariants(const QString& path)
{
  QFileInfo info(path);
  QDir dir(info.path());
  if (!dir.exists()) {
    return {};
  }

  QStringList result;
  const QString target = info.fileName();
  for (const QString& entry :
       dir.entryList(QDir::Files | QDir::Hidden | QDir::System)) {
    if (entry.compare(target, Qt::CaseInsensitive) == 0) {
      result.append(dir.absoluteFilePath(entry));
    }
  }
  return result;
}
}  // namespace

WinePrefix::WinePrefix(const QString& prefixPath)
    : m_prefixPath(QDir::cleanPath(prefixPath))
{
  MOBase::log::debug("WinePrefix: initialized with path '{}'", m_prefixPath);
}

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

QString WinePrefix::userProfilePath() const
{
  return QDir(driveC()).filePath("users/steamuser");
}

bool WinePrefix::deployPlugins(const QStringList& plugins, const QString& dataDir,
                               PluginListMechanism mechanism) const
{
  if (!isValid()) {
    MOBase::log::error("deployPlugins: prefix '{}' is not valid (drive_c not found)",
                       m_prefixPath);
    return false;
  }

  if (mechanism == PluginListMechanism::None) {
    MOBase::log::debug("deployPlugins: game has no plugin-list mechanism, skipping");
    return true;
  }

  const QString pluginsDir = QDir(appdataLocal()).filePath(dataDir);
  MOBase::log::info("deployPlugins: target dir='{}', count={}, mechanism={}",
                    pluginsDir, plugins.size(),
                    mechanism == PluginListMechanism::PluginsTxt ? "PluginsTxt"
                                                                 : "FileTime");

  if (!QDir().mkpath(pluginsDir)) {
    MOBase::log::error("deployPlugins: failed to create directory '{}'", pluginsDir);
    return false;
  }

  // Clear ALL stale plugin-list files in AppData — any case of "plugins.txt"
  // and any "loadorder.txt".  loadorder.txt is MO2-internal (profile only);
  // leaving a stale one in the prefix confuses sync-back if mechanism changes.
  const QString pluginsCanonical = QDir(pluginsDir).filePath("plugins.txt");
  const QString loadOrderPath    = QDir(pluginsDir).filePath("loadorder.txt");
  for (const QString& variant : findCaseVariants(pluginsCanonical)) {
    MOBase::log::debug("deployPlugins: removing stale plugins variant '{}'", variant);
    QFile::remove(variant);
  }
  for (const QString& variant : findCaseVariants(loadOrderPath)) {
    MOBase::log::debug("deployPlugins: removing stale loadorder variant '{}'", variant);
    QFile::remove(variant);
  }

  // PluginsTxt games (SSE/AE, FO4, Starfield, ...) read "Plugins.txt" with
  // '*' prefix for enabled.  FileTime games (FNV, FO3, Skyrim LE) read
  // lowercase "plugins.txt" listing enabled plugins only (no prefix) and
  // derive order from file mtimes.  Lines are already in the correct game
  // format because they come straight from the profile's plugins.txt, which
  // MO2 writes per-game via writePluginLists().
  const bool useCapitalP = mechanism == PluginListMechanism::PluginsTxt;
  const QString targetPath =
      QDir(pluginsDir).filePath(useCapitalP ? "Plugins.txt" : "plugins.txt");

  QFile pluginsFile(targetPath);
  if (!pluginsFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
    MOBase::log::error("deployPlugins: failed to open '{}' for writing", targetPath);
    return false;
  }

  QTextStream pluginsStream(&pluginsFile);
  for (const QString& plugin : plugins) {
    pluginsStream << plugin << "\r\n";
  }
  pluginsFile.close();
  MOBase::log::info("deployPlugins: wrote {} plugins to '{}'", plugins.size(),
                    targetPath);

  return true;
}

bool WinePrefix::deployProfileIni(const QString& sourceIniPath,
                                  const QString& targetIniPath) const
{
  const QFileInfo iniInfo(sourceIniPath);
  if (!iniInfo.exists() || !iniInfo.isFile()) {
    MOBase::log::warn("deployProfileIni: source '{}' does not exist or is not a file",
                      sourceIniPath);
    return false;
  }

  MOBase::log::debug("deployProfileIni: '{}' -> '{}'", sourceIniPath, targetIniPath);
  const QString destination = QDir::cleanPath(targetIniPath);

  // Back up ALL case-insensitive variants (e.g. both skyrimprefs.ini and
  // SkyrimPrefs.ini). Linux is case-sensitive, so the game may create a
  // different-case file alongside ours. Backing up all variants ensures
  // a clean deploy and correct restore later.
  const QStringList variants = findCaseVariants(destination);
  for (const QString& variant : variants) {
    const QString backup = variant + BackupIniSuffix;
    if (!restoreBackedUpIni(variant, backup)) {
      return false;
    }
    if (QFile::exists(variant) && !QFile::rename(variant, backup)) {
      return false;
    }
  }

  // If the exact-case file wasn't among the variants (didn't exist yet),
  // still restore any stale backup for it.
  if (!variants.contains(destination)) {
    const QString backup = destination + BackupIniSuffix;
    if (!restoreBackedUpIni(destination, backup)) {
      return false;
    }
  }

  if (!copyFileWithParents(iniInfo.absoluteFilePath(), destination)) {
    return false;
  }

  // Create a lowercase alias so the game can find the INI regardless of
  // which casing it uses (e.g. FalloutNV reads "fallout.ini" but we deploy
  // "Fallout.ini").
  const QFileInfo destInfo(destination);
  const QString lowerName = destInfo.fileName().toLower();
  if (lowerName != destInfo.fileName()) {
    const QString lowerPath = QDir(destInfo.path()).filePath(lowerName);
    QFile::remove(lowerPath);  // remove stale copy/symlink if any
    QFile::link(destInfo.fileName(), lowerPath);
  }

  return true;
}

bool WinePrefix::deployProfileSaves(const QString& profileSaveDir,
                                    const QString& absoluteSaveDir,
                                    bool /*clearDestination*/) const
{
  if (!isValid()) {
    MOBase::log::error("deployProfileSaves: prefix '{}' is not valid", m_prefixPath);
    return false;
  }

  MOBase::log::debug("deployProfileSaves: profileSaveDir='{}', absoluteSaveDir='{}'",
                     profileSaveDir, absoluteSaveDir);

  // Ensure the profile saves dir exists — the game will write into it
  // directly via the symlink.
  if (!QDir().mkpath(profileSaveDir)) {
    MOBase::log::error("deployProfileSaves: cannot create profile saves dir '{}'",
                       profileSaveDir);
    return false;
  }

  const QFileInfo saveDirInfo(absoluteSaveDir);
  const QString parentDir = saveDirInfo.dir().absolutePath();
  const QString leafName = saveDirInfo.fileName();
  const QString lowerSaveDir =
      QDir(parentDir).filePath(leafName.toLower());

  if (!QDir().mkpath(parentDir)) {
    return false;
  }

  // Symlink both the proper-case and lowercase save dirs straight to the
  // profile saves dir. Writes land in the profile immediately — no copy-in
  // / copy-out dance, crash-safe, and profile switches only swap the link.
  auto relink = [&profileSaveDir](const QString& linkPath) -> bool {
    QFileInfo fi(linkPath);
    if (fi.isSymLink()) {
      QFile::remove(linkPath);
    } else if (QDir(linkPath).exists()) {
      // Existing real directory from a pre-symlink install. Preserve any
      // contents by copying into the profile, then remove so we can link.
      copyTreeContents(linkPath, profileSaveDir);
      QDir(linkPath).removeRecursively();
    } else if (fi.exists()) {
      QFile::remove(linkPath);
    }
    if (!QFile::link(profileSaveDir, linkPath)) {
      MOBase::log::warn("deployProfileSaves: failed to symlink '{}' -> '{}'",
                        linkPath, profileSaveDir);
      return false;
    }
    return true;
  };

  if (!relink(absoluteSaveDir))
    return false;
  if (absoluteSaveDir != lowerSaveDir && !relink(lowerSaveDir))
    return false;

  return true;
}

void WinePrefix::undeployProfileSaves(const QString& absoluteSaveDir) const
{
  if (!isValid())
    return;

  const QFileInfo saveDirInfo(absoluteSaveDir);
  const QString parentDir = saveDirInfo.dir().absolutePath();
  const QString leafName = saveDirInfo.fileName();
  const QString lowerSaveDir =
      QDir(parentDir).filePath(leafName.toLower());

  auto unlinkIfSymlink = [](const QString& path) {
    if (QFileInfo(path).isSymLink()) {
      QFile::remove(path);
    }
  };
  unlinkIfSymlink(absoluteSaveDir);
  if (absoluteSaveDir != lowerSaveDir)
    unlinkIfSymlink(lowerSaveDir);
}

bool WinePrefix::syncSavesBack(const QString& profileSaveDir,
                               const QString& absoluteSaveDir) const
{
  if (!isValid()) {
    MOBase::log::error("syncSavesBack: prefix '{}' is not valid", m_prefixPath);
    return false;
  }

  MOBase::log::debug("syncSavesBack: profileSaveDir='{}', absoluteSaveDir='{}'",
                     profileSaveDir, absoluteSaveDir);

  // With the symlink-based deploy, writes already land in the profile — no
  // sync needed. Fall through to the legacy copy path only for pre-existing
  // installs where the save dir is still a real directory.
  if (QFileInfo(absoluteSaveDir).isSymLink()) {
    return true;
  }

  const QFileInfo saveDirInfo(absoluteSaveDir);
  const QString parentDir = saveDirInfo.dir().absolutePath();
  const QString leafName = saveDirInfo.fileName();
  const QString lowerSaveDir =
      QDir(parentDir).filePath(leafName.toLower());

  QString sourceSavesDir;
  if (QDir(absoluteSaveDir).exists()) {
    sourceSavesDir = absoluteSaveDir;
  } else if (absoluteSaveDir != lowerSaveDir && QDir(lowerSaveDir).exists()) {
    sourceSavesDir = lowerSaveDir;
  } else {
    return true;
  }

  if (!QDir().mkpath(profileSaveDir)) {
    return false;
  }

  // Mirror deletions first, then copy remaining files.  Without the delete
  // pass, a save the user deleted in-game would remain in the profile
  // because copyTreeContents only copies files present in the source.
  mirrorDeletions(sourceSavesDir, profileSaveDir);

  const bool copied = copyTreeContents(sourceSavesDir, profileSaveDir);
  if (!copied) {
    MOBase::log::warn("Failed syncing saves from '{}' to '{}'", sourceSavesDir,
                      profileSaveDir);
  }

  const QString backupUpper =
      QDir(parentDir).filePath(".mo2linux_backup_" + leafName);
  const QString backupLower =
      QDir(parentDir).filePath(".mo2linux_backup_" + leafName.toLower());
  if (!restoreBackedUpSaves(absoluteSaveDir, lowerSaveDir,
                            backupUpper, backupLower)) {
    MOBase::log::warn("Failed restoring backed up global saves in '{}'", parentDir);
    return false;
  }

  return copied;
}

void WinePrefix::restoreStaleBackups() const
{
  if (!isValid()) {
    return;
  }

  // Scan the entire prefix for stale .mo2linux_backup INI files.
  // These are left behind when MO2 crashes after deploying profile INIs.
  QDirIterator it(driveC(), QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    if (!it.fileName().endsWith(BackupIniSuffix)) {
      continue;
    }

    const QString backupPath = it.filePath();
    const QString livePath =
        backupPath.left(backupPath.length() - QString(BackupIniSuffix).length());

    MOBase::log::info("Restoring stale INI backup '{}' -> '{}'", backupPath, livePath);
    if (!restoreBackedUpIni(livePath, backupPath)) {
      MOBase::log::warn("Failed to restore stale INI backup '{}'", backupPath);
    }
  }

  // Also restore stale save backups — scan entire prefix for .mo2linux_backup_*
  // directories (saves may be in Documents/My Games/.../Saves, Saved Games/..., etc.)
  QSet<QString> processedBackups;
  QDirIterator saveIt(driveC(), QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot,
                      QDirIterator::Subdirectories);
  while (saveIt.hasNext()) {
    saveIt.next();
    const QString dirName = saveIt.fileName();
    if (!dirName.startsWith(".mo2linux_backup_")) {
      continue;
    }

    const QString backupPath = saveIt.filePath();
    const QString parentDir = QFileInfo(backupPath).dir().absolutePath();
    const QString originalLeaf = dirName.mid(QString(".mo2linux_backup_").length());
    if (originalLeaf.isEmpty()) {
      continue;
    }

    // Use lowercase key to deduplicate upper/lower variants in same parent
    const QString dedupeKey = parentDir + "/" + originalLeaf.toLower();
    if (processedBackups.contains(dedupeKey)) {
      continue;
    }
    processedBackups.insert(dedupeKey);

    const QString liveUpper = QDir(parentDir).filePath(originalLeaf);
    const QString liveLower = QDir(parentDir).filePath(originalLeaf.toLower());
    const QString backupUpper =
        QDir(parentDir).filePath(".mo2linux_backup_" + originalLeaf);
    const QString backupLower =
        QDir(parentDir).filePath(".mo2linux_backup_" + originalLeaf.toLower());

    MOBase::log::info("Restoring stale save backups: '{}' in '{}'",
                      originalLeaf, parentDir);
    if (!restoreBackedUpSaves(liveUpper, liveLower, backupUpper, backupLower)) {
      MOBase::log::warn("Failed to restore stale save backups in '{}'", parentDir);
    }
  }
}

bool WinePrefix::syncProfileInisBack(
    const QList<QPair<QString, QString>>& iniMappings) const
{
  MOBase::log::debug("syncProfileInisBack: {} INI mappings to sync back",
                     iniMappings.size());
  bool allCopied = true;
  for (const auto& mapping : iniMappings) {
    const QString profileIniPath = QDir::cleanPath(mapping.first);
    const QString prefixIniPath  = QDir::cleanPath(mapping.second);
    MOBase::log::debug("syncProfileInisBack: profile='{}' <- prefix='{}'",
                       profileIniPath, prefixIniPath);

    // Find ALL case-insensitive variants of the INI file (e.g. both
    // skyrimprefs.ini and SkyrimPrefs.ini may exist on Linux).
    // Pick the most recently modified one — that's the file the game wrote to.
    const QStringList variants = findCaseVariants(prefixIniPath);

    QString newestVariant;
    QDateTime newestTime;
    for (const QString& variant : variants) {
      const QFileInfo fi(variant);
      if (fi.lastModified() > newestTime) {
        newestTime    = fi.lastModified();
        newestVariant = variant;
      }
    }

    if (newestVariant.isEmpty()) {
      // No INI file found at all — try to restore from any backup.
      const QString backupIniPath = prefixIniPath + BackupIniSuffix;
      if (!restoreBackedUpIni(prefixIniPath, backupIniPath)) {
        allCopied = false;
      }
      continue;
    }

    // Sync the game's version back to the profile.
    if (!copyFileWithParents(newestVariant, profileIniPath)) {
      allCopied = false;
    }

    // Remove ALL variants (including stale deployed copies), then
    // restore ALL backed-up originals.
    for (const QString& variant : variants) {
      QFile::remove(variant);
    }

    // Restore all backups (there may be multiple from different case variants).
    const QStringList backupVariants =
        findCaseVariants(prefixIniPath + BackupIniSuffix);
    for (const QString& backup : backupVariants) {
      const QString livePath =
          backup.left(backup.length() - QString(BackupIniSuffix).length());
      if (!restoreBackedUpIni(livePath, backup)) {
        allCopied = false;
      }
    }
  }

  return allCopied;
}

QDateTime WinePrefix::prefixPluginsMTime(const QString& dataDir) const
{
  if (!isValid()) {
    return {};
  }
  const QString pluginsDir = QDir(appdataLocal()).filePath(dataDir);
  if (!QDir(pluginsDir).exists()) {
    return {};
  }
  QDateTime newest;
  for (const QString& v :
       findCaseVariants(QDir(pluginsDir).filePath("plugins.txt"))) {
    const QDateTime t = QFileInfo(v).lastModified();
    if (!newest.isValid() || t > newest) {
      newest = t;
    }
  }
  return newest;
}

bool WinePrefix::syncPluginsBack(const QString& profilePluginsPath,
                                 const QString& dataDir,
                                 PluginListMechanism mechanism) const
{
  if (!isValid()) {
    MOBase::log::error("syncPluginsBack: prefix '{}' is not valid", m_prefixPath);
    return false;
  }

  if (mechanism == PluginListMechanism::None) {
    return true;
  }

  const QString pluginsDir = QDir(appdataLocal()).filePath(dataDir);
  if (!QDir(pluginsDir).exists()) {
    MOBase::log::debug("syncPluginsBack: prefix plugins dir '{}' does not exist",
                       pluginsDir);
    return true;
  }

  // Pick the newest case variant of the game's plugin-list file, sync it
  // to the profile, then mirror that content into every sibling variant in
  // the prefix so they don't drift.  LOOT edits whichever case it opened;
  // without mirroring, the untouched sibling keeps stale content.
  // loadorder.txt is MO2-internal — never touched in the prefix and never
  // read back; MO2 re-derives it from the synced plugins file.
  const QStringList variants =
      findCaseVariants(QDir(pluginsDir).filePath("plugins.txt"));
  if (variants.isEmpty()) {
    MOBase::log::debug("syncPluginsBack: no plugins.txt variant found in '{}'",
                       pluginsDir);
    return true;
  }

  QString newest;
  QDateTime newestTime;
  for (const QString& v : variants) {
    const QFileInfo fi(v);
    if (!newestTime.isValid() || fi.lastModified() > newestTime) {
      newestTime = fi.lastModified();
      newest     = v;
    }
  }

  // Active-plugin count guard.  Bethesda games rewrite Plugins.txt as part
  // of normal shutdown, but on a crash (e.g. a buggy SKSE plugin going down
  // mid-frame) the engine can write the file with the active set partially
  // cleared — every plugin name still listed, but most without their leading
  // '*'.  A naive copy-back propagates that damage into the profile, where
  // refreshESPList re-derives state from disk and savePluginList persists
  // the broken active list.  External edits (LOOT reordering) never drop the
  // active count materially, so a large drop is a strong signal that this
  // is a crash artifact, not a legitimate user-edit.  Refuse the copy in
  // that case and let the profile's existing list stand.
  auto countStarredLines = [](const QString& path) -> int {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
      return -1;
    }
    int count = 0;
    while (!f.atEnd()) {
      const QByteArray line = f.readLine();
      // Skip leading whitespace to be tolerant of formatting quirks.
      int i = 0;
      while (i < line.size() &&
             (line[i] == ' ' || line[i] == '\t')) {
        ++i;
      }
      if (i < line.size() && line[i] == '*') {
        ++count;
      }
    }
    return count;
  };

  const int profileStars = countStarredLines(profilePluginsPath);
  const int candidateStars = countStarredLines(newest);
  if (profileStars > 10 && candidateStars >= 0) {
    const int absDrop = profileStars - candidateStars;
    const double relDrop =
        static_cast<double>(absDrop) / static_cast<double>(profileStars);
    if (absDrop > 10 && relDrop > 0.30) {
      MOBase::log::warn(
          "syncPluginsBack: refusing copy — active plugin count would drop "
          "from {} to {} ({:.0f}% loss). Likely a game-crash artifact, not "
          "a real edit. Profile preserved; prefix file left in place at '{}'.",
          profileStars, candidateStars, relDrop * 100.0, newest);
      return true;
    }
  }

  MOBase::log::info("syncPluginsBack: '{}' <- '{}'", profilePluginsPath, newest);
  if (!copyFileWithParents(newest, profilePluginsPath)) {
    MOBase::log::error("syncPluginsBack: failed to copy plugins.txt back to '{}'",
                       profilePluginsPath);
    return false;
  }

  for (const QString& sibling : variants) {
    if (sibling == newest) {
      continue;
    }
    if (!QFile::remove(sibling)) {
      MOBase::log::warn("syncPluginsBack: failed to remove stale sibling '{}'",
                        sibling);
      continue;
    }
    if (!QFile::copy(newest, sibling)) {
      MOBase::log::warn("syncPluginsBack: failed to mirror '{}' -> '{}'",
                        newest, sibling);
    }
  }

  // Clear any stale loadorder.txt that an older build may have written.
  // The game never reads it; leaving it around only invites confusion.
  for (const QString& stale :
       findCaseVariants(QDir(pluginsDir).filePath("loadorder.txt"))) {
    MOBase::log::debug("syncPluginsBack: removing stale loadorder variant '{}'",
                       stale);
    QFile::remove(stale);
  }

  return true;
}

// ── Wine registry (.reg file) access ─────────────────────────────────────────

// Wine .reg files use doubled backslashes in key paths:
//   [Software\\Bethesda Softworks\\Skyrim Special Edition]
// Values are stored as:
//   "Installed Path"="C:\\path\\to\\game"

static QString escapeRegKey(const QString& key)
{
  // Convert normal backslash path to Wine .reg double-backslash format
  QString escaped = key;
  escaped.replace("\\", "\\\\");
  return escaped;
}

static QString unescapeRegValue(const QString& val)
{
  // Wine .reg files escape path separators as "\\\\" and non-ASCII UTF-16
  // code units as "\\xNNNN". Decode both forms in one pass so a literal path
  // component beginning with x is not mistaken for a Unicode escape after
  // collapsing doubled separators.
  QString unescaped;
  unescaped.reserve(val.size());
  for (qsizetype i = 0; i < val.size(); ++i) {
    if (val.at(i) != '\\' || i + 1 >= val.size()) {
      unescaped += val.at(i);
      continue;
    }
    if (val.at(i + 1) == '\\') {
      unescaped += '\\';
      ++i;
      continue;
    }
    if (val.at(i + 1).toLower() == 'x' && i + 5 < val.size()) {
      bool ok = false;
      const ushort codeUnit = val.mid(i + 2, 4).toUShort(&ok, 16);
      if (ok) {
        unescaped += QChar(codeUnit);
        i += 5;
        continue;
      }
    }
    unescaped += val.at(i);
  }
  return unescaped;
}

static QString escapeRegValue(const QString& val)
{
  QString escaped = val;
  escaped.replace("\\", "\\\\");
  return escaped;
}

QString WinePrefix::readRegistryValue(const QString& regFile,
                                      const QString& subKey,
                                      const QString& valueName) const
{
  const QString filePath = m_prefixPath + "/" + regFile;
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return {};
  }

  // Wine .reg section headers have an optional trailing timestamp:
  //   [Software\\Bethesda Softworks\\Skyrim Special Edition] 1774203819
  const QString sectionPrefix =
      "[" + escapeRegKey(subKey) + "]";
  const QString valuePrefix = "\"" + valueName + "\"=";

  bool inSection = false;
  QTextStream in(&file);
  while (!in.atEnd()) {
    const QString line = in.readLine().trimmed();

    if (line.startsWith('[')) {
      inSection = line.startsWith(sectionPrefix, Qt::CaseInsensitive);
      continue;
    }

    if (inSection && line.startsWith(valuePrefix, Qt::CaseInsensitive)) {
      // Extract value: "Name"="value" or "Name"=str(2):"value"
      int eqPos = line.indexOf('=');
      if (eqPos < 0) continue;
      QString rhs = line.mid(eqPos + 1);

      // Handle str(2):"..." (REG_EXPAND_SZ) and regular "..." (REG_SZ)
      int firstQuote = rhs.indexOf('"');
      int lastQuote  = rhs.lastIndexOf('"');
      if (firstQuote >= 0 && lastQuote > firstQuote) {
        return unescapeRegValue(rhs.mid(firstQuote + 1, lastQuote - firstQuote - 1));
      }
      return {};
    }
  }

  return {};
}

bool WinePrefix::writeRegistryValue(const QString& regFile,
                                    const QString& subKey,
                                    const QString& valueName,
                                    const QString& value) const
{
  const QString filePath = m_prefixPath + "/" + regFile;
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    MOBase::log::error("writeRegistryValue: cannot open '{}'", filePath);
    return false;
  }

  const QString sectionPrefix = "[" + escapeRegKey(subKey) + "]";
  const QString valuePrefix   = "\"" + valueName + "\"=";
  const QString newLine       = "\"" + valueName + "\"=\"" + escapeRegValue(value) + "\"";

  QStringList lines;
  bool inSection = false;
  bool replaced  = false;
  bool sectionFound = false;

  QTextStream in(&file);
  while (!in.atEnd()) {
    QString line = in.readLine();
    const QString trimmed = line.trimmed();

    if (trimmed.startsWith('[')) {
      if (inSection && !replaced) {
        // End of our section without finding the value — insert it
        lines.append(newLine);
        replaced = true;
      }
      inSection = trimmed.startsWith(sectionPrefix, Qt::CaseInsensitive);
      if (inSection) sectionFound = true;
    }

    if (inSection && trimmed.startsWith(valuePrefix, Qt::CaseInsensitive)) {
      lines.append(newLine);
      replaced = true;
      continue;
    }

    lines.append(line);
  }
  file.close();

  // If section existed but value wasn't found (and wasn't inserted above)
  if (sectionFound && !replaced) {
    for (int i = 0; i < lines.size(); ++i) {
      if (lines[i].trimmed().startsWith(sectionPrefix, Qt::CaseInsensitive)) {
        lines.insert(i + 1, newLine);
        replaced = true;
        break;
      }
    }
  }

  // If section doesn't exist at all, append it
  if (!sectionFound) {
    lines.append("");
    lines.append(sectionPrefix);
    lines.append(newLine);
    replaced = true;
  }

  if (!replaced) {
    return false;
  }

  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    MOBase::log::error("writeRegistryValue: cannot write '{}'", filePath);
    return false;
  }
  QTextStream out(&file);
  for (const auto& l : lines) {
    out << l << "\n";
  }

  MOBase::log::info("Updated Wine registry: [{}] \"{}\"=\"{}\" in {}",
                    subKey, valueName, value, regFile);
  return true;
}

QString WinePrefix::readHklmValue(const QString& subKey,
                                  const QString& valueName) const
{
  return readRegistryValue("system.reg", subKey, valueName);
}

bool WinePrefix::writeHklmValue(const QString& subKey,
                                const QString& valueName,
                                const QString& value) const
{
  return writeRegistryValue("system.reg", subKey, valueName, value);
}
