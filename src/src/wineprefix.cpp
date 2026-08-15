#include "wineprefix.h"
#include "wineprofileinisync.h"
#include "wineregistryfile.h"
#include "winesavedeployment.h"
#include "winesaverouting.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <uibase/filesystemutilities.h>
#include <uibase/log.h>

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
