#include "mo2filesystem.h"

#include <fcntl.h>
#include <linux/falloc.h>
#include <linux/fs.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if __has_include(<linux/msdos_fs.h>)
#include <linux/msdos_fs.h>
#endif

namespace
{
namespace fs = std::filesystem;

// Mod files are immutable during a game session, so cache aggressively.
// The VFS tree is built once at mount time and only mutated by our own
// create/rename/unlink handlers (which invalidate affected entries).
//
// ctx->cache_disabled (Settings > Proton/Wine tab, or the
// FLUORINE_VFS_DISABLE_CACHE env var as a fallback) is a diagnostic escape
// hatch: it zeroes the kernel-facing TTLs/attr cache below and disables
// keep_cache (see fuseDropFeature(FUSE_CAP_AUTO_INVAL_DATA) further down,
// which is skipped too when this is set). Use it to test whether a "stale
// VFS data" report is actually a cache-invalidation bug in this file, before
// chasing one. It is not meant to stay on permanently — see the June 2026
// "Fix shader cache write failures and INI key clobber" commit for why
// keep_cache=1 exists.
double ttlSeconds(const Mo2FsContext* ctx) { return ctx->cache_disabled ? 0.0 : 86400.0; }              // 24 hours
double negativeTtlSeconds(const Mo2FsContext* ctx) { return ctx->cache_disabled ? 0.0 : 3600.0; }       // 1 hour — Wine probes many non-existent files
double attrCacheSeconds(const Mo2FsContext* ctx) { return ctx->cache_disabled ? 0.0 : 86400.0; }
int vfsKeepCache(const Mo2FsContext* ctx) { return ctx->cache_disabled ? 0 : 1; }
constexpr size_t MAX_RETAINED_RO_FDS  = 1024;
constexpr uint64_t SLOW_OP_LOG_NS     = 100ull * 1000ull * 1000ull;
constexpr uint64_t AUDIO_TRACE_LOG_LIMIT = 4096;

void fillStatForDir(struct stat* st, fuse_ino_t ino, uid_t uid, gid_t gid);
void fillStatForFile(struct stat* st, fuse_ino_t ino, uid_t uid, gid_t gid,
                     uint64_t size,
                     const std::chrono::system_clock::time_point& mtime,
                     const std::string& real_path = {},
                     mode_t cached_mode = 0);
mode_t regularFileVfsMode(mode_t sourceMode);
void invalidateLookupCache(Mo2FsContext* ctx, const std::string& dirPath);
void invalidateAttrCache(Mo2FsContext* ctx, fuse_ino_t ino);
bool pathTouchesMutation(const std::string& cachedPath,
                         const std::string& changedPath);
bool isStrictDescendantPath(const std::string& path, const std::string& parent);
bool shouldTracePath(const std::string& path);
bool shouldTraceAudioReadPath(const std::string& path);
std::shared_ptr<VfsRuntimeIndex> runtimeIndex(const Mo2FsContext* ctx);

void markCatalogDirty(Mo2FsContext* ctx, const std::string& relativePath,
                      const std::string& realPath = {})
{
  if (ctx == nullptr || relativePath.empty()) return;
  std::string provider = ctx->default_catalog_root;
  if (!realPath.empty()) {
    const fs::path real = fs::path(realPath).lexically_normal();
    size_t bestLength = 0;
    for (const auto& [root, origin] : ctx->catalog_providers) {
      (void)origin;
      const fs::path normalizedRoot = fs::path(root).lexically_normal();
      const fs::path candidate = real.lexically_relative(normalizedRoot);
      if (!candidate.empty() && !candidate.is_absolute() &&
          *candidate.begin() != ".." && root.size() > bestLength) {
        provider = root;
        bestLength = root.size();
      }
    }
  }
  if (provider.empty()) return;
  std::scoped_lock lock(ctx->dirty_paths_mutex);
  ctx->dirty_provider_paths[provider].insert(
      fs::path(relativePath).lexically_normal().generic_string());
}

int fuseErrnoFromError(std::error_code ec, int fallback = EIO)
{
  if (!ec) {
    return fallback;
  }

  if (ec == std::errc::no_such_file_or_directory) return ENOENT;
  if (ec == std::errc::not_a_directory) return ENOTDIR;
  if (ec == std::errc::is_a_directory) return EISDIR;
  if (ec == std::errc::file_exists) return EEXIST;
  if (ec == std::errc::permission_denied) return EACCES;
  if (ec == std::errc::directory_not_empty) return ENOTEMPTY;
  if (ec == std::errc::invalid_argument) return EINVAL;
  if (ec == std::errc::too_many_symbolic_link_levels) return ELOOP;
  if (ec == std::errc::filename_too_long) return ENAMETOOLONG;

  if (ec.category() == std::generic_category() ||
      ec.category() == std::system_category()) {
    return ec.value() != 0 ? ec.value() : fallback;
  }
  return fallback;
}

// RAII helper that records per-op wall-clock nanoseconds into a counter.
struct OpTimer
{
  std::atomic<uint64_t>* sink;
  const char* op;
  std::string path;
  std::chrono::steady_clock::time_point start;
  explicit OpTimer(std::atomic<uint64_t>* s, const char* opName = nullptr,
                   std::string opPath = {})
      : sink(s), op(opName), path(std::move(opPath)),
        start(std::chrono::steady_clock::now()) {}
  ~OpTimer()
  {
    if (sink == nullptr) return;
    const auto end = std::chrono::steady_clock::now();
    const uint64_t ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    sink->fetch_add(ns, std::memory_order_relaxed);
    if (op != nullptr && ns >= SLOW_OP_LOG_NS) {
      std::fprintf(stderr, "[VFS] slow op=%s elapsed=%.1fms path='%s'\n",
                   op, ns / 1e6, path.c_str());
    }
  }
  OpTimer(const OpTimer&)            = delete;
  OpTimer& operator=(const OpTimer&)  = delete;
};

bool fuseHasFeature(const struct fuse_conn_info* conn, uint32_t flag)
{
  if (conn == nullptr) {
    return false;
  }
  return (conn->capable & flag) != 0;
}

bool fuseWantsFeature(const struct fuse_conn_info* conn, uint32_t flag)
{
  if (conn == nullptr) {
    return false;
  }
  return (conn->want & flag) != 0;
}

bool fuseRequestFeature(struct fuse_conn_info* conn, uint32_t flag)
{
  if (conn == nullptr) {
    return false;
  }
  if ((conn->capable & flag) == 0) {
    return false;
  }
  conn->want |= flag;
  return true;
}

void fuseDropFeature(struct fuse_conn_info* conn, uint32_t flag)
{
  if (conn == nullptr) {
    return;
  }
  conn->want &= ~flag;
}

void maybeLogCounters(Mo2FsContext* ctx)
{
  if (ctx == nullptr) {
    return;
  }

  const uint64_t tick = ctx->op_tick.fetch_add(1, std::memory_order_relaxed) + 1;
  if ((tick % 50000) != 0) {
    return;
  }

  const uint64_t lc = ctx->lookup_count.load(std::memory_order_relaxed);
  const uint64_t gc = ctx->getattr_count.load(std::memory_order_relaxed);
  const uint64_t rc = ctx->readdir_count.load(std::memory_order_relaxed);
  const uint64_t oc = ctx->open_count.load(std::memory_order_relaxed);
  const uint64_t rdc = ctx->read_count.load(std::memory_order_relaxed);
  const uint64_t wc = ctx->write_count.load(std::memory_order_relaxed);
  const uint64_t cc = ctx->create_count.load(std::memory_order_relaxed);
  const uint64_t rnc = ctx->rename_count.load(std::memory_order_relaxed);
  const uint64_t sc = ctx->setattr_count.load(std::memory_order_relaxed);
  const uint64_t uc = ctx->unlink_count.load(std::memory_order_relaxed);
  const uint64_t fc = ctx->flush_count.load(std::memory_order_relaxed);
  const uint64_t fsc = ctx->fsync_count.load(std::memory_order_relaxed);
  const uint64_t ic = ctx->ioctl_count.load(std::memory_order_relaxed);

  std::fprintf(stderr,
               "[VFS] ops lookup=%llu getattr=%llu readdir=%llu open=%llu read=%llu "
               "write=%llu create=%llu rename=%llu setattr=%llu unlink=%llu "
               "flush=%llu fsync=%llu ioctl=%llu",
               static_cast<unsigned long long>(lc),
               static_cast<unsigned long long>(gc),
               static_cast<unsigned long long>(rc),
               static_cast<unsigned long long>(oc),
               static_cast<unsigned long long>(rdc),
               static_cast<unsigned long long>(wc),
               static_cast<unsigned long long>(cc),
               static_cast<unsigned long long>(rnc),
               static_cast<unsigned long long>(sc),
               static_cast<unsigned long long>(uc),
               static_cast<unsigned long long>(fc),
               static_cast<unsigned long long>(fsc),
               static_cast<unsigned long long>(ic));
  {
    std::scoped_lock lock(ctx->open_files_mutex);
    std::fprintf(stderr, " open_handles=%zu\n", ctx->open_files.size());
  }
  std::fprintf(stderr,
               "[VFS] cache lookup_hit=%llu lookup_miss=%llu lookup_inval=%llu "
               "lookup_archive=%llu "
               "attr_hit=%llu attr_miss=%llu dir_hit=%llu dir_miss=%llu "
               "readdir_blob_hit=%llu readdirplus_blob_hit=%llu "
               "lazy_ro_open=%llu ro_fd_hit=%llu ro_fd_evict=%llu\n",
               static_cast<unsigned long long>(
                   ctx->lookup_cache_hits.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   ctx->lookup_cache_misses.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   ctx->lookup_cache_invalidations.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   ctx->archive_lookup_candidates.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   ctx->attr_cache_hits.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   ctx->attr_cache_misses.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   ctx->dir_cache_hits.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   ctx->dir_cache_misses.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   ctx->readdir_blob_hits.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   ctx->readdirplus_blob_hits.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   ctx->lazy_ro_fd_opens.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   ctx->retained_ro_fd_hits.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   ctx->retained_ro_fd_evictions.load(std::memory_order_relaxed)));
  std::fprintf(stderr,
               "[VFS] index lookup_base=%llu lookup_overlay=%llu "
               "negative_hit=%llu negative_first=%llu tombstone=%llu "
               "invariant_miss=%llu node_hit=%llu node_miss=%llu "
               "dir_inval=%llu dir_erased=%llu opendir=%llu releasedir=%llu\n",
               static_cast<unsigned long long>(
                   ctx->lookup_base_hits.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   ctx->lookup_overlay_hits.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   ctx->lookup_negative_hits.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   ctx->lookup_negative_first.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   ctx->lookup_tombstones.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   ctx->lookup_index_invariant_misses.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   ctx->node_index_hits.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   ctx->node_index_misses.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   ctx->dir_cache_invalidations.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   ctx->dir_cache_entries_erased.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   ctx->opendir_count.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   ctx->releasedir_count.load(std::memory_order_relaxed)));
  std::fprintf(stderr,
               "[VFS] io bytes_read=%llu bytes_written=%llu cow_writes=%llu\n",
               static_cast<unsigned long long>(
                   ctx->read_bytes.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   ctx->write_bytes.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   ctx->cow_write_count.load(std::memory_order_relaxed)));
  if (ctx->trace_audio_reads) {
    std::fprintf(
        stderr,
        "[VFS audio-io] summary reads=%llu short=%llu errors=%llu "
        "requested=%llu actual=%llu\n",
        static_cast<unsigned long long>(
            ctx->audio_trace_read_count.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            ctx->audio_trace_short_read_count.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            ctx->audio_trace_error_count.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            ctx->audio_trace_bytes_requested.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            ctx->audio_trace_bytes_returned.load(std::memory_order_relaxed)));
  }
  {
    size_t lookupSize = 0;
    size_t attrSize = 0;
    size_t dirSize = 0;
    size_t readdirBlobSize = 0;
    size_t readdirPlusBlobSize = 0;
    size_t openDirSize = 0;
    size_t nodeSize = 0;
    size_t baseIndexSize = 0;
    size_t overlayIndexSize = 0;
    size_t negativeIndexSize = 0;
    {
      std::scoped_lock lock(ctx->lookup_cache_mutex);
      lookupSize = ctx->lookup_cache.size();
    }
    {
      std::scoped_lock lock(ctx->attr_cache_mutex);
      attrSize = ctx->attr_cache.size();
    }
    {
      std::scoped_lock lock(ctx->dir_cache_mutex);
      dirSize = ctx->dir_cache.size();
      readdirBlobSize = ctx->readdir_blob_cache.size();
      readdirPlusBlobSize = ctx->readdirplus_blob_cache.size();
    }
    {
      std::scoped_lock lock(ctx->open_dirs_mutex);
      openDirSize = ctx->open_dirs.size();
    }
    {
      std::scoped_lock lock(ctx->node_cache_mutex);
      nodeSize = ctx->node_cache.size();
    }
    if (auto index = runtimeIndex(ctx); index != nullptr) {
      baseIndexSize = index->baseLookupCount();
      overlayIndexSize = index->overlayCount();
      negativeIndexSize = index->negativeCount();
    }
    std::fprintf(stderr,
                 "[VFS] cache_size lookup=%zu attr=%zu dir=%zu readdir_blob=%zu "
                 "readdirplus_blob=%zu open_dirs=%zu node=%zu "
                 "index_base=%zu index_overlay=%zu index_negative=%zu\n",
                 lookupSize, attrSize, dirSize, readdirBlobSize,
                 readdirPlusBlobSize, openDirSize, nodeSize, baseIndexSize,
                 overlayIndexSize, negativeIndexSize);
  }

  // Per-op wall-clock totals and averages (microseconds).
  auto avgUs = [](uint64_t ns, uint64_t count) -> double {
    return count == 0 ? 0.0 : (static_cast<double>(ns) / 1000.0) / static_cast<double>(count);
  };
  const uint64_t lns = ctx->lookup_ns.load(std::memory_order_relaxed);
  const uint64_t gns = ctx->getattr_ns.load(std::memory_order_relaxed);
  const uint64_t rns = ctx->readdir_ns.load(std::memory_order_relaxed);
  const uint64_t ons = ctx->open_ns.load(std::memory_order_relaxed);
  const uint64_t rdns = ctx->read_ns.load(std::memory_order_relaxed);
  const uint64_t wns = ctx->write_ns.load(std::memory_order_relaxed);
  const uint64_t cns = ctx->create_ns.load(std::memory_order_relaxed);
  const uint64_t rnns = ctx->rename_ns.load(std::memory_order_relaxed);
  const uint64_t sns = ctx->setattr_ns.load(std::memory_order_relaxed);
  const uint64_t uns = ctx->unlink_ns.load(std::memory_order_relaxed);
  const uint64_t fns = ctx->flush_ns.load(std::memory_order_relaxed);
  const uint64_t fsns = ctx->fsync_ns.load(std::memory_order_relaxed);
  std::fprintf(stderr,
               "[VFS] time lookup=%.1fms/%.1fus-avg getattr=%.1fms/%.1fus readdir=%.1fms/%.1fus "
               "open=%.1fms/%.1fus read=%.1fms/%.1fus write=%.1fms/%.1fus\n",
               lns / 1e6, avgUs(lns, lc),
               gns / 1e6, avgUs(gns, gc),
               rns / 1e6, avgUs(rns, rc),
               ons / 1e6, avgUs(ons, oc),
               rdns / 1e6, avgUs(rdns, rdc),
               wns / 1e6, avgUs(wns, wc));
  std::fprintf(stderr,
               "[VFS] time_mut create=%.1fms/%.1fus rename=%.1fms/%.1fus "
               "setattr=%.1fms/%.1fus unlink=%.1fms/%.1fus flush=%.1fms/%.1fus "
               "fsync=%.1fms/%.1fus\n",
               cns / 1e6, avgUs(cns, cc),
               rnns / 1e6, avgUs(rnns, rnc),
               sns / 1e6, avgUs(sns, sc),
               uns / 1e6, avgUs(uns, uc),
               fns / 1e6, avgUs(fns, fc),
               fsns / 1e6, avgUs(fsns, fsc));

  // Process-wide CPU usage + delta since last tick. Lets us tell whether
  // high per-op wall time is spent burning CPU (parsing, mutex contention)
  // or blocked on disk IO (delta CPU ≪ delta wall).
  {
    struct rusage ru{};
    if (::getrusage(RUSAGE_SELF, &ru) == 0) {
      const uint64_t user_us =
          static_cast<uint64_t>(ru.ru_utime.tv_sec) * 1000000ull +
          static_cast<uint64_t>(ru.ru_utime.tv_usec);
      const uint64_t sys_us =
          static_cast<uint64_t>(ru.ru_stime.tv_sec) * 1000000ull +
          static_cast<uint64_t>(ru.ru_stime.tv_usec);
      const uint64_t now_ns = static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::steady_clock::now().time_since_epoch())
              .count());

      const uint64_t prev_user =
          ctx->last_cpu_user_us.exchange(user_us, std::memory_order_relaxed);
      const uint64_t prev_sys =
          ctx->last_cpu_sys_us.exchange(sys_us, std::memory_order_relaxed);
      const uint64_t prev_wall =
          ctx->last_tick_wall_ns.exchange(now_ns, std::memory_order_relaxed);

      const double d_user_s =
          prev_user == 0 ? 0.0 : (user_us - prev_user) / 1e6;
      const double d_sys_s =
          prev_sys == 0 ? 0.0 : (sys_us - prev_sys) / 1e6;
      const double d_wall_s =
          prev_wall == 0 ? 0.0 : (now_ns - prev_wall) / 1e9;
      const double busy_pct =
          d_wall_s > 0.0 ? ((d_user_s + d_sys_s) / d_wall_s) * 100.0 : 0.0;

      const long rss_mb = ru.ru_maxrss / 1024;  // ru_maxrss is KB on Linux
      std::fprintf(
          stderr,
          "[VFS] cpu user=%.2fs sys=%.2fs (Δuser=%.2fs Δsys=%.2fs Δwall=%.2fs busy=%.1f%%) rss=%ldMB\n",
          user_us / 1e6, sys_us / 1e6, d_user_s, d_sys_s, d_wall_s, busy_pct, rss_mb);
    }
  }

  auto logTop = [](const char* label, const std::unordered_map<std::string, uint64_t>& m) {
    if (m.empty()) {
      return;
    }

    std::vector<std::pair<std::string, uint64_t>> top(m.begin(), m.end());
    const size_t keep = std::min<size_t>(3, top.size());
    std::partial_sort(
        top.begin(), top.begin() + static_cast<std::ptrdiff_t>(keep), top.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    std::fprintf(stderr, "[VFS] hot %s:", label);
    for (size_t i = 0; i < keep; ++i) {
      std::fprintf(stderr, " [%llu] %s",
                   static_cast<unsigned long long>(top[i].second),
                   top[i].first.c_str());
    }
    std::fputc('\n', stderr);
  };

  {
    std::scoped_lock lock(ctx->path_stats_mutex);
    logTop("lookup_hit", ctx->lookup_hit_paths);
    logTop("lookup_miss", ctx->lookup_miss_paths);
    logTop("getattr", ctx->getattr_paths);
    logTop("readdir", ctx->readdir_paths);
    logTop("write", ctx->write_paths);
    logTop("create", ctx->create_paths);
    logTop("setattr", ctx->setattr_paths);
    logTop("flush", ctx->flush_paths);
    ctx->lookup_hit_paths.clear();
    ctx->lookup_miss_paths.clear();
    ctx->getattr_paths.clear();
    ctx->readdir_paths.clear();
    ctx->write_paths.clear();
    ctx->create_paths.clear();
    ctx->setattr_paths.clear();
    ctx->flush_paths.clear();
  }
}

void invalidateDirCache(Mo2FsContext* ctx, const std::string& dirPath,
                        bool invalidateLookups = true)
{
  if (ctx == nullptr) {
    return;
  }

  // A child mutation changes exactly its parent's listing.  In particular,
  // an update in the VFS root must not evict every descendant directory blob.
  // Directory rename/removal uses invalidateDirSubtreeCache() below when the
  // descendants themselves are no longer addressable by their old paths.
  std::size_t erased = 0;
  {
    std::scoped_lock lock(ctx->open_dirs_mutex);
    for (auto it = ctx->open_dirs.begin(); it != ctx->open_dirs.end();) {
      if (it->second.path == dirPath) {
        it = ctx->open_dirs.erase(it);
        ++erased;
      } else {
        ++it;
      }
    }
  }
  {
    std::scoped_lock cacheLock(ctx->dir_cache_mutex);
    for (auto it = ctx->dir_cache.begin(); it != ctx->dir_cache.end();) {
      if (it->first == dirPath) {
        it = ctx->dir_cache.erase(it);
        ++erased;
      } else {
        ++it;
      }
    }
    for (auto it = ctx->readdir_blob_cache.begin();
         it != ctx->readdir_blob_cache.end();) {
      if (it->first == dirPath) {
        it = ctx->readdir_blob_cache.erase(it);
        ++erased;
      } else {
        ++it;
      }
    }
    for (auto it = ctx->readdirplus_blob_cache.begin();
         it != ctx->readdirplus_blob_cache.end();) {
      if (it->first == dirPath) {
        it = ctx->readdirplus_blob_cache.erase(it);
        ++erased;
      } else {
        ++it;
      }
    }
  }
  ctx->dir_cache_invalidations.fetch_add(1, std::memory_order_relaxed);
  ctx->dir_cache_entries_erased.fetch_add(erased, std::memory_order_relaxed);
  if (invalidateLookups) {
    invalidateLookupCache(ctx, dirPath);
  }
}

void invalidateDirSubtreeCache(Mo2FsContext* ctx, const std::string& path)
{
  if (ctx == nullptr) return;

  std::size_t erased = 0;
  {
    std::scoped_lock lock(ctx->open_dirs_mutex);
    for (auto it = ctx->open_dirs.begin(); it != ctx->open_dirs.end();) {
      if (it->second.path == path || isStrictDescendantPath(it->second.path, path)) {
        it = ctx->open_dirs.erase(it);
        ++erased;
      } else {
        ++it;
      }
    }
  }
  {
    std::scoped_lock lock(ctx->dir_cache_mutex);
    auto eraseSubtree = [&path, &erased](auto& cache) {
      for (auto it = cache.begin(); it != cache.end();) {
        if (it->first == path || isStrictDescendantPath(it->first, path)) {
          it = cache.erase(it);
          ++erased;
        } else {
          ++it;
        }
      }
    };
    eraseSubtree(ctx->dir_cache);
    eraseSubtree(ctx->readdir_blob_cache);
    eraseSubtree(ctx->readdirplus_blob_cache);
  }
  ctx->dir_cache_invalidations.fetch_add(1, std::memory_order_relaxed);
  ctx->dir_cache_entries_erased.fetch_add(erased, std::memory_order_relaxed);
}

// Invalidate lookup cache entries for a directory whose children changed.
// The lookup cache is keyed by (parent_ino, normalized_child_name), so we
// need to remove all entries with the given parent inode.
void invalidateLookupCache(Mo2FsContext* ctx, const std::string& dirPath)
{
  if (ctx == nullptr) {
    return;
  }

  fuse_ino_t parentIno = 0;
  {
    std::shared_lock lock(ctx->inode_mutex);
    parentIno = dirPath.empty() ? 1 : ctx->inodes->get(dirPath);
  }

  std::scoped_lock lock(ctx->lookup_cache_mutex);
  if (parentIno == 0) {
    if (!ctx->lookup_cache.empty()) {
      ctx->lookup_cache.clear();
      ctx->lookup_cache_invalidations.fetch_add(1, std::memory_order_relaxed);
    }
    return;
  }

  size_t erased = 0;
  for (auto it = ctx->lookup_cache.begin(); it != ctx->lookup_cache.end();) {
    if (it->first.first == parentIno) {
      it = ctx->lookup_cache.erase(it);
      ++erased;
    } else {
      ++it;
    }
  }
  if (erased != 0) {
    ctx->lookup_cache_invalidations.fetch_add(1, std::memory_order_relaxed);
  }
}

void invalidateAttrCache(Mo2FsContext* ctx, fuse_ino_t ino)
{
  if (ctx == nullptr) {
    return;
  }

  std::scoped_lock lock(ctx->attr_cache_mutex);
  ctx->attr_cache.erase(ino);
}

bool isStrictDescendantPath(const std::string& path, const std::string& parent)
{
  if (parent.empty()) {
    return !path.empty();
  }

  return path.size() > parent.size() && path[parent.size()] == '/' &&
         path.compare(0, parent.size(), parent) == 0;
}

bool pathTouchesMutation(const std::string& cachedPath, const std::string& changedPath)
{
  return cachedPath == changedPath ||
         isStrictDescendantPath(cachedPath, changedPath) ||
         isStrictDescendantPath(changedPath, cachedPath);
}

bool shouldTracePath(const std::string& path)
{
  const std::string lower = normalizeForLookup(path);
  return lower.ends_with(".hkx") ||
         lower.find("meshes/actors/") != std::string::npos;
}

bool shouldTraceAudioReadPath(const std::string& path)
{
  const std::string lower = normalizeForLookup(path);
  if (lower.ends_with(".xwm") || lower.ends_with(".fuz") ||
      lower.ends_with(".wav") || lower.ends_with(".ogg") ||
      lower.ends_with(".lip")) {
    return true;
  }

  const bool archive = lower.ends_with(".bsa") || lower.ends_with(".ba2");
  return archive &&
         (lower.find("sound") != std::string::npos ||
          lower.find("voice") != std::string::npos ||
          lower.find("music") != std::string::npos ||
          lower.find("audio") != std::string::npos);
}

void logAudioRead(Mo2FsContext* ctx, const char* event,
                  const std::string& path, const std::string& realPath,
                  off_t offset, size_t requested, ssize_t actual,
                  uint64_t virtualSize, int error)
{
  const uint64_t line =
      ctx->audio_trace_log_count.fetch_add(1, std::memory_order_relaxed);
  if (line < AUDIO_TRACE_LOG_LIMIT) {
    const bool atEof = actual >= 0 &&
                       static_cast<uint64_t>(offset) +
                               static_cast<uint64_t>(actual) >=
                           virtualSize;
    std::fprintf(stderr,
                 "[VFS audio-io] %s path='%s' source='%s' offset=%lld "
                 "requested=%zu actual=%zd size=%llu eof=%d errno=%d "
                 "message='%s'\n",
                 event, path.c_str(), realPath.c_str(),
                 static_cast<long long>(offset), requested, actual,
                 static_cast<unsigned long long>(virtualSize), atEof ? 1 : 0,
                 error, error == 0 ? "" : std::strerror(error));
  } else if (line == AUDIO_TRACE_LOG_LIMIT) {
    std::fprintf(stderr,
                 "[VFS audio-io] per-read log limit reached (%llu); "
                 "aggregate counters remain active\n",
                 static_cast<unsigned long long>(AUDIO_TRACE_LOG_LIMIT));
  }
}

// Clear all node_cache entries whose path is |path|, any descendant under
// |path|/, or any ancestor of |path|. Must be called while tree_mutex is held
// exclusively during mutations.
//
// SUBTREE invalidation matters for any mutation that destroys VfsNodes
// (removeFromTree on a directory, rename of a directory): every descendant
// VfsNode is destroyed too, so every cached pointer into that subtree is
// now dangling. Ancestor invalidation matters because removeFromTree() prunes
// empty parents recursively, so removing a leaf can also destroy cached parent
// directory nodes.
void invalidateNodeCache(Mo2FsContext* ctx, const std::string& path)
{
  if (ctx == nullptr) {
    return;
  }

  // Lock order is tree (exclusive, held by caller) → inode (shared) →
  // node_cache (exclusive).  Keep that order consistent everywhere.
  std::shared_lock ilock(ctx->inode_mutex);
  std::scoped_lock nlock(ctx->node_cache_mutex);

  // O(N) over the cache, but each ino->path lookup is O(1) and N is bounded
  // by the cache size; mutations are infrequent compared to reads.
  for (auto it = ctx->node_cache.begin(); it != ctx->node_cache.end();) {
    const std::string entryPath = ctx->inodes->getPath(it->first);
    if (pathTouchesMutation(entryPath, path)) {
      it = ctx->node_cache.erase(it);
    } else {
      ++it;
    }
  }
}

struct NodeSnapshot
{
  bool found        = false;
  bool is_directory = false;
  bool is_backing   = false;
  uint64_t size     = 0;
  std::chrono::system_clock::time_point mtime;
  std::string real_path;
  mode_t cached_mode = 0;
};

Mo2FsContext* getContext(fuse_req_t req)
{
  return static_cast<Mo2FsContext*>(fuse_req_userdata(req));
}

std::vector<std::string> splitPath(const std::string& path)
{
  std::vector<std::string> out;
  std::string clean = path;
  std::replace(clean.begin(), clean.end(), '\\', '/');

  size_t start = 0;
  while (start < clean.size()) {
    while (start < clean.size() && clean[start] == '/') {
      ++start;
    }
    if (start >= clean.size()) {
      break;
    }
    const size_t end = clean.find('/', start);
    if (end == std::string::npos) {
      out.push_back(clean.substr(start));
      break;
    }
    out.push_back(clean.substr(start, end - start));
    start = end + 1;
  }

  return out;
}

std::string joinPath(const std::string& base, const std::string& name)
{
  if (base.empty()) {
    return name;
  }
  return base + "/" + name;
}

bool isSameOrDescendant(const std::string& path, const std::string& root)
{
  const fs::path normPath = fs::path(path).lexically_normal();
  const fs::path normRoot = fs::path(root).lexically_normal();
  const std::string pathStr = normPath.string();
  std::string rootStr       = normRoot.string();
  if (pathStr == rootStr) {
    return true;
  }
  if (!rootStr.empty() && rootStr.back() != '/') {
    rootStr.push_back('/');
  }
  return pathStr.rfind(rootStr, 0) == 0;
}

std::string originForPath(Mo2FsContext* ctx, const std::string& realPath)
{
  if (ctx != nullptr) {
    const std::string stagingRoot = ctx->overwrite->stagingPath("");
    const std::string overwriteRoot = ctx->overwrite->overwritePath("");
    if (isSameOrDescendant(realPath, stagingRoot)) {
      return "Staging";
    }
    if (isSameOrDescendant(realPath, overwriteRoot)) {
      return "Overwrite";
    }
  }
  return "Mod";
}

std::shared_ptr<VfsRuntimeIndex> runtimeIndex(const Mo2FsContext* ctx)
{
  if (ctx == nullptr) return {};
  std::shared_lock lock(ctx->runtime_index_mutex);
  return ctx->runtime_index;
}

std::string parentPath(const std::string& path)
{
  const std::size_t slash = path.rfind('/');
  return slash == std::string::npos ? std::string{} : path.substr(0, slash);
}

std::string leafName(const std::string& path)
{
  const std::size_t slash = path.rfind('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

NodeSnapshot snapshotFromIndexed(const VfsIndexedNode& node)
{
  NodeSnapshot snap;
  snap.found = true;
  snap.is_directory = node.is_directory;
  snap.is_backing = node.is_backing;
  snap.size = node.size;
  snap.mtime = node.mtime;
  snap.real_path = node.real_path;
  snap.cached_mode = node.cached_mode;
  return snap;
}

void publishRuntimeSnapshot(Mo2FsContext* ctx, const std::string& path,
                            fuse_ino_t ino, const NodeSnapshot& snap)
{
  auto index = runtimeIndex(ctx);
  if (index == nullptr || !snap.found || path.empty() || ino == 0) return;

  const std::string parent = parentPath(path);
  fuse_ino_t parentIno = 1;
  if (!parent.empty()) {
    std::unique_lock lock(ctx->inode_mutex);
    parentIno = ctx->inodes->getOrCreate(parent);
  }

  VfsIndexedNode node = snap.is_directory
      ? VfsRuntimeIndex::makeDirectoryNode(ino, path)
      : VfsRuntimeIndex::makeFileNode(
            ino, path, snap.real_path, snap.is_backing, snap.size,
            snap.mtime, snap.cached_mode);
  index->publish(parentIno, leafName(path), node);
}

void tombstoneRuntimePath(Mo2FsContext* ctx, const std::string& path,
                          fuse_ino_t ino)
{
  auto index = runtimeIndex(ctx);
  if (index == nullptr || path.empty()) return;
  const std::string parent = parentPath(path);
  fuse_ino_t parentIno = 1;
  if (!parent.empty()) {
    std::shared_lock lock(ctx->inode_mutex);
    parentIno = ctx->inodes->get(parent);
  }
  if (parentIno != 0) index->tombstone(parentIno, leafName(path), ino);
}

// Look up the canonical (mod-provided) display name for a child entry.
// Returns the display name if found, or the original name if not.
std::string canonicalChildName(const Mo2FsContext* ctx, const std::string& parentPath,
                               const std::string& name)
{
  std::shared_lock lock(ctx->tree_mutex);
  const VfsNode* parent = parentPath.empty()
      ? &ctx->tree->root
      : ctx->tree->root.resolve(splitPath(parentPath));
  if (parent == nullptr || !parent->is_directory) {
    return name;
  }
  const std::string key = normalizeForLookup(name);
  auto it = parent->dir_info.display_names.find(key);
  if (it != parent->dir_info.display_names.end()) {
    return it->second;
  }
  return name;
}

std::string inodeToPath(const Mo2FsContext* ctx, fuse_ino_t ino, bool* ok)
{
  std::shared_lock lock(ctx->inode_mutex);
  const std::string path = ctx->inodes->getPath(ino);

  if (ino == 1) {
    *ok = true;
    return "";
  }

  *ok = !path.empty();
  return path;
}

NodeSnapshot snapshotForPath(const Mo2FsContext* ctx, const std::string& path)
{
  NodeSnapshot snap;
  std::shared_lock lock(ctx->tree_mutex);

  const VfsNode* node = path.empty() ? &ctx->tree->root : ctx->tree->root.resolve(splitPath(path));
  if (node == nullptr) {
    return snap;
  }

  snap.found        = true;
  snap.is_directory = node->is_directory;
  if (!node->is_directory) {
    snap.real_path  = node->file_info.real_path;
    snap.size       = node->file_info.size;
    snap.mtime      = node->file_info.mtime;
    snap.is_backing = node->file_info.is_backing;
    snap.cached_mode = node->file_info.cached_mode;
  }

  return snap;
}

// Fill a snapshot from an already-resolved VfsNode (caller holds tree_mutex shared).
void snapshotFromNode(const VfsNode* node, NodeSnapshot& snap)
{
  snap.found        = true;
  snap.is_directory = node->is_directory;
  if (!node->is_directory) {
    snap.real_path  = node->file_info.real_path;
    snap.size       = node->file_info.size;
    snap.mtime      = node->file_info.mtime;
    snap.is_backing = node->file_info.is_backing;
    snap.cached_mode = node->file_info.cached_mode;
  }
}

// Resolve a parent inode to its VfsNode*, using the node_cache for O(1) hits.
// Caller must hold tree_mutex (shared).  Falls back to tree walk on cache miss
// and populates the cache.
const VfsNode* resolveByInode(Mo2FsContext* ctx, fuse_ino_t ino)
{
  if (ino == 1) {
    return &ctx->tree->root;
  }

  // Check node_cache (fast path — no splitPath, no tree walk)
  {
    std::scoped_lock nlock(ctx->node_cache_mutex);
    auto cacheIt = ctx->node_cache.find(ino);
    if (cacheIt != ctx->node_cache.end()) {
      return cacheIt->second;
    }
  }

  // Cache miss — resolve via inode→path→tree walk
  std::string path;
  {
    std::shared_lock ilock(ctx->inode_mutex);
    path = ctx->inodes->getPath(ino);
  }
  if (path.empty()) {
    return nullptr;
  }

  const VfsNode* node = ctx->tree->root.resolve(splitPath(path));
  if (node != nullptr) {
    // Validity of node pointer is tied to tree_mutex shared (held by caller)
    // — mutations acquire tree_mutex exclusive and clear the cache before
    // any pointer becomes dangling.  Serialize map write against other
    // concurrent shared readers.
    std::scoped_lock nlock(ctx->node_cache_mutex);
    ctx->node_cache[ino] = node;
  }
  return node;
}

// Combined lookup: resolves parent by inode (cached), looks up child in one
// hash probe, returns canonical name + snapshot.  Single tree_mutex acquisition.
struct LookupResult
{
  bool found         = false;
  std::string canonical_name;
  NodeSnapshot snap;
};

LookupResult lookupChild(Mo2FsContext* ctx, fuse_ino_t parentIno, const char* name)
{
  LookupResult result;
  std::shared_lock lock(ctx->tree_mutex);

  const VfsNode* parent = resolveByInode(ctx, parentIno);
  if (parent == nullptr || !parent->is_directory) {
    return result;
  }

  const std::string key = normalizeForLookup(name);

  // Get canonical display name
  auto nameIt = parent->dir_info.display_names.find(key);
  result.canonical_name = (nameIt != parent->dir_info.display_names.end())
                              ? nameIt->second
                              : std::string(name);

  // Look up child node — single hash probe
  auto childIt = parent->dir_info.children.find(key);
  if (childIt == parent->dir_info.children.end()) {
    return result;
  }

  const VfsNode* child = childIt->second.get();
  result.found = true;
  snapshotFromNode(child, result.snap);

  return result;
}

struct ChildSnapshot
{
  std::string name;
  bool is_dir = false;
  uint64_t size = 0;
  std::chrono::system_clock::time_point mtime;
  std::string real_path;
  mode_t cached_mode = 0;  // permission bits from stat() or VfsNode cache
};

std::vector<ChildSnapshot> listChildrenSnapshot(
    const Mo2FsContext* ctx, const std::string& path, bool* ok)
{
  std::vector<ChildSnapshot> out;
  std::shared_lock lock(ctx->tree_mutex);

  const VfsNode* node = path.empty() ? &ctx->tree->root : ctx->tree->root.resolve(splitPath(path));
  if (node == nullptr || !node->is_directory) {
    *ok = false;
    return out;
  }

  *ok = true;
  for (const auto& [name, child] : node->listChildren()) {
    ChildSnapshot snap;
    snap.name   = name;
    snap.is_dir = child->is_directory;
    if (!child->is_directory) {
      snap.size      = child->file_info.size;
      snap.mtime     = child->file_info.mtime;
      snap.real_path = child->file_info.real_path;

      snap.cached_mode = child->file_info.cached_mode != 0
                             ? child->file_info.cached_mode
                             : static_cast<mode_t>(0644);
    }
    out.push_back(std::move(snap));
  }

  return out;
}

std::vector<Mo2FsContext::DirEntry> buildDirEntries(
    Mo2FsContext* ctx, const std::string& path, fuse_ino_t selfIno, bool* ok)
{
  auto children = listChildrenSnapshot(ctx, path, ok);
  if (!*ok) {
    return {};
  }

  std::vector<Mo2FsContext::DirEntry> entries;
  entries.reserve(children.size() + 2);
  entries.push_back(Mo2FsContext::DirEntry{.ino=selfIno, .name=".", .is_dir=true});

  std::unique_lock lock(ctx->inode_mutex);
  // Nested directories must report their actual parent inode.  Advertising
  // root for every ".." is mostly harmless with plain readdir (the kernel
  // already knows the dentry), but readdirplus turns it into authoritative
  // inode metadata and can poison Wine's directory cache.
  std::string parentPath;
  if (!path.empty()) {
    const size_t slash = path.rfind('/');
    parentPath = slash == std::string::npos ? std::string{} : path.substr(0, slash);
  }
  const fuse_ino_t parentIno = path.empty() ? selfIno
                                            : ctx->inodes->getOrCreate(parentPath);
  entries.push_back(
      Mo2FsContext::DirEntry{.ino=parentIno, .name="..", .is_dir=true});

  for (const auto& child : children) {
    const std::string childPath = joinPath(path, child.name);
    entries.push_back(
        Mo2FsContext::DirEntry{.ino=ctx->inodes->getOrCreate(childPath), .name=child.name,
                               .is_dir=child.is_dir, .size=child.size, .mtime=child.mtime,
                               .real_path=child.real_path, .cached_mode=child.cached_mode});
  }

  return entries;
}

void samplePathStat(Mo2FsContext* ctx, const char* op, const std::string& path,
                    bool miss = false)
{
  if (ctx == nullptr) {
    return;
  }

  const uint64_t sampleTick =
      ctx->path_sample_tick.fetch_add(1, std::memory_order_relaxed) + 1;
  if ((sampleTick % 128) != 0) {
    return;
  }

  std::scoped_lock lock(ctx->path_stats_mutex);
  if (std::strcmp(op, "lookup") == 0) {
    if (miss) {
      ++ctx->lookup_miss_paths[path];
    } else {
      ++ctx->lookup_hit_paths[path];
    }
    return;
  }
  if (std::strcmp(op, "getattr") == 0) {
    ++ctx->getattr_paths[path];
    return;
  }
  if (std::strcmp(op, "readdir") == 0) {
    ++ctx->readdir_paths[path];
    return;
  }
  if (std::strcmp(op, "write") == 0) {
    ++ctx->write_paths[path];
    return;
  }
  if (std::strcmp(op, "create") == 0) {
    ++ctx->create_paths[path];
    return;
  }
  if (std::strcmp(op, "setattr") == 0) {
    ++ctx->setattr_paths[path];
    return;
  }
  if (std::strcmp(op, "flush") == 0) {
    ++ctx->flush_paths[path];
  }
}

std::shared_ptr<std::vector<Mo2FsContext::DirEntry>> getOrBuildDirEntries(
    Mo2FsContext* ctx, const std::string& path, fuse_ino_t ino, bool* ok)
{
  {
    std::scoped_lock lock(ctx->dir_cache_mutex);
    auto it = ctx->dir_cache.find(path);
    if (it != ctx->dir_cache.end()) {
      *ok = true;
      ctx->dir_cache_hits.fetch_add(1, std::memory_order_relaxed);
      return it->second;
    }
  }
  ctx->dir_cache_misses.fetch_add(1, std::memory_order_relaxed);

  auto entries = std::make_shared<std::vector<Mo2FsContext::DirEntry>>(
      buildDirEntries(ctx, path, ino, ok));
  if (!*ok) {
    return {};
  }

  {
    std::scoped_lock lock(ctx->dir_cache_mutex);
    auto [it, inserted] = ctx->dir_cache.emplace(path, entries);
    if (!inserted) {
      return it->second;
    }
  }

  return entries;
}

std::vector<char> buildReaddirBlob(
    fuse_req_t req, const std::vector<Mo2FsContext::DirEntry>& entries)
{
  std::vector<char> blob;
  for (const auto& entry : entries) {
    struct stat st;
    std::memset(&st, 0, sizeof(st));
    st.st_ino = entry.ino;
    if (entry.is_dir) {
      st.st_mode = S_IFDIR | 0755;
    } else {
      // Use cached mode bits (populated during tree snapshot) — no stat() needed.
      mode_t mode = entry.cached_mode != 0 ? entry.cached_mode : static_cast<mode_t>(0644);
      st.st_mode = S_IFREG | regularFileVfsMode(mode);
    }

    const size_t entSize =
        fuse_add_direntry(req, nullptr, 0, entry.name.c_str(), &st, 0);
    if (entSize == 0) {
      continue;
    }
    const off_t nextOff = static_cast<off_t>(blob.size() + entSize);
    const size_t oldLen = blob.size();
    blob.resize(oldLen + entSize);
    fuse_add_direntry(req, blob.data() + oldLen, entSize, entry.name.c_str(), &st,
                      nextOff);
  }
  return blob;
}

std::vector<char> buildReaddirPlusBlob(
    fuse_req_t req, const Mo2FsContext* ctx,
    const std::vector<Mo2FsContext::DirEntry>& entries)
{
  std::vector<char> blob;
  for (const auto& entry : entries) {
    struct fuse_entry_param e;
    std::memset(&e, 0, sizeof(e));
    e.ino           = entry.ino;
    e.attr_timeout  = ttlSeconds(ctx);
    e.entry_timeout = ttlSeconds(ctx);

    if (entry.is_dir) {
      fillStatForDir(&e.attr, entry.ino, ctx->uid, ctx->gid);
    } else {
      fillStatForFile(&e.attr, entry.ino, ctx->uid, ctx->gid, entry.size,
                      entry.mtime, entry.real_path, entry.cached_mode);
    }

    const size_t entSize =
        fuse_add_direntry_plus(req, nullptr, 0, entry.name.c_str(), &e, 0);
    if (entSize == 0) {
      continue;
    }
    const off_t nextOff = static_cast<off_t>(blob.size() + entSize);
    const size_t oldLen = blob.size();
    blob.resize(oldLen + entSize);
    fuse_add_direntry_plus(req, blob.data() + oldLen, entSize, entry.name.c_str(),
                           &e, nextOff);
  }
  return blob;
}

std::shared_ptr<std::vector<char>> getOrBuildReaddirBlob(
    Mo2FsContext* ctx, fuse_req_t req, const std::string& path,
    const std::shared_ptr<std::vector<Mo2FsContext::DirEntry>>& entries)
{
  {
    std::scoped_lock lock(ctx->dir_cache_mutex);
    auto it = ctx->readdir_blob_cache.find(path);
    if (it != ctx->readdir_blob_cache.end()) {
      return it->second;
    }
  }

  auto built = std::make_shared<std::vector<char>>(buildReaddirBlob(req, *entries));
  {
    std::scoped_lock lock(ctx->dir_cache_mutex);
    auto [it, inserted] = ctx->readdir_blob_cache.emplace(path, built);
    if (!inserted) {
      return it->second;
    }
  }
  return built;
}

std::shared_ptr<std::vector<char>> getOrBuildReaddirPlusBlob(
    Mo2FsContext* ctx, fuse_req_t req, const std::string& path,
    const std::shared_ptr<std::vector<Mo2FsContext::DirEntry>>& entries)
{
  {
    std::scoped_lock lock(ctx->dir_cache_mutex);
    auto it = ctx->readdirplus_blob_cache.find(path);
    if (it != ctx->readdirplus_blob_cache.end()) {
      return it->second;
    }
  }

  auto built = std::make_shared<std::vector<char>>(
      buildReaddirPlusBlob(req, ctx, *entries));
  {
    std::scoped_lock lock(ctx->dir_cache_mutex);
    auto [it, inserted] = ctx->readdirplus_blob_cache.emplace(path, built);
    if (!inserted) {
      return it->second;
    }
  }
  return built;
}

void pruneRetainedReadOnlyFds(Mo2FsContext* ctx)
{
  if (ctx == nullptr) {
    return;
  }

  std::scoped_lock lock(ctx->open_files_mutex);
  size_t retained = 0;
  for (const auto& [fh, of] : ctx->open_files) {
    (void)fh;
    if (!of.writable && of.fd >= 0) {
      ++retained;
    }
  }
  if (retained <= MAX_RETAINED_RO_FDS) {
    return;
  }

  while (retained > MAX_RETAINED_RO_FDS) {
    auto victim = ctx->open_files.end();
    for (auto it = ctx->open_files.begin(); it != ctx->open_files.end(); ++it) {
      if (it->second.writable || it->second.fd < 0) {
        continue;
      }
      if (victim == ctx->open_files.end() ||
          it->second.last_read_tick < victim->second.last_read_tick) {
        victim = it;
      }
    }
    if (victim == ctx->open_files.end()) {
      return;
    }

    close(victim->second.fd);
    victim->second.fd = -1;
    ctx->retained_ro_fd_evictions.fetch_add(1, std::memory_order_relaxed);
    --retained;
  }
}

void fillStatForDir(struct stat* st, fuse_ino_t ino, uid_t uid, gid_t gid)
{
  std::memset(st, 0, sizeof(struct stat));
  st->st_ino   = ino;
  st->st_mode  = S_IFDIR | 0755;
  st->st_nlink = 2;
  st->st_uid   = uid;
  st->st_gid   = gid;
  // Keep synthetic directory timestamps stable so kernel/user-space attr caching
  // stays effective across repeated getattr/readdir probes.
  constexpr time_t kVirtualDirTime = 946684800;  // 2000-01-01 00:00:00 UTC
  st->st_mtim.tv_sec = kVirtualDirTime;
  st->st_atim.tv_sec = kVirtualDirTime;
  st->st_ctim.tv_sec = kVirtualDirTime;
}

mode_t regularFileVfsMode(mode_t sourceMode)
{
  mode_t mode = sourceMode != 0 ? sourceMode : static_cast<mode_t>(0644);
  // The merged VFS is copy-on-write, so expose source files as writable even
  // when their physical base-game or mod files are read-only.
  mode |= S_IRUSR | S_IWUSR;
  return mode;
}

void fillStatForFile(struct stat* st, fuse_ino_t ino, uid_t uid, gid_t gid,
                     uint64_t size,
                     const std::chrono::system_clock::time_point& mtime,
                     const std::string& real_path,
                     mode_t cached_mode)
{
  std::memset(st, 0, sizeof(struct stat));
  st->st_ino   = ino;
  st->st_nlink = 1;
  st->st_uid   = uid;
  st->st_gid   = gid;
  st->st_size  = static_cast<off_t>(size);

  // Runtime metadata replies are authoritative from the immutable VFS tree.
  // Never resolve the backing path here; base-game paths are deliberately
  // relative to backing_dir_fd and a plain stat() would be incorrect anyway.
  (void)real_path;
  const mode_t mode = cached_mode != 0 ? cached_mode : static_cast<mode_t>(0644);
  st->st_mode = S_IFREG | regularFileVfsMode(mode);

  const auto secs = std::chrono::duration_cast<std::chrono::seconds>(
      mtime.time_since_epoch());
  st->st_mtim.tv_sec = secs.count();
  st->st_ctim.tv_sec = secs.count();
  st->st_atim.tv_sec = secs.count();
}

void replyEntryFromSnapshot(fuse_req_t req, const Mo2FsContext* ctx, fuse_ino_t ino,
                            const NodeSnapshot& snap)
{
  struct fuse_entry_param e;
  std::memset(&e, 0, sizeof(e));
  e.ino           = ino;
  e.attr_timeout  = ttlSeconds(ctx);
  e.entry_timeout = ttlSeconds(ctx);

  if (snap.is_directory) {
    fillStatForDir(&e.attr, ino, ctx->uid, ctx->gid);
  } else {
    fillStatForFile(&e.attr, ino, ctx->uid, ctx->gid, snap.size, snap.mtime,
                    snap.real_path, snap.cached_mode);
  }

  fuse_reply_entry(req, &e);
}

bool isWritableOpen(int flags)
{
  return (flags & O_WRONLY) != 0 || (flags & O_RDWR) != 0;
}

std::chrono::system_clock::time_point fileMtimeOrNow(const std::string& path)
{
  std::error_code ec;
  const auto mtime = fs::last_write_time(path, ec);
  if (ec) {
    return std::chrono::system_clock::now();
  }

  const auto nowFs  = fs::file_time_type::clock::now();
  const auto nowSys = std::chrono::system_clock::now();
  return nowSys + std::chrono::duration_cast<std::chrono::system_clock::duration>(
                      mtime - nowFs);
}

std::chrono::system_clock::time_point timePointFromTimespec(const struct timespec& ts)
{
  return std::chrono::system_clock::time_point(std::chrono::seconds(ts.tv_sec) +
                                               std::chrono::nanoseconds(ts.tv_nsec));
}

void updateFileNodeKnown(Mo2FsContext* ctx, const std::string& relative,
                         const std::string& realPath, const std::string& origin,
                         uint64_t size,
                         std::chrono::system_clock::time_point mtime)
{
  std::scoped_lock namespaceLock(ctx->namespace_mutation_mutex);
  std::unique_lock lock(ctx->tree_mutex);
  ctx->tree->root.insertFile(splitPath(relative), realPath, size, mtime, origin);
  invalidateNodeCache(ctx, relative);
  lock.unlock();

  fuse_ino_t ino = 0;
  {
    std::unique_lock ilock(ctx->inode_mutex);
    ino = ctx->inodes->getOrCreate(relative);
  }
  if (ino != 0) {
    invalidateAttrCache(ctx, ino);
    const NodeSnapshot snap = snapshotForPath(ctx, relative);
    publishRuntimeSnapshot(ctx, relative, ino, snap);
  }
}

void updateFileNode(Mo2FsContext* ctx, const std::string& relative,
                    const std::string& realPath, const std::string& origin)
{
  std::error_code ec;
  const uint64_t size = static_cast<uint64_t>(fs::file_size(realPath, ec));
  const auto mtime    = fileMtimeOrNow(realPath);
  updateFileNodeKnown(ctx, relative, realPath, origin, ec ? 0 : size, mtime);
}

bool markOpenFileDirty(Mo2FsContext* ctx, uint64_t fh, uint64_t endOffset)
{
  if (ctx == nullptr) {
    return false;
  }

  std::scoped_lock lock(ctx->open_files_mutex);
  auto it = ctx->open_files.find(fh);
  if (it == ctx->open_files.end()) {
    return false;
  }

  it->second.metadata_dirty = true;
  it->second.content_dirty = true;
  it->second.virtual_size = std::max<uint64_t>(it->second.virtual_size, endOffset);
  it->second.virtual_mtime = std::chrono::system_clock::now();
  const bool invalidateRange = it->second.range_invalidation_pending;
  it->second.range_invalidation_pending = false;
  return invalidateRange;
}

void invalidateKernelContent(Mo2FsContext* ctx, fuse_ino_t ino,
                             off_t offset, off_t length)
{
  if (ctx == nullptr || ctx->session == nullptr || ino == 0) return;

  bool coveredByWholeInvalidation = false;
  {
    std::scoped_lock lock(ctx->kernel_invalidation_mutex);
    if (ctx->stop_kernel_invalidations) return;

    // Whole-inode invalidation supersedes any queued ranges for this inode.
    // Likewise, a queued whole-inode request already covers this one.
    if (offset == 0 && length == 0) {
      std::erase_if(ctx->kernel_invalidations,
                    [ino](const Mo2FsContext::KernelInvalidation& pending) {
                      return pending.ino == ino;
                    });
    } else {
      const auto whole = std::find_if(
          ctx->kernel_invalidations.begin(), ctx->kernel_invalidations.end(),
          [ino](const Mo2FsContext::KernelInvalidation& pending) {
            return pending.ino == ino && pending.offset == 0 &&
                   pending.length == 0;
          });
      if (whole != ctx->kernel_invalidations.end()) {
        coveredByWholeInvalidation = true;
      }
    }
    if (!coveredByWholeInvalidation) {
      ctx->kernel_invalidations.push_back({ino, offset, length});
    }
  }
  if (!coveredByWholeInvalidation) {
    ctx->kernel_invalidation_cv.notify_one();
  }
  invalidateAttrCache(ctx, ino);
}

void runKernelInvalidations(Mo2FsContext* ctx)
{
  if (ctx == nullptr) return;

  for (;;) {
    Mo2FsContext::KernelInvalidation pending;
    {
      std::unique_lock lock(ctx->kernel_invalidation_mutex);
      ctx->kernel_invalidation_cv.wait(lock, [ctx]() {
        return ctx->stop_kernel_invalidations ||
               !ctx->kernel_invalidations.empty();
      });
      if (ctx->stop_kernel_invalidations) return;
      pending = ctx->kernel_invalidations.front();
      ctx->kernel_invalidations.pop_front();
    }

    const int result = fuse_lowlevel_notify_inval_inode(
        ctx->session, pending.ino, pending.offset, pending.length);
    if (result != 0 && result != -ENOENT) {
      std::fprintf(stderr,
                   "[VFS] content invalidation failed ino=%llu off=%lld "
                   "len=%lld rc=%d\n",
                   static_cast<unsigned long long>(pending.ino),
                   static_cast<long long>(pending.offset),
                   static_cast<long long>(pending.length), result);
    }
  }
}

void stopKernelInvalidations(Mo2FsContext* ctx)
{
  if (ctx == nullptr) return;
  {
    std::scoped_lock lock(ctx->kernel_invalidation_mutex);
    ctx->stop_kernel_invalidations = true;
    ctx->kernel_invalidations.clear();
  }
  ctx->kernel_invalidation_cv.notify_all();
}

void flushDirtyOpenFileMetadata(Mo2FsContext* ctx, uint64_t fh, fuse_ino_t ino)
{
  if (ctx == nullptr) {
    return;
  }

  std::string relativePath;
  std::string realPath;
  uint64_t size = 0;
  std::chrono::system_clock::time_point mtime;
  bool metadataDirty = false;
  bool contentDirty = false;
  {
    std::scoped_lock lock(ctx->open_files_mutex);
    auto it = ctx->open_files.find(fh);
    if (it == ctx->open_files.end()) {
      return;
    }
    metadataDirty = it->second.metadata_dirty;
    contentDirty = it->second.content_dirty;
    if (!metadataDirty && !contentDirty) return;
    relativePath = it->second.relative_path;
    realPath     = it->second.real_path;
    size         = it->second.virtual_size;
    mtime        = it->second.virtual_mtime;
    it->second.metadata_dirty = false;
    it->second.content_dirty = false;
  }

  if (metadataDirty && !relativePath.empty() && !realPath.empty()) {
    updateFileNodeKnown(ctx, relativePath, realPath, originForPath(ctx, realPath),
                        size, mtime);
  }
  if (contentDirty) {
    // A whole-inode invalidation after the completed handle operation closes
    // the stale-tail/size hole left by range invalidation. This is once per
    // flush/release, not once per shader-cache write chunk.
    invalidateKernelContent(ctx, ino, 0, 0);
  }
}

bool ensureWritableOpenFile(Mo2FsContext* ctx, uint64_t fh, fuse_ino_t ino,
                            int* outFd, std::string* outRelativePath)
{
  if (ctx == nullptr || outFd == nullptr) {
    return false;
  }

  int fd = -1;
  std::string relativePath;
  std::string realPath;
  bool writable = false;
  bool cowPending = false;
  bool isBacking = false;
  {
    std::shared_lock lock(ctx->open_files_mutex);
    auto it = ctx->open_files.find(fh);
    if (it == ctx->open_files.end()) {
      errno = EBADF;
      return false;
    }
    fd           = it->second.fd;
    writable     = it->second.writable;
    cowPending   = it->second.cow_pending;
    isBacking    = it->second.is_backing;
    relativePath = it->second.relative_path;
    realPath     = it->second.real_path;
  }

  if (!writable) {
    errno = EACCES;
    return false;
  }

  if (cowPending) {
    try {
      std::string newPath;
      if (isBacking && ctx->backing_dir_fd >= 0) {
        newPath = ctx->overwrite->copyOnWriteFromFd(ctx->backing_dir_fd, relativePath);
      } else {
        newPath = ctx->overwrite->copyOnWrite(realPath, relativePath);
      }

      int newFd = open(newPath.c_str(), O_RDWR | O_CLOEXEC);
      if (newFd < 0) {
        return false;
      }
      if (fd >= 0) {
        close(fd);
      }

      {
        std::scoped_lock lock(ctx->open_files_mutex);
        auto it = ctx->open_files.find(fh);
        if (it == ctx->open_files.end()) {
          close(newFd);
          errno = EBADF;
          return false;
        }
        it->second.fd          = newFd;
        it->second.real_path   = newPath;
        it->second.is_backing  = false;
        it->second.cow_pending = false;
        it->second.range_invalidation_pending = true;
      }
      fd = newFd;
      realPath = newPath;
      updateFileNode(ctx, relativePath, newPath, originForPath(ctx, newPath));
      fuse_lowlevel_notify_inval_inode(ctx->session, ino, 0, 0);
    } catch (...) {
      errno = EIO;
      return false;
    }
  }

  if (fd < 0) {
    errno = EBADF;
    return false;
  }

  *outFd = fd;
  if (outRelativePath != nullptr) {
    *outRelativePath = relativePath;
  }
  return true;
}

bool ensureReadableOpenFile(Mo2FsContext* ctx, uint64_t fh, int* outFd,
                            bool* outCloseWhenDone)
{
  if (ctx == nullptr || outFd == nullptr || outCloseWhenDone == nullptr) {
    return false;
  }

  int fd = -1;
  std::string realPath;
  bool isBacking = false;
  {
    std::shared_lock lock(ctx->open_files_mutex);
    auto it = ctx->open_files.find(fh);
    if (it == ctx->open_files.end()) {
      errno = EBADF;
      return false;
    }
    fd        = it->second.fd;
    realPath  = it->second.real_path;
    isBacking = it->second.is_backing;
  }

  if (fd >= 0) {
    *outFd = fd;
    *outCloseWhenDone = false;
    return true;
  }

  if (isBacking && ctx->backing_dir_fd >= 0) {
    fd = openat(ctx->backing_dir_fd, realPath.c_str(), O_RDONLY | O_CLOEXEC);
  } else {
    fd = open(realPath.c_str(), O_RDONLY | O_CLOEXEC);
  }
  if (fd < 0) {
    return false;
  }
  *outFd = fd;
  *outCloseWhenDone = true;
  return true;
}

}  // namespace

void mo2RunKernelInvalidations(Mo2FsContext* ctx)
{
  runKernelInvalidations(ctx);
}

void mo2StopKernelInvalidations(Mo2FsContext* ctx)
{
  stopKernelInvalidations(ctx);
}

std::size_t mo2PrewarmLookupIndex(Mo2FsContext* ctx)
{
  if (ctx == nullptr || ctx->tree == nullptr || ctx->inodes == nullptr) {
    return 0;
  }

  std::shared_ptr<VfsRuntimeIndex> index;
  {
    std::shared_lock treeLock(ctx->tree_mutex);
    std::unique_lock inodeLock(ctx->inode_mutex);
    const std::size_t expected =
        ctx->tree->file_count + ctx->tree->dir_count + 1;
    ctx->inodes->reserve(expected);
    index = VfsRuntimeIndex::build(*ctx->tree, *ctx->inodes);
  }
  {
    std::unique_lock lock(ctx->runtime_index_mutex);
    ctx->runtime_index = index;
  }
  {
    std::scoped_lock lock(ctx->lookup_cache_mutex);
    ctx->lookup_cache.clear();
  }
  {
    std::scoped_lock lock(ctx->attr_cache_mutex);
    ctx->attr_cache.clear();
  }
  return index->baseLookupCount();
}

void mo2_init(void* userdata, struct fuse_conn_info* conn)
{
  auto* ctx = static_cast<Mo2FsContext*>(userdata);

  if (ctx != nullptr) {
    ctx->trace_audio_reads =
        std::getenv("FLUORINE_VFS_TRACE_AUDIO_READS") != nullptr;
    if (ctx->trace_audio_reads) {
      std::fprintf(stderr,
                   "[VFS audio-io] enabled: matching reads use streamed pread "
                   "diagnostics (log limit %llu)\n",
                   static_cast<unsigned long long>(AUDIO_TRACE_LOG_LIMIT));
    }
  }

  // Bump RLIMIT_NOFILE.  We hold one real fd per open file so games that
  // stream hundreds of BSAs concurrently would otherwise hit the default
  // 1024 soft limit.  Raise to hard limit (or a sane cap).
  {
    struct rlimit rl{};
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
      rlim_t wanted = rl.rlim_max;
      if (wanted == RLIM_INFINITY || wanted > 1048576) {
        wanted = 1048576;
      }
      if (rl.rlim_cur < wanted) {
        rl.rlim_cur = wanted;
        if (setrlimit(RLIMIT_NOFILE, &rl) != 0) {
          std::fprintf(stderr, "[VFS] setrlimit(NOFILE) failed: errno=%d\n", errno);
        } else {
          std::fprintf(stderr, "[VFS] RLIMIT_NOFILE raised to %llu\n",
                       static_cast<unsigned long long>(rl.rlim_cur));
        }
      }
    }
  }

  // ── Disable AUTO_INVAL_DATA (CRITICAL for performance) ──
  // AUTO_INVAL_DATA forces a getattr() on EVERY read() to check mtime,
  // completely bypassing attr_timeout.  This alone causes ~4x throughput
  // reduction.  Our VFS tree is immutable during a session — we handle
  // invalidation ourselves via fuse_lowlevel_notify_inval_inode() when
  // files are created/renamed/deleted through our own handlers.
  //
  // ctx->cache_disabled leaves this feature enabled instead, so the kernel
  // re-validates via getattr on every read rather than trusting our own
  // invalidation calls — see the TTL/keep_cache toggle above.
  if (!ctx->cache_disabled) {
    fuseDropFeature(conn, FUSE_CAP_AUTO_INVAL_DATA);
  }

  // Let us control page cache invalidation explicitly.
  if (fuseHasFeature(conn, FUSE_CAP_EXPLICIT_INVAL_DATA)) {
    fuseRequestFeature(conn, FUSE_CAP_EXPLICIT_INVAL_DATA);
  }

  // Keep Wine on plain readdir by default. READDIRPLUS remains an opt-in
  // visible as authoritative directory-cache state and has caused nested
  // plugin resources to become intermittently unresolvable under Proton.
  // Metadata replies remain cheap because they come from the immutable tree.
  if (fuseHasFeature(conn, FUSE_CAP_READDIRPLUS)) {
    if (ctx->readdirplus_enabled) {
      fuseRequestFeature(conn, FUSE_CAP_READDIRPLUS);
    } else {
      fuseDropFeature(conn, FUSE_CAP_READDIRPLUS);
      fuseDropFeature(conn, FUSE_CAP_READDIRPLUS_AUTO);
    }
  }

#ifdef FUSE_CAP_NO_OPENDIR_SUPPORT
  ctx->no_opendir_supported =
      !ctx->disable_no_opendir &&
      fuseHasFeature(conn, FUSE_CAP_NO_OPENDIR_SUPPORT);
  if (ctx->no_opendir_supported) {
    fuseRequestFeature(conn, FUSE_CAP_NO_OPENDIR_SUPPORT);
  }
#endif

  // NOTE: FUSE_CAP_WRITEBACK_CACHE intentionally NOT enabled.
  // It causes extra getattr calls for cache coherency, which hurts
  // our read-heavy VFS more than the write buffering helps.

  // Cache symlink targets in the kernel page cache.
  if (fuseHasFeature(conn, FUSE_CAP_CACHE_SYMLINKS)) {
    fuseRequestFeature(conn, FUSE_CAP_CACHE_SYMLINKS);
  }

  // Allow concurrent lookup()/readdir() on the same directory.
  if (fuseHasFeature(conn, FUSE_CAP_PARALLEL_DIROPS)) {
    fuseRequestFeature(conn, FUSE_CAP_PARALLEL_DIROPS);
  }

  // Splice: reduce kernel↔userspace data copies for reads and writes.
  if (fuseHasFeature(conn, FUSE_CAP_SPLICE_WRITE)) {
    fuseRequestFeature(conn, FUSE_CAP_SPLICE_WRITE);
  }
  if (fuseHasFeature(conn, FUSE_CAP_SPLICE_MOVE)) {
    fuseRequestFeature(conn, FUSE_CAP_SPLICE_MOVE);
  }
  if (fuseHasFeature(conn, FUSE_CAP_SPLICE_READ)) {
    fuseRequestFeature(conn, FUSE_CAP_SPLICE_READ);
  }

  // Allow concurrent submission of split direct I/O requests.
  // Harmless when not triggered; helps if Wine opens files with O_DIRECT.
  if (fuseHasFeature(conn, FUSE_CAP_ASYNC_DIO)) {
    fuseRequestFeature(conn, FUSE_CAP_ASYNC_DIO);
  }

  // Softer dentry invalidation: mark entries as expired rather than
  // forcefully removing them, reducing cascading cache evictions.
  if (fuseHasFeature(conn, FUSE_CAP_EXPIRE_ONLY)) {
    fuseRequestFeature(conn, FUSE_CAP_EXPIRE_ONLY);
  }

  // Maximize async I/O slots (default is 12).  The kernel will still only
  // dispatch as many as there are actual concurrent requests, so higher
  // values just raise the ceiling without wasting memory.
  conn->max_background      = 32767;
  conn->congestion_threshold = 24576;

  // Request large read/write buffers.  libfuse sizes its receive buffer at
  // session creation time based on (max_write + header); overshooting here
  // yields a mismatch where the kernel expects room for a big write but
  // libfuse's buffer is smaller, and reads from /dev/fuse fail with EINVAL.
  // Stay conservative: 1MB matches libfuse's bufsize ceiling on most kernels
  // and is the largest value the kernel's FUSE driver typically accepts.
  constexpr unsigned int ONE_MB = 1 * 1024 * 1024;
  if (conn->max_readahead < ONE_MB) {
    conn->max_readahead = ONE_MB;
  }
  if (conn->max_write < ONE_MB) {
    conn->max_write = ONE_MB;
  }
  // max_read MUST match the "-o max_read=..." mount option passed to
  // fuse_session_new() or libfuse errors out with
  //   "init() and fuse_session_new() requested different maximum read size"
  conn->max_read = ONE_MB;

  std::fprintf(stderr,
               "[VFS] init: auto_inval=%d explicit_inval=%d readdirplus=%d "
               "no_opendir=%d max_bg=%u max_readahead=%u\n",
               fuseWantsFeature(conn, FUSE_CAP_AUTO_INVAL_DATA) ? 1 : 0,
               fuseWantsFeature(conn, FUSE_CAP_EXPLICIT_INVAL_DATA) ? 1 : 0,
               fuseWantsFeature(conn, FUSE_CAP_READDIRPLUS) ? 1 : 0,
#ifdef FUSE_CAP_NO_OPENDIR_SUPPORT
               fuseWantsFeature(conn, FUSE_CAP_NO_OPENDIR_SUPPORT) ? 1 : 0,
#else
               0,
#endif
               conn->max_background, conn->max_readahead);
  std::fprintf(stderr,
               "[VFS] init_caps: libfuse=%s headers=%d.%d proto=%u.%u "
               "capable=0x%08x want=0x%08x max_write=%u max_read=%u\n",
               fuse_pkgversion(), FUSE_MAJOR_VERSION, FUSE_MINOR_VERSION,
               conn->proto_major, conn->proto_minor, conn->capable,
               conn->want, conn->max_write, conn->max_read);
  std::fprintf(stderr,
               "[VFS] feature_gates: no_opendir_disabled=%d "
               "readdirplus_enabled=%d zero_file_flags=%d\n",
               ctx->disable_no_opendir ? 1 : 0,
               ctx->readdirplus_enabled ? 1 : 0,
               ctx->zero_file_flags ? 1 : 0);
}

void mo2_lookup(fuse_req_t req, fuse_ino_t parent, const char* name)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || name == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }
  OpTimer _t(&ctx->lookup_ns, "lookup");
  ctx->lookup_count.fetch_add(1, std::memory_order_relaxed);
  maybeLogCounters(ctx);

  auto index = runtimeIndex(ctx);
  VfsIndexedLookup indexed;
  if (index != nullptr) indexed = index->lookup(parent, name);

  if (indexed.node.has_value()) {
    ctx->lookup_cache_hits.fetch_add(1, std::memory_order_relaxed);
    if (indexed.source == VfsLookupSource::Base) {
      ctx->lookup_base_hits.fetch_add(1, std::memory_order_relaxed);
    } else {
      ctx->lookup_overlay_hits.fetch_add(1, std::memory_order_relaxed);
    }
    _t.path = indexed.node->virtual_path;
    samplePathStat(ctx, "lookup", _t.path, false);

    struct fuse_entry_param e {};
    e.ino = indexed.node->ino;
    e.attr_timeout = ttlSeconds(ctx);
    e.entry_timeout = ttlSeconds(ctx);
    if (indexed.node->is_directory) {
      fillStatForDir(&e.attr, indexed.node->ino, ctx->uid, ctx->gid);
    } else {
      fillStatForFile(&e.attr, indexed.node->ino, ctx->uid, ctx->gid,
                      indexed.node->size, indexed.node->mtime,
                      indexed.node->real_path, indexed.node->cached_mode);
    }
    fuse_reply_entry(req, &e);
    return;
  }

  if (indexed.source == VfsLookupSource::Negative ||
      indexed.source == VfsLookupSource::Tombstone) {
    ctx->lookup_cache_hits.fetch_add(1, std::memory_order_relaxed);
    if (indexed.source == VfsLookupSource::Negative) {
      ctx->lookup_negative_hits.fetch_add(1, std::memory_order_relaxed);
    } else {
      ctx->lookup_tombstones.fetch_add(1, std::memory_order_relaxed);
    }
    struct fuse_entry_param e {};
    e.attr_timeout = negativeTtlSeconds(ctx);
    e.entry_timeout = negativeTtlSeconds(ctx);
    fuse_reply_entry(req, &e);
    return;
  }

  // This is a first-time absent probe, not a failure to find catalog data.
  // Verify against the live tree before answering ENOENT so an index invariant
  // violation self-heals and can never make a visible file unreachable.
  ctx->lookup_cache_misses.fetch_add(1, std::memory_order_relaxed);
  ctx->lookup_negative_first.fetch_add(1, std::memory_order_relaxed);
  const auto lr = lookupChild(ctx, parent, name);
  if (!lr.found) {
    std::shared_ptr<const VfsArchiveMemberIndex> archiveMembers;
    {
      std::shared_lock lock(ctx->tree_mutex);
      archiveMembers = ctx->archive_members;
    }
    if (archiveMembers != nullptr && archiveMembers->memberCount() != 0) {
      bool parentOk = false;
      const std::string parentPath = inodeToPath(ctx, parent, &parentOk);
      if (parentOk) {
        const std::string candidate = normalizeForLookup(joinPath(parentPath, name));
        if (archiveMembers->mightContain(candidate)) {
          ctx->archive_lookup_candidates.fetch_add(1, std::memory_order_relaxed);
        }
      }
    }

    // USVFS-parity: games (e.g. Starfield) sometimes skip mkdir for intermediate
    // directories and go straight to CreateFile("ShaderCache/Lighting/X.pso").
    // USVFS intercepts at Win32 level and auto-creates parent dirs in overwrite.
    // FUSE sits below the kernel path-resolver, so ENOENT here prevents mo2_create
    // from ever being called. Work around it: if the requested name looks like a
    // directory (no '.' — file-like names are excluded to avoid spurious dirs),
    // auto-create it in staging so the kernel can continue resolving the path.
    // createDirectory uses create_directories internally, so staging parent dirs
    // are also created if the parent came from overwrite rather than this session.
    // USVFS-parity: games sometimes skip mkdir for intermediate directories and
    // go straight to CreateFile("ShaderCache/Lighting/X.pso"). The kernel's
    // path-resolver needs a positive inode for each component before it can
    // call mo2_create. Insert a phantom virtual directory — no physical creation.
    // createFile() calls create_directories on the parent path, so the real dir
    // will materialize on disk only when a file is actually written inside it.
    // Gated behind auto_create_dirs: most games (e.g. BG3) must not have
    // phantom directories injected because they shadow real entries resolved
    // through normal VFS traversal.
    const std::string_view nameView(name);
    if (ctx->auto_create_dirs && nameView.find('.') == std::string_view::npos) {
      bool parentOk = false;
      const std::string parentPath = inodeToPath(ctx, parent, &parentOk);
      if (parentOk) {
        std::scoped_lock namespaceLock(ctx->namespace_mutation_mutex);
        const std::string childName = canonicalChildName(ctx, parentPath, name);
        const std::string childPath = joinPath(parentPath, childName);
        {
          std::unique_lock lock(ctx->tree_mutex);
          ctx->tree->root.insertDirectory(splitPath(childPath));
          ++ctx->tree->dir_count;
          invalidateNodeCache(ctx, childPath);
        }
        // This lookup key was absent when we entered the miss path and is
        // populated immediately below. Existing sibling lookup entries remain
        // valid, so do not scan/erase the entire prewarmed parent cache. Only
        // its directory listing changed.
        invalidateDirCache(ctx, parentPath, false);
        fuse_ino_t dirIno;
        {
          std::unique_lock lock(ctx->inode_mutex);
          dirIno = ctx->inodes->getOrCreate(childPath);
        }
        NodeSnapshot snap;
        snap.found = true;
        snap.is_directory = true;
        publishRuntimeSnapshot(ctx, childPath, dirIno, snap);
        struct fuse_entry_param e {};
        e.ino           = dirIno;
        e.attr_timeout  = ttlSeconds(ctx);
        e.entry_timeout = ttlSeconds(ctx);
        fillStatForDir(&e.attr, dirIno, ctx->uid, ctx->gid);
        fuse_reply_entry(req, &e);
        return;
      }
    }

    if (index != nullptr && !ctx->cache_disabled) {
      index->recordNegative(parent, name, std::chrono::seconds(3600));
    }
    struct fuse_entry_param e {};
    e.attr_timeout  = negativeTtlSeconds(ctx);
    e.entry_timeout = negativeTtlSeconds(ctx);
    fuse_reply_entry(req, &e);
    return;
  }

  // Build child path for inode allocation
  bool ok = false;
  const std::string parentPath = inodeToPath(ctx, parent, &ok);
  if (!ok) {
    fuse_reply_err(req, ENOENT);
    return;
  }
  const std::string childPath = joinPath(parentPath, lr.canonical_name);
  _t.path = childPath;
  samplePathStat(ctx, "lookup", childPath, false);

  fuse_ino_t childIno = 0;
  {
    std::shared_lock lock(ctx->inode_mutex);
    childIno = ctx->inodes->get(childPath);
  }
  if (childIno == 0) {
    std::unique_lock lock(ctx->inode_mutex);
    childIno = ctx->inodes->getOrCreate(childPath);
  }

  ctx->lookup_index_invariant_misses.fetch_add(1, std::memory_order_relaxed);
  publishRuntimeSnapshot(ctx, childPath, childIno, lr.snap);

  // Build the entry_param for the kernel dcache.
  struct fuse_entry_param e {};
  e.ino           = childIno;
  e.attr_timeout  = ttlSeconds(ctx);
  e.entry_timeout = ttlSeconds(ctx);
  if (lr.snap.is_directory) {
    fillStatForDir(&e.attr, childIno, ctx->uid, ctx->gid);
  } else {
    fillStatForFile(&e.attr, childIno, ctx->uid, ctx->gid, lr.snap.size, lr.snap.mtime,
                    lr.snap.real_path, lr.snap.cached_mode);
  }

  fuse_reply_entry(req, &e);
}

void mo2_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* /*fi*/)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }
  OpTimer _t(&ctx->getattr_ns, "getattr");
  ctx->getattr_count.fetch_add(1, std::memory_order_relaxed);
  maybeLogCounters(ctx);

  auto index = runtimeIndex(ctx);
  if (index != nullptr) {
    auto node = index->node(ino);
    if (node.has_value()) {
      ctx->attr_cache_hits.fetch_add(1, std::memory_order_relaxed);
      ctx->node_index_hits.fetch_add(1, std::memory_order_relaxed);
      _t.path = node->virtual_path.empty() ? "/" : node->virtual_path;
      struct stat st {};
      if (node->is_directory) {
        fillStatForDir(&st, ino, ctx->uid, ctx->gid);
      } else {
        fillStatForFile(&st, ino, ctx->uid, ctx->gid, node->size, node->mtime,
                        node->real_path, node->cached_mode);
      }
      fuse_reply_attr(req, &st, ttlSeconds(ctx));
      return;
    }
  }

  ctx->attr_cache_misses.fetch_add(1, std::memory_order_relaxed);
  ctx->node_index_misses.fetch_add(1, std::memory_order_relaxed);
  NodeSnapshot snap;
  {
    std::shared_lock lock(ctx->tree_mutex);
    const VfsNode* node = resolveByInode(ctx, ino);
    if (node == nullptr) {
      fuse_reply_err(req, ENOENT);
      return;
    }
    snapshotFromNode(node, snap);
  }

  bool pathOk = false;
  const std::string path = inodeToPath(ctx, ino, &pathOk);
  if (pathOk && !path.empty()) publishRuntimeSnapshot(ctx, path, ino, snap);

  struct stat st;
  if (snap.is_directory) {
    fillStatForDir(&st, ino, ctx->uid, ctx->gid);
  } else {
    fillStatForFile(&st, ino, ctx->uid, ctx->gid, snap.size, snap.mtime,
                    snap.real_path, snap.cached_mode);
  }

  fuse_reply_attr(req, &st, ttlSeconds(ctx));
}

void mo2_opendir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || fi == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  ctx->opendir_count.fetch_add(1, std::memory_order_relaxed);
  auto index = runtimeIndex(ctx);
  const auto node = index != nullptr ? index->node(ino)
                                     : std::optional<VfsIndexedNode>{};
  if (!node.has_value()) {
    fuse_reply_err(req, ENOENT);
    return;
  }
  if (!node->is_directory) {
    fuse_reply_err(req, ENOTDIR);
    return;
  }
  if (ctx->no_opendir_supported) {
    // With FUSE_CAP_NO_OPENDIR_SUPPORT, one ENOSYS makes the kernel treat this
    // and all future opendir calls as successful without userspace messages.
    fuse_reply_err(req, ENOSYS);
    return;
  }

  fi->fh            = 0;
  fi->keep_cache    = vfsKeepCache(ctx);   // Don't invalidate cached readdir on reopen
  fi->cache_readdir = vfsKeepCache(ctx);   // Let kernel cache directory entries
  fuse_reply_open(req, fi);
}

void mo2_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                 struct fuse_file_info* fi)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || off < 0) {
    fuse_reply_err(req, EINVAL);
    return;
  }
  OpTimer _t(&ctx->readdir_ns, "readdir");
  ctx->readdir_count.fetch_add(1, std::memory_order_relaxed);
  maybeLogCounters(ctx);

  (void)fi;
  bool ok = false;
  const std::string path = inodeToPath(ctx, ino, &ok);
  if (!ok) {
    fuse_reply_err(req, ENOENT);
    return;
  }
  bool listOk = false;
  auto entries = getOrBuildDirEntries(ctx, path, ino, &listOk);
  if (!listOk || !entries) {
    fuse_reply_err(req, ENOTDIR);
    return;
  }
  auto readdirBlob = getOrBuildReaddirBlob(ctx, req, path, entries);

  if (readdirBlob != nullptr) {
    ctx->readdir_blob_hits.fetch_add(1, std::memory_order_relaxed);
    _t.path = path;
    samplePathStat(ctx, "readdir", path);
    const size_t start = static_cast<size_t>(off);
    if (start >= readdirBlob->size()) {
      fuse_reply_buf(req, nullptr, 0);
      return;
    }
    const size_t n = std::min<size_t>(size, readdirBlob->size() - start);
    fuse_reply_buf(req, readdirBlob->data() + start, n);
    return;
  }

  samplePathStat(ctx, "readdir", path);
  _t.path = path;

  // Cap the buffer.  size comes from the kernel and is normally bounded by
  // FUSE protocol limits, but a corrupt/oversized request would otherwise
  // trigger a multi-MB allocation and risk std::bad_alloc.
  constexpr size_t kReaddirBufMax = 1 * 1024 * 1024;
  if (size > kReaddirBufMax) {
    size = kReaddirBufMax;
  }
  std::vector<char> buf(size);
  size_t used = 0;

  for (size_t i = static_cast<size_t>(off); i < entries->size(); ++i) {
    struct stat st;
    std::memset(&st, 0, sizeof(st));
    st.st_ino = (*entries)[i].ino;
    if ((*entries)[i].is_dir) {
      st.st_mode = S_IFDIR | 0755;
    } else {
      mode_t mode = (*entries)[i].cached_mode != 0 ? (*entries)[i].cached_mode
                                                   : static_cast<mode_t>(0644);
      st.st_mode = S_IFREG | regularFileVfsMode(mode);
    }

    const size_t ent = fuse_add_direntry(req, buf.data() + used, size - used,
                                         (*entries)[i].name.c_str(), &st,
                                         static_cast<off_t>(i + 1));
    if (ent > size - used) {
      break;
    }
    used += ent;
  }

  fuse_reply_buf(req, buf.data(), used);
}

void mo2_readdirplus(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                     struct fuse_file_info* fi)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || off < 0) {
    fuse_reply_err(req, EINVAL);
    return;
  }
  OpTimer _t(&ctx->readdir_ns, "readdirplus");
  ctx->readdir_count.fetch_add(1, std::memory_order_relaxed);
  maybeLogCounters(ctx);

  (void)fi;
  bool ok = false;
  const std::string path = inodeToPath(ctx, ino, &ok);
  if (!ok) {
    fuse_reply_err(req, ENOENT);
    return;
  }
  bool listOk = false;
  auto entries = getOrBuildDirEntries(ctx, path, ino, &listOk);
  if (!listOk || !entries) {
    fuse_reply_err(req, ENOTDIR);
    return;
  }
  auto readdirPlusBlob = getOrBuildReaddirPlusBlob(ctx, req, path, entries);

  if (readdirPlusBlob != nullptr) {
    ctx->readdirplus_blob_hits.fetch_add(1, std::memory_order_relaxed);
    _t.path = path;
    samplePathStat(ctx, "readdir", path);
    const size_t start = static_cast<size_t>(off);
    if (start >= readdirPlusBlob->size()) {
      fuse_reply_buf(req, nullptr, 0);
      return;
    }
    const size_t n = std::min<size_t>(size, readdirPlusBlob->size() - start);
    fuse_reply_buf(req, readdirPlusBlob->data() + start, n);
    return;
  }

  samplePathStat(ctx, "readdir", path);
  _t.path = path;

  constexpr size_t kReaddirPlusBufMax = 1 * 1024 * 1024;
  if (size > kReaddirPlusBufMax) {
    size = kReaddirPlusBufMax;
  }
  std::vector<char> buf(size);
  size_t used = 0;

  for (size_t i = static_cast<size_t>(off); i < entries->size(); ++i) {
    struct fuse_entry_param e;
    std::memset(&e, 0, sizeof(e));
    e.ino           = (*entries)[i].ino;
    e.attr_timeout  = ttlSeconds(ctx);
    e.entry_timeout = ttlSeconds(ctx);

    if ((*entries)[i].is_dir) {
      fillStatForDir(&e.attr, (*entries)[i].ino, ctx->uid, ctx->gid);
    } else {
      fillStatForFile(&e.attr, (*entries)[i].ino, ctx->uid, ctx->gid,
                      (*entries)[i].size, (*entries)[i].mtime,
                      (*entries)[i].real_path, (*entries)[i].cached_mode);
    }

    const size_t ent = fuse_add_direntry_plus(
        req, buf.data() + used, size - used, (*entries)[i].name.c_str(), &e,
        static_cast<off_t>(i + 1));
    if (ent > size - used) {
      break;
    }
    used += ent;
  }

  fuse_reply_buf(req, buf.data(), used);
}

void mo2_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || fi == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }
  OpTimer _t(&ctx->open_ns, "open");
  ctx->open_count.fetch_add(1, std::memory_order_relaxed);
  maybeLogCounters(ctx);

  bool ok = false;
  const std::string path = inodeToPath(ctx, ino, &ok);
  _t.path = path;
  if (!ok) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  NodeSnapshot snap;
  auto index = runtimeIndex(ctx);
  if (index != nullptr) {
    auto node = index->node(ino);
    if (node.has_value()) {
      ctx->node_index_hits.fetch_add(1, std::memory_order_relaxed);
      snap = snapshotFromIndexed(*node);
    }
  }
  if (!snap.found) {
    ctx->node_index_misses.fetch_add(1, std::memory_order_relaxed);
    std::shared_lock lock(ctx->tree_mutex);
    const VfsNode* node = resolveByInode(ctx, ino);
    if (node != nullptr) snapshotFromNode(node, snap);
  }
  if (!snap.found || snap.is_directory) {
    fuse_reply_err(req, snap.is_directory ? EISDIR : ENOENT);
    return;
  }

  std::string realPath = snap.real_path;
  const bool writable  = isWritableOpen(fi->flags);
  const bool truncateOnOpen = writable && ((fi->flags & O_TRUNC) != 0);
  const bool tracePath = shouldTracePath(path);
  bool isBacking       = snap.is_backing;
  bool cowPending      = false;
  bool isTracked       = false;
  bool invalidateWrites = false;
  uint64_t openSize    = snap.size;
  auto openMtime       = snap.mtime;

  // Write strategy:
  //   1. Files already in staging/overwrite → open R/W directly.
  //   2. Tracked files (user moved from Overwrite to a mod) → open R/W
  //      in-place so writes go back to the user's dedicated mod folder.
  //   3. Existing mod/data-dir/base-game files → open R/O and COW to staging
  //      on the first actual write().
  //   4. O_TRUNC on a COW source must materialize + truncate immediately so
  //      the caller sees normal POSIX open(..., O_TRUNC) semantics.
  //
  // This matches upstream USVFS behavior more closely than the previous
  // conservative "copy everything writable into overwrite" approach.
  //
  // Read strategy: create the handle cheaply, then open the backing fd on the
  // first read and retain a bounded LRU set. Wine probes many files it never
  // reads, while streamed files still get splice-friendly retained fds.
  int fd = -1;

  if (writable) {
    if (tracePath) {
      std::fprintf(stderr,
                   "[VFS] open writable path='%s' real='%s' flags=0x%x "
                   "truncate=%d backing=%d size=%llu\n",
                   path.c_str(), realPath.c_str(), fi->flags,
                   truncateOnOpen ? 1 : 0, isBacking ? 1 : 0,
                   static_cast<unsigned long long>(snap.size));
    }
    const std::string stagedPath = ctx->overwrite->stagingPath(path);
    const std::string owPath     = ctx->overwrite->overwritePath(path);
    bool alreadyStaged = (realPath == stagedPath || realPath == owPath);

    // Check if this file is tracked to a mod folder — even if the VFS
    // resolves it to overwrite (overwrite wins in priority), the write
    // should go to the mod folder so the user's dedicated mod stays updated.
    std::string trackedMod;
    if (ctx->tracked_writes) {
      trackedMod = ctx->tracked_writes->modFolderFor(path);
    }

    if (!trackedMod.empty()) {
      // Tracked file — open R/W in-place in the mod folder.
      const std::string modFilePath = trackedMod + "/" + path;
      int openFlags = O_RDWR | O_CLOEXEC;
      if (truncateOnOpen) {
        openFlags |= O_TRUNC;
      }
      fd = open(modFilePath.c_str(), openFlags);
      if (fd >= 0) {
        if (tracePath) {
          std::fprintf(stderr,
                       "[VFS] open tracked ok path='%s' real='%s' fd=%d\n",
                       path.c_str(), modFilePath.c_str(), fd);
        }
        realPath  = modFilePath;
        isTracked = true;
        invalidateWrites = true;
        if (truncateOnOpen) {
          openSize = 0;
          openMtime = std::chrono::system_clock::now();
          updateFileNodeKnown(ctx, path, realPath, originForPath(ctx, realPath),
                              openSize, openMtime);
          // Flush stale kernel page cache so the next read sees the truncated
          // content, not the pre-truncation data cached from a prior open.
          fuse_lowlevel_notify_inval_inode(ctx->session, ino, 0, 0);
        }
      } else {
        if (tracePath) {
          std::fprintf(stderr,
                       "[VFS] open tracked failed path='%s' real='%s' errno=%d "
                       "message='%s'\n",
                       path.c_str(), modFilePath.c_str(), errno,
                       std::strerror(errno));
        }
        // Mod file disappeared — fall through to normal handling
        trackedMod.clear();
      }
    }

    if (fd < 0 && alreadyStaged) {
      // Already in staging/overwrite — open R/W directly.
      int openFlags = O_RDWR | O_CLOEXEC;
      if (truncateOnOpen) {
        openFlags |= O_TRUNC;
      }
      fd = open(realPath.c_str(), openFlags);
      if (fd >= 0) invalidateWrites = true;
      if (fd >= 0 && truncateOnOpen) {
        if (tracePath) {
          std::fprintf(stderr,
                       "[VFS] open staged truncate ok path='%s' real='%s' fd=%d\n",
                       path.c_str(), realPath.c_str(), fd);
        }
        openSize = 0;
        openMtime = std::chrono::system_clock::now();
        updateFileNodeKnown(ctx, path, realPath, originForPath(ctx, realPath),
                            openSize, openMtime);
        fuse_lowlevel_notify_inval_inode(ctx->session, ino, 0, 0);
      }
    } else if (fd < 0 && truncateOnOpen) {
      try {
        std::string newPath;
        if (tracePath) {
          std::fprintf(stderr,
                       "[VFS] open truncate COW begin path='%s' source='%s' "
                       "backing=%d\n",
                       path.c_str(), realPath.c_str(), isBacking ? 1 : 0);
        }
        if (isBacking && ctx->backing_dir_fd >= 0) {
          newPath = ctx->overwrite->copyOnWriteFromFd(ctx->backing_dir_fd, path);
        } else {
          newPath = ctx->overwrite->copyOnWrite(realPath, path);
        }

        fd = open(newPath.c_str(), O_RDWR | O_CLOEXEC | O_TRUNC);
        if (fd >= 0) {
          if (tracePath) {
            std::fprintf(stderr,
                         "[VFS] open truncate COW ok path='%s' staging='%s' fd=%d\n",
                         path.c_str(), newPath.c_str(), fd);
          }
          realPath    = newPath;
          isBacking   = false;
          cowPending  = false;
          invalidateWrites = true;
          openSize    = 0;
          openMtime   = std::chrono::system_clock::now();
          updateFileNodeKnown(ctx, path, newPath, originForPath(ctx, newPath),
                              openSize, openMtime);
          // The file's backing path changed from mod/base-game to staging.
          // Flush the kernel page cache so subsequent reads go through FUSE
          // and see the staging content, not the pre-COW cached data.
          fuse_lowlevel_notify_inval_inode(ctx->session, ino, 0, 0);
        }
      } catch (const std::exception& e) {
        if (tracePath) {
          std::fprintf(stderr,
                       "[VFS] open truncate COW exception path='%s' source='%s' "
                       "what='%s'\n",
                       path.c_str(), realPath.c_str(), e.what());
        }
        fuse_reply_err(req, EIO);
        return;
      } catch (...) {
        if (tracePath) {
          std::fprintf(stderr,
                       "[VFS] open truncate COW unknown exception path='%s' "
                       "source='%s'\n",
                       path.c_str(), realPath.c_str());
        }
        fuse_reply_err(req, EIO);
        return;
      }
    } else if (fd < 0) {
      // Existing mod/base-game file — open R/O, defer COW to first write().
      if (ctx->backing_dir_fd >= 0) {
        fd = isBacking ? openat(ctx->backing_dir_fd, realPath.c_str(), O_RDONLY | O_CLOEXEC)
                       : open(realPath.c_str(), O_RDONLY | O_CLOEXEC);
      } else {
        fd = open(realPath.c_str(), O_RDONLY | O_CLOEXEC);
      }
      if (tracePath && fd < 0) {
        std::fprintf(stderr,
                     "[VFS] open lazy COW source failed path='%s' real='%s' "
                     "backing=%d errno=%d message='%s'\n",
                     path.c_str(), realPath.c_str(), isBacking ? 1 : 0,
                     errno, std::strerror(errno));
      }
      cowPending = true;
    }
    if (fd < 0) {
      if (tracePath) {
        std::fprintf(stderr,
                     "[VFS] open final failed path='%s' real='%s' errno=%d "
                     "message='%s'\n",
                     path.c_str(), realPath.c_str(), errno,
                     std::strerror(errno));
      }
      fuse_reply_err(req, errno != 0 ? errno : EIO);
      return;
    }
  } else {
    // Read-only open: delay the real fd until first read. Wine often opens
    // files only to probe metadata, and retaining those fds can exhaust the
    // process limit on large modlists.
    fd = -1;
  }

  const uint64_t fh = ctx->next_fh.fetch_add(1, std::memory_order_relaxed);
  {
    std::scoped_lock lock(ctx->open_files_mutex);
    Mo2FsContext::OpenFile of;
    of.fd           = fd;
    of.real_path     = realPath;
    of.writable      = writable;
    of.is_backing    = isBacking;
    of.cow_pending   = cowPending;
    of.is_tracked    = isTracked;
    of.relative_path = path;
    of.range_invalidation_pending = invalidateWrites;
    of.content_dirty = truncateOnOpen && fd >= 0;
    of.virtual_size  = openSize;
    of.virtual_mtime = openMtime;
    ctx->open_files[fh] = std::move(of);
  }

  fi->fh = fh;
  fi->keep_cache = vfsKeepCache(ctx);

  fuse_reply_open(req, fi);
}

void mo2_read(fuse_req_t req, fuse_ino_t /*ino*/, size_t size, off_t off,
              struct fuse_file_info* fi)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || fi == nullptr || off < 0) {
    fuse_reply_err(req, EINVAL);
    return;
  }
  OpTimer _t(&ctx->read_ns, "read");
  ctx->read_count.fetch_add(1, std::memory_order_relaxed);
  maybeLogCounters(ctx);

  int fd = -1;
  std::string realPath;
  bool isBacking = false;
  bool writable = false;
  uint64_t virtualSize = 0;
  {
    std::shared_lock lock(ctx->open_files_mutex);
    auto it = ctx->open_files.find(fi->fh);
    if (it == ctx->open_files.end()) {
      fuse_reply_err(req, EBADF);
      return;
    }
    fd       = it->second.fd;
    realPath = it->second.real_path;
    isBacking = it->second.is_backing;
    writable = it->second.writable;
    _t.path = it->second.relative_path;
    virtualSize = it->second.virtual_size;
  }
  if (writable) {
    std::scoped_lock lock(ctx->open_files_mutex);
    auto it = ctx->open_files.find(fi->fh);
    if (it != ctx->open_files.end()) {
      it->second.range_invalidation_pending = true;
    }
  }
  ctx->read_bytes.fetch_add(static_cast<uint64_t>(size), std::memory_order_relaxed);
  const bool traceAudioRead =
      ctx->trace_audio_reads && shouldTraceAudioReadPath(_t.path);
  if (traceAudioRead) {
    ctx->audio_trace_read_count.fetch_add(1, std::memory_order_relaxed);
    ctx->audio_trace_bytes_requested.fetch_add(
        static_cast<uint64_t>(size), std::memory_order_relaxed);
  }

  int localFd = fd;
  if (localFd < 0) {
    if (isBacking && ctx->backing_dir_fd >= 0) {
      localFd = openat(ctx->backing_dir_fd, realPath.c_str(), O_RDONLY);
    } else {
      localFd = open(realPath.c_str(), O_RDONLY);
    }
    if (localFd < 0) {
      const int openError = errno != 0 ? errno : EIO;
      if (traceAudioRead) {
        ctx->audio_trace_error_count.fetch_add(1, std::memory_order_relaxed);
        logAudioRead(ctx, "open-error", _t.path, realPath, off, size, -1,
                     virtualSize, openError);
      }
      fuse_reply_err(req, EIO);
      return;
    }
    ctx->lazy_ro_fd_opens.fetch_add(1, std::memory_order_relaxed);
    if (!writable) {
      const uint64_t tick = ctx->fd_lru_tick.fetch_add(1, std::memory_order_relaxed) + 1;
      bool retained = false;
      {
        std::scoped_lock lock(ctx->open_files_mutex);
        auto it = ctx->open_files.find(fi->fh);
        if (it != ctx->open_files.end() && it->second.fd < 0 && !it->second.writable) {
          it->second.fd = localFd;
          it->second.last_read_tick = tick;
          retained = true;
        }
      }
      if (!retained) {
        close(localFd);
        fuse_reply_err(req, EBADF);
        return;
      }
      pruneRetainedReadOnlyFds(ctx);
    }
  } else if (!writable) {
    ctx->retained_ro_fd_hits.fetch_add(1, std::memory_order_relaxed);
    const uint64_t tick = ctx->fd_lru_tick.fetch_add(1, std::memory_order_relaxed) + 1;
    std::scoped_lock lock(ctx->open_files_mutex);
    auto it = ctx->open_files.find(fi->fh);
    if (it != ctx->open_files.end()) {
      it->second.last_read_tick = tick;
    }
  }

  if (traceAudioRead) {
    std::vector<char> data(size);
    ssize_t actual = 0;
    if (size != 0) {
      do {
        actual = pread(localFd, data.data(), size, off);
      } while (actual < 0 && errno == EINTR);
    }

    if (actual < 0) {
      const int readError = errno != 0 ? errno : EIO;
      ctx->audio_trace_error_count.fetch_add(1, std::memory_order_relaxed);
      logAudioRead(ctx, "read-error", _t.path, realPath, off, size, actual,
                   virtualSize, readError);
      fuse_reply_err(req, readError);
      return;
    }

    ctx->audio_trace_bytes_returned.fetch_add(
        static_cast<uint64_t>(actual), std::memory_order_relaxed);
    if (static_cast<size_t>(actual) != size) {
      ctx->audio_trace_short_read_count.fetch_add(1,
                                                  std::memory_order_relaxed);
    }
    logAudioRead(ctx, "read", _t.path, realPath, off, size, actual,
                 virtualSize, 0);
    const int replyStatus = fuse_reply_buf(
        req, actual == 0 ? nullptr : data.data(), static_cast<size_t>(actual));
    if (replyStatus != 0) {
      const int replyError = replyStatus < 0 ? -replyStatus : replyStatus;
      ctx->audio_trace_error_count.fetch_add(1, std::memory_order_relaxed);
      logAudioRead(ctx, "reply-error", _t.path, realPath, off, size, actual,
                   virtualSize, replyError);
    }
    return;
  }

  // Zero-copy read: splice data directly from backing fd to /dev/fuse,
  // bypassing userspace entirely.  The kernel transfers data kernel-to-kernel.
  struct fuse_bufvec buf = FUSE_BUFVEC_INIT(size);
  buf.buf[0].flags = static_cast<fuse_buf_flags>(FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK);
  buf.buf[0].fd    = localFd;
  buf.buf[0].pos   = off;
  fuse_reply_data(req, &buf, FUSE_BUF_SPLICE_MOVE);
}

void mo2_write(fuse_req_t req, fuse_ino_t ino, const char* buf, size_t size,
               off_t off, struct fuse_file_info* fi)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || fi == nullptr || off < 0 || (buf == nullptr && size > 0)) {
    fuse_reply_err(req, EINVAL);
    return;
  }
  OpTimer _t(&ctx->write_ns, "write");
  ctx->write_count.fetch_add(1, std::memory_order_relaxed);
  maybeLogCounters(ctx);

  int fd = -1;
  std::string relativePath;
  std::string realPath;
  bool writable   = false;
  bool cowPending  = false;
  bool isBacking   = false;
  {
    std::shared_lock lock(ctx->open_files_mutex);
    auto it = ctx->open_files.find(fi->fh);
    if (it == ctx->open_files.end()) {
      fuse_reply_err(req, EBADF);
      return;
    }
    fd           = it->second.fd;
    writable     = it->second.writable;
    cowPending   = it->second.cow_pending;
    isBacking    = it->second.is_backing;
    relativePath = it->second.relative_path;
    realPath     = it->second.real_path;
    _t.path      = relativePath;
  }

  if (!writable) {
    fuse_reply_err(req, EACCES);
    return;
  }

  // Lazy COW applies to any existing VFS source that is not already staging,
  // overwrite, or an explicitly tracked custom-output file. This preserves
  // mod files and lets generated output land in staging/overwrite.
  if (cowPending) {
    ctx->cow_write_count.fetch_add(1, std::memory_order_relaxed);
    try {
      std::string newPath;
      if (isBacking && ctx->backing_dir_fd >= 0) {
        newPath = ctx->overwrite->copyOnWriteFromFd(ctx->backing_dir_fd, relativePath);
      } else {
        newPath = ctx->overwrite->copyOnWrite(realPath, relativePath);
      }

      int newFd = open(newPath.c_str(), O_RDWR);
      if (newFd < 0) {
        fuse_reply_err(req, EIO);
        return;
      }

      if (fd >= 0) close(fd);
      fd = newFd;

      {
        std::scoped_lock lock(ctx->open_files_mutex);
        auto it = ctx->open_files.find(fi->fh);
        if (it != ctx->open_files.end()) {
          it->second.fd          = newFd;
          it->second.real_path   = newPath;
          it->second.is_backing  = false;
          it->second.cow_pending = false;
          it->second.range_invalidation_pending = true;
        }
      }
      realPath = newPath;
      updateFileNode(ctx, relativePath, newPath, originForPath(ctx, newPath));
      // Lazy COW: backing path just changed to staging. Flush the kernel page
      // cache so the caller's next read sees staging content, not the stale
      // pre-COW pages. This is the counterpart to the O_TRUNC invalidation in
      // mo2_open — without it, repeated WritePrivateProfileString calls on the
      // same INI file each read the old mod-file content and only the last
      // write survives.
      fuse_lowlevel_notify_inval_inode(ctx->session, ino, 0, 0);
    } catch (...) {
      fuse_reply_err(req, EIO);
      return;
    }
  }

  if (fd < 0) {
    fuse_reply_err(req, EBADF);
    return;
  }

  const ssize_t written = pwrite(fd, buf, size, off);
  if (written < 0) {
    fuse_reply_err(req, EIO);
    return;
  }
  ctx->write_bytes.fetch_add(static_cast<uint64_t>(written),
                             std::memory_order_relaxed);
  samplePathStat(ctx, "write", relativePath);
  const bool invalidateRange = markOpenFileDirty(
      ctx, fi->fh,
      static_cast<uint64_t>(off) + static_cast<uint64_t>(written));
  if (invalidateRange && written > 0) {
    invalidateKernelContent(ctx, ino, off, static_cast<off_t>(written));
  }
  markCatalogDirty(ctx, relativePath, realPath);
  fuse_reply_write(req, static_cast<size_t>(written));
}

void mo2_create(fuse_req_t req, fuse_ino_t parent, const char* name, mode_t mode,
                struct fuse_file_info* fi)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || fi == nullptr || name == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }
  OpTimer _t(&ctx->create_ns, "create");
  ctx->create_count.fetch_add(1, std::memory_order_relaxed);
  maybeLogCounters(ctx);
  std::scoped_lock namespaceLock(ctx->namespace_mutation_mutex);

  bool ok = false;
  const std::string parentPath = inodeToPath(ctx, parent, &ok);
  if (!ok) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  const std::string relative =
      joinPath(parentPath, canonicalChildName(ctx, parentPath, name));
  _t.path = relative;

  const auto preCreateSnap = snapshotForPath(ctx, relative);
  if (preCreateSnap.found && preCreateSnap.is_directory) {
    fuse_reply_err(req, EISDIR);
    return;
  }

  std::string realPath;

  // If this file is tracked to a mod folder, create it there instead of staging
  std::string trackedMod;
  if (ctx->tracked_writes) {
    trackedMod = ctx->tracked_writes->modFolderFor(relative);
  }

  if (!trackedMod.empty()) {
    realPath = trackedMod + "/" + relative;
    // Ensure parent directories exist in the mod folder
    std::error_code ec;
    fs::create_directories(fs::path(realPath).parent_path(), ec);
  }

  int fd = -1;
  if (!trackedMod.empty()) {
    fd = open(realPath.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC,
              mode != 0 ? (mode & 07777) : 0644);
    if (fd < 0) {
      std::fprintf(stderr,
                   "[VFS] tracked create failed path='%s' real='%s' errno=%d "
                   "message='%s'\n",
                   relative.c_str(), realPath.c_str(), errno,
                   std::strerror(errno));
      // Fall back to staging
      trackedMod.clear();
    }
  }

  if (trackedMod.empty()) {
    std::error_code createError;
    fd = ctx->overwrite->createFile(relative, mode, &realPath, &createError);
    if (fd < 0) {
      const std::string stagingPath = ctx->overwrite->stagingPath(relative);
      std::fprintf(stderr,
                   "[VFS] create failed path='%s' staging='%s' error=%d "
                   "message='%s'\n",
                   relative.c_str(), stagingPath.c_str(), createError.value(),
                   createError.message().c_str());
      fuse_reply_err(req, fuseErrnoFromError(createError));
      return;
    }
  }

  struct stat createdSt {};
  if (fstat(fd, &createdSt) != 0) {
    close(fd);
    fuse_reply_err(req, EIO);
    return;
  }
  const auto createdMtime = timePointFromTimespec(createdSt.st_mtim);
  const uint64_t createdSize = static_cast<uint64_t>(createdSt.st_size);
  const std::string origin = trackedMod.empty() ? "Staging" : "TrackedMod";

  updateFileNodeKnown(ctx, relative, realPath, origin, createdSize, createdMtime);
  invalidateDirCache(ctx, parentPath);
  if (!preCreateSnap.found) {
    std::unique_lock lock(ctx->tree_mutex);
    ++ctx->tree->file_count;
  }

  fuse_ino_t newIno;
  {
    std::unique_lock lock(ctx->inode_mutex);
    newIno = ctx->inodes->getOrCreate(relative);
  }

  const uint64_t fh = ctx->next_fh.fetch_add(1, std::memory_order_relaxed);

  {
    std::scoped_lock lock(ctx->open_files_mutex);
    Mo2FsContext::OpenFile of;
    of.fd           = fd;
    of.real_path     = realPath;
    of.writable      = true;
    of.is_backing    = false;
    of.relative_path = relative;
    of.virtual_size  = createdSize;
    of.virtual_mtime = createdMtime;
    ctx->open_files[fh] = std::move(of);
  }

  fi->fh         = fh;
  fi->keep_cache = vfsKeepCache(ctx);

  struct fuse_entry_param e;
  std::memset(&e, 0, sizeof(e));
  e.ino           = newIno;
  e.attr_timeout  = ttlSeconds(ctx);
  e.entry_timeout = ttlSeconds(ctx);
  fillStatForFile(&e.attr, newIno, ctx->uid, ctx->gid, createdSize,
                  createdMtime, realPath, createdSt.st_mode & 0777);

  samplePathStat(ctx, "create", relative);
  markCatalogDirty(ctx, relative, realPath);
  fuse_reply_create(req, &e, fi);
}

void mo2_rename(fuse_req_t req, fuse_ino_t parent, const char* name,
                fuse_ino_t newparent, const char* newname, unsigned int flags)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || name == nullptr || newname == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }
  OpTimer _t(&ctx->rename_ns, "rename");
  ctx->rename_count.fetch_add(1, std::memory_order_relaxed);
  maybeLogCounters(ctx);
  std::scoped_lock namespaceLock(ctx->namespace_mutation_mutex);

  // Reject unsupported flags (only RENAME_NOREPLACE is supported).
  // Wine uses renameat2(RENAME_NOREPLACE) for MoveFileW() calls where
  // ReplaceIfExists is FALSE (e.g. xEdit saving plugins).
  if (flags & ~(unsigned int)RENAME_NOREPLACE) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  bool okParent = false;
  bool okNewParent = false;
  const std::string parentPath = inodeToPath(ctx, parent, &okParent);
  const std::string newParentPath = inodeToPath(ctx, newparent, &okNewParent);
  if (!okParent || !okNewParent) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  const std::string oldRelative = joinPath(parentPath, canonicalChildName(ctx, parentPath, name));
  const std::string newRelative =
      joinPath(newParentPath, canonicalChildName(ctx, newParentPath, newname));
  _t.path = oldRelative + " -> " + newRelative;

  const auto oldSnap = snapshotForPath(ctx, oldRelative);
  if (!oldSnap.found) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  // RENAME_NOREPLACE: fail if destination already exists in the VFS
  if (flags & RENAME_NOREPLACE) {
    const auto destSnap = snapshotForPath(ctx, newRelative);
    if (destSnap.found) {
      fuse_reply_err(req, EEXIST);
      return;
    }
  }

  std::string newRealPath;

  std::error_code renameError;
  if (!ctx->overwrite->rename(oldRelative, newRelative, &renameError)) {
    // Source file is not in staging or overwrite — it's a backing (game) file
    // or a mod file. Copy it to staging at the destination path instead of
    // moving the original. This is the VFS equivalent of a rename: the file
    // appears at the new path and disappears from the old path in the virtual
    // view, but we never modify the real game/mod directories.
    if (renameError != std::make_error_code(std::errc::no_such_file_or_directory)) {
      fuse_reply_err(req, fuseErrnoFromError(renameError));
      return;
    }
    if (oldSnap.is_directory) {
      fuse_reply_err(req, EACCES);
      return;
    }

    try {
      if (oldSnap.is_backing && ctx->backing_dir_fd >= 0) {
        newRealPath = ctx->overwrite->copyOnWriteFromFd(ctx->backing_dir_fd,
                                                        oldRelative, newRelative);
      } else {
        newRealPath = ctx->overwrite->copyOnWrite(oldSnap.real_path, newRelative);
      }
    } catch (...) {
      std::fprintf(stderr, "[VFS] rename COW failed: '%s' -> '%s'\n",
                   oldRelative.c_str(), newRelative.c_str());
      fuse_reply_err(req, EIO);
      return;
    }
  }

  fuse_ino_t oldIno = 0;
  fuse_ino_t newIno = 0;
  {
    std::shared_lock lock(ctx->inode_mutex);
    oldIno = ctx->inodes->get(oldRelative);
    newIno = ctx->inodes->get(newRelative);
  }
  {
    std::unique_lock lock(ctx->tree_mutex);
    invalidateNodeCache(ctx, oldRelative);
    ctx->tree->root.removeFromTree(splitPath(oldRelative));

    if (oldSnap.is_directory) {
      ctx->tree->root.insertDirectory(splitPath(newRelative));
    } else {
      if (newRealPath.empty()) {
        const std::string staged = ctx->overwrite->stagingPath(newRelative);
        const std::string over   = ctx->overwrite->overwritePath(newRelative);
        newRealPath = fs::exists(staged) ? staged : over;
      }
      ctx->tree->root.insertFile(splitPath(newRelative), newRealPath, oldSnap.size,
                                 oldSnap.mtime, "Staging");
    }
    invalidateNodeCache(ctx, newRelative);
  }

  {
    std::unique_lock lock(ctx->inode_mutex);
    ctx->inodes->rename(oldRelative, newRelative);
  }
  tombstoneRuntimePath(ctx, oldRelative, oldIno);
  NodeSnapshot renamedSnap = oldSnap;
  renamedSnap.is_backing = false;
  if (!renamedSnap.is_directory) renamedSnap.real_path = newRealPath;
  publishRuntimeSnapshot(ctx, newRelative, oldIno, renamedSnap);
  if (oldIno != 0) {
    invalidateAttrCache(ctx, oldIno);
  }
  if (newIno != 0 && newIno != oldIno) {
    invalidateAttrCache(ctx, newIno);
  }
  // rename-over-existing is the normal atomic INI replacement strategy. The
  // destination inode may have 24-hour keep_cache pages from before the
  // replacement, so invalidate both identities before returning success.
  if (oldIno != 0) {
    invalidateKernelContent(ctx, oldIno, 0, 0);
  }
  if (newIno != 0 && newIno != oldIno) {
    invalidateKernelContent(ctx, newIno, 0, 0);
  }
  invalidateDirCache(ctx, parentPath);
  if (newParentPath != parentPath) {
    invalidateDirCache(ctx, newParentPath);
  }
  if (oldSnap.is_directory) {
    invalidateDirSubtreeCache(ctx, oldRelative);
    invalidateDirSubtreeCache(ctx, newRelative);
  }

  markCatalogDirty(ctx, oldRelative);
  markCatalogDirty(ctx, newRelative);

  fuse_reply_err(req, 0);
}

void mo2_setattr(fuse_req_t req, fuse_ino_t ino, struct stat* attr, int to_set,
                 struct fuse_file_info* fi)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }
  OpTimer _t(&ctx->setattr_ns, "setattr");
  ctx->setattr_count.fetch_add(1, std::memory_order_relaxed);
  maybeLogCounters(ctx);
  std::scoped_lock namespaceLock(ctx->namespace_mutation_mutex);

  if (ino == 1) {
    _t.path = "/";
    struct stat st;
    fillStatForDir(&st, 1, ctx->uid, ctx->gid);
    fuse_reply_attr(req, &st, ttlSeconds(ctx));
    return;
  }

  bool ok = false;
  const std::string path = inodeToPath(ctx, ino, &ok);
  _t.path = path;
  if (!ok) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  if ((to_set & FUSE_SET_ATTR_SIZE) != 0 && attr != nullptr) {
    if (shouldTracePath(path)) {
      std::fprintf(stderr,
                   "[VFS] setattr size path='%s' size=%llu fh=%llu\n",
                   path.c_str(),
                   static_cast<unsigned long long>(attr->st_size),
                   static_cast<unsigned long long>(fi != nullptr ? fi->fh : 0));
    }
    std::string target;
    bool targetIsBacking = false;
    uint64_t fh = 0;

    if (fi != nullptr) {
      fh = fi->fh;
      std::shared_lock lock(ctx->open_files_mutex);
      auto it = ctx->open_files.find(fh);
      if (it != ctx->open_files.end()) {
        target          = it->second.real_path;
        targetIsBacking = it->second.is_backing;
      }
    }

    if (target.empty()) {
      const auto snap = snapshotForPath(ctx, path);
      if (!snap.found || snap.is_directory) {
        fuse_reply_err(req, ENOENT);
        return;
      }
      target          = snap.real_path;
      targetIsBacking = snap.is_backing;
    }

    // If file is tracked to a mod, redirect target to mod folder
    if (ctx->tracked_writes) {
      const std::string trackedMod = ctx->tracked_writes->modFolderFor(path);
      if (!trackedMod.empty()) {
        const std::string modFilePath = trackedMod + "/" + path;
        if (fs::exists(modFilePath)) {
          target          = modFilePath;
          targetIsBacking = false;
        }
      }
    }

    // COW any non-staging source before truncating. Tracked files and already
    // staged/overwrite files are truncated in place.
    const std::string stagedPath = ctx->overwrite->stagingPath(path);
    const std::string overwritePath = ctx->overwrite->overwritePath(path);
    const bool alreadyWritableTarget =
        fs::path(target).lexically_normal().string() ==
            fs::path(stagedPath).lexically_normal().string() ||
        fs::path(target).lexically_normal().string() ==
            fs::path(overwritePath).lexically_normal().string();
    const bool trackedTarget =
        ctx->tracked_writes != nullptr &&
        !ctx->tracked_writes->modFolderFor(path).empty() &&
        !targetIsBacking;
    if (!alreadyWritableTarget && !trackedTarget) {
      try {
        if (targetIsBacking && ctx->backing_dir_fd >= 0) {
          target = ctx->overwrite->copyOnWriteFromFd(ctx->backing_dir_fd, path);
        } else {
          target = ctx->overwrite->copyOnWrite(target, path);
        }
      } catch (...) {
        fuse_reply_err(req, EIO);
        return;
      }

      if (fi != nullptr) {
        std::scoped_lock lock(ctx->open_files_mutex);
        auto it = ctx->open_files.find(fh);
        if (it != ctx->open_files.end()) {
          const int newFd = open(target.c_str(), O_RDWR);
          if (newFd < 0) {
            fuse_reply_err(req, EIO);
            return;
          }
          if (it->second.fd >= 0) close(it->second.fd);
          it->second.fd          = newFd;
          it->second.real_path   = target;
          it->second.writable    = true;
          it->second.is_backing  = false;
          it->second.cow_pending = false;
        }
      }
    }

    // Wine can issue a handle-based truncate after opening the FUSE file with
    // O_RDONLY, even though the corresponding Windows handle is writable. The
    // COW branch above upgrades that handle as a side effect, but files already
    // in staging/overwrite or a tracked output mod skipped COW and retained a
    // read-only (often lazy, fd-less) handle. The following write then failed
    // with EACCES. Ensure every handle-based size change leaves a writable fd.
    if (fi != nullptr) {
      bool needsWritableFd = false;
      {
        std::shared_lock lock(ctx->open_files_mutex);
        auto it = ctx->open_files.find(fh);
        needsWritableFd = it != ctx->open_files.end() &&
                          (!it->second.writable || it->second.fd < 0);
      }

      if (needsWritableFd) {
        const int newFd = open(target.c_str(), O_RDWR | O_CLOEXEC);
        if (newFd < 0) {
          if (shouldTracePath(path)) {
            std::fprintf(stderr,
                         "[VFS] setattr writable reopen failed path='%s' "
                         "target='%s' errno=%d message='%s'\n",
                         path.c_str(), target.c_str(), errno,
                         std::strerror(errno));
          }
          fuse_reply_err(req, errno != 0 ? errno : EIO);
          return;
        }

        std::scoped_lock lock(ctx->open_files_mutex);
        auto it = ctx->open_files.find(fh);
        if (it == ctx->open_files.end()) {
          close(newFd);
          fuse_reply_err(req, EBADF);
          return;
        }
        if (it->second.fd >= 0) {
          close(it->second.fd);
        }
        it->second.fd          = newFd;
        it->second.real_path   = target;
        it->second.writable    = true;
        it->second.is_backing  = false;
        it->second.cow_pending = false;
        it->second.is_tracked  = trackedTarget;

        if (shouldTracePath(path)) {
          std::fprintf(stderr,
                       "[VFS] setattr upgraded writable handle path='%s' "
                       "target='%s' fd=%d\n",
                       path.c_str(), target.c_str(), newFd);
        }
      }
    }

    bool resized = false;
    if (fi != nullptr) {
      std::scoped_lock lock(ctx->open_files_mutex);
      auto it = ctx->open_files.find(fh);
      if (it != ctx->open_files.end() && it->second.fd >= 0) {
        if (ftruncate(it->second.fd, static_cast<off_t>(attr->st_size)) != 0) {
          if (shouldTracePath(path)) {
            std::fprintf(stderr,
                         "[VFS] setattr ftruncate failed path='%s' target='%s' "
                         "errno=%d message='%s'\n",
                         path.c_str(), target.c_str(), errno,
                         std::strerror(errno));
          }
          fuse_reply_err(req, EIO);
          return;
        }
        resized = true;
      }
    }

    if (!resized) {
      std::error_code ec;
      fs::resize_file(target, static_cast<uint64_t>(attr->st_size), ec);
      if (ec) {
        if (shouldTracePath(path)) {
          std::fprintf(stderr,
                       "[VFS] setattr resize failed path='%s' target='%s' "
                       "error=%d message='%s'\n",
                       path.c_str(), target.c_str(), ec.value(),
                       ec.message().c_str());
        }
        fuse_reply_err(req, EIO);
        return;
      }
    }

    updateFileNode(ctx, path, target, originForPath(ctx, target));
    if (fi != nullptr) {
      std::scoped_lock lock(ctx->open_files_mutex);
      auto it = ctx->open_files.find(fh);
      if (it != ctx->open_files.end()) {
        it->second.virtual_size = static_cast<uint64_t>(attr->st_size);
        it->second.virtual_mtime = std::chrono::system_clock::now();
        it->second.metadata_dirty = false;
      }
    }
    if (fi != nullptr) {
      markOpenFileDirty(ctx, fh, static_cast<uint64_t>(attr->st_size));
    }
    invalidateKernelContent(ctx, ino, 0, 0);
  }
  samplePathStat(ctx, "setattr", path);

  // Handle chmod — propagate permission changes to the real file on disk.
  if ((to_set & FUSE_SET_ATTR_MODE) != 0 && attr != nullptr) {
    const auto snap = snapshotForPath(ctx, path);
    if (snap.found && !snap.is_directory && !snap.real_path.empty()) {
      ::chmod(snap.real_path.c_str(), attr->st_mode & 07777);
    }
  }

  // Handle explicit timestamp changes (utimensat / Wine SetFileTime)
  if ((to_set & (FUSE_SET_ATTR_MTIME | FUSE_SET_ATTR_MTIME_NOW |
                 FUSE_SET_ATTR_ATIME | FUSE_SET_ATTR_ATIME_NOW)) != 0 &&
      attr != nullptr) {
    const auto snap = snapshotForPath(ctx, path);
    if (snap.found && !snap.is_directory) {
      // Apply the timestamp to the real file on disk
      struct timespec times[2];
      // atime
      if (to_set & FUSE_SET_ATTR_ATIME_NOW) {
        times[0].tv_sec  = 0;
        times[0].tv_nsec = UTIME_NOW;
      } else if (to_set & FUSE_SET_ATTR_ATIME) {
        times[0] = attr->st_atim;
      } else {
        times[0].tv_sec  = 0;
        times[0].tv_nsec = UTIME_OMIT;
      }
      // mtime
      if (to_set & FUSE_SET_ATTR_MTIME_NOW) {
        times[1].tv_sec  = 0;
        times[1].tv_nsec = UTIME_NOW;
      } else if (to_set & FUSE_SET_ATTR_MTIME) {
        times[1] = attr->st_mtim;
      } else {
        times[1].tv_sec  = 0;
        times[1].tv_nsec = UTIME_OMIT;
      }

      // Only write timestamps to disk for files we own (staging/overwrite).
      // For mod and base game files, just update the VFS tree in-memory.
      if (!snap.is_backing) {
        utimensat(AT_FDCWD, snap.real_path.c_str(), times, 0);
      }

      // Update the VFS tree mtime without changing origin or triggering COW.
      if (to_set & (FUSE_SET_ATTR_MTIME | FUSE_SET_ATTR_MTIME_NOW)) {
        std::chrono::system_clock::time_point newMtime;
        if (to_set & FUSE_SET_ATTR_MTIME_NOW) {
          newMtime = std::chrono::system_clock::now();
        } else {
          newMtime = std::chrono::system_clock::time_point(
              std::chrono::seconds(attr->st_mtim.tv_sec));
        }
        std::unique_lock lock(ctx->tree_mutex);
        auto components = splitPath(path);
        VfsNode* cur    = &ctx->tree->root;
        for (const auto& part : components) {
          if (!cur->is_directory) {
            cur = nullptr;
            break;
          }
          auto it = cur->dir_info.children.find(normalizeForLookup(part));
          if (it == cur->dir_info.children.end()) {
            cur = nullptr;
            break;
          }
          cur = it->second.get();
        }
        if (cur != nullptr && !cur->is_directory) {
          cur->file_info.mtime = newMtime;
        }
      }
    }
  }

  invalidateAttrCache(ctx, ino);
  const auto snap = snapshotForPath(ctx, path);
  if (!snap.found) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  if (to_set != 0) markCatalogDirty(ctx, path, snap.real_path);
  if (!path.empty()) publishRuntimeSnapshot(ctx, path, ino, snap);

  struct stat st;
  if (snap.is_directory) {
    fillStatForDir(&st, ino, ctx->uid, ctx->gid);
  } else {
    fillStatForFile(&st, ino, ctx->uid, ctx->gid, snap.size, snap.mtime,
                    snap.real_path, snap.cached_mode);
  }
  fuse_reply_attr(req, &st, ttlSeconds(ctx));
}

void mo2_unlink(fuse_req_t req, fuse_ino_t parent, const char* name)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || name == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }
  OpTimer _t(&ctx->unlink_ns, "unlink");
  ctx->unlink_count.fetch_add(1, std::memory_order_relaxed);
  maybeLogCounters(ctx);
  std::scoped_lock namespaceLock(ctx->namespace_mutation_mutex);

  bool ok = false;
  const std::string parentPath = inodeToPath(ctx, parent, &ok);
  if (!ok) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  const std::string relative = joinPath(parentPath, canonicalChildName(ctx, parentPath, name));
  _t.path = relative;
  if (!ctx->overwrite->removeFile(relative)) {
    const auto snap = snapshotForPath(ctx, relative);
    if (!snap.found || snap.is_directory) {
      fuse_reply_err(req, snap.found ? EISDIR : ENOENT);
      return;
    }
  }

  fuse_ino_t removedIno = 0;
  {
    std::shared_lock lock(ctx->inode_mutex);
    removedIno = ctx->inodes->get(relative);
  }
  {
    std::unique_lock lock(ctx->tree_mutex);
    invalidateNodeCache(ctx, relative);
    if (ctx->tree->root.removeFromTree(splitPath(relative))) {
      ctx->tree->file_count = ctx->tree->file_count > 0 ? ctx->tree->file_count - 1 : 0;
    }
  }
  if (removedIno != 0) {
    invalidateAttrCache(ctx, removedIno);
  }
  tombstoneRuntimePath(ctx, relative, removedIno);
  invalidateDirCache(ctx, parentPath);

  markCatalogDirty(ctx, relative);

  fuse_reply_err(req, 0);
}

void mo2_mkdir(fuse_req_t req, fuse_ino_t parent, const char* name, mode_t /*mode*/)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || name == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }
  std::scoped_lock namespaceLock(ctx->namespace_mutation_mutex);

  bool ok = false;
  const std::string parentPath = inodeToPath(ctx, parent, &ok);
  if (!ok) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  const std::string relative =
      joinPath(parentPath, canonicalChildName(ctx, parentPath, name));
  const auto preCreateSnap = snapshotForPath(ctx, relative);
  if (preCreateSnap.found && !preCreateSnap.is_directory) {
    fuse_reply_err(req, EEXIST);
    return;
  }

  std::error_code createError;
  if (!ctx->overwrite->createDirectory(relative, &createError)) {
    const std::string stagingPath = ctx->overwrite->stagingPath(relative);
    std::fprintf(stderr,
                 "[VFS] mkdir failed path='%s' staging='%s' error=%d "
                 "message='%s'\n",
                 relative.c_str(), stagingPath.c_str(), createError.value(),
                 createError.message().c_str());
    fuse_reply_err(req, fuseErrnoFromError(createError));
    return;
  }

  {
    std::unique_lock lock(ctx->tree_mutex);
    ctx->tree->root.insertDirectory(splitPath(relative));
    if (!preCreateSnap.found) {
      ++ctx->tree->dir_count;
    }
    invalidateNodeCache(ctx, relative);
  }
  invalidateDirCache(ctx, parentPath);

  fuse_ino_t dirIno;
  {
    std::unique_lock lock(ctx->inode_mutex);
    dirIno = ctx->inodes->getOrCreate(relative);
  }

  const auto snap = snapshotForPath(ctx, relative);
  if (!snap.found) {
    fuse_reply_err(req, EIO);
    return;
  }
  publishRuntimeSnapshot(ctx, relative, dirIno, snap);

  replyEntryFromSnapshot(req, ctx, dirIno, snap);
}

void mo2_rmdir(fuse_req_t req, fuse_ino_t parent, const char* name)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || name == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }
  if (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0) {
    fuse_reply_err(req, EINVAL);
    return;
  }
  std::scoped_lock namespaceLock(ctx->namespace_mutation_mutex);

  bool ok = false;
  const std::string parentPath = inodeToPath(ctx, parent, &ok);
  if (!ok) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  const std::string relative =
      joinPath(parentPath, canonicalChildName(ctx, parentPath, name));

  // Check VFS tree: the directory must exist and be empty in the merged view.
  // Backing/mod entries are read-only, so reject rmdir on anything that still
  // has children visible through the VFS.
  {
    std::shared_lock lock(ctx->tree_mutex);
    const VfsNode* node = ctx->tree->root.resolve(splitPath(relative));
    if (node == nullptr) {
      fuse_reply_err(req, ENOENT);
      return;
    }
    if (!node->is_directory) {
      fuse_reply_err(req, ENOTDIR);
      return;
    }
    if (!node->dir_info.children.empty()) {
      fuse_reply_err(req, ENOTEMPTY);
      return;
    }
  }

  // Try to remove the real directory from staging/overwrite. If the directory
  // is purely virtual (no real backing in staging/overwrite) that's fine —
  // still accept the rmdir so the VFS view reflects the caller's intent.
  bool notEmpty = false;
  const bool removed = ctx->overwrite->removeDirectory(relative, &notEmpty);
  if (notEmpty) {
    fuse_reply_err(req, ENOTEMPTY);
    return;
  }
  (void)removed;

  {
    std::unique_lock lock(ctx->tree_mutex);
    invalidateNodeCache(ctx, relative);
    if (ctx->tree->root.removeFromTree(splitPath(relative))) {
      ctx->tree->dir_count =
          ctx->tree->dir_count > 0 ? ctx->tree->dir_count - 1 : 0;
    }
  }
  fuse_ino_t removedIno = 0;
  {
    std::shared_lock lock(ctx->inode_mutex);
    removedIno = ctx->inodes->get(relative);
  }
  tombstoneRuntimePath(ctx, relative, removedIno);
  invalidateDirSubtreeCache(ctx, relative);
  invalidateDirCache(ctx, parentPath);

  fuse_reply_err(req, 0);
}

void mo2_release(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || fi == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  flushDirtyOpenFileMetadata(ctx, fi->fh, ino);

  {
    std::scoped_lock lock(ctx->open_files_mutex);
    auto it = ctx->open_files.find(fi->fh);
    if (it != ctx->open_files.end()) {
      if (it->second.fd >= 0) {
        close(it->second.fd);
      }
      ctx->open_files.erase(it);
    }
  }
  fuse_reply_err(req, 0);
}

void mo2_releasedir(fuse_req_t req, fuse_ino_t /*ino*/, struct fuse_file_info* fi)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || fi == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  ctx->releasedir_count.fetch_add(1, std::memory_order_relaxed);
  fuse_reply_err(req, 0);
}

void mo2_forget(fuse_req_t req, fuse_ino_t ino, uint64_t /*nlookup*/)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx != nullptr) {
    invalidateAttrCache(ctx, ino);
  }
  fuse_reply_none(req);
}

void mo2_flush(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || fi == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }
  OpTimer _t(&ctx->flush_ns, "flush");
  ctx->flush_count.fetch_add(1, std::memory_order_relaxed);
  maybeLogCounters(ctx);

  {
    std::shared_lock lock(ctx->open_files_mutex);
    auto it = ctx->open_files.find(fi->fh);
    if (it == ctx->open_files.end()) {
      fuse_reply_err(req, EBADF);
      return;
    }
    _t.path = it->second.relative_path;
  }

  samplePathStat(ctx, "flush", _t.path);
  flushDirtyOpenFileMetadata(ctx, fi->fh, ino);
  fuse_reply_err(req, 0);
}

void mo2_fsync(fuse_req_t req, fuse_ino_t ino, int datasync,
               struct fuse_file_info* fi)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || fi == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }
  OpTimer _t(&ctx->fsync_ns, "fsync");
  ctx->fsync_count.fetch_add(1, std::memory_order_relaxed);
  maybeLogCounters(ctx);

  int fd = -1;
  {
    std::shared_lock lock(ctx->open_files_mutex);
    auto it = ctx->open_files.find(fi->fh);
    if (it == ctx->open_files.end()) {
      fuse_reply_err(req, EBADF);
      return;
    }
    fd = it->second.fd;
    _t.path = it->second.relative_path;
  }

  if (fd >= 0) {
    const int rc = datasync ? fdatasync(fd) : fsync(fd);
    if (rc != 0) {
      fuse_reply_err(req, errno);
      return;
    }
  }
  flushDirtyOpenFileMetadata(ctx, fi->fh, ino);
  fuse_reply_err(req, 0);
}

void mo2_statfs(fuse_req_t req, fuse_ino_t /*ino*/)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  struct statvfs st {};
  if (ctx->backing_dir_fd >= 0 && fstatvfs(ctx->backing_dir_fd, &st) == 0) {
    if (st.f_namemax == 0) {
      st.f_namemax = 255;
    }
    fuse_reply_statfs(req, &st);
    return;
  }

  st.f_bsize   = 4096;
  st.f_frsize  = 4096;
  st.f_blocks  = 1024ull * 1024ull;
  st.f_bfree   = st.f_blocks / 2;
  st.f_bavail  = st.f_bfree;
  st.f_files   = 1024ull * 1024ull;
  st.f_ffree   = st.f_files / 2;
  st.f_favail  = st.f_ffree;
  st.f_namemax = 255;
  fuse_reply_statfs(req, &st);
}

void mo2_getlk(fuse_req_t req, fuse_ino_t /*ino*/, struct fuse_file_info* /*fi*/,
               struct flock* lock)
{
  if (lock == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  lock->l_type = F_UNLCK;
  fuse_reply_lock(req, lock);
}

void mo2_setlk(fuse_req_t req, fuse_ino_t /*ino*/, struct fuse_file_info* /*fi*/,
               struct flock* /*lock*/, int /*sleep*/)
{
  // Wine uses lock probes for Windows share-mode emulation. Fluorine does not
  // currently coordinate cross-process POSIX byte-range locks inside the VFS,
  // so report success rather than surfacing EOPNOTSUPP to Windows callers.
  fuse_reply_err(req, 0);
}

void mo2_flock(fuse_req_t req, fuse_ino_t /*ino*/, struct fuse_file_info* /*fi*/,
               int /*op*/)
{
  fuse_reply_err(req, 0);
}

void mo2_fallocate(fuse_req_t req, fuse_ino_t ino, int mode, off_t offset,
                   off_t length, struct fuse_file_info* fi)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || fi == nullptr || offset < 0 || length < 0) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  if ((mode & ~FALLOC_FL_KEEP_SIZE) != 0) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  int fd = -1;
  std::string relativePath;
  if (!ensureWritableOpenFile(ctx, fi->fh, ino, &fd, &relativePath)) {
    std::string path;
    bool ok = false;
    path = inodeToPath(ctx, ino, &ok);
    if (ok && shouldTracePath(path)) {
      std::fprintf(stderr,
                   "[VFS] fallocate ensure writable failed path='%s' mode=0x%x "
                   "offset=%lld length=%lld errno=%d message='%s'\n",
                   path.c_str(), mode, static_cast<long long>(offset),
                   static_cast<long long>(length), errno, std::strerror(errno));
    }
    fuse_reply_err(req, errno != 0 ? errno : EIO);
    return;
  }

  if (shouldTracePath(relativePath)) {
    std::fprintf(stderr,
                 "[VFS] fallocate path='%s' fd=%d mode=0x%x offset=%lld "
                 "length=%lld\n",
                 relativePath.c_str(), fd, mode,
                 static_cast<long long>(offset),
                 static_cast<long long>(length));
  }

  const off_t end = offset + length;
  if (length > 0 && end < offset) {
    fuse_reply_err(req, EFBIG);
    return;
  }

  int rc = 0;
  if (mode == 0) {
    rc = posix_fallocate(fd, offset, length);
  } else {
    rc = fallocate(fd, mode, offset, length) == 0 ? 0 : errno;
    if (rc == EOPNOTSUPP || rc == ENOSYS) {
      // KEEP_SIZE is a reservation hint. If the backing filesystem cannot
      // reserve without changing size, let Wine/.NET continue normally.
      rc = 0;
    }
  }

  if (rc != 0) {
    if (shouldTracePath(relativePath)) {
      std::fprintf(stderr,
                   "[VFS] fallocate failed path='%s' fd=%d rc=%d mode=0x%x "
                   "offset=%lld length=%lld\n",
                   relativePath.c_str(), fd, rc, mode,
                   static_cast<long long>(offset),
                   static_cast<long long>(length));
    }
    fuse_reply_err(req, rc);
    return;
  }

  std::string changedRealPath;
  struct stat st {};
  if (fstat(fd, &st) == 0) {
    {
      std::shared_lock lock(ctx->open_files_mutex);
      auto it = ctx->open_files.find(fi->fh);
      if (it != ctx->open_files.end()) {
        changedRealPath = it->second.real_path;
      }
    }
    if (!changedRealPath.empty()) {
      updateFileNodeKnown(ctx, relativePath, changedRealPath,
                          originForPath(ctx, changedRealPath),
                          static_cast<uint64_t>(st.st_size),
                          timePointFromTimespec(st.st_mtim));
    }
    if (mode == 0) {
      const bool invalidateRange = markOpenFileDirty(
          ctx, fi->fh, static_cast<uint64_t>(st.st_size));
      if (invalidateRange && length > 0) {
        invalidateKernelContent(ctx, ino, offset, length);
      }
    }
  }

  markCatalogDirty(ctx, relativePath, changedRealPath);

  fuse_reply_err(req, 0);
}

void mo2_copy_file_range(fuse_req_t req, fuse_ino_t ino_in, off_t off_in,
                         struct fuse_file_info* fi_in, fuse_ino_t ino_out,
                         off_t off_out, struct fuse_file_info* fi_out,
                         size_t len, int flags)
{
  (void)ino_in;
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || fi_in == nullptr || fi_out == nullptr ||
      off_in < 0 || off_out < 0 || flags != 0) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  int inFd = -1;
  bool closeIn = false;
  if (!ensureReadableOpenFile(ctx, fi_in->fh, &inFd, &closeIn)) {
    fuse_reply_err(req, errno != 0 ? errno : EIO);
    return;
  }

  int outFd = -1;
  std::string outRelativePath;
  if (!ensureWritableOpenFile(ctx, fi_out->fh, ino_out, &outFd, &outRelativePath)) {
    if (closeIn) close(inFd);
    fuse_reply_err(req, errno != 0 ? errno : EIO);
    return;
  }

  ssize_t copied = copy_file_range(inFd, &off_in, outFd, &off_out, len, 0);
  if (copied < 0 && (errno == EOPNOTSUPP || errno == ENOSYS || errno == EXDEV)) {
    const int savedErrno = errno;
    constexpr size_t kBufSize = 1024 * 1024;
    std::vector<char> tmp(std::min(len, kBufSize));
    size_t total = 0;
    bool failed = false;
    while (total < len) {
      const size_t chunk = std::min(tmp.size(), len - total);
      const ssize_t r = pread(inFd, tmp.data(), chunk,
                              off_in + static_cast<off_t>(total));
      if (r < 0) {
        failed = true;
        break;
      }
      if (r == 0) {
        break;
      }
      const ssize_t w = pwrite(outFd, tmp.data(), static_cast<size_t>(r),
                               off_out + static_cast<off_t>(total));
      if (w < 0) {
        failed = true;
        break;
      }
      total += static_cast<size_t>(w);
      if (w < r) {
        break;
      }
    }
    if (failed) {
      copied = -1;
    } else {
      errno = savedErrno;
      copied = static_cast<ssize_t>(total);
    }
  }

  if (closeIn) close(inFd);

  if (copied < 0) {
    fuse_reply_err(req, errno != 0 ? errno : EIO);
    return;
  }

  const bool invalidateRange = markOpenFileDirty(
      ctx, fi_out->fh,
      static_cast<uint64_t>(off_out) + static_cast<uint64_t>(copied));
  if (invalidateRange && copied > 0) {
    invalidateKernelContent(ctx, ino_out, off_out, static_cast<off_t>(copied));
  }
  ctx->write_bytes.fetch_add(static_cast<uint64_t>(copied),
                             std::memory_order_relaxed);
  samplePathStat(ctx, "write", outRelativePath);
  std::string outputRealPath;
  {
    std::shared_lock lock(ctx->open_files_mutex);
    auto it = ctx->open_files.find(fi_out->fh);
    if (it != ctx->open_files.end()) outputRealPath = it->second.real_path;
  }
  markCatalogDirty(ctx, outRelativePath, outputRealPath);
  fuse_reply_write(req, static_cast<size_t>(copied));
}

void mo2_lseek(fuse_req_t req, fuse_ino_t ino, off_t off, int whence,
               struct fuse_file_info* fi)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  uint64_t size = 0;
  bool haveSize = false;
  if (fi != nullptr) {
    std::shared_lock lock(ctx->open_files_mutex);
    auto it = ctx->open_files.find(fi->fh);
    if (it != ctx->open_files.end()) {
      size = it->second.virtual_size;
      haveSize = true;
    }
  }
  if (!haveSize) {
    bool ok = false;
    const std::string path = inodeToPath(ctx, ino, &ok);
    if (!ok) {
      fuse_reply_err(req, ENOENT);
      return;
    }
    const auto snap = snapshotForPath(ctx, path);
    if (!snap.found || snap.is_directory) {
      fuse_reply_err(req, ENOENT);
      return;
    }
    size = snap.size;
  }

  switch (whence) {
  case SEEK_SET:
    if (off < 0) {
      fuse_reply_err(req, EINVAL);
      return;
    }
    fuse_reply_lseek(req, off);
    return;
  case SEEK_CUR:
    if (off < 0) {
      fuse_reply_err(req, EINVAL);
      return;
    }
    fuse_reply_lseek(req, off);
    return;
  case SEEK_END:
  {
    const off_t result = static_cast<off_t>(size) + off;
    if (result < 0) {
      fuse_reply_err(req, EINVAL);
      return;
    }
    fuse_reply_lseek(req, result);
    return;
  }
#ifdef SEEK_DATA
  case SEEK_DATA:
    if (off < 0) {
      fuse_reply_err(req, EINVAL);
      return;
    }
    if (static_cast<uint64_t>(off) < size) {
      fuse_reply_lseek(req, off);
    } else {
      fuse_reply_err(req, ENXIO);
    }
    return;
#endif
#ifdef SEEK_HOLE
  case SEEK_HOLE:
    if (off < 0) {
      fuse_reply_err(req, EINVAL);
      return;
    }
    if (static_cast<uint64_t>(off) < size) {
      fuse_reply_lseek(req, static_cast<off_t>(size));
    } else {
      fuse_reply_err(req, ENXIO);
    }
    return;
#endif
  default:
    fuse_reply_err(req, EINVAL);
    return;
  }
}

void mo2_getxattr(fuse_req_t req, fuse_ino_t /*ino*/, const char* /*name*/,
                  size_t /*size*/)
{
  fuse_reply_err(req, ENODATA);
}

void mo2_listxattr(fuse_req_t req, fuse_ino_t /*ino*/, size_t size)
{
  if (size == 0) {
    fuse_reply_xattr(req, 0);
    return;
  }
  fuse_reply_buf(req, nullptr, 0);
}

void mo2_setxattr(fuse_req_t req, fuse_ino_t /*ino*/, const char* /*name*/,
                  const char* /*value*/, size_t /*size*/, int /*flags*/)
{
  fuse_reply_err(req, 0);
}

void mo2_removexattr(fuse_req_t req, fuse_ino_t /*ino*/, const char* /*name*/)
{
  fuse_reply_err(req, 0);
}

#if FUSE_USE_VERSION < 35
void mo2_ioctl(fuse_req_t req, fuse_ino_t /*ino*/, int cmd, void* /*arg*/,
               struct fuse_file_info* /*fi*/, unsigned /*flags*/,
               const void* /*in_buf*/, size_t /*in_bufsz*/, size_t out_bufsz)
#else
void mo2_ioctl(fuse_req_t req, fuse_ino_t /*ino*/, unsigned int cmd, void* /*arg*/,
               struct fuse_file_info* /*fi*/, unsigned /*flags*/,
               const void* /*in_buf*/, size_t /*in_bufsz*/, size_t out_bufsz)
#endif
{
  static std::atomic<uint64_t> ioctlSeen{0};
  static std::atomic<unsigned int> lastCmd{0};

  Mo2FsContext* ctx = getContext(req);
  if (ctx != nullptr) {
    ctx->ioctl_count.fetch_add(1, std::memory_order_relaxed);
    maybeLogCounters(ctx);
  }

  const unsigned int ucmd = static_cast<unsigned int>(cmd);
  lastCmd.store(ucmd, std::memory_order_relaxed);
  const uint64_t seen = ioctlSeen.fetch_add(1, std::memory_order_relaxed) + 1;
  if ((seen % 500000) == 0) {
    std::fprintf(stderr, "[VFS] ioctl hotloop cmd=0x%x out=%zu\n",
                 lastCmd.load(std::memory_order_relaxed), out_bufsz);
  }

#ifdef VFAT_IOCTL_READDIR_BOTH
  if (ucmd == static_cast<unsigned int>(VFAT_IOCTL_READDIR_BOTH)) {
    // Force Wine fallback path for vfat-specific ioctl probes.
    fuse_reply_err(req, ENOTTY);
    return;
  }
#endif

  if (ucmd == static_cast<unsigned int>(FS_IOC_GETFLAGS)) {
    if (ctx != nullptr && ctx->zero_file_flags && out_bufsz >= sizeof(uint32_t)) {
      const uint32_t fileFlags = 0;
      fuse_reply_ioctl(req, 0, &fileFlags, sizeof(fileFlags));
      return;
    }
    // The compatibility default is unchanged; zero-flags success is opt-in.
    fuse_reply_err(req, ENOTTY);
    return;
  }

  fuse_reply_err(req, ENOTTY);
}

void mo2_access(fuse_req_t req, fuse_ino_t ino, int mask)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  // Root always exists
  if (ino == 1) {
    fuse_reply_err(req, 0);
    return;
  }

  auto index = runtimeIndex(ctx);
  auto indexedNode = index != nullptr ? index->node(ino)
                                      : std::optional<VfsIndexedNode>{};
  if (!indexedNode.has_value()) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  // X_OK on regular files: check real file permissions
  if ((mask & X_OK) != 0) {
    if (!indexedNode->is_directory && !indexedNode->real_path.empty()) {
      struct stat st {};
      const int statResult =
          indexedNode->is_backing && ctx->backing_dir_fd >= 0
              ? ::fstatat(ctx->backing_dir_fd,
                          indexedNode->real_path.c_str(), &st, 0)
              : ::stat(indexedNode->real_path.c_str(), &st);
      if (statResult == 0) {
        if ((st.st_mode & S_IXUSR) == 0) {
          fuse_reply_err(req, EACCES);
          return;
        }
      }
    }
  }

  fuse_reply_err(req, 0);
}
