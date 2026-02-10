#include "vfstree.h"

#include <QDir>
#include <QString>

#include <algorithm>
#include <filesystem>

namespace
{
namespace fs = std::filesystem;

std::chrono::system_clock::time_point
fsTimeToSystemClock(const fs::file_time_type& t)
{
  const auto nowFs  = fs::file_time_type::clock::now();
  const auto nowSys = std::chrono::system_clock::now();
  return nowSys + std::chrono::duration_cast<std::chrono::system_clock::duration>(t - nowFs);
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

void addDirectoryToTree(VfsTree& tree, const fs::path& walkDir,
                        const fs::path& stripPrefix, const std::string& origin,
                        const std::vector<std::string>& prefix,
                        bool is_backing = false)
{
  if (!fs::exists(walkDir)) {
    return;
  }

  for (auto it = fs::recursive_directory_iterator(
           walkDir, fs::directory_options::skip_permission_denied);
       it != fs::recursive_directory_iterator(); ++it) {
    const auto& entry = *it;
    std::error_code ec;

    const fs::path rel = fs::relative(entry.path(), stripPrefix, ec);
    if (ec || rel.empty()) {
      continue;
    }

    const std::string relStr = rel.generic_string();
    if (relStr == "meta.ini") {
      continue;
    }

    auto relParts = splitPath(relStr);
    std::vector<std::string> components;
    components.reserve(prefix.size() + relParts.size());
    components.insert(components.end(), prefix.begin(), prefix.end());
    components.insert(components.end(), relParts.begin(), relParts.end());

    if (entry.is_directory(ec)) {
      tree.root.insertDirectory(components);
      ++tree.dir_count;
      continue;
    }

    if (!entry.is_regular_file(ec)) {
      continue;
    }

    const auto size  = entry.file_size(ec);
    const auto mtime = entry.last_write_time(ec);
    tree.root.insertFile(components, entry.path().string(), size,
                         ec ? std::chrono::system_clock::time_point{}
                            : fsTimeToSystemClock(mtime),
                         origin, is_backing);
    ++tree.file_count;
  }
}

bool removeNodeRecursive(VfsNode* node, const std::vector<std::string>& components,
                         size_t index)
{
  if (node == nullptr || !node->is_directory || index >= components.size()) {
    return false;
  }

  const std::string key = normalizeForLookup(components[index]);
  auto it               = node->dir_info.children.find(key);
  if (it == node->dir_info.children.end()) {
    return false;
  }

  if (index + 1 == components.size()) {
    node->dir_info.children.erase(it);
    node->dir_info.display_names.erase(key);
    return true;
  }

  VfsNode* child = it->second.get();
  if (!removeNodeRecursive(child, components, index + 1)) {
    return false;
  }

  if (child->is_directory && child->dir_info.children.empty()) {
    node->dir_info.children.erase(key);
    node->dir_info.display_names.erase(key);
  }

  return true;
}

}  // namespace

std::string normalizeForLookup(const std::string& path)
{
  QString q = QString::fromUtf8(path.c_str());
  q         = q.normalized(QString::NormalizationForm_C).toLower();
  q.replace('\\', '/');
  return q.toUtf8().toStdString();
}

void VfsNode::insertFile(const std::vector<std::string>& components,
                         const std::string& real_path, uint64_t size,
                         std::chrono::system_clock::time_point mtime,
                         const std::string& origin, bool is_backing)
{
  if (components.empty()) {
    return;
  }

  VfsNode* current = this;
  for (size_t i = 0; i < components.size(); ++i) {
    const std::string& part = components[i];
    if (part.empty()) {
      continue;
    }

    if (!current->is_directory) {
      current->is_directory = true;
      current->dir_info     = VfsDirInfo{};
    }

    const std::string key = normalizeForLookup(part);
    current->dir_info.display_names[key] = part;

    if (i + 1 == components.size()) {
      auto fileNode              = std::make_unique<VfsNode>();
      fileNode->is_directory     = false;
      fileNode->file_info        = {real_path, size, mtime, origin, is_backing};
      current->dir_info.children[key] = std::move(fileNode);
      return;
    }

    auto it = current->dir_info.children.find(key);
    if (it == current->dir_info.children.end() || !it->second->is_directory) {
      auto newDir          = std::make_unique<VfsNode>();
      newDir->is_directory = true;
      current->dir_info.children[key] = std::move(newDir);
    }

    current = current->dir_info.children[key].get();
  }
}

void VfsNode::insertDirectory(const std::vector<std::string>& components)
{
  if (components.empty()) {
    return;
  }

  VfsNode* current = this;
  for (const auto& part : components) {
    if (part.empty()) {
      continue;
    }

    if (!current->is_directory) {
      current->is_directory = true;
      current->dir_info     = VfsDirInfo{};
    }

    const std::string key = normalizeForLookup(part);
    current->dir_info.display_names[key] = part;

    auto it = current->dir_info.children.find(key);
    if (it == current->dir_info.children.end() || !it->second->is_directory) {
      auto newDir          = std::make_unique<VfsNode>();
      newDir->is_directory = true;
      current->dir_info.children[key] = std::move(newDir);
    }

    current = current->dir_info.children[key].get();
  }
}

const VfsNode* VfsNode::resolve(const std::vector<std::string>& components) const
{
  const VfsNode* current = this;

  for (const auto& part : components) {
    if (part.empty()) {
      continue;
    }

    if (!current->is_directory) {
      return nullptr;
    }

    const std::string key = normalizeForLookup(part);
    auto it               = current->dir_info.children.find(key);
    if (it == current->dir_info.children.end()) {
      return nullptr;
    }

    current = it->second.get();
  }

  return current;
}

std::vector<std::pair<std::string, const VfsNode*>> VfsNode::listChildren() const
{
  std::vector<std::pair<std::string, const VfsNode*>> out;
  if (!is_directory) {
    return out;
  }

  out.reserve(dir_info.children.size());
  for (const auto& [key, node] : dir_info.children) {
    auto display = dir_info.display_names.find(key);
    if (display == dir_info.display_names.end()) {
      continue;
    }
    out.emplace_back(display->second, node.get());
  }

  return out;
}

bool VfsNode::removeFromTree(const std::vector<std::string>& components)
{
  if (components.empty()) {
    return false;
  }
  return removeNodeRecursive(this, components, 0);
}

VfsTree buildVfsTree(const std::vector<std::pair<std::string, std::string>>& mods,
                     const std::string& overwrite_dir)
{
  VfsTree tree;
  tree.root.is_directory = true;
  tree.file_count        = 0;
  tree.dir_count         = 1;

  addDirectoryToTree(tree, fs::path(overwrite_dir), fs::path(overwrite_dir),
                     "Overwrite", {});

  for (const auto& [modName, modPath] : mods) {
    addDirectoryToTree(tree, fs::path(modPath), fs::path(modPath), modName, {});
  }

  return tree;
}

VfsTree buildFullGameVfs(const std::string& game_dir, const std::string& data_dir,
                         const std::vector<std::pair<std::string, std::string>>& mods,
                         const std::string& overwrite_dir)
{
  VfsTree tree;
  tree.root.is_directory = true;
  tree.file_count        = 0;
  tree.dir_count         = 1;

  addDirectoryToTree(tree, fs::path(game_dir), fs::path(game_dir), "_base_game", {});
  addDirectoryToTree(tree, fs::path(overwrite_dir), fs::path(overwrite_dir),
                     "Overwrite", {});

  const auto dataPrefix = splitPath(data_dir);
  for (const auto& [modName, modPath] : mods) {
    // Step D requirement: no Root/ handling. Every mod file is projected under data_dir.
    addDirectoryToTree(tree, fs::path(modPath), fs::path(modPath), modName, dataPrefix);
  }

  return tree;
}

std::vector<CachedBaseFile> scanDataDir(const std::string& data_dir_path)
{
  std::vector<CachedBaseFile> cache;
  const fs::path dataDir(data_dir_path);

  if (!fs::exists(dataDir)) {
    return cache;
  }

  for (auto it = fs::recursive_directory_iterator(
           dataDir, fs::directory_options::skip_permission_denied);
       it != fs::recursive_directory_iterator(); ++it) {
    const auto& entry = *it;
    std::error_code ec;

    const fs::path rel = fs::relative(entry.path(), dataDir, ec);
    if (ec || rel.empty()) {
      continue;
    }

    CachedBaseFile cf;
    cf.relative_path = rel.generic_string();
    cf.is_dir        = entry.is_directory(ec);

    if (!cf.is_dir) {
      if (!entry.is_regular_file(ec)) {
        continue;
      }
      cf.size  = entry.file_size(ec);
      const auto mtime = entry.last_write_time(ec);
      cf.mtime = ec ? std::chrono::system_clock::time_point{}
                     : fsTimeToSystemClock(mtime);
    }

    cache.push_back(std::move(cf));
  }

  return cache;
}

VfsTree buildDataDirVfs(const std::vector<CachedBaseFile>& cached_files,
                        const std::string& data_dir,
                        const std::vector<std::pair<std::string, std::string>>& mods,
                        const std::string& overwrite_dir)
{
  VfsTree tree;
  tree.root.is_directory = true;
  tree.file_count        = 0;
  tree.dir_count         = 1;

  // Layer 1: Base game files from cache (is_backing=true)
  // real_path stores the relative path; FUSE handler uses openat(backing_fd, rel)
  for (const auto& cf : cached_files) {
    const auto components = splitPath(cf.relative_path);
    if (cf.is_dir) {
      tree.root.insertDirectory(components);
      ++tree.dir_count;
    } else {
      tree.root.insertFile(components, cf.relative_path, cf.size, cf.mtime,
                           "_base_game", /*is_backing=*/true);
      ++tree.file_count;
    }
  }

  // Layer 2: Overwrite (higher priority, overwrites base game)
  addDirectoryToTree(tree, fs::path(overwrite_dir), fs::path(overwrite_dir),
                     "Overwrite", {});

  // Layer 3: Mods in priority order (highest priority)
  for (const auto& [modName, modPath] : mods) {
    addDirectoryToTree(tree, fs::path(modPath), fs::path(modPath), modName, {});
  }

  return tree;
}
