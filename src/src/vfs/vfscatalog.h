#ifndef VFS_VFSCATALOG_H
#define VFS_VFSCATALOG_H

#include "archiveindex.h"
#include "vfstree.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct VfsCatalogProgress
{
  uint64_t files_scanned = 0;
  uint64_t files_hashed = 0;
  uint64_t bytes_hashed = 0;
  uint64_t hash_workers = 0;
  uint64_t fingerprint_misses = 0;
  uint64_t fingerprint_uncached = 0;
  uint64_t fingerprint_device_mismatches = 0;
  uint64_t fingerprint_inode_mismatches = 0;
  uint64_t fingerprint_size_mismatches = 0;
  uint64_t fingerprint_mode_mismatches = 0;
  uint64_t fingerprint_mtime_mismatches = 0;
  uint64_t fingerprint_ctime_mismatches = 0;
  uint64_t fingerprint_missing_digests = 0;
  uint64_t provider_roots_changed = 0;
  uint64_t catalog_rows_loaded = 0;
  uint64_t catalog_rows_written = 0;
  uint64_t catalog_rows_deleted = 0;
  uint64_t merkle_roots_reused = 0;
  uint64_t archives_discovered = 0;
  uint64_t archives_indexed = 0;
  uint64_t archives_reused = 0;
  uint64_t archive_members = 0;
  uint64_t archive_membership_cache_hits = 0;
  uint64_t archive_membership_cache_bytes = 0;
  uint64_t archive_errors = 0;
  uint64_t archive_workers = 0;
  uint64_t provider_reconcile_ms = 0;
  uint64_t archive_reconcile_ms = 0;
  uint64_t duplicate_scan_ms = 0;
  uint64_t commit_ms = 0;
  uint64_t current_file_size = 0;
  uint64_t elapsed_ms = 0;
  double hash_mib_per_second = 0.0;
  std::string current_root;
  std::string current_file;
};

struct VfsProviderRoot
{
  std::string root_key;
  std::string origin;
  bool is_backing = false;
  uint64_t file_count = 0;
  VfsDigest digest{};
};

struct VfsCatalogFileDigest
{
  std::string relative_path;
  bool exists = false;
  VfsDigest digest{};
};

struct VfsCatalogRefreshResult
{
  VfsProviderRoot provider_root;
  std::vector<VfsCatalogFileDigest> files;
};

enum class VfsDuplicateState
{
  Identical,
  Different
};

struct VfsCatalogDuplicate
{
  std::string relative_path;
  std::string mod_name;
  std::string mod_path;
  VfsDuplicateState state = VfsDuplicateState::Different;
};

struct VfsCatalogResult
{
  VfsTree tree;
  std::vector<VfsProviderRoot> provider_roots;
  VfsDigest profile_root{};
  std::vector<VfsCatalogDuplicate> overwrite_duplicates;
  std::shared_ptr<const VfsArchiveMemberIndex> archive_member_index;
};

// Persistent per-machine inventory of all VFS providers. The SQLite database
// is always stored in Fluorine's local cache; indexed roots may live on any
// local or network filesystem. SQLite is never consulted by FUSE handlers.
class VfsCatalog
{
public:
  using ProgressCallback = std::function<void(const VfsCatalogProgress&)>;

  explicit VfsCatalog(std::filesystem::path database_path);

  static std::filesystem::path databasePath(const std::string& data_dir);

  // Reconcile every provider using cheap stat fingerprints, BLAKE3-hash only
  // new/changed files, resolve conflicts, and return one immutable generation.
  VfsCatalogResult reconcileAndBuild(
      const std::string& data_dir,
      const std::vector<std::pair<std::string, std::string>>& mods,
      const std::string& overwrite_dir,
      bool scan_base,
      ProgressCallback progress = {});

  // Re-hash specific paths without consulting their cached stat fingerprint.
  // This is used for files Fluorine has just promoted or otherwise mutated.
  VfsCatalogRefreshResult forceRefreshProviderFiles(
      const std::string& root_key,
      const std::string& origin,
      bool is_backing,
      const std::vector<std::string>& relative_paths);

  // Best-effort safety valve after a filesystem mutation succeeds but catalog
  // refresh does not. Missing rows must be re-created on the next reconcile.
  void invalidateProviderFiles(
      const std::string& root_key,
      const std::vector<std::string>& relative_paths) noexcept;

  // Snapshot used only for in-session rebuilds after the real data directory
  // is hidden by the FUSE mount.
  std::vector<CachedBaseFile> loadBaseSnapshot(const std::string& data_dir) const;

private:
  std::filesystem::path m_database_path;
};

#endif
