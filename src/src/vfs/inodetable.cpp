#include "inodetable.h"

#include "vfstree.h"

#include <vector>

namespace
{
std::string canonicalizePath(const std::string& path)
{
  std::string canonical = path;
  for (char& c : canonical) {
    if (c == '\\') {
      c = '/';
    }
  }

  while (!canonical.empty() && canonical.front() == '/') {
    canonical.erase(canonical.begin());
  }
  while (!canonical.empty() && canonical.back() == '/') {
    canonical.pop_back();
  }

  return canonical;
}
}  // namespace

InodeTable::InodeTable()
{
  m_pathToInode.emplace("", 1);
  m_inodeToPath.emplace(1, "");
}

uint64_t InodeTable::getOrCreate(const std::string& path)
{
  const std::string key = normalizeForLookup(path);
  auto existing         = m_pathToInode.find(key);
  if (existing != m_pathToInode.end()) {
    return existing->second;
  }

  const uint64_t ino = m_nextInode++;
  m_pathToInode.emplace(key, ino);
  m_inodeToPath.emplace(ino, canonicalizePath(path));
  return ino;
}

std::string InodeTable::getPath(uint64_t ino) const
{
  auto it = m_inodeToPath.find(ino);
  if (it == m_inodeToPath.end()) {
    return "";
  }
  return it->second;
}

void InodeTable::rename(const std::string& old_path, const std::string& new_path)
{
  const std::string oldCanonical = canonicalizePath(old_path);
  const std::string newCanonical = canonicalizePath(new_path);
  const std::string oldKey       = normalizeForLookup(oldCanonical);
  const std::string newKey       = normalizeForLookup(newCanonical);

  auto it = m_pathToInode.find(oldKey);
  if (it != m_pathToInode.end()) {
    const uint64_t ino = it->second;
    m_pathToInode.erase(it);
    m_pathToInode.emplace(newKey, ino);
    m_inodeToPath[ino] = newCanonical;
  }

  std::vector<std::pair<std::string, uint64_t>> descendants;
  descendants.reserve(m_pathToInode.size());

  const std::string oldPrefix = oldKey.empty() ? oldKey : oldKey + "/";
  for (const auto& [key, ino] : m_pathToInode) {
    if (oldPrefix.empty() || key.rfind(oldPrefix, 0) == 0) {
      descendants.emplace_back(key, ino);
    }
  }

  for (const auto& [descKey, ino] : descendants) {
    if (descKey == oldKey) {
      continue;
    }

    const std::string suffix = descKey.substr(oldPrefix.size());
    const std::string nextKey = newKey.empty() ? suffix : newKey + "/" + suffix;
    m_pathToInode.erase(descKey);
    m_pathToInode.emplace(nextKey, ino);

    auto inodeIt = m_inodeToPath.find(ino);
    if (inodeIt != m_inodeToPath.end()) {
      const std::string suffixCanonical = inodeIt->second.substr(oldCanonical.size());
      inodeIt->second = newCanonical + suffixCanonical;
    }
  }
}
