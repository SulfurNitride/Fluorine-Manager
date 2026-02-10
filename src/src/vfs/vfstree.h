#ifndef VFS_VFSTREE_H
#define VFS_VFSTREE_H

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct VfsNode;

struct VfsFileInfo
{
  std::string real_path;
  uint64_t size = 0;
  std::chrono::system_clock::time_point mtime{};
  std::string origin;
  bool is_backing = false;
};

struct CachedBaseFile
{
  std::string relative_path;
  uint64_t size = 0;
  std::chrono::system_clock::time_point mtime{};
  bool is_dir = false;
};

struct VfsDirInfo
{
  std::unordered_map<std::string, std::unique_ptr<VfsNode>> children;
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
                  const std::string& origin, bool is_backing = false);

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

#endif
