#include "vfstree.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <future>
#include <istream>
#include <mutex>
#include <ostream>
#include <sys/stat.h>

namespace
{
namespace fs = std::filesystem;

// Clock offset computed once and reused for all file time conversions.
// Avoids two now() syscalls per file (was ~600K calls for 300K files).
struct ClockOffset
{
  std::chrono::system_clock::duration offset;

  ClockOffset()
  {
    const auto nowFs  = fs::file_time_type::clock::now();
    const auto nowSys = std::chrono::system_clock::now();
    offset = nowSys.time_since_epoch() -
             std::chrono::duration_cast<std::chrono::system_clock::duration>(
                 nowFs.time_since_epoch());
  }

  std::chrono::system_clock::time_point convert(const fs::file_time_type& t) const
  {
    return std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            t.time_since_epoch()) +
        offset);
  }
};

static const ClockOffset g_clockOffset;

std::chrono::system_clock::time_point
fsTimeToSystemClock(const fs::file_time_type& t)
{
  return g_clockOffset.convert(t);
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

// Fast relative path extraction by stripping a known prefix string.
// Avoids fs::relative() which does expensive canonicalization (~300K calls).
std::string fastRelative(const std::string& fullPath, const std::string& prefix)
{
  if (fullPath.size() <= prefix.size()) {
    return {};
  }
  size_t start = prefix.size();
  // Skip separators after prefix
  while (start < fullPath.size() && (fullPath[start] == '/' || fullPath[start] == '\\')) {
    ++start;
  }
  if (start >= fullPath.size()) {
    return {};
  }
  return fullPath.substr(start);
}

// Per-directory intermediate results for parallel tree building.
struct DirScanResult
{
  struct FileEntry
  {
    std::vector<std::string> components;
    std::string real_path;
    uint64_t size;
    std::chrono::system_clock::time_point mtime;
    mode_t mode = 0;
  };

  std::vector<FileEntry> files;
  std::vector<std::vector<std::string>> dirs;
};

DirScanResult scanOneModDir(const fs::path& walkDir, const std::string& prefixStr,
                            const std::vector<std::string>& prefix)
{
  DirScanResult result;

  if (!fs::exists(walkDir)) {
    return result;
  }

  for (auto it = fs::recursive_directory_iterator(
           walkDir, fs::directory_options::skip_permission_denied);
       it != fs::recursive_directory_iterator(); ++it) {
    const auto& entry = *it;
    std::error_code ec;

    const std::string relStr = fastRelative(entry.path().string(), prefixStr);
    if (relStr.empty()) {
      continue;
    }

    // Skip meta.ini at top level only
    if (relStr == "meta.ini") {
      continue;
    }

    auto relParts = splitPath(relStr);
    std::vector<std::string> components;
    components.reserve(prefix.size() + relParts.size());
    components.insert(components.end(), prefix.begin(), prefix.end());
    components.insert(components.end(), relParts.begin(), relParts.end());

    struct stat st {};
    if (::lstat(entry.path().c_str(), &st) != 0) {
      continue;
    }

    if (S_ISDIR(st.st_mode)) {
      result.dirs.push_back(std::move(components));
      continue;
    }

    if (!S_ISREG(st.st_mode)) {
      continue;
    }

    DirScanResult::FileEntry fe;
    fe.components = std::move(components);
    fe.real_path  = entry.path().string();
    fe.size       = static_cast<uint64_t>(st.st_size);
    fe.mtime      = std::chrono::system_clock::time_point(
        std::chrono::seconds(st.st_mtim.tv_sec) +
        std::chrono::nanoseconds(st.st_mtim.tv_nsec));
    fe.mode       = st.st_mode & 07777;
    result.files.push_back(std::move(fe));
  }

  return result;
}

void addDirectoryToTree(VfsTree& tree, const fs::path& walkDir,
                        const fs::path& stripPrefix, const std::string& origin,
                        const std::vector<std::string>& prefix,
                        bool is_backing = false)
{
  if (!fs::exists(walkDir)) {
    return;
  }

  const std::string prefixStr = stripPrefix.string();

  for (auto it = fs::recursive_directory_iterator(
           walkDir, fs::directory_options::skip_permission_denied);
       it != fs::recursive_directory_iterator(); ++it) {
    const auto& entry = *it;
    std::error_code ec;

    const std::string relStr = fastRelative(entry.path().string(), prefixStr);
    if (relStr.empty()) {
      continue;
    }

    if (relStr == "meta.ini") {
      continue;
    }

    auto relParts = splitPath(relStr);
    std::vector<std::string> components;
    components.reserve(prefix.size() + relParts.size());
    components.insert(components.end(), prefix.begin(), prefix.end());
    components.insert(components.end(), relParts.begin(), relParts.end());

    struct stat st {};
    if (::lstat(entry.path().c_str(), &st) != 0) {
      continue;
    }

    if (S_ISDIR(st.st_mode)) {
      tree.root.insertDirectory(components);
      ++tree.dir_count;
      continue;
    }

    if (!S_ISREG(st.st_mode)) {
      continue;
    }
    const auto mtime = std::chrono::system_clock::time_point(
        std::chrono::seconds(st.st_mtim.tv_sec) +
        std::chrono::nanoseconds(st.st_mtim.tv_nsec));
    tree.root.insertFile(components, entry.path().string(),
                         static_cast<uint64_t>(st.st_size), mtime,
                         origin, is_backing, st.st_mode & 07777);
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
  std::string result;
  result.reserve(path.size());
  for (unsigned char c : path) {
    result.push_back(c == '\\' ? '/' : static_cast<char>(std::tolower(c)));
  }
  return result;
}

void VfsNode::insertFile(const std::vector<std::string>& components,
                         const std::string& real_path, uint64_t size,
                         std::chrono::system_clock::time_point mtime,
                         const std::string& origin, bool is_backing,
                         mode_t cached_mode,
                         std::optional<VfsDigest> cached_blake3)
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
    // Replacing an existing entry through a differently-cased path does not
    // rename that entry on a case-insensitive Windows filesystem. Preserve the
    // spelling established by the first provider while still replacing the
    // node below with the higher-priority provider's content.
    current->dir_info.display_names.try_emplace(key, part);

    if (i + 1 == components.size()) {
      auto fileNode              = std::make_unique<VfsNode>();
      fileNode->is_directory     = false;
      fileNode->file_info        = {.real_path=real_path, .size=size, .mtime=mtime,
                                    .origin=origin, .is_backing=is_backing,
                                    .cached_mode=cached_mode,
                                    .cached_blake3=std::move(cached_blake3)};
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
    // Merged directories follow the same case-preserving rule as files: a
    // later provider may add/replace children, but merely using another case
    // does not rename the already-visible directory entry.
    current->dir_info.display_names.try_emplace(key, part);

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

  const std::string dataDirStr = dataDir.string();

  for (auto it = fs::recursive_directory_iterator(
           dataDir, fs::directory_options::skip_permission_denied);
       it != fs::recursive_directory_iterator(); ++it) {
    const auto& entry = *it;
    std::error_code ec;

    const std::string relStr = fastRelative(entry.path().string(), dataDirStr);
    if (relStr.empty()) {
      continue;
    }

    CachedBaseFile cf;
    cf.relative_path = relStr;
    struct stat st {};
    if (::lstat(entry.path().c_str(), &st) != 0) {
      continue;
    }
    cf.is_dir = S_ISDIR(st.st_mode);

    if (!cf.is_dir) {
      if (!S_ISREG(st.st_mode)) {
        continue;
      }
      cf.size = static_cast<uint64_t>(st.st_size);
      cf.mtime = std::chrono::system_clock::time_point(
          std::chrono::seconds(st.st_mtim.tv_sec) +
          std::chrono::nanoseconds(st.st_mtim.tv_nsec));
      cf.mode = st.st_mode & 07777;
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
                           "_base_game", /*is_backing=*/true, cf.mode);
      ++tree.file_count;
    }
  }

  // Layer 2: Mods in priority order (higher priority).
  // Scan mod directories in parallel — each mod is independent.
  // Use a bounded number of threads to avoid overwhelming the IO subsystem.
  {
    // Launch parallel scans
    std::vector<std::future<DirScanResult>> futures;
    futures.reserve(mods.size());
    for (const auto& [modName, modPath] : mods) {
      futures.push_back(std::async(
          std::launch::async,
          [&modPath]() {
            return scanOneModDir(fs::path(modPath), modPath, {});
          }));
    }

    // Merge results into tree in priority order (sequential — tree isn't thread-safe)
    for (size_t i = 0; i < mods.size(); ++i) {
      auto result = futures[i].get();
      const auto& origin = mods[i].first;

      for (auto& dir : result.dirs) {
        tree.root.insertDirectory(dir);
        ++tree.dir_count;
      }
      for (auto& fe : result.files) {
        tree.root.insertFile(fe.components, fe.real_path, fe.size, fe.mtime,
                             origin, false, fe.mode);
        ++tree.file_count;
      }
    }
  }

  // Layer 3: Overwrite (highest priority)
  addDirectoryToTree(tree, fs::path(overwrite_dir), fs::path(overwrite_dir),
                     "Overwrite", {});

  return tree;
}

void injectExtraFiles(
    VfsTree& tree,
    const std::vector<std::pair<std::string, std::string>>& extra_files)
{
  for (const auto& [relPath, realPath] : extra_files) {
    auto components = splitPath(relPath);
    if (components.empty()) {
      continue;
    }

    std::error_code ec;
    struct stat st {};
    const bool statOk = ::stat(realPath.c_str(), &st) == 0;
    const auto mtime = statOk
        ? std::chrono::system_clock::time_point(
              std::chrono::seconds(st.st_mtim.tv_sec) +
              std::chrono::nanoseconds(st.st_mtim.tv_nsec))
        : std::chrono::system_clock::now();
    tree.root.insertFile(components, realPath,
                         statOk ? static_cast<uint64_t>(st.st_size) : 0ULL,
                         mtime, "_profile", /*is_backing=*/false,
                         statOk ? (st.st_mode & 07777) : 0644);
    ++tree.file_count;
  }
}

namespace
{
// Binary I/O helpers — fixed-size little-endian, raw memcpy.
// Host is x86_64 little-endian; cache files are not portable across endian.
template <typename T>
bool writePod(std::ostream& out, const T& v)
{
  out.write(reinterpret_cast<const char*>(&v), sizeof(T));
  return out.good();
}

template <typename T>
bool readPod(std::istream& in, T& v)
{
  in.read(reinterpret_cast<char*>(&v), sizeof(T));
  return in.good();
}

bool writeStr(std::ostream& out, const std::string& s)
{
  if (s.size() > 0xFFFF) {
    return false;  // length capped at u16
  }
  uint16_t len = static_cast<uint16_t>(s.size());
  if (!writePod(out, len)) return false;
  if (len > 0) {
    out.write(s.data(), len);
  }
  return out.good();
}

bool readStr(std::istream& in, std::string& s)
{
  uint16_t len = 0;
  if (!readPod(in, len)) return false;
  s.resize(len);
  if (len > 0) {
    in.read(s.data(), len);
  }
  return in.good();
}

int64_t timeToNs(std::chrono::system_clock::time_point t)
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      t.time_since_epoch()).count();
}

std::chrono::system_clock::time_point nsToTime(int64_t ns)
{
  return std::chrono::system_clock::time_point(
      std::chrono::duration_cast<std::chrono::system_clock::duration>(
          std::chrono::nanoseconds(ns)));
}
}  // namespace

bool serializeNode(std::ostream& out, const VfsNode& node)
{
  uint8_t isDir = node.is_directory ? 1 : 0;
  if (!writePod(out, isDir)) return false;

  if (!node.is_directory) {
    if (!writeStr(out, node.file_info.real_path)) return false;
    if (!writePod(out, node.file_info.size)) return false;
    int64_t ns = timeToNs(node.file_info.mtime);
    if (!writePod(out, ns)) return false;
    if (!writeStr(out, node.file_info.origin)) return false;
    uint8_t backing = node.file_info.is_backing ? 1 : 0;
    if (!writePod(out, backing)) return false;
    uint32_t mode = node.file_info.cached_mode;
    if (!writePod(out, mode)) return false;
    const uint8_t hasDigest = node.file_info.cached_blake3.has_value() ? 1 : 0;
    if (!writePod(out, hasDigest)) return false;
    if (hasDigest != 0) {
      out.write(reinterpret_cast<const char*>(
                    node.file_info.cached_blake3->data()),
                node.file_info.cached_blake3->size());
      if (!out.good()) return false;
    }
    return true;
  }

  uint32_t childCount = static_cast<uint32_t>(node.dir_info.children.size());
  if (!writePod(out, childCount)) return false;

  for (const auto& [key, child] : node.dir_info.children) {
    if (!writeStr(out, key)) return false;
    auto it = node.dir_info.display_names.find(key);
    const std::string& display = (it != node.dir_info.display_names.end())
                                     ? it->second
                                     : key;
    if (!writeStr(out, display)) return false;
    if (!serializeNode(out, *child)) return false;
  }
  return true;
}

bool deserializeNode(std::istream& in, VfsNode& node)
{
  uint8_t isDir = 0;
  if (!readPod(in, isDir)) return false;
  node.is_directory = (isDir != 0);

  if (!node.is_directory) {
    if (!readStr(in, node.file_info.real_path)) return false;
    if (!readPod(in, node.file_info.size)) return false;
    int64_t ns = 0;
    if (!readPod(in, ns)) return false;
    node.file_info.mtime = nsToTime(ns);
    if (!readStr(in, node.file_info.origin)) return false;
    uint8_t backing = 0;
    if (!readPod(in, backing)) return false;
    node.file_info.is_backing = (backing != 0);
    uint32_t mode = 0;
    if (!readPod(in, mode)) return false;
    node.file_info.cached_mode = static_cast<mode_t>(mode);
    uint8_t hasDigest = 0;
    if (!readPod(in, hasDigest) || hasDigest > 1) return false;
    if (hasDigest != 0) {
      VfsDigest digest{};
      in.read(reinterpret_cast<char*>(digest.data()), digest.size());
      if (!in.good()) return false;
      node.file_info.cached_blake3 = digest;
    } else {
      node.file_info.cached_blake3.reset();
    }
    return true;
  }

  uint32_t childCount = 0;
  if (!readPod(in, childCount)) return false;

  // Sanity bound — refuse absurd counts so corrupt files do not OOM us.
  // 16M children per dir is far beyond any real modlist.
  if (childCount > 16u * 1024u * 1024u) return false;

  node.dir_info.children.reserve(childCount);
  node.dir_info.display_names.reserve(childCount);
  for (uint32_t i = 0; i < childCount; ++i) {
    std::string key, display;
    if (!readStr(in, key)) return false;
    if (!readStr(in, display)) return false;
    auto child = std::make_unique<VfsNode>();
    if (!deserializeNode(in, *child)) return false;
    node.dir_info.display_names.emplace(key, std::move(display));
    node.dir_info.children.emplace(std::move(key), std::move(child));
  }
  return true;
}

void stampPluginTimestamps(VfsTree& tree,
                           const std::vector<std::string>& load_order)
{
  if (load_order.empty()) {
    return;
  }

  // Start from a base time and increment by 60 seconds per plugin.
  // This gives LOOT a clear, unambiguous ordering via file timestamps.
  auto baseTime = std::chrono::system_clock::now() -
                  std::chrono::seconds(static_cast<long>(load_order.size()) * 60);

  for (size_t i = 0; i < load_order.size(); ++i) {
    const auto components = splitPath(load_order[i]);
    if (components.empty()) {
      continue;
    }

    const std::string key = normalizeForLookup(load_order[i]);
    auto it = tree.root.dir_info.children.find(key);
    if (it == tree.root.dir_info.children.end()) {
      continue;
    }

    VfsNode* node = it->second.get();
    if (node != nullptr && !node->is_directory) {
      node->file_info.mtime = baseTime + std::chrono::seconds(static_cast<long>(i) * 60);
    }
  }
}
