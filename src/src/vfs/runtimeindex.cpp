#include "runtimeindex.h"

#include <functional>
#include <mutex>
#include <utility>

namespace
{
std::string joinPath(const std::string& base, const std::string& name)
{
  return base.empty() ? name : base + "/" + name;
}

}  // namespace

std::size_t VfsRuntimeIndex::LookupKeyHash::operator()(
    const LookupKey& key) const
{
  const std::size_t h1 = std::hash<uint64_t>{}(key.parent);
  const std::size_t h2 = std::hash<std::string>{}(key.name);
  return h1 ^ (h2 * 0x9e3779b97f4a7c15ULL + 0x9e3779b9 + (h1 << 6) +
               (h1 >> 2));
}

VfsIndexedNode VfsRuntimeIndex::makeDirectoryNode(
    uint64_t ino, std::string virtualPath)
{
  VfsIndexedNode node;
  node.ino = ino;
  node.virtual_path = std::move(virtualPath);
  node.is_directory = true;
  return node;
}

VfsIndexedNode VfsRuntimeIndex::makeFileNode(
    uint64_t ino, std::string virtualPath, std::string realPath,
    bool isBacking, uint64_t size,
    std::chrono::system_clock::time_point mtime, mode_t cachedMode)
{
  VfsIndexedNode node;
  node.ino = ino;
  node.virtual_path = std::move(virtualPath);
  node.real_path = std::move(realPath);
  node.is_backing = isBacking;
  node.size = size;
  node.mtime = mtime;
  node.cached_mode = cachedMode;
  return node;
}

std::shared_ptr<VfsRuntimeIndex> VfsRuntimeIndex::build(
    const VfsTree& tree, InodeTable& inodes)
{
  auto index = std::make_shared<VfsRuntimeIndex>();
  const std::size_t expected = tree.file_count + tree.dir_count + 1;
  index->m_baseLookups.reserve(expected);
  index->m_baseNodes.resize(2);
  index->m_baseNodes[1] = makeDirectoryNode(1, std::string{});
  index->m_baseNodeCount = 1;

  const auto visit = [&](const auto& self, const VfsNode& parent,
                         const std::string& parentPath,
                         uint64_t parentIno) -> void {
    for (const auto& [key, childPtr] : parent.dir_info.children) {
      if (childPtr == nullptr) continue;
      const auto displayIt = parent.dir_info.display_names.find(key);
      const std::string name = displayIt != parent.dir_info.display_names.end()
                                   ? displayIt->second
                                   : key;
      const std::string childPath = joinPath(parentPath, name);
      const uint64_t childIno = inodes.getOrCreate(childPath);

      VfsIndexedNode node;
      if (childPtr->is_directory) {
        node = makeDirectoryNode(childIno, childPath);
      } else {
        node = makeFileNode(
            childIno, childPath, childPtr->file_info.real_path,
            childPtr->file_info.is_backing, childPtr->file_info.size,
            childPtr->file_info.mtime, childPtr->file_info.cached_mode);
      }
      if (childIno >= index->m_baseNodes.size()) {
        index->m_baseNodes.resize(static_cast<std::size_t>(childIno + 1));
      }
      if (!index->m_baseNodes[childIno].has_value()) {
        ++index->m_baseNodeCount;
      }
      index->m_baseNodes[childIno] = std::move(node);
      index->m_baseLookups.insert_or_assign(
          LookupKey{parentIno, key}, Child{childIno});

      if (childPtr->is_directory) {
        self(self, *childPtr, childPath, childIno);
      }
    }
  };
  visit(visit, tree.root, std::string{}, 1);
  return index;
}

VfsIndexedLookup VfsRuntimeIndex::lookup(
    uint64_t parent, const std::string& name) const
{
  const LookupKey key{parent, normalizeForLookup(name)};
  {
    std::shared_lock lock(m_overlayMutex);
    auto overlay = m_overlayLookups.find(key);
    if (overlay != m_overlayLookups.end()) {
      if (overlay->second.tombstone) {
        return {.source=VfsLookupSource::Tombstone};
      }
      auto nodeIt = m_overlayNodes.find(overlay->second.child.ino);
      if (nodeIt != m_overlayNodes.end()) {
        return {.source=VfsLookupSource::Overlay,
                .node=nodeIt->second};
      }
    }
  }

  auto base = m_baseLookups.find(key);
  if (base != m_baseLookups.end()) {
    std::shared_lock lock(m_overlayMutex);
    if (!m_hiddenInodes.contains(base->second.ino)) {
      auto overlayNode = m_overlayNodes.find(base->second.ino);
      if (overlayNode != m_overlayNodes.end()) {
        return {.source=VfsLookupSource::Overlay,
                .node=overlayNode->second};
      }
      const uint64_t ino = base->second.ino;
      if (ino < m_baseNodes.size() && m_baseNodes[ino].has_value()) {
        return {.source=VfsLookupSource::Base,
                .node=*m_baseNodes[ino]};
      }
    }
  }

  {
    std::shared_lock lock(m_overlayMutex);
    auto negative = m_negativeLookups.find(key);
    if (negative != m_negativeLookups.end() &&
        std::chrono::steady_clock::now() < negative->second) {
      return {.source=VfsLookupSource::Negative};
    }
  }
  return {.source=VfsLookupSource::Missing};
}

std::optional<VfsIndexedNode> VfsRuntimeIndex::node(uint64_t ino) const
{
  std::shared_lock lock(m_overlayMutex);
  if (m_hiddenInodes.contains(ino)) return std::nullopt;
  auto overlay = m_overlayNodes.find(ino);
  if (overlay != m_overlayNodes.end()) return overlay->second;
  return ino < m_baseNodes.size() ? m_baseNodes[ino] : std::nullopt;
}

void VfsRuntimeIndex::publish(uint64_t parent, const std::string& name,
                              const VfsIndexedNode& node)
{
  const LookupKey key{parent, normalizeForLookup(name)};
  std::unique_lock lock(m_overlayMutex);
  m_overlayNodes.insert_or_assign(node.ino, node);
  m_hiddenInodes.erase(node.ino);
  m_overlayLookups.insert_or_assign(
      key, OverlayChild{false, Child{node.ino}});
  m_negativeLookups.erase(key);
}

void VfsRuntimeIndex::tombstone(uint64_t parent, const std::string& name,
                                uint64_t ino)
{
  const LookupKey key{parent, normalizeForLookup(name)};
  std::unique_lock lock(m_overlayMutex);
  m_overlayLookups.insert_or_assign(key, OverlayChild{true, {}});
  m_negativeLookups.erase(key);
  if (ino != 0) {
    m_hiddenInodes.insert(ino);
    m_overlayNodes.erase(ino);
  }
}

void VfsRuntimeIndex::recordNegative(uint64_t parent, const std::string& name,
                                     std::chrono::seconds ttl)
{
  const LookupKey key{parent, normalizeForLookup(name)};
  std::unique_lock lock(m_overlayMutex);
  if (m_overlayLookups.contains(key) || m_baseLookups.contains(key)) return;
  m_negativeLookups.insert_or_assign(key,
                                     std::chrono::steady_clock::now() + ttl);
}

void VfsRuntimeIndex::eraseNegative(uint64_t parent, const std::string& name)
{
  std::unique_lock lock(m_overlayMutex);
  m_negativeLookups.erase(LookupKey{parent, normalizeForLookup(name)});
}

std::size_t VfsRuntimeIndex::baseLookupCount() const
{
  return m_baseLookups.size();
}

std::size_t VfsRuntimeIndex::baseNodeCount() const
{
  return m_baseNodeCount;
}

std::size_t VfsRuntimeIndex::overlayCount() const
{
  std::shared_lock lock(m_overlayMutex);
  return m_overlayLookups.size();
}

std::size_t VfsRuntimeIndex::negativeCount() const
{
  std::shared_lock lock(m_overlayMutex);
  return m_negativeLookups.size();
}
