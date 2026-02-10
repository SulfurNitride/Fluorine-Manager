#include "fuseconnector.h"

#include "settings.h"
#include "vfs/vfstree.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QVariant>

#include <iplugingame.h>

#include <algorithm>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <unistd.h>

using namespace MOBase;

namespace
{
namespace fs = std::filesystem;

std::string decodeProcMountField(const std::string& in)
{
  std::string out;
  out.reserve(in.size());

  for (size_t i = 0; i < in.size();) {
    if (in[i] == '\\' && i + 3 < in.size() && std::isdigit(in[i + 1]) &&
        std::isdigit(in[i + 2]) && std::isdigit(in[i + 3])) {
      const std::string oct = in.substr(i + 1, 3);
      const int value       = std::stoi(oct, nullptr, 8);
      out.push_back(static_cast<char>(value));
      i += 4;
      continue;
    }

    out.push_back(in[i]);
    ++i;
  }

  return out;
}

bool isMountPoint(const QString& path)
{
  QFile mounts(QStringLiteral("/proc/mounts"));
  if (!mounts.open(QIODevice::ReadOnly)) {
    return false;
  }

  const auto mountPoint = QDir::cleanPath(path);
  while (!mounts.atEnd()) {
    const auto line  = QString::fromUtf8(mounts.readLine()).trimmed();
    const auto parts = line.split(' ', Qt::SkipEmptyParts);
    if (parts.size() < 2) {
      continue;
    }

    const QString current = QString::fromStdString(
        decodeProcMountField(parts[1].toStdString()));
    if (QDir::cleanPath(current) == mountPoint) {
      return true;
    }
  }

  return false;
}

bool runUnmountCommand(const QString& program, const QStringList& args)
{
  QProcess p;
  p.start(program, args);
  if (!p.waitForFinished(3000)) {
    p.kill();
    return false;
  }

  return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

std::vector<std::pair<std::string, std::string>>
buildModsFromMapping(const MappingType& mapping, const QString& dataDir,
                     const QString& overwriteDir)
{
  std::vector<std::pair<std::string, std::string>> mods;
  std::set<std::string> seen;

  const QString dataPrefix = QDir::cleanPath(dataDir) + "/";
  const QString overPrefix = QDir::cleanPath(overwriteDir) + "/";

  for (const auto& map : mapping) {
    if (!map.isDirectory) {
      continue;
    }

    const QString src = QDir::cleanPath(QDir::fromNativeSeparators(map.source));
    const QString dst = QDir::cleanPath(QDir::fromNativeSeparators(map.destination));

    if (!(dst == QDir::cleanPath(dataDir) || dst.startsWith(dataPrefix))) {
      continue;
    }

    if (src == QDir::cleanPath(overwriteDir) || src.startsWith(overPrefix)) {
      continue;
    }

    const std::string srcStd = src.toStdString();
    if (!seen.insert(srcStd).second) {
      continue;
    }

    const QString name = QFileInfo(src).fileName();
    mods.emplace_back(name.toStdString(), srcStd);
  }

  return mods;
}

void setupFuseOps(struct fuse_lowlevel_ops* ops)
{
  std::memset(ops, 0, sizeof(struct fuse_lowlevel_ops));
  ops->lookup  = mo2_lookup;
  ops->getattr = mo2_getattr;
  ops->readdir = mo2_readdir;
  ops->open    = mo2_open;
  ops->read    = mo2_read;
  ops->write   = mo2_write;
  ops->create  = mo2_create;
  ops->rename  = mo2_rename;
  ops->setattr = mo2_setattr;
  ops->unlink  = mo2_unlink;
  ops->mkdir   = mo2_mkdir;
  ops->release = mo2_release;
}

}  // namespace

FuseConnector::FuseConnector(QObject* parent) : QObject(parent)
{
  log::debug("FUSE connector initialized");
}

FuseConnector::~FuseConnector()
{
  unmount();
}

bool FuseConnector::mount(
    const QString& mount_point, const QString& overwrite_dir, const QString& game_dir,
    const QString& data_dir_name,
    const std::vector<std::pair<std::string, std::string>>& mods)
{
  if (m_mounted) {
    unmount();
  }

  m_overwriteDir = overwrite_dir.toStdString();
  m_gameDir      = game_dir.toStdString();
  m_dataDirName  = data_dir_name.toStdString();
  m_lastMods     = mods;

  // Compute the actual data directory path and mount directly on it
  m_dataDirPath = (fs::path(m_gameDir) / m_dataDirName).string();
  m_mountPoint  = m_dataDirPath;

  if (!fs::exists(m_dataDirPath)) {
    throw FuseConnectorException(
        QObject::tr("Game data directory does not exist: %1")
            .arg(QString::fromStdString(m_dataDirPath)));
  }

  const fs::path overwritePath(m_overwriteDir);
  m_stagingDir = (overwritePath.parent_path() / "VFS_staging").string();

  std::error_code ec;
  fs::create_directories(m_stagingDir, ec);
  fs::create_directories(m_overwriteDir, ec);

  // Scan + cache base game files BEFORE mounting (after mount they're hidden)
  m_baseFileCache = scanDataDir(m_dataDirPath);
  log::debug("Cached {} base game entries from {}", m_baseFileCache.size(),
             QString::fromStdString(m_dataDirPath));

  // Open fd to data dir BEFORE mounting so we can access original files
  m_backingFd = open(m_dataDirPath.c_str(), O_RDONLY | O_DIRECTORY);
  if (m_backingFd < 0) {
    throw FuseConnectorException(
        QObject::tr("Failed to open backing fd for %1")
            .arg(QString::fromStdString(m_dataDirPath)));
  }

  tryCleanupStaleMount(QString::fromStdString(m_mountPoint));

  // Build tree using cached base files + mods + overwrite
  auto tree = std::make_shared<VfsTree>(
      buildDataDirVfs(m_baseFileCache, m_dataDirPath, mods, m_overwriteDir));

  m_context                 = std::make_shared<Mo2FsContext>();
  m_context->tree           = tree;
  m_context->inodes         = std::make_unique<InodeTable>();
  m_context->overwrite      = std::make_unique<OverwriteManager>(m_stagingDir, m_overwriteDir);
  m_context->backing_dir_fd = m_backingFd;
  m_context->uid            = ::getuid();
  m_context->gid            = ::getgid();

  // NOTE: Do NOT include mount_point here — low-level API passes it
  // separately to fuse_session_mount(). Including it here causes
  // "fuse: unknown option(s)" error.
  std::vector<std::string> argvStorage = {
      "mo2fuse", "-o", "fsname=mo2linux", "-o", "default_permissions",
      "-o",      "noatime"};

  std::vector<char*> argv;
  argv.reserve(argvStorage.size());
  for (auto& s : argvStorage) {
    argv.push_back(s.data());
  }

  struct fuse_args args = FUSE_ARGS_INIT(static_cast<int>(argv.size()), argv.data());

  struct fuse_lowlevel_ops ops;
  setupFuseOps(&ops);

  m_session = fuse_session_new(&args, &ops, sizeof(ops), m_context.get());
  if (m_session == nullptr) {
    close(m_backingFd);
    m_backingFd = -1;
    throw FuseConnectorException(QObject::tr("Failed to create FUSE session"));
  }

  if (fuse_session_mount(m_session, m_mountPoint.c_str()) != 0) {
    fuse_session_destroy(m_session);
    m_session = nullptr;
    close(m_backingFd);
    m_backingFd = -1;
    throw FuseConnectorException(
        QObject::tr("Failed to mount FUSE at %1")
            .arg(QString::fromStdString(m_mountPoint)));
  }

  m_fuseThread = std::thread([this]() {
    fuse_session_loop_mt(m_session, nullptr);
  });

  m_mounted = true;
  log::debug("FUSE mounted on data dir {}", QString::fromStdString(m_mountPoint));
  return true;
}

void FuseConnector::unmount()
{
  if (!m_mounted) {
    return;
  }

  if (m_session != nullptr) {
    fuse_session_exit(m_session);
    fuse_session_unmount(m_session);
  }

  if (m_fuseThread.joinable()) {
    m_fuseThread.join();
  }

  if (m_session != nullptr) {
    fuse_session_destroy(m_session);
    m_session = nullptr;
  }

  flushStaging();

  if (m_backingFd >= 0) {
    close(m_backingFd);
    m_backingFd = -1;
  }

  m_context.reset();
  m_mounted = false;

  log::debug("FUSE unmounted from {}", QString::fromStdString(m_mountPoint));
}

bool FuseConnector::isMounted() const
{
  return m_mounted;
}

void FuseConnector::rebuild(
    const std::vector<std::pair<std::string, std::string>>& mods,
    const QString& overwrite_dir, const QString& data_dir_name)
{
  if (!m_mounted || m_context == nullptr) {
    return;
  }

  m_overwriteDir = overwrite_dir.toStdString();
  m_dataDirName  = data_dir_name.toStdString();
  m_lastMods     = mods;

  // Use cached base files - can't re-scan the data dir since it's behind our mount
  auto newTree = std::make_shared<VfsTree>(
      buildDataDirVfs(m_baseFileCache, m_dataDirPath, mods, m_overwriteDir));

  std::unique_lock lock(m_context->tree_mutex);
  m_context->tree.swap(newTree);
}

void FuseConnector::updateMapping(const MappingType& mapping)
{
  auto* game = qApp->property("managed_game").value<MOBase::IPluginGame*>();
  if (game == nullptr) {
    throw FuseConnectorException(QObject::tr("Managed game not available"));
  }

  const QString gameDir      = game->gameDirectory().absolutePath();
  const QString dataDirPath  = game->dataDirectory().absolutePath();
  const QString dataDirName  = game->dataDirectory().dirName();
  const QString overwriteDir = Settings::instance().paths().overwrite();

  auto mods = buildModsFromMapping(mapping, dataDirPath, overwriteDir);

  if (!m_mounted) {
    // mount_point param is ignored — mount() computes it from gameDir + dataDirName
    mount(dataDirPath, overwriteDir, gameDir, dataDirName, mods);
  } else {
    rebuild(mods, overwriteDir, dataDirName);
  }
}

void FuseConnector::updateParams(MOBase::log::Levels /*logLevel*/,
                                 env::CoreDumpTypes /*coreDumpType*/,
                                 const QString& /*crashDumpsPath*/,
                                 std::chrono::seconds /*spawnDelay*/,
                                 QString /*executableBlacklist*/,
                                 const QStringList& /*skipFileSuffixes*/,
                                 const QStringList& /*skipDirectories*/)
{}

void FuseConnector::updateForcedLibraries(
    const QList<MOBase::ExecutableForcedLoadSetting>& /*forced*/)
{}

void FuseConnector::flushStaging()
{
  if (m_stagingDir.empty() || m_overwriteDir.empty()) {
    return;
  }

  const fs::path staging(m_stagingDir);
  const fs::path overwrite(m_overwriteDir);
  if (!fs::exists(staging)) {
    return;
  }

  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(
           staging, fs::directory_options::skip_permission_denied);
       it != fs::recursive_directory_iterator(); ++it) {
    const auto& entry = *it;
    const fs::path rel = fs::relative(entry.path(), staging, ec);
    if (ec || rel.empty()) {
      continue;
    }

    const fs::path dest = overwrite / rel;
    if (entry.is_directory(ec)) {
      fs::create_directories(dest, ec);
      continue;
    }

    if (!entry.is_regular_file(ec)) {
      continue;
    }

    fs::create_directories(dest.parent_path(), ec);
    fs::rename(entry.path(), dest, ec);
    if (ec) {
      ec.clear();
      fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing, ec);
      if (!ec) {
        fs::remove(entry.path(), ec);
      }
    }
  }

  fs::remove_all(staging, ec);
}

void FuseConnector::flushStagingLive()
{
  if (!m_mounted || m_context == nullptr) {
    return;
  }

  // Move staged files to overwrite
  flushStaging();

  // Re-create the staging dir (flushStaging removes it)
  std::error_code ec;
  fs::create_directories(m_stagingDir, ec);

  // Rebuild the VFS tree to pick up new overwrite files
  auto newTree = std::make_shared<VfsTree>(
      buildDataDirVfs(m_baseFileCache, m_dataDirPath, m_lastMods, m_overwriteDir));

  {
    std::unique_lock lock(m_context->tree_mutex);
    m_context->tree.swap(newTree);
  }

  // Re-create OverwriteManager with fresh staging dir
  m_context->overwrite = std::make_unique<OverwriteManager>(m_stagingDir, m_overwriteDir);

  log::debug("Live staging flush complete");
}

void FuseConnector::tryCleanupStaleMount(const QString& path)
{
  if (!isMountPoint(path)) {
    return;
  }

  const QString clean = QDir::cleanPath(path);

  if (runUnmountCommand("fusermount3", {"-u", clean}) ||
      runUnmountCommand("fusermount", {"-u", clean})) {
    return;
  }

  runUnmountCommand("umount", {clean});
  runUnmountCommand("umount", {"-l", clean});
  runUnmountCommand("fusermount3", {"-uz", clean});
  runUnmountCommand("fusermount", {"-uz", clean});
}
