#include "fuseconnector.h"

#include "settings.h"
#include "sleepinhibitor.h"
#include "vfs/vfscatalog.h"
#include "vfs/vfsindex.h"
#include "vfs/vfstree.h"

#include <QCoreApplication>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProgressDialog>
#include <QSaveFile>
#include <QStandardPaths>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QThread>
#include <QVariant>

#include <iplugingame.h>

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

using namespace MOBase;

// Global mount point for signal-handler cleanup (async-signal-safe access).
static char g_fuseMountPoint[4096] = {0};

void setFuseMountPointForCrashCleanup(const char* path)
{
  if (path != nullptr) {
    std::strncpy(g_fuseMountPoint, path, sizeof(g_fuseMountPoint) - 1);
    g_fuseMountPoint[sizeof(g_fuseMountPoint) - 1] = '\0';
  } else {
    g_fuseMountPoint[0] = '\0';
  }
}

const char* getFuseMountPointForCrashCleanup()
{
  return g_fuseMountPoint[0] != '\0' ? g_fuseMountPoint : nullptr;
}

namespace
{
namespace fs = std::filesystem;

std::string digestPrefix(const VfsDigest& digest)
{
  constexpr char hex[] = "0123456789abcdef";
  std::string result(16, '0');
  for (size_t i = 0; i < 8; ++i) {
    result[i * 2] = hex[digest[i] >> 4];
    result[i * 2 + 1] = hex[digest[i] & 0x0f];
  }
  return result;
}

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
  // Suppress stderr from fusermount/umount to avoid confusing terminal output
  // when unmount fails (e.g. permission denied in Flatpak sandbox).
  auto tryRun = [&](const QString& cmd, const QStringList& cmdArgs) -> bool {
    QProcess p;
    p.setStandardErrorFile(QProcess::nullDevice());
    p.start(cmd, cmdArgs);
    if (!p.waitForFinished(3000)) {
      p.kill();
      return false;
    }
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
  };

  return tryRun(program, args);
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

// Exception barrier between libfuse3 (C, no unwind support) and our C++
// callbacks.  An uncaught exception unwinding into libfuse3 hits std::terminate
// and aborts the process, leaving the FUSE mount orphaned and wedging anything
// that touches it in D-state.  Reply with ENOMEM/EIO and stay alive instead.
namespace
{

void replyExceptionError(fuse_req_t req, const char* op,
                         unsigned long long ino, const std::exception* e) noexcept
{
  // fuse_reply_err itself shouldn't allocate but log first in case it does.
  if (e != nullptr) {
    std::fprintf(stderr, "[VFS] %s(ino=%llu): caught exception: %s\n", op, ino,
                 e->what());
  } else {
    std::fprintf(stderr, "[VFS] %s(ino=%llu): caught unknown exception\n", op, ino);
  }
  // ENOMEM for bad_alloc, EIO otherwise — distinguished at call site.
}

// The handlers below capture the inode (or parent inode for lookup) so a
// std::bad_alloc / std::exception log line identifies which path was being
// processed — essential for diagnosing cache/tree corruption (e.g. #210).
#define MO2_TRY_REPLY(req, op, ino, errno_) \
  catch (const std::bad_alloc& e) { \
    replyExceptionError((req), (op), \
                        static_cast<unsigned long long>(ino), &e); \
    fuse_reply_err((req), ENOMEM); \
  } catch (const std::exception& e) { \
    replyExceptionError((req), (op), \
                        static_cast<unsigned long long>(ino), &e); \
    fuse_reply_err((req), (errno_)); \
  } catch (...) { \
    replyExceptionError((req), (op), \
                        static_cast<unsigned long long>(ino), nullptr); \
    fuse_reply_err((req), (errno_)); \
  }

void wrap_init(void* userdata, struct fuse_conn_info* conn) noexcept
{
  try { mo2_init(userdata, conn); }
  catch (const std::exception& e) {
    std::fprintf(stderr, "[VFS] init: caught exception: %s\n", e.what());
  } catch (...) {
    std::fprintf(stderr, "[VFS] init: caught unknown exception\n");
  }
}

void wrap_lookup(fuse_req_t req, fuse_ino_t parent, const char* name) noexcept
{
  try { mo2_lookup(req, parent, name); }
  MO2_TRY_REPLY(req, "lookup", parent, EIO)
}

void wrap_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi) noexcept
{
  try { mo2_getattr(req, ino, fi); }
  MO2_TRY_REPLY(req, "getattr", ino, EIO)
}

void wrap_opendir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi) noexcept
{
  try { mo2_opendir(req, ino, fi); }
  MO2_TRY_REPLY(req, "opendir", ino, EIO)
}

void wrap_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                  struct fuse_file_info* fi) noexcept
{
  try { mo2_readdir(req, ino, size, off, fi); }
  MO2_TRY_REPLY(req, "readdir", ino, EIO)
}

void wrap_readdirplus(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                      struct fuse_file_info* fi) noexcept
{
  try { mo2_readdirplus(req, ino, size, off, fi); }
  MO2_TRY_REPLY(req, "readdirplus", ino, EIO)
}

void wrap_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi) noexcept
{
  try { mo2_open(req, ino, fi); }
  MO2_TRY_REPLY(req, "open", ino, EIO)
}

void wrap_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
               struct fuse_file_info* fi) noexcept
{
  try { mo2_read(req, ino, size, off, fi); }
  MO2_TRY_REPLY(req, "read", ino, EIO)
}

void wrap_write(fuse_req_t req, fuse_ino_t ino, const char* buf, size_t size,
                off_t off, struct fuse_file_info* fi) noexcept
{
  try { mo2_write(req, ino, buf, size, off, fi); }
  MO2_TRY_REPLY(req, "write", ino, EIO)
}

void wrap_create(fuse_req_t req, fuse_ino_t parent, const char* name, mode_t mode,
                 struct fuse_file_info* fi) noexcept
{
  try { mo2_create(req, parent, name, mode, fi); }
  MO2_TRY_REPLY(req, "create", parent, EIO)
}

void wrap_rename(fuse_req_t req, fuse_ino_t parent, const char* name,
                 fuse_ino_t newparent, const char* newname, unsigned int flags) noexcept
{
  try { mo2_rename(req, parent, name, newparent, newname, flags); }
  MO2_TRY_REPLY(req, "rename", parent, EIO)
}

void wrap_setattr(fuse_req_t req, fuse_ino_t ino, struct stat* attr, int to_set,
                  struct fuse_file_info* fi) noexcept
{
  try { mo2_setattr(req, ino, attr, to_set, fi); }
  MO2_TRY_REPLY(req, "setattr", ino, EIO)
}

void wrap_unlink(fuse_req_t req, fuse_ino_t parent, const char* name) noexcept
{
  try { mo2_unlink(req, parent, name); }
  MO2_TRY_REPLY(req, "unlink", parent, EIO)
}

void wrap_mkdir(fuse_req_t req, fuse_ino_t parent, const char* name, mode_t mode) noexcept
{
  try { mo2_mkdir(req, parent, name, mode); }
  MO2_TRY_REPLY(req, "mkdir", parent, EIO)
}

void wrap_rmdir(fuse_req_t req, fuse_ino_t parent, const char* name) noexcept
{
  try { mo2_rmdir(req, parent, name); }
  MO2_TRY_REPLY(req, "rmdir", parent, EIO)
}

void wrap_release(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi) noexcept
{
  try { mo2_release(req, ino, fi); }
  MO2_TRY_REPLY(req, "release", ino, EIO)
}

void wrap_releasedir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi) noexcept
{
  try { mo2_releasedir(req, ino, fi); }
  MO2_TRY_REPLY(req, "releasedir", ino, EIO)
}

void wrap_forget(fuse_req_t req, fuse_ino_t ino, uint64_t nlookup) noexcept
{
  try { mo2_forget(req, ino, nlookup); }
  catch (...) { fuse_reply_none(req); }
}

void wrap_flush(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi) noexcept
{
  try { mo2_flush(req, ino, fi); }
  MO2_TRY_REPLY(req, "flush", ino, EIO)
}

void wrap_fsync(fuse_req_t req, fuse_ino_t ino, int datasync,
                struct fuse_file_info* fi) noexcept
{
  try { mo2_fsync(req, ino, datasync, fi); }
  MO2_TRY_REPLY(req, "fsync", ino, EIO)
}

void wrap_access(fuse_req_t req, fuse_ino_t ino, int mask) noexcept
{
  try { mo2_access(req, ino, mask); }
  MO2_TRY_REPLY(req, "access", ino, EIO)
}

void wrap_statfs(fuse_req_t req, fuse_ino_t ino) noexcept
{
  try { mo2_statfs(req, ino); }
  MO2_TRY_REPLY(req, "statfs", ino, EIO)
}

void wrap_getlk(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi,
                struct flock* lock) noexcept
{
  try { mo2_getlk(req, ino, fi, lock); }
  MO2_TRY_REPLY(req, "getlk", ino, EIO)
}

void wrap_setlk(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi,
                struct flock* lock, int sleep) noexcept
{
  try { mo2_setlk(req, ino, fi, lock, sleep); }
  MO2_TRY_REPLY(req, "setlk", ino, EIO)
}

void wrap_flock(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi,
                int op) noexcept
{
  try { mo2_flock(req, ino, fi, op); }
  MO2_TRY_REPLY(req, "flock", ino, EIO)
}

void wrap_fallocate(fuse_req_t req, fuse_ino_t ino, int mode, off_t offset,
                    off_t length, struct fuse_file_info* fi) noexcept
{
  try { mo2_fallocate(req, ino, mode, offset, length, fi); }
  MO2_TRY_REPLY(req, "fallocate", ino, EIO)
}

void wrap_copy_file_range(fuse_req_t req, fuse_ino_t ino_in, off_t off_in,
                          struct fuse_file_info* fi_in, fuse_ino_t ino_out,
                          off_t off_out, struct fuse_file_info* fi_out,
                          size_t len, int flags) noexcept
{
  try {
    mo2_copy_file_range(req, ino_in, off_in, fi_in, ino_out, off_out,
                        fi_out, len, flags);
  }
  MO2_TRY_REPLY(req, "copy_file_range", ino_out, EIO)
}

void wrap_lseek(fuse_req_t req, fuse_ino_t ino, off_t off, int whence,
                struct fuse_file_info* fi) noexcept
{
  try { mo2_lseek(req, ino, off, whence, fi); }
  MO2_TRY_REPLY(req, "lseek", ino, EIO)
}

void wrap_getxattr(fuse_req_t req, fuse_ino_t ino, const char* name,
                   size_t size) noexcept
{
  try { mo2_getxattr(req, ino, name, size); }
  MO2_TRY_REPLY(req, "getxattr", ino, EIO)
}

void wrap_listxattr(fuse_req_t req, fuse_ino_t ino, size_t size) noexcept
{
  try { mo2_listxattr(req, ino, size); }
  MO2_TRY_REPLY(req, "listxattr", ino, EIO)
}

void wrap_setxattr(fuse_req_t req, fuse_ino_t ino, const char* name,
                   const char* value, size_t size, int flags) noexcept
{
  try { mo2_setxattr(req, ino, name, value, size, flags); }
  MO2_TRY_REPLY(req, "setxattr", ino, EIO)
}

void wrap_removexattr(fuse_req_t req, fuse_ino_t ino, const char* name) noexcept
{
  try { mo2_removexattr(req, ino, name); }
  MO2_TRY_REPLY(req, "removexattr", ino, EIO)
}

#if FUSE_USE_VERSION < 35
void wrap_ioctl(fuse_req_t req, fuse_ino_t ino, int cmd, void* arg,
                struct fuse_file_info* fi, unsigned flags, const void* in_buf,
                size_t in_bufsz, size_t out_bufsz) noexcept
#else
void wrap_ioctl(fuse_req_t req, fuse_ino_t ino, unsigned int cmd, void* arg,
                struct fuse_file_info* fi, unsigned flags, const void* in_buf,
                size_t in_bufsz, size_t out_bufsz) noexcept
#endif
{
  try { mo2_ioctl(req, ino, cmd, arg, fi, flags, in_buf, in_bufsz, out_bufsz); }
  MO2_TRY_REPLY(req, "ioctl", ino, EIO)
}

#undef MO2_TRY_REPLY

}  // namespace

void setupFuseOps(struct fuse_lowlevel_ops* ops)
{
  std::memset(ops, 0, sizeof(struct fuse_lowlevel_ops));
  ops->init        = wrap_init;
  ops->lookup      = wrap_lookup;
  ops->forget      = wrap_forget;
  ops->getattr     = wrap_getattr;
  ops->opendir     = wrap_opendir;
  ops->readdir     = wrap_readdir;
  ops->readdirplus = wrap_readdirplus;
  ops->open        = wrap_open;
  ops->read        = wrap_read;
  ops->write       = wrap_write;
  ops->create      = wrap_create;
  ops->rename      = wrap_rename;
  ops->setattr     = wrap_setattr;
  ops->unlink      = wrap_unlink;
  ops->mkdir       = wrap_mkdir;
  ops->rmdir       = wrap_rmdir;
  ops->release     = wrap_release;
  ops->releasedir  = wrap_releasedir;
  ops->flush       = wrap_flush;
  ops->fsync       = wrap_fsync;
  ops->access      = wrap_access;
  ops->statfs      = wrap_statfs;
  ops->getlk       = wrap_getlk;
  ops->setlk       = wrap_setlk;
  ops->flock       = wrap_flock;
  ops->fallocate   = wrap_fallocate;
  ops->copy_file_range = wrap_copy_file_range;
  ops->lseek       = wrap_lseek;
  ops->getxattr    = wrap_getxattr;
  ops->listxattr   = wrap_listxattr;
  ops->setxattr    = wrap_setxattr;
  ops->removexattr = wrap_removexattr;
  ops->ioctl       = wrap_ioctl;
}

}  // namespace

FuseConnector::FuseConnector(QObject* parent) : QObject(parent)
{
  log::debug("FUSE connector initialized");

  // Purely descriptive: gives logind an accurate reason to show instead of
  // "systemd (1)" if a suspend/shutdown/lock is attempted while mounted. It
  // does not try to unmount around suspend — see sleepinhibitor.h for why
  // that would be unsafe (the mount's lifetime tracks the running game's).
  m_sleepInhibitor = new SleepInhibitor(this);
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

  // Use the caller-supplied data directory path directly.  Re-computing it
  // as gameDir/dataDirName breaks games where the data directory IS the game
  // directory (e.g. BG3 with GameDataPath=""), because dirName() returns the
  // last path component and appending it produces a non-existent double path.
  m_dataDirPath = mount_point.toStdString();
  m_mountPoint  = m_dataDirPath;

  if (!fs::exists(m_dataDirPath)) {
    throw FuseConnectorException(
        QObject::tr("Game data directory does not exist: %1")
            .arg(QString::fromStdString(m_dataDirPath)));
  }

  tryCleanupStaleMount(QString::fromStdString(m_mountPoint));

  const fs::path overwritePath(m_overwriteDir);
  m_stagingDir = (overwritePath.parent_path() / "VFS_staging").string();

  const fs::path promotionDestination = m_customOutputDir.empty()
                                            ? fs::path(m_overwriteDir)
                                            : fs::path(m_customOutputDir);
  const StagingPromotionResult recovery =
      StagingPromotion::recover(m_stagingDir, promotionDestination);
  if (recovery.blocked()) {
    throw FuseConnectorException(
        QObject::tr("%1\n\nRecovery files: %2")
            .arg(QString::fromStdString(recovery.message),
                 QString::fromStdString(recovery.recovery_path.string())));
  }
  if (recovery.status == StagingPromotionStatus::Recovered) {
    log::info("Recovered and verified {} interrupted VFS promotion(s)",
              recovery.files.size());
  }

  std::error_code ec;
  fs::create_directories(m_stagingDir, ec);
  fs::create_directories(m_overwriteDir, ec);
  if (!m_customOutputDir.empty()) {
    fs::create_directories(m_customOutputDir, ec);
  }

  const auto mountStart = std::chrono::steady_clock::now();

  // Open fd to data dir BEFORE mounting so we can access original files
  m_backingFd = open(m_dataDirPath.c_str(), O_RDONLY | O_DIRECTORY);
  if (m_backingFd < 0) {
    throw FuseConnectorException(
        QObject::tr("Failed to open backing fd for %1")
            .arg(QString::fromStdString(m_dataDirPath)));
  }

  // Reconcile the persistent local catalog before mounting. Unchanged files
  // need only a stat fingerprint; BLAKE3 is recalculated only for drifted
  // files. The returned tree is a complete immutable in-memory generation.
  const auto treeStart = std::chrono::steady_clock::now();
  const fs::path catalogDatabase = VfsCatalog::databasePath(m_dataDirPath);
  VfsCatalog catalog(catalogDatabase);
  std::fprintf(stderr, "[VFS] [catalog] database='%s'\n",
               catalogDatabase.c_str());
  uint64_t lastProgress = 0;
  VfsCatalogProgress finalProgress;
  std::unique_ptr<QProgressDialog> catalogProgress;
  if (qApp != nullptr && QThread::currentThread() == qApp->thread()) {
    catalogProgress = std::make_unique<QProgressDialog>(
        QObject::tr("Checking cached file metadata…"), QObject::tr("Cancel"),
        0, 0, QApplication::activeWindow());
    catalogProgress->setWindowTitle(QObject::tr("Verifying game files"));
    catalogProgress->setMinimumDuration(750);
    catalogProgress->setAutoClose(false);
  }
  auto catalogResult = catalog.reconcileAndBuild(
      m_dataDirPath, mods, m_overwriteDir, true,
      [&lastProgress, &catalogProgress, &finalProgress](const VfsCatalogProgress& p) {
        finalProgress = p;
        if (catalogProgress) {
          if (p.fingerprint_misses == 0) {
            catalogProgress->setWindowTitle(
                QObject::tr("Verifying game files"));
            catalogProgress->setLabelText(
                QObject::tr("Checking cached metadata: %1 files verified…\n%2")
                    .arg(p.files_scanned)
                    .arg(QString::fromStdString(p.current_root)));
          } else {
            catalogProgress->setWindowTitle(
                QObject::tr("Indexing changed game files"));
            catalogProgress->setLabelText(QObject::tr(
                "Verified %1 files; hashed %2 of %3 changed/new files "
                "(%4 MiB at %5 MiB/s)…\n%6")
                .arg(p.files_scanned)
                .arg(p.files_hashed)
                .arg(p.fingerprint_misses)
                .arg(p.bytes_hashed / (1024 * 1024))
                .arg(p.hash_mib_per_second, 0, 'f', 1)
                .arg(QString::fromStdString(p.current_file)));
          }
          QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
          if (catalogProgress->wasCanceled()) {
            throw std::runtime_error("VFS catalog verification cancelled");
          }
        }
        if (p.files_scanned == lastProgress && p.files_scanned != 0) return;
        if ((p.files_scanned % 16384) != 0 && p.files_scanned != 0) return;
        lastProgress = p.files_scanned;
        std::fprintf(stderr,
                     "[VFS] [catalog] scanned=%llu hashed=%llu bytes=%llu root='%s'\n",
                     static_cast<unsigned long long>(p.files_scanned),
                     static_cast<unsigned long long>(p.files_hashed),
                     static_cast<unsigned long long>(p.bytes_hashed),
                     p.current_root.c_str());
      });
  auto tree = std::make_shared<VfsTree>(std::move(catalogResult.tree));
  if (catalogProgress) catalogProgress->close();
  std::fprintf(stderr,
               "[VFS] [catalog] complete scanned=%llu hashed=%llu bytes=%llu "
               "elapsed_ms=%llu mib_s=%.1f hash_workers=%llu "
               "fingerprint_misses=%llu uncached=%llu device=%llu inode=%llu "
               "size=%llu mode=%llu mtime=%llu ctime=%llu no_digest=%llu "
               "roots_changed=%llu "
               "catalog_loaded=%llu catalog_written=%llu catalog_deleted=%llu "
               "merkle_reused=%llu "
               "archives=%llu indexed=%llu reused=%llu members=%llu errors=%llu "
               "membership_cache_hits=%llu membership_cache_bytes=%llu "
               "archive_workers=%llu providers_ms=%llu archives_ms=%llu "
               "duplicates_ms=%llu commit_ms=%llu profile=%s\n",
               static_cast<unsigned long long>(finalProgress.files_scanned),
               static_cast<unsigned long long>(finalProgress.files_hashed),
               static_cast<unsigned long long>(finalProgress.bytes_hashed),
               static_cast<unsigned long long>(finalProgress.elapsed_ms),
               finalProgress.hash_mib_per_second,
               static_cast<unsigned long long>(finalProgress.hash_workers),
               static_cast<unsigned long long>(finalProgress.fingerprint_misses),
               static_cast<unsigned long long>(finalProgress.fingerprint_uncached),
               static_cast<unsigned long long>(
                   finalProgress.fingerprint_device_mismatches),
               static_cast<unsigned long long>(
                   finalProgress.fingerprint_inode_mismatches),
               static_cast<unsigned long long>(
                   finalProgress.fingerprint_size_mismatches),
               static_cast<unsigned long long>(
                   finalProgress.fingerprint_mode_mismatches),
               static_cast<unsigned long long>(
                   finalProgress.fingerprint_mtime_mismatches),
               static_cast<unsigned long long>(
                   finalProgress.fingerprint_ctime_mismatches),
               static_cast<unsigned long long>(
                   finalProgress.fingerprint_missing_digests),
               static_cast<unsigned long long>(finalProgress.provider_roots_changed),
               static_cast<unsigned long long>(finalProgress.catalog_rows_loaded),
               static_cast<unsigned long long>(finalProgress.catalog_rows_written),
               static_cast<unsigned long long>(finalProgress.catalog_rows_deleted),
               static_cast<unsigned long long>(finalProgress.merkle_roots_reused),
               static_cast<unsigned long long>(finalProgress.archives_discovered),
               static_cast<unsigned long long>(finalProgress.archives_indexed),
               static_cast<unsigned long long>(finalProgress.archives_reused),
               static_cast<unsigned long long>(finalProgress.archive_members),
               static_cast<unsigned long long>(finalProgress.archive_errors),
               static_cast<unsigned long long>(
                   finalProgress.archive_membership_cache_hits),
               static_cast<unsigned long long>(
                   finalProgress.archive_membership_cache_bytes),
               static_cast<unsigned long long>(finalProgress.archive_workers),
               static_cast<unsigned long long>(
                   finalProgress.provider_reconcile_ms),
               static_cast<unsigned long long>(
                   finalProgress.archive_reconcile_ms),
               static_cast<unsigned long long>(finalProgress.duplicate_scan_ms),
               static_cast<unsigned long long>(finalProgress.commit_ms),
               digestPrefix(catalogResult.profile_root).c_str());
  m_baseFileCache = catalog.loadBaseSnapshot(m_dataDirPath);
  m_cachedDataDirPath = m_dataDirPath;

  // Inject file-level data-dir mappings (e.g. plugins.txt, loadorder.txt).
  // Always re-applied after load — these are session-scoped, not cached.
  injectExtraFiles(*tree, m_extraVfsFiles);

  // Stamp plugin timestamps to match load order so LOOT sees unambiguous ordering.
  // Also session-scoped; load order can change between launches.
  if (!m_pluginLoadOrder.empty()) {
    stampPluginTimestamps(*tree, m_pluginLoadOrder);
  }

  const auto publicationStart = std::chrono::steady_clock::now();
  const VfsIndexPublicationResult publication =
      publishIndex(*tree, catalogResult);
  const auto publicationMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - publicationStart)
          .count();
  std::fprintf(
      stderr,
      "[VFS] index publication success=%d reused=%d files=%zu elapsed_ms=%lld\n",
      publication.success ? 1 : 0,
      publication.reused_existing ? 1 : 0,
      publication.file_count,
      static_cast<long long>(publicationMs));

  {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - treeStart).count();
    std::fprintf(stderr, "[VFS] built tree (%zu files, %zu dirs) in %lldms (%s)\n",
                 tree->file_count, tree->dir_count,
                 static_cast<long long>(ms),
                 "catalog");
  }

  // Load tracked writes (files user moved from Overwrite to a mod)
  m_trackedWrites = std::make_shared<TrackedWrites>();
  std::fprintf(stderr, "[VFS] tracking file path: '%s' (overwrite: '%s', %zu mods)\n",
               m_trackingFilePath.c_str(), m_overwriteDir.c_str(), mods.size());
  if (!m_trackingFilePath.empty()) {
    const bool existed = fs::exists(m_trackingFilePath);
    std::fprintf(stderr, "[VFS] tracking file %s\n", existed ? "exists" : "does NOT exist (first run)");
    m_trackedWrites->load(m_trackingFilePath);
    if (existed) {
      m_trackedWrites->detectManualMoves(m_overwriteDir, mods);
    }
    // NOTE: initialScan() is no longer called on first run.  Its heuristic
    // ("file exists in both overwrite and a mod → track it") produces false
    // positives: game-generated overwrite files that happen to share a name
    // with a mod file get incorrectly tracked.  Tracking now only happens
    // through explicit user actions (UI move/sync/drag-drop) or the
    // snapshot-based detectManualMoves().
    const auto& duplicates = catalogResult.overwrite_duplicates;
    if (!duplicates.empty()) {
      size_t identical = 0;
      size_t different = 0;
      for (const auto& dup : duplicates) {
        if (dup.state == VfsDuplicateState::Identical) {
          ++identical;
        } else {
          ++different;
        }
      }

      std::fprintf(stderr,
                   "[VFS] overwrite duplicate scan: %zu matches (%zu identical, %zu different)\n",
                   duplicates.size(), identical, different);
      const size_t preview = std::min<size_t>(duplicates.size(), 10);
      for (size_t i = 0; i < preview; ++i) {
        const auto& dup = duplicates[i];
        std::fprintf(stderr,
                     "[VFS]   %s -> %s (%s)\n",
                     dup.relative_path.c_str(), dup.mod_name.c_str(),
                     dup.state == VfsDuplicateState::Identical ? "identical"
                                                               : "different");
      }
    } else {
      std::fprintf(stderr, "[VFS] overwrite duplicate scan: no exact path matches\n");
    }

    m_trackedWrites->save(m_trackingFilePath);
  } else {
    std::fprintf(stderr, "[VFS] WARNING: tracking file path is empty!\n");
  }

  m_context                        = std::make_shared<Mo2FsContext>();
  m_context->tree                  = tree;
  m_context->archive_members       = catalogResult.archive_member_index;
  m_context->inodes                = std::make_unique<InodeTable>();
  m_context->overwrite             = std::make_unique<OverwriteManager>(m_stagingDir, m_overwriteDir);
  m_context->tracked_writes        = m_trackedWrites;
  m_context->backing_dir_fd        = m_backingFd;
  m_context->default_catalog_root  = promotionDestination.string();
  m_context->catalog_providers.reserve(mods.size() + 1);
  for (const auto& [name, path] : mods) {
    m_context->catalog_providers.emplace_back(path, name);
  }
  m_context->catalog_providers.emplace_back(promotionDestination.string(),
                                             "Overwrite");
  if (promotionDestination != fs::path(m_overwriteDir)) {
    m_context->catalog_providers.emplace_back(m_overwriteDir, "Overwrite");
  }
  m_context->uid                   = ::getuid();
  m_context->gid                   = ::getgid();
  m_context->cache_disabled        = m_disableVfsCache ||
                                     std::getenv("FLUORINE_VFS_DISABLE_CACHE") != nullptr;
  m_context->disable_no_opendir    =
      std::getenv("FLUORINE_VFS_DISABLE_NO_OPENDIR") != nullptr;
  m_context->readdirplus_enabled   =
      std::getenv("FLUORINE_VFS_ENABLE_READDIRPLUS") != nullptr;
  m_context->zero_file_flags       =
      std::getenv("FLUORINE_VFS_ZERO_FILE_FLAGS") != nullptr;
  m_context->auto_create_dirs      = m_autoCreateDirs;
  const auto indexStart = std::chrono::steady_clock::now();
  const std::size_t prewarmed = mo2PrewarmLookupIndex(m_context.get());
  const auto indexMs = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - indexStart).count();
  std::fprintf(stderr,
               "[VFS] built immutable runtime index (%zu lookup entries) in %lldms\n",
               prewarmed, static_cast<long long>(indexMs));
  // NOTE: Do NOT include mount_point here — low-level API passes it
  // separately to fuse_session_mount(). Including it here causes
  // "fuse: unknown option(s)" error.
  //
  // max_read=1MB: raise per-read-request cap from default 128KB.  Must
  // match conn->max_read set in mo2_init() or libfuse rejects the mount
  // with a max-read-mismatch error.  Going higher than 1MB triggers
  // "fuse: reading device: Invalid argument" on some kernels where
  // libfuse's receive buffer is sized off max_write + header and the
  // kernel reads don't fit.  1MB is the safe ceiling.
  std::vector<std::string> argvStorage = {
      "mo2fuse", "-o", "fsname=mo2linux", "-o", "noatime",
      "-o", "max_read=1048576"};
  std::fprintf(stderr, "[VFS] libfuse=%s headers=%d.%d\n",
               fuse_pkgversion(), FUSE_MAJOR_VERSION, FUSE_MINOR_VERSION);

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
  m_context->session = m_session;

  if (fuse_session_mount(m_session, m_mountPoint.c_str()) != 0) {
    fuse_session_destroy(m_session);
    m_session = nullptr;
    close(m_backingFd);
    m_backingFd = -1;
    throw FuseConnectorException(
        QObject::tr("Failed to mount FUSE at %1")
            .arg(QString::fromStdString(m_mountPoint)));
  }

  // Reverse inode invalidation may wait for a kernel folio currently owned by
  // an in-flight FUSE write. Keep it off the finite request-worker pool so the
  // request can reply and release that folio.
  m_invalidationThread = std::thread([context = m_context]() {
    mo2RunKernelInvalidations(context.get());
  });

  m_fuseThread = std::thread([this]() {
    // Enable clone_fd: each worker thread gets its own /dev/fuse fd,
    // eliminating contention on a single fd lock under heavy parallel I/O.
    struct fuse_loop_config* cfg = fuse_loop_cfg_create();
    fuse_loop_cfg_set_clone_fd(cfg, 1);
    fuse_loop_cfg_set_max_threads(cfg, 16);
    fuse_session_loop_mt(m_session, cfg);
    fuse_loop_cfg_destroy(cfg);
  });

  m_mounted = true;
  setFuseMountPointForCrashCleanup(m_mountPoint.c_str());
  if (m_sleepInhibitor != nullptr) {
    const QString reason =
        QStringLiteral("Fluorine: mod filesystem active for %1")
            .arg(QFileInfo(QString::fromStdString(m_gameDir)).fileName());
    m_sleepInhibitor->setActive(true, reason);
  }
  {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - mountStart).count();
    std::fprintf(stderr, "[VFS] mounted on '%s' in %lldms total\n",
                 m_mountPoint.c_str(), static_cast<long long>(ms));
  }
  return true;
}

void FuseConnector::unmount()
{
  const auto clearBaseSnapshot = [this]() {
    // The base snapshot is only a mount-time compatibility cache. It is not
    // consulted by the live rebuild path, so retaining hundreds of thousands
    // of cached path strings after unmount needlessly keeps their storage
    // owned by FuseConnector across idle sessions.
    std::vector<CachedBaseFile>().swap(m_baseFileCache);
    m_cachedDataDirPath.clear();
  };

  if (!m_mounted) {
    clearBaseSnapshot();
    cleanupExternalMappings();
    clearIndexRootLocator();
    if (m_rootBuilderEnabled) {
      clearRootFiles();
    }
    return;
  }

  const auto unmountStart = std::chrono::steady_clock::now();

  if (m_session != nullptr) {
    fuse_session_exit(m_session);
    fuse_session_unmount(m_session);
  }

  if (m_fuseThread.joinable()) {
    m_fuseThread.join();
  }

  if (m_context != nullptr) {
    mo2StopKernelInvalidations(m_context.get());
  }
  if (m_invalidationThread.joinable()) {
    m_invalidationThread.join();
  }

  if (m_session != nullptr) {
    if (m_context != nullptr) {
      m_context->session = nullptr;
    }
    fuse_session_destroy(m_session);
    m_session = nullptr;
  }

  // A forced/session-driven unmount is not guaranteed to deliver release()
  // for every kernel-visible file handle. All request and invalidation workers
  // have stopped, so close any retained descriptors before staging promotion
  // inspects or moves their files.
  if (m_context != nullptr) {
    const Mo2OpenFileCleanupResult cleanup =
        mo2CloseOpenFiles(m_context.get());
    std::fprintf(
        stderr,
        "[VFS] closed remaining open handles logical=%zu descriptors=%zu "
        "writable=%zu close_errors=%zu\n",
        cleanup.logical_handles, cleanup.descriptors_closed,
        cleanup.writable_descriptors_closed, cleanup.close_errors);
    if (cleanup.close_errors != 0) {
      log::warn("Failed to close {} of {} remaining VFS file descriptors",
                cleanup.close_errors, cleanup.descriptors_closed);
    }
  }

  std::unordered_map<std::string, std::unordered_set<std::string>> dirtyProviderPaths;
  if (m_context != nullptr) {
    std::scoped_lock const lock(m_context->dirty_paths_mutex);
    dirtyProviderPaths = m_context->dirty_provider_paths;
  }
  std::vector<std::string> promotedPaths;

  {
    const auto t0 = std::chrono::steady_clock::now();
    if (m_discardStaging) {
      // Discard all COW'd files instead of moving them to overwrite.
      log::info("Discarding staging directory (discard flag set)");
      std::error_code ec;
      fs::remove_all(m_stagingDir, ec);
      m_discardStaging = false;
    } else {
      try {
        const StagingPromotionResult promotion = flushStaging();
        promotedPaths.reserve(promotion.files.size());
        for (const StagingPromotedFile& file : promotion.files) {
          promotedPaths.push_back(file.relative_path);
        }
        if (promotion.blocked()) {
          log::error("VFS staging promotion blocked: {} (recovery: {})",
                     QString::fromStdString(promotion.message),
                     QString::fromStdString(promotion.recovery_path.string()));
        }
      } catch (const std::exception& error) {
        // Leave the durable journal and staged data in place.  The next mount
        // will replay or preserve it before exposing the VFS again.
        log::error("VFS staging promotion failed; it will be recovered on the next launch: {}",
                   error.what());
      }
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    std::fprintf(stderr, "[VFS] flushed staging in %lldms\n",
                 static_cast<long long>(ms));
  }

  const std::string promotionRoot = m_customOutputDir.empty()
                                        ? m_overwriteDir
                                        : m_customOutputDir;
  if (auto provider = dirtyProviderPaths.find(promotionRoot);
      provider != dirtyProviderPaths.end()) {
    for (const std::string& path : promotedPaths) provider->second.erase(path);
    if (provider->second.empty()) dirtyProviderPaths.erase(provider);
  }

  // Writes to tracked output mods can bypass VFS_staging, and removals have no
  // promoted file to verify. Force-refresh exactly those provider/path pairs
  // after all FUSE handles are closed.
  if (!dirtyProviderPaths.empty()) {
    VfsCatalog catalog(VfsCatalog::databasePath(m_dataDirPath));
    for (const auto& [root, paths] : dirtyProviderPaths) {
      std::string origin = root == (m_customOutputDir.empty()
                                        ? m_overwriteDir
                                        : m_customOutputDir)
                               ? "Overwrite"
                               : root;
      if (m_context != nullptr) {
        for (const auto& [providerRoot, providerOrigin] :
             m_context->catalog_providers) {
          if (providerRoot == root) {
            origin = providerOrigin;
            break;
          }
        }
      }
      const std::vector<std::string> relativePaths(paths.begin(), paths.end());
      try {
        catalog.forceRefreshProviderFiles(root, origin, false, relativePaths);
      } catch (const std::exception& error) {
        catalog.invalidateProviderFiles(root, relativePaths);
        log::error("Unable to force-refresh {} dirty catalog path(s) under '{}': {}",
                   relativePaths.size(), QString::fromStdString(root), error.what());
      }
    }
  }

  // Snapshot overwrite contents for next session's manual-move detection,
  // then save tracking data.
  //
  // NOTE: initialScan() is intentionally NOT called here.  It was previously
  // run at every unmount, which caused false-positive tracking: any file that
  // exists in both overwrite and a mod got auto-tracked, even if the overwrite
  // copy was a game-generated modification (not a user move).  Tracking should
  // only happen through explicit user actions (UI move/sync) or the
  // snapshot-based detectManualMoves() at mount time.
  if (m_trackedWrites && !m_trackingFilePath.empty()) {
    m_trackedWrites->snapshotOverwrite(m_overwriteDir);
    m_trackedWrites->save(m_trackingFilePath);
  }

  if (m_backingFd >= 0) {
    close(m_backingFd);
    m_backingFd = -1;
  }

  m_context.reset();
  clearBaseSnapshot();
  m_mounted = false;
  setFuseMountPointForCrashCleanup(nullptr);
  if (m_sleepInhibitor != nullptr) {
    m_sleepInhibitor->setActive(false, {});
  }

  // Clean up symlinks created for non-data-dir mappings.
  cleanupExternalMappings();

  // The game-root index locator is session-scoped. The immutable database and
  // instance locator remain available for the next publication.
  clearIndexRootLocator();

  // VFS Root Builder: remove deployed root files and restore backups.
  if (m_rootBuilderEnabled) {
    clearRootFiles();
  }

  {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - unmountStart).count();
    std::fprintf(stderr, "[VFS] unmounted from '%s' in %lldms total\n",
                 m_mountPoint.c_str(), static_cast<long long>(ms));
  }
}

bool FuseConnector::isMounted() const
{
  return m_mounted;
}

void FuseConnector::discardStagingOnUnmount()
{
  m_discardStaging = true;
}

void FuseConnector::setPluginLoadOrder(const std::vector<std::string>& load_order)
{
  m_pluginLoadOrder = load_order;
}

void FuseConnector::setTrackingFilePath(const std::string& path)
{
  m_trackingFilePath = path;
  std::fprintf(stderr, "[VFS] setTrackingFilePath: '%s'\n", path.c_str());
}

void FuseConnector::setIndexPublicationContext(
    VfsIndexPublicationContext context)
{
  m_indexPublicationContext = std::move(context);
}

std::shared_ptr<TrackedWrites> FuseConnector::trackedWrites() const
{
  return m_trackedWrites;
}

VfsIndexPublicationResult FuseConnector::publishIndex(
    VfsTree& tree, const VfsCatalogResult& catalogResult)
{
  // Remove a previous session's root locator before producing a replacement.
  // The mapped Data locator is injected into the new tree only on success.
  clearIndexRootLocator();

  VfsIndexPublisher publisher;
  if (m_indexPublicationContext.output_base.empty()) {
    VfsIndexPublisher::removePublicationArtifacts(tree);
    return {.success=false,
            .error="index publication context is not configured"};
  }

  VfsIndexPublicationResult publication = publisher.publish(
      tree, catalogResult.provider_roots, catalogResult.profile_root,
      fs::path(m_dataDirPath), m_indexPublicationContext,
      catalogResult.archive_member_index);
  if (!publication.success) {
    log::warn("VFS index publication failed; continuing without an index "
              "locator: {}",
              publication.error);
    return publication;
  }
  if (catalogResult.archive_member_index &&
      !catalogResult.archive_member_index->complete()) {
    log::warn("VFS index archive proof omitted because one or more visible "
              "archives could not be cataloged; guarded consumer "
              "canonicalization will remain disabled");
  }

  injectExtraFiles(
      tree, {{kVfsIndexVirtualLocator, publication.locator_path.string()}});
  if (!deployIndexRootLocator(publication)) {
    log::warn("VFS index generation {} is available through Data, but the "
              "temporary game-root locator could not be deployed",
              publication.generation);
  }
  log::info("{} VFS index generation {} with {} resolved files",
            publication.reused_existing ? "Reused" : "Published",
            publication.generation, publication.file_count);
  return publication;
}

bool FuseConnector::deployIndexRootLocator(
    VfsIndexPublicationResult& publication)
{
  if (m_gameDir.empty() || m_indexPublicationContext.output_base.empty() ||
      publication.locator_path.empty()) {
    return false;
  }

  namespace fs = std::filesystem;
  const fs::path storage =
      m_indexPublicationContext.output_base / ".vfs-indexer";
  const fs::path manifest = storage / "root-deployment.json";
  const fs::path target = fs::path(m_gameDir) / kVfsIndexLocatorName;
  if (target.lexically_normal() ==
      publication.locator_path.lexically_normal()) {
    // A portable instance may already publish directly into the game root.
    // That persistent instance locator must not be treated as a temporary
    // deployment and removed on unmount.
    publication.root_locator_path = target;
    publication.root_locator_deployed = true;
    return true;
  }
  const fs::path backup =
      storage / ("root-locator-backup-" + publication.generation);
  const fs::path temporary =
      target.parent_path() /
      (std::string(".vfs-index-") + publication.generation + ".tmp");
  std::error_code error;

  try {
    fs::create_directories(storage, error);
    if (error) throw std::runtime_error(error.message());

    bool hadBackup = false;
    if (fs::is_regular_file(target, error)) {
      error.clear();
      fs::copy_file(target, backup, fs::copy_options::overwrite_existing,
                    error);
      if (error) throw std::runtime_error(error.message());
      hadBackup = true;
    }

    const auto saveManifest = [&](const QString& state) {
      QJsonObject json;
      json.insert(QStringLiteral("state"), state);
      json.insert(QStringLiteral("generation"),
                  QString::fromStdString(publication.generation));
      json.insert(QStringLiteral("target"),
                  QString::fromStdString(target.string()));
      json.insert(QStringLiteral("source"),
                  QString::fromStdString(publication.locator_path.string()));
      json.insert(QStringLiteral("temporary"),
                  QString::fromStdString(temporary.string()));
      json.insert(QStringLiteral("backup"),
                  QString::fromStdString(backup.string()));
      json.insert(QStringLiteral("had_backup"), hadBackup);
      QSaveFile output(QString::fromStdString(manifest.string()));
      if (!output.open(QIODevice::WriteOnly) ||
          output.write(QJsonDocument(json).toJson(QJsonDocument::Compact)) < 0 ||
          !output.commit()) {
        throw std::runtime_error("unable to save root locator manifest");
      }
    };

    saveManifest(QStringLiteral("deploying"));
    fs::copy_file(publication.locator_path, temporary,
                  fs::copy_options::overwrite_existing, error);
    if (error) throw std::runtime_error(error.message());
    if (fs::exists(target, error) || fs::is_symlink(target, error)) {
      error.clear();
      fs::remove(target, error);
      if (error) throw std::runtime_error(error.message());
    }
    fs::rename(temporary, target, error);
    if (error) throw std::runtime_error(error.message());
    saveManifest(QStringLiteral("complete"));

    publication.root_locator_path = target;
    publication.root_locator_deployed = true;
    return true;
  } catch (const std::exception& exception) {
    log::warn("Unable to deploy VFS index root locator '{}': {}",
              QString::fromStdString(target.string()), exception.what());
    clearIndexRootLocator();
    return false;
  }
}

void FuseConnector::clearIndexRootLocator()
{
  if (m_indexPublicationContext.output_base.empty()) return;

  namespace fs = std::filesystem;
  const fs::path storage =
      m_indexPublicationContext.output_base / ".vfs-indexer";
  const fs::path manifest = storage / "root-deployment.json";
  QFile input(QString::fromStdString(manifest.string()));
  if (!input.open(QIODevice::ReadOnly)) return;
  const QJsonDocument document = QJsonDocument::fromJson(input.readAll());
  input.close();
  if (!document.isObject()) {
    log::warn("Leaving malformed VFS index root deployment manifest at '{}'",
              QString::fromStdString(manifest.string()));
    return;
  }

  const QJsonObject json = document.object();
  const QString state = json.value(QStringLiteral("state")).toString();
  const fs::path target =
      json.value(QStringLiteral("target")).toString().toStdString();
  const fs::path source =
      json.value(QStringLiteral("source")).toString().toStdString();
  const fs::path temporary =
      json.value(QStringLiteral("temporary")).toString().toStdString();
  const fs::path backup =
      json.value(QStringLiteral("backup")).toString().toStdString();
  const bool hadBackup =
      json.value(QStringLiteral("had_backup")).toBool(false);

  std::error_code error;
  fs::remove(temporary, error);
  error.clear();

  const auto filesEqual = [](const fs::path& leftPath,
                             const fs::path& rightPath) {
    std::error_code compareError;
    if (!fs::is_regular_file(leftPath, compareError) ||
        !fs::is_regular_file(rightPath, compareError) ||
        fs::file_size(leftPath, compareError) !=
            fs::file_size(rightPath, compareError) ||
        compareError) {
      return false;
    }
    std::ifstream left(leftPath, std::ios::binary);
    std::ifstream right(rightPath, std::ios::binary);
    return std::equal(std::istreambuf_iterator<char>(left),
                      std::istreambuf_iterator<char>(),
                      std::istreambuf_iterator<char>(right));
  };

  // A failure before replacement leaves the original target and its byte-for-
  // byte backup in place. This is safe to collapse without mistaking the
  // original file for a user modification made during a completed session.
  if (state == QStringLiteral("deploying") && hadBackup &&
      filesEqual(target, backup)) {
    fs::remove(backup, error);
    fs::remove(manifest, error);
    return;
  }

  bool generatedUnchanged = !fs::exists(target, error);
  if (!generatedUnchanged && fs::is_regular_file(target, error) &&
      fs::is_regular_file(source, error)) {
    generatedUnchanged = filesEqual(target, source);
  }

  if (!generatedUnchanged) {
    log::warn("VFS index root locator '{}' changed during the session; "
              "leaving it and its recoverable backup untouched",
              QString::fromStdString(target.string()));
    return;
  }

  fs::remove(target, error);
  error.clear();
  if (hadBackup && fs::exists(backup, error)) {
    error.clear();
    fs::rename(backup, target, error);
    if (error) {
      log::warn("Unable to restore root locator backup '{}' to '{}': {}",
                QString::fromStdString(backup.string()),
                QString::fromStdString(target.string()), error.message());
      return;
    }
  } else {
    fs::remove(backup, error);
  }
  fs::remove(manifest, error);
}

void FuseConnector::rebuild(
    const std::vector<std::pair<std::string, std::string>>& mods,
    const QString& overwrite_dir, const QString& data_dir_name)
{
  if (!m_mounted) {
    return;
  }

  m_overwriteDir = overwrite_dir.toStdString();
  m_dataDirName  = data_dir_name.toStdString();
  m_lastMods     = mods;

  if (m_context == nullptr) {
    return;
  }

  VfsCatalog catalog(VfsCatalog::databasePath(m_dataDirPath));
  auto catalogResult = catalog.reconcileAndBuild(
      m_dataDirPath, mods, m_overwriteDir, false);
  auto newTree = std::make_shared<VfsTree>(std::move(catalogResult.tree));

  // Inject file-level data-dir mappings (e.g. plugins.txt, loadorder.txt)
  injectExtraFiles(*newTree, m_extraVfsFiles);

  // Stamp plugin timestamps to match load order
  if (!m_pluginLoadOrder.empty()) {
    stampPluginTimestamps(*newTree, m_pluginLoadOrder);
  }

  publishIndex(*newTree, catalogResult);

  std::scoped_lock const namespaceLock(m_context->namespace_mutation_mutex);
  std::shared_ptr<VfsRuntimeIndex> newRuntimeIndex;
  {
    std::unique_lock const lock(m_context->inode_mutex);
    newRuntimeIndex = VfsRuntimeIndex::build(
        *newTree, *m_context->inodes, m_context->uid, m_context->gid);
  }
  {
    std::unique_lock const treeLock(m_context->tree_mutex);
    std::unique_lock const indexLock(m_context->runtime_index_mutex);
    m_context->tree.swap(newTree);
    m_context->runtime_index.swap(newRuntimeIndex);
    m_context->archive_members = catalogResult.archive_member_index;
  }
  {
    std::scoped_lock const lock(m_context->open_dirs_mutex);
    m_context->open_dirs.clear();
  }
  {
    std::scoped_lock const lock(m_context->dir_cache_mutex);
    m_context->dir_cache.clear();
    m_context->readdir_blob_cache.clear();
    m_context->readdirplus_blob_cache.clear();
  }
  {
    std::scoped_lock const lock(m_context->lookup_cache_mutex);
    m_context->lookup_cache.clear();
  }
  {
    std::scoped_lock const lock(m_context->attr_cache_mutex);
    m_context->attr_cache.clear();
  }
  {
    std::scoped_lock const lock(m_context->node_cache_mutex);
    m_context->node_cache.clear();
  }
}

void FuseConnector::updateMapping(const MappingType& mapping)
{
  const auto updateStart = std::chrono::steady_clock::now();
  auto* game = qApp->property("managed_game").value<MOBase::IPluginGame*>();
  if (game == nullptr) {
    throw FuseConnectorException(QObject::tr("Managed game not available"));
  }

  // Propagate the game's auto-create-dirs preference to the VFS context.
  // Starfield sets this to true; most other games (e.g. BG3) leave it false.
  m_autoCreateDirs = game->needsAutoCreateDirectories();
  if (m_context) {
    m_context->auto_create_dirs = m_autoCreateDirs;
  }

  const QString gameDir      = game->gameDirectory().absolutePath();
  const QString dataDirPath  = game->dataDirectory().absolutePath();
  const QString dataDirName  = game->dataDirectory().dirName();
  const QString overwriteDir = Settings::instance().paths().overwrite();

  // Set m_gameDir early so deployRootFiles() can use it before mount().
  m_gameDir = gameDir.toStdString();

  // Auto-derive tracking file path if not explicitly set
  if (m_trackingFilePath.empty() && !overwriteDir.isEmpty()) {
    QDir const owDir(overwriteDir);
    QString const trackPath = QDir::cleanPath(owDir.absoluteFilePath("../tracked_writes.json"));
    m_trackingFilePath = trackPath.toStdString();
    std::fprintf(stderr, "[VFS] auto-derived tracking path: '%s'\n",
                 m_trackingFilePath.c_str());
  }

  auto mods = buildModsFromMapping(mapping, dataDirPath, overwriteDir);

  // Check if any data-dir mapping has createTarget set — that directory
  // should receive newly created files instead of the overwrite directory.
  // Only consider mappings that target the data directory; non-data-dir
  // mappings (e.g. profile-specific saves → __MO_Saves in My Games) are
  // deployed as real symlinks and should NOT redirect VFS staging output.
  m_customOutputDir.clear();
  {
    const QString cleanDataDir = QDir::cleanPath(dataDirPath);
    const QString dataPrefix   = cleanDataDir + QStringLiteral("/");
    for (const auto& map : mapping) {
      if (!map.createTarget || !map.isDirectory) {
        continue;
      }
      const QString dst =
          QDir::cleanPath(QDir::fromNativeSeparators(map.destination));
      const bool targetsDataDir =
          (dst == cleanDataDir || dst.startsWith(dataPrefix));
      log::debug("Found createTarget mapping: source='{}', dest='{}', "
                 "targetsDataDir={}",
                 map.source, map.destination, targetsDataDir);
      if (targetsDataDir) {
        m_customOutputDir =
            QDir::cleanPath(QDir::fromNativeSeparators(map.source)).toStdString();
        log::debug("Custom output directory set to: {}",
                   QString::fromStdString(m_customOutputDir));
        break;
      }
    }
  }
  if (m_customOutputDir.empty()) {
    log::debug("No data-dir createTarget mapping found, using overwrite dir");
  }

  // Deploy non-data-dir mappings as real symlinks and collect file-level
  // data-dir mappings for VFS tree injection.
  {
    const auto t0 = std::chrono::steady_clock::now();
    deployExternalMappings(mapping, dataDirPath);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    std::fprintf(stderr, "[VFS] deployed external mappings (%zu symlinks, %zu extra files) "
                 "in %lldms\n",
                 m_externalSymlinks.size(), m_extraVfsFiles.size(),
                 static_cast<long long>(ms));
  }

  // VFS Root Builder: deploy Root/ files to game dir BEFORE mounting.
  // This ensures files deployed under Data/ are included in the base file scan.
  if (m_rootBuilderEnabled) {
    deployRootFiles(mods);
    m_baseFileCache.clear();  // force rescan to include root-deployed Data/ files
  }

  if (!m_mounted) {
    mount(dataDirPath, overwriteDir, gameDir, dataDirName, mods);
  } else {
    rebuild(mods, overwriteDir, dataDirName);
  }

  {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - updateStart).count();
    std::fprintf(stderr, "[VFS] updateMapping completed in %lldms total\n",
                 static_cast<long long>(ms));
  }
}

void FuseConnector::deployExternalMappings(const MappingType& mapping,
                                            const QString& dataDir)
{
  cleanupExternalMappings();
  m_extraVfsFiles.clear();

  const QString cleanDataDir = QDir::cleanPath(dataDir);
  const QString dataPrefix   = cleanDataDir + QStringLiteral("/");

  // Helper: create a directory (and all missing parents) and record each
  // segment we actually created so cleanup can remove it later.
  std::error_code ec;
  auto createTrackedDirs = [&](const fs::path& dirPath) {
    std::vector<fs::path> toCreate;
    for (fs::path p = dirPath; !p.empty() && !fs::exists(p, ec);
         p = p.parent_path()) {
      toCreate.push_back(p);
      if (p == p.root_path()) {
        break;
      }
    }
    // Build top-down so nested dirs succeed.
    for (auto it = toCreate.rbegin(); it != toCreate.rend(); ++it) {
      if (fs::create_directory(*it, ec) && !ec) {
        m_externalDirs.push_back(it->string());
      }
    }
  };

  // --- Pass 1: createTarget directory mappings must be processed first ---
  // If a per-file mapping (e.g. a mod .pak) creates entries in the
  // destination before the directory symlink is established, the
  // directory symlink check sees real content and falls back to
  // per-file symlinks.  Processing the directory mapping first avoids
  // this: the symlink is created while the destination is still empty,
  // then file mappings create their symlinks through the directory
  // symlink into the overwrite directory.
  for (const auto& map : mapping) {
    const QString src =
        QDir::cleanPath(QDir::fromNativeSeparators(map.source));
    const QString dst =
        QDir::cleanPath(QDir::fromNativeSeparators(map.destination));

    const bool targetsDataDir =
        (dst == cleanDataDir || dst.startsWith(dataPrefix));

    if (targetsDataDir) {
      continue;
    }

    if (!map.isDirectory || !map.createTarget) {
      continue;
    }

    const fs::path srcPath(src.toStdString());
    const fs::path dstPath(dst.toStdString());

    if (!fs::exists(srcPath, ec)) {
      fs::create_directories(srcPath, ec);
      if (ec) {
        ec.clear();
      }
    }
    const bool dstExists  = fs::exists(dstPath, ec);
    ec.clear();
    const bool dstIsLink  = dstExists && fs::is_symlink(dstPath, ec);
    ec.clear();
    const bool dstIsEmpty = dstExists && fs::is_directory(dstPath, ec) &&
                            fs::is_empty(dstPath, ec);
    ec.clear();

    if (!dstExists || dstIsLink || dstIsEmpty) {
      createTrackedDirs(dstPath.parent_path());
      if (dstIsLink) {
        fs::remove(dstPath, ec);
        ec.clear();
      } else if (dstIsEmpty) {
        fs::remove(dstPath, ec);
        ec.clear();
      }
      fs::create_directory_symlink(srcPath, dstPath, ec);
      if (!ec) {
        m_externalSymlinks.push_back(dstPath.string());
        log::debug("Deployed directory symlink {} -> {}", dst, src);
        continue;
      }
      log::warn("Failed to symlink directory {} -> {}: {}", dst, src,
                QString::fromStdString(ec.message()));
      ec.clear();
    } else {
      log::warn(
          "Mapped folder {} contains real files; falling back to per-file "
          "symlinks. Move existing contents into {} and restart to fully "
          "redirect new writes.",
          dst, src);
    }
  }

  // --- Pass 2: everything else (file mappings, non-createTarget dirs) ---
  for (const auto& map : mapping) {
    const QString src =
        QDir::cleanPath(QDir::fromNativeSeparators(map.source));
    const QString dst =
        QDir::cleanPath(QDir::fromNativeSeparators(map.destination));

    const bool targetsDataDir =
        (dst == cleanDataDir || dst.startsWith(dataPrefix));

    if (targetsDataDir) {
      if (!map.isDirectory) {
        // File-level mapping INTO the data directory (e.g. plugins.txt).
        // FUSE sits on top, so we cannot create a physical symlink there.
        // Record it for injection into the VFS tree instead.
        const QString relPath = dst.startsWith(dataPrefix)
                                    ? dst.mid(dataPrefix.length())
                                    : QFileInfo(src).fileName();
        m_extraVfsFiles.emplace_back(relPath.toStdString(), src.toStdString());
      }
      // Directory-level data-dir mappings are handled by the FUSE VFS.
      continue;
    }

    // Skip createTarget directory mappings — already handled in pass 1.
    if (map.isDirectory && map.createTarget) {
      continue;
    }

    if (map.isDirectory) {
      const fs::path srcPath(src.toStdString());
      const fs::path dstPath(dst.toStdString());

      if (!fs::exists(srcPath, ec)) {
        continue;
      }

      for (auto it = fs::recursive_directory_iterator(
               srcPath, fs::directory_options::skip_permission_denied);
           it != fs::recursive_directory_iterator(); ++it) {
        const auto& entry = *it;
        const fs::path rel = fs::relative(entry.path(), srcPath, ec);
        if (ec || rel.empty()) {
          continue;
        }

        const fs::path destPath = fs::path(dst.toStdString()) / rel;
        if (entry.is_directory(ec)) {
          createTrackedDirs(destPath);
        } else if (entry.is_regular_file(ec) || entry.is_symlink(ec)) {
          createTrackedDirs(destPath.parent_path());
          if (fs::exists(destPath, ec) && !fs::is_symlink(destPath, ec)) {
            // Never overwrite real game files — only replace our own symlinks.
            continue;
          }
          if (fs::is_symlink(destPath, ec)) {
            fs::remove(destPath, ec);
          }
          fs::create_symlink(entry.path(), destPath, ec);
          if (!ec) {
            m_externalSymlinks.push_back(destPath.string());
          } else {
            log::warn("Failed to symlink {} -> {}: {}",
                      QString::fromStdString(destPath.string()),
                      QString::fromStdString(entry.path().string()),
                      QString::fromStdString(ec.message()));
          }
        }
      }
    } else {
      // Single file symlink.
      const fs::path destPath(dst.toStdString());
      createTrackedDirs(destPath.parent_path());
      if (fs::exists(destPath, ec) && !fs::is_symlink(destPath, ec)) {
        continue;
      }
      if (fs::is_symlink(destPath, ec)) {
        fs::remove(destPath, ec);
      }
      fs::create_symlink(fs::path(src.toStdString()), destPath, ec);
      if (!ec) {
        m_externalSymlinks.push_back(destPath.string());
      } else {
        log::warn("Failed to symlink {} -> {}: {}", dst, src,
                  QString::fromStdString(ec.message()));
      }
    }
  }

  if (!m_externalSymlinks.empty()) {
    log::debug("Deployed {} external symlinks for non-data-dir mappings",
               m_externalSymlinks.size());
  }
  if (!m_extraVfsFiles.empty()) {
    log::debug("Collected {} extra file mappings for VFS injection",
               m_extraVfsFiles.size());
  }
}

void FuseConnector::cleanupExternalMappings()
{
  if (m_externalSymlinks.empty() && m_externalDirs.empty()) {
    return;
  }

  std::error_code ec;
  for (const auto& path : m_externalSymlinks) {
    if (fs::is_symlink(path, ec)) {
      fs::remove(path, ec);
    }
  }

  // Remove created dirs deepest-first so children are gone before parents.
  // Only removes if empty — any user-placed file inside causes the dir (and
  // its ancestors) to stay, which is the safe behavior.
  std::sort(m_externalDirs.begin(), m_externalDirs.end(),
            [](const std::string& a, const std::string& b) {
              return a.size() > b.size();
            });
  std::size_t removedDirs = 0;
  for (const auto& path : m_externalDirs) {
    if (fs::is_directory(path, ec) && fs::is_empty(path, ec)) {
      fs::remove(path, ec);
      if (!ec) {
        ++removedDirs;
      }
    }
  }

  log::debug("Cleaned up {} external symlinks, {} dirs",
             m_externalSymlinks.size(), removedDirs);
  m_externalSymlinks.clear();
  m_externalDirs.clear();
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

StagingPromotionResult FuseConnector::flushStaging()
{
  if (m_stagingDir.empty() || m_overwriteDir.empty()) {
    return {};
  }

  const fs::path staging(m_stagingDir);
  const fs::path overwrite = m_customOutputDir.empty()
                                 ? fs::path(m_overwriteDir)
                                 : fs::path(m_customOutputDir);

  log::debug("flushStaging: staging='{}', customOutput='{}', dest='{}'",
             QString::fromStdString(m_stagingDir),
             QString::fromStdString(m_customOutputDir),
             QString::fromStdString(overwrite.string()));

  StagingPromotionResult result = StagingPromotion::promote(staging, overwrite);
  if (result.status == StagingPromotionStatus::Promoted ||
      result.status == StagingPromotionStatus::Recovered) {
    std::vector<std::string> relativePaths;
    relativePaths.reserve(result.files.size());
    for (const StagingPromotedFile& file : result.files) {
      relativePaths.push_back(file.relative_path);
    }

    VfsCatalog catalog(VfsCatalog::databasePath(m_dataDirPath));
    try {
      const VfsCatalogRefreshResult refreshed = catalog.forceRefreshProviderFiles(
          overwrite.string(), "Overwrite", false, relativePaths);
      if (refreshed.files.size() != result.files.size()) {
        throw std::runtime_error("Catalog refresh returned an incomplete result");
      }
      for (size_t i = 0; i < result.files.size(); ++i) {
        if (!refreshed.files[i].exists ||
            refreshed.files[i].relative_path != result.files[i].relative_path ||
            refreshed.files[i].digest != result.files[i].digest) {
          throw std::runtime_error(
              "Catalog digest did not match promoted file " +
              result.files[i].relative_path);
        }
      }
    } catch (...) {
      // The destination has already been atomically installed and verified.
      // Remove possibly stale rows so the next full reconcile must hash them.
      catalog.invalidateProviderFiles(overwrite.string(), relativePaths);
      throw;
    }
    log::info("Promoted and BLAKE3-verified {} staged VFS file(s)",
              result.files.size());
  }
  return result;
}

void FuseConnector::flushStagingLive()
{
  if (!m_mounted) {
    return;
  }

  if (m_context == nullptr) {
    return;
  }

  // A writable handle can continue changing an unlinked staging inode after a
  // live promotion.  Only synchronize while no such handles exist; unmount is
  // the normal durable path.
  {
    std::shared_lock const lock(m_context->open_files_mutex);
    for (const auto& [handle, file] : m_context->open_files) {
      (void)handle;
      if (file.writable || file.cow_pending) {
        throw FuseConnectorException(QObject::tr(
            "Cannot synchronize VFS staging while writable files are open. "
            "Close the game or application first."));
      }
    }
  }

  StagingPromotionResult promotion;
  try {
    promotion = flushStaging();
  } catch (...) {
    std::error_code recoveryError;
    fs::create_directories(m_stagingDir, recoveryError);
    m_context->overwrite =
        std::make_unique<OverwriteManager>(m_stagingDir, m_overwriteDir);
    // If the filesystem promotion succeeded but catalog publication failed,
    // rebuild from disk before returning the error so the live tree cannot
    // retain paths to removed staging files.
    rebuild(m_lastMods, QString::fromStdString(m_overwriteDir),
            QString::fromStdString(m_dataDirName));
    throw;
  }
  if (promotion.blocked()) {
    std::error_code recoveryError;
    fs::create_directories(m_stagingDir, recoveryError);
    m_context->overwrite =
        std::make_unique<OverwriteManager>(m_stagingDir, m_overwriteDir);
    rebuild(m_lastMods, QString::fromStdString(m_overwriteDir),
            QString::fromStdString(m_dataDirName));
    throw FuseConnectorException(
        QObject::tr("%1\n\nRecovery files: %2")
            .arg(QString::fromStdString(promotion.message),
                 QString::fromStdString(promotion.recovery_path.string())));
  }

  // Re-create the staging dir (flushStaging removes it)
  std::error_code ec;
  fs::create_directories(m_stagingDir, ec);

  // Rebuild the VFS tree to pick up new overwrite files
  VfsCatalog catalog(VfsCatalog::databasePath(m_dataDirPath));
  auto catalogResult = catalog.reconcileAndBuild(
      m_dataDirPath, m_lastMods, m_overwriteDir, false);
  auto newTree = std::make_shared<VfsTree>(std::move(catalogResult.tree));

  injectExtraFiles(*newTree, m_extraVfsFiles);
  if (!m_pluginLoadOrder.empty()) {
    stampPluginTimestamps(*newTree, m_pluginLoadOrder);
  }
  publishIndex(*newTree, catalogResult);

  std::scoped_lock const namespaceLock(m_context->namespace_mutation_mutex);
  std::shared_ptr<VfsRuntimeIndex> newRuntimeIndex;
  {
    std::unique_lock const lock(m_context->inode_mutex);
    newRuntimeIndex = VfsRuntimeIndex::build(
        *newTree, *m_context->inodes, m_context->uid, m_context->gid);
  }
  {
    std::unique_lock const treeLock(m_context->tree_mutex);
    std::unique_lock const indexLock(m_context->runtime_index_mutex);
    m_context->tree.swap(newTree);
    m_context->runtime_index.swap(newRuntimeIndex);
    m_context->archive_members = catalogResult.archive_member_index;
  }
  {
    std::scoped_lock const lock(m_context->open_dirs_mutex);
    m_context->open_dirs.clear();
  }
  {
    std::scoped_lock const lock(m_context->dir_cache_mutex);
    m_context->dir_cache.clear();
    m_context->readdir_blob_cache.clear();
    m_context->readdirplus_blob_cache.clear();
  }
  {
    std::scoped_lock const lock(m_context->lookup_cache_mutex);
    m_context->lookup_cache.clear();
  }
  {
    std::scoped_lock const lock(m_context->attr_cache_mutex);
    m_context->attr_cache.clear();
  }
  {
    std::scoped_lock const lock(m_context->node_cache_mutex);
    m_context->node_cache.clear();
  }

  // Re-create OverwriteManager with fresh staging dir
  m_context->overwrite = std::make_unique<OverwriteManager>(m_stagingDir, m_overwriteDir);

  if (!promotion.files.empty()) {
    const std::string root = m_customOutputDir.empty()
                                 ? m_overwriteDir
                                 : m_customOutputDir;
    std::scoped_lock const lock(m_context->dirty_paths_mutex);
    auto provider = m_context->dirty_provider_paths.find(root);
    if (provider != m_context->dirty_provider_paths.end()) {
      for (const StagingPromotedFile& file : promotion.files) {
        provider->second.erase(file.relative_path);
      }
      if (provider->second.empty()) {
        m_context->dirty_provider_paths.erase(provider);
      }
    }
  }

  log::debug("Live staging flush complete");
}

// Detect a stale FUSE mount by probing with stat().  Returns true if
// the path exists in the mount table OR if accessing it gives ENOTCONN
// (which happens when the FUSE daemon died but the mount is listed
// under a different path due to symlinks).
static bool isStaleOrMounted(const QString& path)
{
  if (isMountPoint(path)) {
    return true;
  }

  // Probe the path directly — ENOTCONN means dead FUSE mount even if
  // /proc/mounts lists it under a different (canonical) path.
  struct stat st;
  return ::stat(path.toLocal8Bit().constData(), &st) != 0 && errno == ENOTCONN;
}

static void doUnmount(const QString& path)
{
  const QString clean = QDir::cleanPath(path);

  if (runUnmountCommand("fusermount3", {"-u", clean}) ||
      runUnmountCommand("fusermount", {"-u", clean})) {
    log::info("stale mount at '{}' cleaned up successfully", path);
    return;
  }

  // Graceful unmount failed — try force/lazy variants.
  runUnmountCommand("umount", {clean});
  runUnmountCommand("umount", {"-l", clean});
  runUnmountCommand("fusermount3", {"-uz", clean});
  runUnmountCommand("fusermount", {"-uz", clean});

  if (!isStaleOrMounted(path)) {
    log::info("stale mount at '{}' cleaned up (lazy unmount)", path);
  } else {
    log::error("failed to clean up stale mount at '{}'", path);
  }
}

static void cleanupStaleMo2Mounts(const QString& keepPath)
{
  QFile mounts(QStringLiteral("/proc/mounts"));
  if (!mounts.open(QIODevice::ReadOnly)) {
    return;
  }

  const QString cleanKeep = QDir::cleanPath(keepPath);
  const QString trashRoot =
      QDir::cleanPath(QDir::homePath() + "/.local/share/Trash/files");

  while (!mounts.atEnd()) {
    const auto line  = QString::fromUtf8(mounts.readLine()).trimmed();
    const auto parts = line.split(' ', Qt::SkipEmptyParts);
    if (parts.size() < 3) {
      continue;
    }
    if (parts[0] != QStringLiteral("mo2linux")) {
      continue;
    }

    const QString mp = QDir::cleanPath(QString::fromStdString(
        decodeProcMountField(parts[1].toStdString())));
    if (mp == cleanKeep) {
      continue;
    }

    const bool underTrash =
        mp == trashRoot || mp.startsWith(trashRoot + QDir::separator());
    if (!underTrash && !isStaleOrMounted(mp)) {
      continue;
    }

    log::warn("cleaning stale mo2linux mount at '{}'", mp);
    doUnmount(mp);
  }
}

void FuseConnector::tryCleanupStaleMount(const QString& path)
{
  cleanupStaleMo2Mounts(path);

  if (!isStaleOrMounted(path)) {
    return;
  }

  log::warn("stale FUSE mount detected at '{}', attempting cleanup", path);
  doUnmount(path);
}

// ── VFS Root Builder ─────────────────────────────────────────────────────────

void FuseConnector::setRootBuilderEnabled(bool enabled,
                                          const std::string& storageDir)
{
  m_rootBuilderEnabled = enabled;
  m_rootStorageDir     = storageDir;
}

void FuseConnector::prepareRootFilesForUsvfs(const MappingType& mapping)
{
  auto* game = qApp->property("managed_game").value<MOBase::IPluginGame*>();
  if (game == nullptr) {
    throw FuseConnectorException(QObject::tr("Managed game not available"));
  }

  const QString gameDir = game->gameDirectory().absolutePath();
  const QString dataDir = game->dataDirectory().absolutePath();
  const QString overwriteDir = Settings::instance().paths().overwrite();
  m_gameDir = gameDir.toStdString();

  if (m_rootBuilderEnabled) {
    deployRootFiles(buildModsFromMapping(mapping, dataDir, overwriteDir));
  }
}

static std::string findRootDir(const std::string& modPath)
{
  // Case-insensitive search for "Root" subdirectory
  namespace fs = std::filesystem;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(modPath, ec)) {
    if (entry.is_directory(ec)) {
      const auto name = entry.path().filename().string();
      if (name.size() == 4) {
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower == "root") {
          return entry.path().string();
        }
      }
    }
  }
  return {};
}

static bool reflinkCopy(const std::string& src, const std::string& dst)
{
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::create_directories(fs::path(dst).parent_path(), ec);

  // Try reflink (CoW) first via cp --reflink=auto
  if (QProcess::execute("cp", {"--reflink=auto", "--no-preserve=mode",
                                QString::fromStdString(src),
                                QString::fromStdString(dst)}) == 0) {
    return true;
  }

  // Fallback to regular copy
  return fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
}

static bool reflinkBackupCopy(const std::string& src, const std::string& dst)
{
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::create_directories(fs::path(dst).parent_path(), ec);

  // Root payloads commonly replace large upscaler DLLs. On copy-on-write
  // filesystems an ordinary backup needlessly reads and writes hundreds of
  // MiB on every launch, while a reflink is effectively metadata-only.
  if (QProcess::execute("cp", {"--reflink=auto", "--preserve=all", "--",
                                QString::fromStdString(src),
                                QString::fromStdString(dst)}) == 0) {
    return true;
  }

  return fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
}

static void loadRootManifest(const std::string& storageDir,
                             std::vector<std::string>& deployed,
                             std::vector<std::string>& dirs,
                             std::map<std::string, std::string>& backups)
{
  namespace fs = std::filesystem;
  const auto manifestPath = fs::path(storageDir) / "manifest.json";
  std::ifstream in(manifestPath);
  if (!in.is_open()) return;

  try {
    std::string const content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    QJsonDocument const doc = QJsonDocument::fromJson(
        QByteArray::fromStdString(content));
    if (doc.isNull()) return;

    const auto obj = doc.object();
    for (const auto& v : obj["deployed"].toArray()) {
      deployed.push_back(v.toString().toStdString());
    }
    // "dirs" was added later — older manifests won't have it, which is fine.
    for (const auto& v : obj["dirs"].toArray()) {
      dirs.push_back(v.toString().toStdString());
    }
    const auto bk = obj["backups"].toObject();
    for (auto it = bk.begin(); it != bk.end(); ++it) {
      backups[it.key().toStdString()] = it.value().toString().toStdString();
    }
  } catch (...) {}
}

static void saveRootManifest(const std::string& storageDir,
                             const std::vector<std::string>& deployed,
                             const std::vector<std::string>& dirs,
                             const std::map<std::string, std::string>& backups)
{
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::create_directories(storageDir, ec);

  QJsonArray arr;
  for (const auto& f : deployed) {
    arr.append(QString::fromStdString(f));
  }

  QJsonArray dirArr;
  for (const auto& d : dirs) {
    dirArr.append(QString::fromStdString(d));
  }

  QJsonObject bk;
  for (const auto& [dst, bak] : backups) {
    bk[QString::fromStdString(dst)] = QString::fromStdString(bak);
  }

  QJsonObject obj;
  obj["deployed"] = arr;
  obj["dirs"]     = dirArr;
  obj["backups"]  = bk;

  const auto manifestPath = fs::path(storageDir) / "manifest.json";
  std::ofstream out(manifestPath);
  if (out.is_open()) {
    out << QJsonDocument(obj).toJson().toStdString();
  }
}

void FuseConnector::deployRootFiles(
    const std::vector<std::pair<std::string, std::string>>& mods)
{
  if (!m_rootBuilderEnabled || m_gameDir.empty() || m_rootStorageDir.empty()) {
    return;
  }

  namespace fs = std::filesystem;
  const auto t0 = std::chrono::steady_clock::now();

  // Clear any previous deployment
  clearRootFiles();

  m_rootDeployedFiles.clear();
  m_rootDeployedDirs.clear();
  m_rootBackups.clear();

  const fs::path gameRoot(m_gameDir);
  const std::string backupDir = (fs::path(m_rootStorageDir) / "backup").string();
  std::set<std::string> deployedSet;

  // Create dst.parent_path() one segment at a time, recording each segment
  // we actually had to create (so it can be removed on cleanup if empty).
  // Stops walking up once it hits an existing dir or the gameRoot itself —
  // we never want to remove the game root or any pre-existing user dir.
  auto trackedCreateParents = [&](const fs::path& filePath) {
    std::error_code ec2;
    std::vector<fs::path> toCreate;
    for (fs::path p = filePath.parent_path();
         !p.empty() && p != gameRoot && !fs::exists(p, ec2);
         p = p.parent_path()) {
      toCreate.push_back(p);
      if (p == p.root_path()) break;
    }
    for (auto it = toCreate.rbegin(); it != toCreate.rend(); ++it) {
      if (fs::create_directory(*it, ec2) && !ec2) {
        m_rootDeployedDirs.push_back(it->string());
      }
    }
  };

  for (const auto& [modName, modPath] : mods) {
    const auto rootDir = findRootDir(modPath);
    if (rootDir.empty()) continue;

    std::error_code ec;
    for (const auto& entry :
         fs::recursive_directory_iterator(rootDir, ec)) {
      if (!entry.is_regular_file(ec)) continue;

      const auto relPath = fs::relative(entry.path(), rootDir, ec).string();
      const auto dst     = (fs::path(m_gameDir) / relPath).string();

      if (deployedSet.contains(dst)) continue;  // higher-priority mod already deployed

      // Backup existing file
      if (fs::exists(dst, ec) && !deployedSet.contains(dst)) {
        const auto bak = (fs::path(backupDir) / relPath).string();
        fs::create_directories(fs::path(bak).parent_path(), ec);
        if (reflinkBackupCopy(dst, bak)) {
          m_rootBackups[dst] = bak;
        } else {
          std::fprintf(stderr,
                       "[RootBuilder] failed to back up '%s' -> '%s'\n",
                       dst.c_str(), bak.c_str());
          continue;
        }
      }

      // Deploy: always copy (exe/dll need it, and symlinks can confuse Wine)
      if (fs::exists(dst, ec) || fs::is_symlink(dst, ec)) {
        fs::remove(dst, ec);
      }
      trackedCreateParents(fs::path(dst));

      if (!reflinkCopy(entry.path().string(), dst)) {
        std::fprintf(stderr, "[RootBuilder] failed to copy '%s' -> '%s'\n",
                     entry.path().c_str(), dst.c_str());
        continue;
      }

      m_rootDeployedFiles.push_back(dst);
      deployedSet.insert(dst);
    }
  }

  saveRootManifest(m_rootStorageDir, m_rootDeployedFiles, m_rootDeployedDirs,
                   m_rootBackups);

  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - t0).count();
  std::fprintf(stderr, "[RootBuilder] deployed %zu files (%zu backups) in %lldms\n",
               m_rootDeployedFiles.size(), m_rootBackups.size(),
               static_cast<long long>(ms));
}

void FuseConnector::clearRootFiles()
{
  if (m_rootStorageDir.empty()) return;

  namespace fs = std::filesystem;
  std::error_code ec;

  // Load manifest if we don't have in-memory state
  if (m_rootDeployedFiles.empty() && m_rootDeployedDirs.empty()) {
    loadRootManifest(m_rootStorageDir, m_rootDeployedFiles, m_rootDeployedDirs,
                     m_rootBackups);
  }

  if (m_rootDeployedFiles.empty() && m_rootDeployedDirs.empty() &&
      m_rootBackups.empty()) {
    // An interrupted no-op deployment can leave an empty manifest behind.
    // It represents no live deployment and should not make later runs look as
    // though Root Builder state still needs recovery.
    fs::remove(fs::path(m_rootStorageDir) / "manifest.json", ec);
    fs::remove(fs::path(m_rootStorageDir) / "backup", ec);
    return;
  }

  int removed = 0;
  for (const auto& dst : m_rootDeployedFiles) {
    if (fs::exists(dst, ec) || fs::is_symlink(dst, ec)) {
      fs::remove(dst, ec);
      ++removed;
    }
  }

  // Restore backups
  for (const auto& [dst, bak] : m_rootBackups) {
    if (fs::exists(bak, ec)) {
      fs::create_directories(fs::path(dst).parent_path(), ec);
      fs::rename(bak, dst, ec);
    }
  }

  // Remove dirs we created in game root, deepest-first, only if empty.
  // A non-empty dir means the user (or game) put something there — leave it.
  std::sort(m_rootDeployedDirs.begin(), m_rootDeployedDirs.end(),
            [](const std::string& a, const std::string& b) {
              return a.size() > b.size();
            });
  std::size_t removedDirs = 0;
  for (const auto& d : m_rootDeployedDirs) {
    if (fs::is_directory(d, ec) && fs::is_empty(d, ec)) {
      fs::remove(d, ec);
      if (!ec) ++removedDirs;
    }
  }

  // Clean up backup directory and manifest
  const auto backupDir = fs::path(m_rootStorageDir) / "backup";
  fs::remove_all(backupDir, ec);
  fs::remove(fs::path(m_rootStorageDir) / "manifest.json", ec);

  std::fprintf(stderr,
               "[RootBuilder] cleared %d deployed files, %zu dirs, restored %zu backups\n",
               removed, removedDirs, m_rootBackups.size());

  m_rootDeployedFiles.clear();
  m_rootDeployedDirs.clear();
  m_rootBackups.clear();
}
