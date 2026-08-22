#ifndef VFS_VFSTREE_H
#define VFS_VFSTREE_H

#include <array>
#include <chrono>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct VfsNode;

using VfsDigest = std::array<unsigned char, 32>;

struct VfsFileInfo
{
  std::string real_path;
  uint64_t size = 0;
  std::chrono::system_clock::time_point mtime;
  std::string origin;
  bool is_backing = false;
  mode_t cached_mode = 0;  // permission bits from stat() at tree-build time
  // Reused from the persistent catalog. Session/profile mappings deliberately
  // leave this empty rather than hashing on the launch path.
  std::optional<VfsDigest> cached_blake3;
};

struct CachedBaseFile
{
  std::string relative_path;
  uint64_t size = 0;
  std::chrono::system_clock::time_point mtime;
  bool is_dir = false;
  mode_t mode = 0;
};

struct VfsDirInfo
{
  std::unordered_map<std::string, std::unique_ptr<VfsNode>> children;
  // Case-preserving names keyed by the case-insensitive lookup key. The first
  // spelling survives later provider overwrites, matching Windows replacement
  // behavior while children still hold the highest-priority winning nodes.
  std::unordered_map<std::string, std::string> display_names;
};

struct VfsNode
{
  bool is_directory = true;
  VfsFileInfo file_info;
  VfsDirInfo dir_info;

  void insertFile(const std::vector<std::string>& components,
                  const std::string& real_path, uint64_t size,
                  std::chrono::system_clock::time_point mtime,
                  const std::string& origin, bool is_backing = false,
                  mode_t cached_mode = 0,
                  std::optional<VfsDigest> cached_blake3 = std::nullopt);

  void insertDirectory(const std::vector<std::string>& components);

  const VfsNode* resolve(const std::vector<std::string>& components) const;

  std::vector<std::pair<std::string, const VfsNode*>> listChildren() const;

  bool removeFromTree(const std::vector<std::string>& components);
};

struct VfsTree
{
  VfsNode root;
  size_t file_count = 0;
  size_t dir_count  = 0;
};

std::string normalizeForLookup(const std::string& path);

VfsTree buildVfsTree(const std::vector<std::pair<std::string, std::string>>& mods,
                     const std::string& overwrite_dir);

VfsTree buildFullGameVfs(const std::string& game_dir, const std::string& data_dir,
                         const std::vector<std::pair<std::string, std::string>>& mods,
                         const std::string& overwrite_dir);

std::vector<CachedBaseFile> scanDataDir(const std::string& data_dir_path);

VfsTree buildDataDirVfs(const std::vector<CachedBaseFile>& cached_files,
                        const std::string& data_dir,
                        const std::vector<std::pair<std::string, std::string>>& mods,
                        const std::string& overwrite_dir);

// Inject individual file mappings into an already-built VFS tree.
// Each entry is (relative_vfs_path, absolute_real_path).  Inserted with
// highest priority (overwrites any existing entry at the same path).
void injectExtraFiles(
    VfsTree& tree,
    const std::vector<std::pair<std::string, std::string>>& extra_files);

// Binary serialize/deserialize a VfsNode subtree (depth-first).
// Returns true on success. Used by ScanCache to persist the merged
// tree to disk and skip the full mod walk on warm boot.
bool serializeNode(std::ostream& out, const VfsNode& node);
bool deserializeNode(std::istream& in, VfsNode& node);

// Stamp plugin files (.esp/.esm/.esl) in the VFS with incrementing
// timestamps matching their position in the load order.  This ensures
// tools like LOOT see an unambiguous timestamp-based ordering that
// matches plugins.txt.
void stampPluginTimestamps(VfsTree& tree,
                           const std::vector<std::string>& load_order);

#endif
