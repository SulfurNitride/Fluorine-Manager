#ifndef FUSECONNECTOR_H
#define FUSECONNECTOR_H

#include "envdump.h"
#include "vfs/mo2filesystem.h"

#include <QObject>
#include <QString>

#include <exception>
#include <memory>
#include <thread>
#include <uibase/executableinfo.h>
#include <uibase/filemapping.h>
#include <uibase/log.h>

class FuseConnectorException : public std::exception
{
public:
  explicit FuseConnectorException(const QString& text)
      : std::exception(), m_Message(text.toLocal8Bit())
  {}

  const char* what() const throw() override { return m_Message.constData(); }

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

  void unmount();
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

private:
  void flushStaging();
  static void tryCleanupStaleMount(const QString& path);

  std::string m_mountPoint;
  std::string m_stagingDir;
  std::string m_overwriteDir;
  std::string m_gameDir;
  std::string m_dataDirName;
  std::string m_dataDirPath;
  int m_backingFd = -1;
  std::vector<CachedBaseFile> m_baseFileCache;

  std::vector<std::pair<std::string, std::string>> m_lastMods;

  std::shared_ptr<Mo2FsContext> m_context;

  struct fuse_session* m_session = nullptr;
  std::thread m_fuseThread;
  bool m_mounted = false;
};

#endif
