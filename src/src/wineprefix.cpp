#include "wineprefix.h"
#include "winepluginlistsync.h"
#include "wineprofileinisync.h"
#include "wineregistryfile.h"
#include "winesavedeployment.h"
#include "winesaverouting.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QTextStream>
#include <uibase/filesystemutilities.h>
#include <uibase/log.h>
#include <uibase/transactionalwritefile.h>

#include <memory>

namespace
{
constexpr const char* BackupIniSuffix = ".mo2linux_backup";

bool restoreBackedUpIni(const QString& liveIni, const QString& backupIni)
{
  const auto result = WineProfileIniSync::restoreLegacyBackup(liveIni, backupIni);
  if (!result)
    MOBase::log::warn("Could not restore INI backup '{}': {}", backupIni, result.error);
  return result.success;
}

// Find all files in the same directory that match the filename
// case-insensitively. E.g. for "skyrimprefs.ini" returns {"skyrimprefs.ini",
// "SkyrimPrefs.ini"} if both exist.
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

WinePrefix::WinePrefix(const QString& prefixPath,
                       const QString& userProfilePath)
    : m_prefixPath(QDir::cleanPath(prefixPath)),
      m_userProfilePath(userProfilePath.isEmpty()
                            ? QDir(QDir::cleanPath(prefixPath))
                                  .filePath("drive_c/users/steamuser")
                            : QDir::cleanPath(userProfilePath))
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
  return QDir(m_userProfilePath).filePath("Documents");
}

QString WinePrefix::myGamesPath() const
{
  return QDir(documentsPath()).filePath("My Games");
}

QString WinePrefix::appdataLocal() const
{
  return QDir(m_userProfilePath).filePath("AppData/Local");
}

QString WinePrefix::userProfilePath() const
{
  return m_userProfilePath;
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
                                  const QString& targetIniPath, const QString& ownerId,
                                  WineProfileIniSync::Deployment& deployment) const
{
  MOBase::log::debug("deployProfileIni: '{}' -> '{}'", sourceIniPath, targetIniPath);
  const auto result =
      WineProfileIniSync::deploy(sourceIniPath, targetIniPath, ownerId, deployment);
  if (!result) {
    MOBase::log::error("deployProfileIni: {}", result.error);
    return false;
  }
  return true;
}

bool WinePrefix::deployProfileSaves(const QString& profileSaveDir,
                                    const QString& absoluteSaveDir,
                                    bool /*clearDestination*/, const QString& ownerId,
                                    bool* cleanupRequired) const
{
  if (cleanupRequired)
    *cleanupRequired = false;
  if (!isValid()) {
    MOBase::log::error("deployProfileSaves: prefix '{}' is not valid", m_prefixPath);
    return false;
  }

  MOBase::log::debug("deployProfileSaves: profileSaveDir='{}', absoluteSaveDir='{}'",
                     profileSaveDir, absoluteSaveDir);

  const auto result = WineSaveDeployment::deployLinks(driveC(), profileSaveDir,
                                                      absoluteSaveDir, ownerId);
  if (cleanupRequired)
    *cleanupRequired = result.cleanupRequired;
  if (!result) {
    MOBase::log::error("deployProfileSaves: {}", result.error);
  }
  return result.success;
}

bool WinePrefix::prepareProfileSavesBindTarget(const QString& profileSaveDir,
                                               const QString& absoluteSaveDir,
                                               const QString& ownerId,
                                               bool* cleanupRequired) const
{
  if (cleanupRequired)
    *cleanupRequired = false;
  if (!isValid()) {
    MOBase::log::error("prepareProfileSavesBindTarget: prefix '{}' is not valid",
                       m_prefixPath);
    return false;
  }
  const auto result = WineSaveDeployment::prepareBindTarget(driveC(), profileSaveDir,
                                                            absoluteSaveDir, ownerId);
  if (cleanupRequired)
    *cleanupRequired = result.cleanupRequired;
  if (!result) {
    MOBase::log::error("prepareProfileSavesBindTarget: {}", result.error);
  }
  return result.success;
}

bool WinePrefix::syncSavesBack(const QString& profileSaveDir,
                               const QString& absoluteSaveDir, const QString& ownerId,
                               bool* topologyComplete, bool* cleanupRequired) const
{
  if (topologyComplete)
    *topologyComplete = false;
  if (cleanupRequired)
    *cleanupRequired = false;
  if (!isValid()) {
    MOBase::log::error("syncSavesBack: prefix '{}' is not valid", m_prefixPath);
    return false;
  }

  MOBase::log::debug("syncSavesBack: profileSaveDir='{}', absoluteSaveDir='{}'",
                     profileSaveDir, absoluteSaveDir);

  const auto result = WineSaveDeployment::synchronizeAndRestore(
      driveC(), profileSaveDir, absoluteSaveDir, ownerId);
  if (topologyComplete)
    *topologyComplete = result.topologyComplete;
  if (cleanupRequired)
    *cleanupRequired = result.cleanupRequired;
  if (!result) {
    MOBase::log::error("syncSavesBack: {}", result.error);
  } else if (!result.error.isEmpty()) {
    MOBase::log::warn("syncSavesBack: {}", result.error);
  }
  return result.success;
}

bool WinePrefix::rollbackProfileSaves(const QString& profileSaveDir,
                                      const QString& absoluteSaveDir,
                                      const QString& ownerId, bool* topologyComplete,
                                      bool* cleanupRequired) const
{
  if (topologyComplete)
    *topologyComplete = false;
  if (cleanupRequired)
    *cleanupRequired = false;
  if (!isValid()) {
    MOBase::log::error("rollbackProfileSaves: prefix '{}' is not valid", m_prefixPath);
    return false;
  }
  const auto result = WineSaveDeployment::rollbackLinks(driveC(), profileSaveDir,
                                                        absoluteSaveDir, ownerId);
  if (topologyComplete)
    *topologyComplete = result.topologyComplete;
  if (cleanupRequired)
    *cleanupRequired = result.cleanupRequired;
  if (!result) {
    MOBase::log::error("rollbackProfileSaves: {}", result.error);
  } else if (!result.error.isEmpty()) {
    MOBase::log::warn("rollbackProfileSaves: {}", result.error);
  }
  return result.success;
}

void WinePrefix::restoreStaleBackups() const
{
  if (!isValid()) {
    return;
  }

  QLockFile prefixLease(WineSaveDeployment::leasePathFor(m_prefixPath, QString{}));
  prefixLease.setStaleLockTime(0);
  if (!prefixLease.tryLock(0) &&
      (!prefixLease.removeStaleLockFile() || !prefixLease.tryLock(0))) {
    MOBase::log::warn(
        "Deferring stale Wine INI recovery because prefix '{}' is active in "
        "another Fluorine process",
        m_prefixPath);
    return;
  }

  // A manager may have crashed while its Wine child kept running. The
  // persistent save-session marker is deliberately not auto-adopted, and the
  // same rule applies to deployed INIs: restoring them underneath that child
  // would mutate its live configuration and invalidate the routing receipt.
  if (WineSaveDeployment::hasPersistedSessionLease(m_prefixPath)) {
    MOBase::log::warn(
        "Deferring stale Wine INI recovery because prefix '{}' still has an "
        "unresolved save-session owner",
        m_prefixPath);
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

    QString routingOwner;
    bool routingPending = false;
    const auto routing =
        WineSaveRouting::pendingOwner(livePath, routingOwner, routingPending);
    if (!routing || routingPending) {
      MOBase::log::warn(
          "Deferring stale INI backup '{}' because its save-routing receipt "
          "is {}{}",
          backupPath, routing ? "still owned by launch '" : "invalid: ",
          routing ? routingOwner + QStringLiteral("'") : routing.error);
      continue;
    }

    MOBase::log::info("Restoring stale INI backup '{}' -> '{}'", backupPath, livePath);
    if (!restoreBackedUpIni(livePath, backupPath)) {
      MOBase::log::warn("Failed to restore stale INI backup '{}'", backupPath);
    }
  }

  // Save backups carry profile/launch ownership and are recovered only after
  // the exact configured save path and selected profile are known. A broad
  // name-only prefix scan could otherwise rename unrelated application data.
}

bool WinePrefix::syncProfileInisBack(QList<WineProfileIniSync::Deployment>& deployments,
                                     const QString& ownerId, bool publishChanges,
                                     WineProfileIniSync::CleanupPhase& phase) const
{
  MOBase::log::debug("syncProfileInisBack: {} INI deployments to finish",
                     deployments.size());
  const auto result =
      WineProfileIniSync::finish(deployments, ownerId, publishChanges, phase);
  if (!result) {
    MOBase::log::error("syncProfileInisBack: {}", result.error);
  }
  return result.success;
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
  for (const QString& v : findCaseVariants(QDir(pluginsDir).filePath("plugins.txt"))) {
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

  MOBase::TransactionalWriteFile profileFile(profilePluginsPath);
  const auto sourceRead = WinePluginListSync::read(newest);
  if (!sourceRead.snapshot) {
    MOBase::log::error("syncPluginsBack: {}", sourceRead.error);
    return false;
  }
  const WinePluginListSync::Snapshot& sourceSnapshot = *sourceRead.snapshot;

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
    const auto result = WinePluginListSync::read(path);
    return result.snapshot ? WinePluginListSync::countStarred(result.snapshot->contents)
                           : -1;
  };

  const int profileStars   = countStarredLines(profilePluginsPath);
  const int candidateStars = WinePluginListSync::countStarred(sourceSnapshot.contents);
  if (WinePluginListSync::isSuspiciousActiveDrop(profileStars, candidateStars)) {
    const int absDrop = profileStars - candidateStars;
    const double relDrop =
        static_cast<double>(absDrop) / static_cast<double>(profileStars);
    MOBase::log::warn(
        "syncPluginsBack: refusing copy — active plugin count would drop "
        "from {} to {} ({:.0f}% loss). Likely a game-crash artifact, not "
        "a real edit. Profile preserved; prefix file left in place at '{}'.",
        profileStars, candidateStars, relDrop * 100.0, newest);
    return true;
  }

  MOBase::log::info("syncPluginsBack: '{}' <- '{}'", profilePluginsPath, newest);
  QString publicationError;
  if (!WinePluginListSync::publish(profileFile, sourceSnapshot, publicationError)) {
    MOBase::log::error("syncPluginsBack: failed to publish '{}': {}",
                       profilePluginsPath, publicationError);
    return false;
  }

  bool allMirrored = true;
  for (const QString& sibling : variants) {
    if (sibling == newest || WinePluginListSync::isSameFile(sibling, sourceSnapshot)) {
      continue;
    }
    MOBase::TransactionalWriteFile siblingFile(sibling);
    QString siblingError;
    if (!WinePluginListSync::publish(siblingFile, sourceSnapshot, siblingError)) {
      MOBase::log::error("syncPluginsBack: failed to mirror '{}' into '{}': {}", newest,
                         sibling, siblingError);
      allMirrored = false;
    }
  }

  // Clear any stale loadorder.txt that an older build may have written.
  // The game never reads it; leaving it around only invites confusion.
  for (const QString& stale :
       findCaseVariants(QDir(pluginsDir).filePath("loadorder.txt"))) {
    MOBase::log::debug("syncPluginsBack: removing stale loadorder variant '{}'", stale);
    QFile::remove(stale);
  }

  return allMirrored;
}

// ── Wine registry (.reg file) access ─────────────────────────────────────────

QString WinePrefix::readRegistryValue(const QString& regFile, const QString& subKey,
                                      const QString& valueName) const
{
  QList<WineRegistryFile::Query> queries{{subKey, valueName}};
  const auto result =
      WineRegistryFile::readValues(QDir(m_prefixPath).filePath(regFile), queries);
  if (!result)
  {
    MOBase::log::error("Could not read Wine registry '{}': {}", regFile, result.error);
    return {};
  }
  return queries.first().present ? queries.first().value : QString{};
}

bool WinePrefix::writeRegistryValue(const QString& regFile, const QString& subKey,
                                    const QString& valueName,
                                    const QString& value) const
{
  const auto result = WineRegistryFile::updateValues(
      QDir(m_prefixPath).filePath(regFile), {{subKey, valueName, value}});
  if (!result)
  {
    MOBase::log::error("Could not update Wine registry '{}': {}", regFile,
                       result.error);
  }
  return static_cast<bool>(result);
}

QString WinePrefix::readHklmValue(const QString& subKey, const QString& valueName) const
{
  return readRegistryValue("system.reg", subKey, valueName);
}

bool WinePrefix::writeHklmValue(const QString& subKey, const QString& valueName,
                                const QString& value) const
{
  return writeHklmValues({{subKey, valueName, value}});
}

bool WinePrefix::readHklmValues(QList<WineRegistryFile::Query>& queries,
                                QString* error) const
{
  const auto result = WineRegistryFile::readValues(
      QDir(m_prefixPath).filePath(QStringLiteral("system.reg")), queries);
  if (!result && error != nullptr)
  {
    *error = result.error;
  }
  return static_cast<bool>(result);
}

bool WinePrefix::writeHklmValues(const QList<WineRegistryFile::Update>& updates,
                                 QString* error, bool prefixLeaseHeld) const
{
  std::unique_ptr<QLockFile> prefixLease;
  if (!prefixLeaseHeld)
  {
    prefixLease = std::make_unique<QLockFile>(
        WineSaveDeployment::leasePathFor(m_prefixPath, QString{}));
    prefixLease->setStaleLockTime(0);
    if (!prefixLease->tryLock(0) &&
        (!prefixLease->removeStaleLockFile() || !prefixLease->tryLock(0)))
    {
      if (error != nullptr)
      {
        *error =
            QStringLiteral("The Wine prefix is active in another Fluorine process.");
      }
      return false;
    }
  }
  if (!prefixLeaseHeld &&
      WineSaveDeployment::hasPersistedSessionLease(m_prefixPath))
  {
    if (error != nullptr)
    {
      *error =
          QStringLiteral("The Wine prefix has an unresolved managed launch owner.");
    }
    return false;
  }

  const auto result = WineRegistryFile::updateValues(
      QDir(m_prefixPath).filePath(QStringLiteral("system.reg")), updates);
  if (!result && error != nullptr)
  {
    *error = result.error;
  }
  return static_cast<bool>(result);
}

bool WinePrefix::pruneExtraDrives(QStringList& removed, QString* error,
                                  bool prefixLeaseHeld) const
{
  removed.clear();
  std::unique_ptr<QLockFile> prefixLease;
  if (!prefixLeaseHeld)
  {
    prefixLease = std::make_unique<QLockFile>(
        WineSaveDeployment::leasePathFor(m_prefixPath, QString{}));
    prefixLease->setStaleLockTime(0);
    if (!prefixLease->tryLock(0) &&
        (!prefixLease->removeStaleLockFile() || !prefixLease->tryLock(0)))
    {
      if (error != nullptr)
      {
        *error =
            QStringLiteral("The Wine prefix is active in another Fluorine process.");
      }
      return false;
    }
  }
  if (!prefixLeaseHeld &&
      WineSaveDeployment::hasPersistedSessionLease(m_prefixPath))
  {
    if (error != nullptr)
    {
      *error =
          QStringLiteral("The Wine prefix has an unresolved managed launch owner.");
    }
    return false;
  }

  QStringList registryRemoved;
  const auto registryResult = WineRegistryFile::removeDriveMappings(
      QDir(m_prefixPath).filePath(QStringLiteral("system.reg")), registryRemoved);
  if (!registryResult)
  {
    if (error != nullptr)
    {
      *error = registryResult.error;
    }
    return false;
  }

  const QDir dosdevices(QDir(m_prefixPath).filePath(QStringLiteral("dosdevices")));
  if (dosdevices.exists())
  {
    const QStringList entries = dosdevices.entryList(
        QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System);
    for (const QString& entry : entries)
    {
      const QString lower = entry.toLower();
      if (lower.size() != 2 || !lower.endsWith(':') || !lower.front().isLetter() ||
          lower == QStringLiteral("c:") || lower == QStringLiteral("z:"))
      {
        continue;
      }

      const QString path = dosdevices.filePath(entry);
      const QFileInfo info(path);
      if (!info.isSymbolicLink())
      {
        if (error != nullptr)
        {
          *error =
              QStringLiteral("Refusing to remove non-symlink Wine drive mapping '%1'.")
                  .arg(path);
        }
        return false;
      }
      if (!QFile::remove(path))
      {
        if (error != nullptr)
        {
          *error =
              QStringLiteral("Could not remove Wine drive mapping '%1'.").arg(path);
        }
        return false;
      }
      removed.append(entry.toUpper());
    }
  }

  removed.append(registryRemoved);
  removed.removeDuplicates();
  removed.sort();
  return true;
}
