#ifndef FUSECONNECTOR_H
#define FUSECONNECTOR_H

#include "envdump.h"
#include "vfs/mo2filesystem.h"
#include "vfs/stagingpromotion.h"
#include "vfs/trackedwrites.h"
#include "vfs/vfsindex.h"

#include <QObject>
#include <QString>

#include <exception>
#include <memory>
#include <thread>
#include <uibase/executableinfo.h>
#include <uibase/filemapping.h>
#include <uibase/log.h>

class SleepInhibitor;

class FuseConnectorException : public std::exception
{
public:
  explicit FuseConnectorException(const QString& text)
      :  m_Message(text.toLocal8Bit())
  {}

  const char* what() const noexcept override { return m_Message.constData(); }

private:
  QByteArray m_Message;
};

class FuseConnector : public QObject
{
  Q_OBJECT

public:
  explicit FuseConnector(QObject* parent = nullptr);
  ~FuseConnector() override;

  bool mount(const QString& mount_point, const QString& overwrite_dir,
             const QString& game_dir, const QString& data_dir_name,
             const std::vector<std::pair<std::string, std::string>>& mods);

  void setPluginLoadOrder(const std::vector<std::string>& load_order);
  void setTrackingFilePath(const std::string& path);
  void setIndexPublicationContext(VfsIndexPublicationContext context);
  std::shared_ptr<TrackedWrites> trackedWrites() const;

  // Diagnostic: zeroes the VFS's kernel-facing cache TTLs and disables
  // keep_cache for the next mount(). See Mo2FsContext::cache_disabled in
  // vfs/mo2filesystem.h for what this actually toggles.
  void setDisableVfsCache(bool disabled) { m_disableVfsCache = disabled; }

  // When true, the FUSE VFS inserts phantom directories for missing path
  // components (needed by Starfield).  Must be set before the next mount().
  void setAutoCreateDirs(bool enabled) { m_autoCreateDirs = enabled; }

  void unmount();
  void discardStagingOnUnmount();
  bool isMounted() const;

  void rebuild(const std::vector<std::pair<std::string, std::string>>& mods,
               const QString& overwrite_dir, const QString& data_dir_name);

  void flushStagingLive();

  void updateMapping(const MappingType& mapping);
  void updateParams(MOBase::log::Levels logLevel, env::CoreDumpTypes coreDumpType,
                    const QString& crashDumpsPath, std::chrono::seconds spawnDelay,
                    QString executableBlacklist, const QStringList& skipFileSuffixes,
                    const QStringList& skipDirectories);
  void updateForcedLibraries(
      const QList<MOBase::ExecutableForcedLoadSetting>& forced);

  static void tryCleanupStaleMount(const QString& path);

  // VFS Root Builder: deploy/clear mod Root/ files to the game directory.
  void setRootBuilderEnabled(bool enabled, const std::string& storageDir = {});
  void deployRootFiles(
      const std::vector<std::pair<std::string, std::string>>& mods);
  // Deploy only the backend-independent Root/ payload for an upcoming USVFS
  // session. The normal FUSE update path performs this as part of mount().
  void prepareRootFilesForUsvfs(const MappingType& mapping);
  void clearRootFiles();

private:
  void stopFuseRuntime();
  void resetPartialMountRuntime();
  void cleanupMountDeployments();
  void rollbackMountStartup() noexcept;
  StagingPromotionResult flushStaging();
  VfsIndexPublicationResult publishIndex(
      VfsTree& tree, const VfsCatalogResult& catalog_result);
  bool deployIndexRootLocator(VfsIndexPublicationResult& publication);
  void clearIndexRootLocator();
  void deployExternalMappings(const MappingType& mapping, const QString& dataDir);
  void cleanupExternalMappings();

  std::string m_mountPoint;
  std::string m_stagingDir;
  std::string m_overwriteDir;
  std::string m_customOutputDir;
  std::string m_gameDir;
  std::string m_dataDirName;
  std::string m_dataDirPath;
  int m_backingFd = -1;
  std::vector<CachedBaseFile> m_baseFileCache;
  std::string m_cachedDataDirPath;

  std::vector<std::pair<std::string, std::string>> m_lastMods;
  std::vector<std::string> m_pluginLoadOrder;
  VfsIndexPublicationContext m_indexPublicationContext;

  // Symlinks created for non-data-dir mappings (e.g. Paks, OBSE, UE4SS).
  std::vector<std::string> m_externalSymlinks;
  // Directories we created for non-data-dir mappings — removed (only if
  // empty) on cleanup. Stored deepest-first so reverse-iteration unwinds
  // child dirs before their parents.
  std::vector<std::string> m_externalDirs;
  // File-level mappings targeting the data directory (e.g. plugins.txt).
  // Injected into the VFS tree after building.  (relPath, absRealPath)
  std::vector<std::pair<std::string, std::string>> m_extraVfsFiles;

  std::shared_ptr<Mo2FsContext> m_context;
  std::shared_ptr<TrackedWrites> m_trackedWrites;
  std::string m_trackingFilePath;

  struct fuse_session* m_session = nullptr;
  std::thread m_fuseThread;
  std::thread m_invalidationThread;
  bool m_mounted        = false;
  bool m_sessionMounted = false;
  bool m_discardStaging = false;
  bool m_disableVfsCache = false;
  bool m_autoCreateDirs = false;

  // Gives logind an accurate reason for suspend/shutdown/lock prompts while
  // mounted, instead of the generic "systemd (1)". See sleepinhibitor.h.
  SleepInhibitor* m_sleepInhibitor = nullptr;

  // VFS Root Builder state
  bool m_rootBuilderEnabled = false;
  std::string m_rootStorageDir;
  std::vector<std::string> m_rootDeployedFiles;
  std::vector<std::string> m_rootDeployedDirs;  // dirs we created in game root
  std::map<std::string, std::string> m_rootBackups;  // dst -> backup path
};

#endif
