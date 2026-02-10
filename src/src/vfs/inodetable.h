#ifndef VFS_INODETABLE_H
#define VFS_INODETABLE_H

#include <cstdint>
#include <string>
#include <unordered_map>

class InodeTable
{
public:
  InodeTable();

  uint64_t getOrCreate(const std::string& path);
  std::string getPath(uint64_t ino) const;
  void rename(const std::string& old_path, const std::string& new_path);

private:
  std::unordered_map<std::string, uint64_t> m_pathToInode;
  std::unordered_map<uint64_t, std::string> m_inodeToPath;
  uint64_t m_nextInode = 2;
};

#endif
