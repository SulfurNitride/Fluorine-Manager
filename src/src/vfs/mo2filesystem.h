#ifndef VFS_MO2FILESYSTEM_H
#define VFS_MO2FILESYSTEM_H

#include <fuse3/fuse_lowlevel.h>

#include "archiveindex.h"
#include "inodetable.h"
#include "overwritemanager.h"
#include "runtimeindex.h"
#include "trackedwrites.h"
#include "vfstree.h"

#include <atomic>
#include <cstddef>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct Mo2OpenFileCleanupResult
{
  std::size_t logical_handles = 0;
  std::size_t descriptors_closed = 0;
  std::size_t writable_descriptors_closed = 0;
  std::size_t close_errors = 0;
};

struct Mo2FsContext
{
  ~Mo2FsContext();

  std::shared_ptr<VfsTree> tree;
  std::shared_ptr<const VfsArchiveMemberIndex> archive_members;
  mutable std::shared_mutex tree_mutex;

  // The catalog-derived positive generation is immutable for its lifetime.
  // Runtime creates/replacements/tombstones live in the index overlay.
  std::shared_ptr<VfsRuntimeIndex> runtime_index;
  mutable std::shared_mutex runtime_index_mutex;
  // Serializes catalog generation replacement with runtime namespace writes.
  mutable std::recursive_mutex namespace_mutation_mutex;

  std::unique_ptr<InodeTable> inodes;
  mutable std::shared_mutex inode_mutex;

  // Fast inode→VfsNode* cache.  Pointers are valid while tree_mutex is held
  // (shared for reads).  Invalidated under exclusive tree_mutex during mutations.
  // Access (read + write) is serialized by node_cache_mutex — tree_mutex-shared
  // is NOT enough because multiple shared readers would race when populating
  // the cache on miss.
  std::unordered_map<fuse_ino_t, const VfsNode*> node_cache;
  mutable std::mutex node_cache_mutex;

  std::unique_ptr<OverwriteManager> overwrite;
  std::shared_ptr<TrackedWrites> tracked_writes;

  int backing_dir_fd = -1;

  struct OpenFile
  {
    int fd = -1;
    std::string real_path;
    bool writable    = false;
    bool is_backing  = false;
    bool cow_pending = false;   // true = opened R/O, will COW on first write()
    bool is_tracked  = false;   // true = user moved this from Overwrite to a mod
    std::string relative_path;
    uint64_t last_read_tick = 0;
    bool metadata_dirty = false;
    // Existing mutable providers may already have kernel pages retained by
    // keep_cache. Invalidate the first range written by this handle, then
    // coalesce the final whole-file invalidation at flush/release.
    bool range_invalidation_pending = false;
    bool content_dirty = false;
    uint64_t virtual_size = 0;
    std::chrono::system_clock::time_point virtual_mtime;
  };

  std::unordered_map<uint64_t, OpenFile> open_files;
  mutable std::shared_mutex open_files_mutex;
  std::atomic<uint64_t> next_fh{1};

  // Relative paths Fluorine itself mutated during this mount. These bypass
  // fingerprint reuse and are BLAKE3-hashed again after FUSE has stopped.
  std::unordered_map<std::string, std::unordered_set<std::string>> dirty_provider_paths;
  std::vector<std::pair<std::string, std::string>> catalog_providers; // root, origin
  std::string default_catalog_root;
  mutable std::mutex dirty_paths_mutex;

  struct DirEntry
  {
    fuse_ino_t ino = 0;
    std::string name;
    bool is_dir = false;
    uint64_t size = 0;
    std::chrono::system_clock::time_point mtime;
    std::string real_path;
    mode_t cached_mode = 0;
  };
  struct OpenDir
  {
    std::string path;
    std::shared_ptr<std::vector<DirEntry>> entries;
    std::shared_ptr<std::vector<char>> readdir_blob;
    std::shared_ptr<std::vector<char>> readdirplus_blob;
  };
  std::unordered_map<uint64_t, OpenDir> open_dirs;
  mutable std::mutex open_dirs_mutex;
  std::unordered_map<std::string, std::shared_ptr<std::vector<DirEntry>>> dir_cache;
  std::unordered_map<std::string, std::shared_ptr<std::vector<char>>> readdir_blob_cache;
  std::unordered_map<std::string, std::shared_ptr<std::vector<char>>> readdirplus_blob_cache;
  mutable std::mutex dir_cache_mutex;
  struct CachedAttr
  {
    struct stat st {};
    std::chrono::steady_clock::time_point expires_at;
    bool valid = false;
  };
  std::unordered_map<fuse_ino_t, CachedAttr> attr_cache;
  mutable std::mutex attr_cache_mutex;

  // Userspace lookup cache: (parent_ino, normalized_child_name) → (child_ino, entry_param).
  // The kernel FUSE dcache is case-sensitive, but Wine probes the same directory
  // with many case variants ("Shaders", "shaders", "SHADERS").  Each variant is a
  // kernel dcache miss that reaches userspace.  This cache makes those re-lookups
  // lock-free (no tree_mutex, no inode_mutex).
  struct LookupCacheEntry
  {
    fuse_ino_t child_ino = 0;
    struct fuse_entry_param entry{};
  };
  struct PairHash {
    size_t operator()(const std::pair<fuse_ino_t, std::string>& p) const {
      size_t const h1 = std::hash<fuse_ino_t>{}(p.first);
      size_t const h2 = std::hash<std::string>{}(p.second);
      return h1 ^ (h2 * 0x9e3779b97f4a7c15ULL + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
  };
  std::unordered_map<std::pair<fuse_ino_t, std::string>, LookupCacheEntry, PairHash> lookup_cache;
  // Reads dominate after the catalog prewarms this table.  A shared mutex
  // prevents independent Wine lookup requests from serializing globally;
  // mutations still take exclusive ownership while invalidating entries.
  mutable std::shared_mutex lookup_cache_mutex;

  std::atomic<uint64_t> next_dh{1};
  std::atomic<uint64_t> fd_lru_tick{1};
  std::atomic<uint64_t> lookup_count{0};
  std::atomic<uint64_t> getattr_count{0};
  std::atomic<uint64_t> readdir_count{0};
  std::atomic<uint64_t> open_count{0};
  std::atomic<uint64_t> read_count{0};
  std::atomic<uint64_t> write_count{0};
  std::atomic<uint64_t> create_count{0};
  std::atomic<uint64_t> rename_count{0};
  std::atomic<uint64_t> setattr_count{0};
  std::atomic<uint64_t> unlink_count{0};
  std::atomic<uint64_t> flush_count{0};
  std::atomic<uint64_t> fsync_count{0};
  std::atomic<uint64_t> ioctl_count{0};
  std::atomic<uint64_t> op_tick{0};
  // Cumulative wall-clock nanoseconds spent inside each op handler.
  // Paired with the *_count fields so we can report avg latency per op.
  std::atomic<uint64_t> lookup_ns{0};
  std::atomic<uint64_t> getattr_ns{0};
  std::atomic<uint64_t> readdir_ns{0};
  std::atomic<uint64_t> open_ns{0};
  std::atomic<uint64_t> read_ns{0};
  std::atomic<uint64_t> write_ns{0};
  std::atomic<uint64_t> create_ns{0};
  std::atomic<uint64_t> rename_ns{0};
  std::atomic<uint64_t> setattr_ns{0};
  std::atomic<uint64_t> unlink_ns{0};
  std::atomic<uint64_t> flush_ns{0};
  std::atomic<uint64_t> fsync_ns{0};
  std::atomic<uint64_t> lookup_cache_hits{0};
  std::atomic<uint64_t> lookup_cache_misses{0};
  std::atomic<uint64_t> lookup_cache_invalidations{0};
  std::atomic<uint64_t> archive_lookup_candidates{0};
  std::atomic<uint64_t> lookup_base_hits{0};
  std::atomic<uint64_t> lookup_overlay_hits{0};
  std::atomic<uint64_t> lookup_negative_hits{0};
  std::atomic<uint64_t> lookup_negative_first{0};
  std::atomic<uint64_t> lookup_tombstones{0};
  std::atomic<uint64_t> lookup_index_invariant_misses{0};
  std::atomic<uint64_t> node_index_hits{0};
  std::atomic<uint64_t> node_index_misses{0};
  std::atomic<uint64_t> dir_cache_invalidations{0};
  std::atomic<uint64_t> dir_cache_entries_erased{0};
  std::atomic<uint64_t> opendir_count{0};
  std::atomic<uint64_t> releasedir_count{0};
  std::atomic<uint64_t> attr_cache_hits{0};
  std::atomic<uint64_t> attr_cache_misses{0};
  std::atomic<uint64_t> dir_cache_hits{0};
  std::atomic<uint64_t> dir_cache_misses{0};
  std::atomic<uint64_t> readdir_blob_hits{0};
  std::atomic<uint64_t> readdirplus_blob_hits{0};
  std::atomic<uint64_t> lazy_ro_fd_opens{0};
  std::atomic<uint64_t> retained_ro_fd_hits{0};
  std::atomic<uint64_t> retained_ro_fd_evictions{0};
  std::atomic<uint64_t> read_bytes{0};
  std::atomic<uint64_t> write_bytes{0};
  std::atomic<uint64_t> cow_write_count{0};
  // Opt-in streamed-read diagnostics for audio assets and audio archives.
  // Normal reads remain zero-copy. When FLUORINE_VFS_TRACE_AUDIO_READS is set,
  // matching reads use pread() so requested and actual byte counts can be
  // compared and short/error replies can be observed precisely.
  std::atomic<uint64_t> audio_trace_read_count{0};
  std::atomic<uint64_t> audio_trace_short_read_count{0};
  std::atomic<uint64_t> audio_trace_error_count{0};
  std::atomic<uint64_t> audio_trace_bytes_requested{0};
  std::atomic<uint64_t> audio_trace_bytes_returned{0};
  std::atomic<uint64_t> audio_trace_log_count{0};
  // CPU snapshot from previous stats tick (microseconds, from getrusage).
  // Used to compute per-tick CPU delta so we can distinguish disk-bound vs
  // CPU-bound slowness in the VFS layer.
  std::atomic<uint64_t> last_cpu_user_us{0};
  std::atomic<uint64_t> last_cpu_sys_us{0};
  std::atomic<uint64_t> last_tick_wall_ns{0};
  std::unordered_map<std::string, uint64_t> lookup_hit_paths;
  std::unordered_map<std::string, uint64_t> lookup_miss_paths;
  std::unordered_map<std::string, uint64_t> getattr_paths;
  std::unordered_map<std::string, uint64_t> readdir_paths;
  std::unordered_map<std::string, uint64_t> write_paths;
  std::unordered_map<std::string, uint64_t> create_paths;
  std::unordered_map<std::string, uint64_t> setattr_paths;
  std::unordered_map<std::string, uint64_t> flush_paths;
  mutable std::mutex path_stats_mutex;
  std::atomic<uint64_t> path_sample_tick{0};

  uid_t uid = 0;
  gid_t gid = 0;

  // Set once after fuse_session_new(); used by notify_inval_inode calls to
  // flush stale kernel page cache without needing fuse_req_session(req).
  struct fuse_session* session = nullptr;

  // Kernel page-cache invalidation can block while a FUSE write request owns
  // the folio being invalidated. Running the notification from that request's
  // worker deadlocks: the kernel waits for the FUSE reply while the worker
  // waits for the kernel to release the folio. Queue notifications to a
  // dedicated thread so request workers can always send their replies.
  struct KernelInvalidation
  {
    fuse_ino_t ino = 0;
    off_t offset = 0;
    off_t length = 0;
  };
  std::deque<KernelInvalidation> kernel_invalidations;
  std::mutex kernel_invalidation_mutex;
  std::condition_variable kernel_invalidation_cv;
  bool stop_kernel_invalidations = false;

  // Diagnostic toggle (Settings > Proton/Wine tab, or FLUORINE_VFS_DISABLE_CACHE
  // env var): zeroes the kernel-facing TTLs and disables keep_cache so a
  // "stale VFS data" report can be tested against caching as the suspect.
  // Not meant to stay on by default — see the June 2026 shader-cache/INI-
  // clobber fix for why keep_cache=1 exists normally.
  bool cache_disabled = false;
  bool no_opendir_supported = false;
  bool disable_no_opendir = false;
  bool readdirplus_enabled = false;
  bool zero_file_flags = false;
  bool trace_audio_reads = false;

  // When true, mo2_lookup inserts phantom directories for missing path
  // components that look like directories (no dot).  Needed by Starfield
  // which calls CreateFile on paths whose parents haven't been mkdir'd.
  // Most games must leave this false to avoid shadowing real entries.
  bool auto_create_dirs = false;
};

// Eagerly materialize the persistent catalog's resolved tree as runtime inode
// and lookup entries. Must be called before the FUSE session starts.
std::size_t mo2PrewarmLookupIndex(Mo2FsContext* ctx);

// Lifecycle for the dedicated kernel page-cache invalidation worker.
void mo2RunKernelInvalidations(Mo2FsContext* ctx);
void mo2StopKernelInvalidations(Mo2FsContext* ctx);

// Close and forget every open file handle after FUSE request workers stop.
// Safe to call repeatedly; the context destructor calls it as a fallback.
Mo2OpenFileCleanupResult mo2CloseOpenFiles(Mo2FsContext* ctx) noexcept;

void mo2_init(void* userdata, struct fuse_conn_info* conn);
void mo2_lookup(fuse_req_t req, fuse_ino_t parent, const char* name);
void mo2_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi);
void mo2_opendir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi);
void mo2_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                 struct fuse_file_info* fi);
void mo2_readdirplus(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                     struct fuse_file_info* fi);
void mo2_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi);
void mo2_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
              struct fuse_file_info* fi);
void mo2_write(fuse_req_t req, fuse_ino_t ino, const char* buf, size_t size,
               off_t off, struct fuse_file_info* fi);
void mo2_create(fuse_req_t req, fuse_ino_t parent, const char* name, mode_t mode,
                struct fuse_file_info* fi);
void mo2_rename(fuse_req_t req, fuse_ino_t parent, const char* name,
                fuse_ino_t newparent, const char* newname, unsigned int flags);
void mo2_setattr(fuse_req_t req, fuse_ino_t ino, struct stat* attr, int to_set,
                 struct fuse_file_info* fi);
void mo2_unlink(fuse_req_t req, fuse_ino_t parent, const char* name);
void mo2_mkdir(fuse_req_t req, fuse_ino_t parent, const char* name, mode_t mode);
void mo2_rmdir(fuse_req_t req, fuse_ino_t parent, const char* name);
void mo2_release(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi);
void mo2_releasedir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi);
void mo2_forget(fuse_req_t req, fuse_ino_t ino, uint64_t nlookup);
void mo2_flush(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi);
void mo2_fsync(fuse_req_t req, fuse_ino_t ino, int datasync,
               struct fuse_file_info* fi);
void mo2_statfs(fuse_req_t req, fuse_ino_t ino);
void mo2_getlk(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi,
               struct flock* lock);
void mo2_setlk(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi,
               struct flock* lock, int sleep);
void mo2_flock(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi, int op);
void mo2_fallocate(fuse_req_t req, fuse_ino_t ino, int mode, off_t offset,
                   off_t length, struct fuse_file_info* fi);
void mo2_copy_file_range(fuse_req_t req, fuse_ino_t ino_in, off_t off_in,
                         struct fuse_file_info* fi_in, fuse_ino_t ino_out,
                         off_t off_out, struct fuse_file_info* fi_out,
                         size_t len, int flags);
void mo2_lseek(fuse_req_t req, fuse_ino_t ino, off_t off, int whence,
               struct fuse_file_info* fi);
void mo2_getxattr(fuse_req_t req, fuse_ino_t ino, const char* name, size_t size);
void mo2_listxattr(fuse_req_t req, fuse_ino_t ino, size_t size);
void mo2_setxattr(fuse_req_t req, fuse_ino_t ino, const char* name,
                  const char* value, size_t size, int flags);
void mo2_removexattr(fuse_req_t req, fuse_ino_t ino, const char* name);
#if FUSE_USE_VERSION < 35
void mo2_ioctl(fuse_req_t req, fuse_ino_t ino, int cmd, void* arg,
               struct fuse_file_info* fi, unsigned flags, const void* in_buf,
               size_t in_bufsz, size_t out_bufsz);
#else
void mo2_ioctl(fuse_req_t req, fuse_ino_t ino, unsigned int cmd, void* arg,
               struct fuse_file_info* fi, unsigned flags, const void* in_buf,
               size_t in_bufsz, size_t out_bufsz);
#endif
void mo2_access(fuse_req_t req, fuse_ino_t ino, int mask);

#endif
