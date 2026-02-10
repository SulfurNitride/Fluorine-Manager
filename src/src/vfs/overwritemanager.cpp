#include "overwritemanager.h"

#include <fcntl.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>

namespace
{
namespace fs = std::filesystem;

std::string sanitizeRelative(const std::string& relative)
{
  std::string out = relative;
  for (char& c : out) {
    if (c == '\\') {
      c = '/';
    }
  }
  while (!out.empty() && out.front() == '/') {
    out.erase(out.begin());
  }
  return out;
}
}  // namespace

OverwriteManager::OverwriteManager(const std::string& staging_dir,
                                   const std::string& overwrite_dir)
    : m_stagingDir(staging_dir), m_overwriteDir(overwrite_dir)
{
  std::error_code ec;
  fs::create_directories(m_stagingDir, ec);
  fs::create_directories(m_overwriteDir, ec);
}

std::string OverwriteManager::stagingPath(const std::string& relative_path) const
{
  return (fs::path(m_stagingDir) / sanitizeRelative(relative_path)).string();
}

std::string OverwriteManager::overwritePath(const std::string& relative_path) const
{
  return (fs::path(m_overwriteDir) / sanitizeRelative(relative_path)).string();
}

std::string OverwriteManager::copyOnWrite(const std::string& source_path,
                                          const std::string& relative_path)
{
  const fs::path dest = stagingPath(relative_path);

  std::error_code ec;
  fs::create_directories(dest.parent_path(), ec);

  if (fs::exists(dest, ec)) {
    return dest.string();
  }

  if (!source_path.empty() && fs::exists(source_path, ec)) {
    fs::copy_file(source_path, dest, fs::copy_options::overwrite_existing, ec);
    if (ec) {
      throw fs::filesystem_error("copyOnWrite", fs::path(source_path), dest, ec);
    }
  } else {
    std::ofstream out(dest, std::ios::binary);
    out.close();
  }

  return dest.string();
}

std::string OverwriteManager::copyOnWriteFromFd(int dir_fd,
                                                const std::string& relative_path)
{
  const fs::path dest = stagingPath(relative_path);

  std::error_code ec;
  fs::create_directories(dest.parent_path(), ec);

  if (fs::exists(dest, ec)) {
    return dest.string();
  }

  const std::string rel = sanitizeRelative(relative_path);
  const int src_fd      = openat(dir_fd, rel.c_str(), O_RDONLY);
  if (src_fd < 0) {
    // Source doesn't exist in backing dir, create empty file
    std::ofstream out(dest, std::ios::binary);
    out.close();
    return dest.string();
  }

  std::ofstream out(dest, std::ios::binary);
  if (!out) {
    close(src_fd);
    throw fs::filesystem_error("copyOnWriteFromFd", dest,
                               std::error_code(errno, std::generic_category()));
  }

  char buf[65536];
  ssize_t n;
  while ((n = read(src_fd, buf, sizeof(buf))) > 0) {
    out.write(buf, n);
  }

  close(src_fd);
  return dest.string();
}

std::string OverwriteManager::writeFile(const std::string& relative_path,
                                        const std::vector<uint8_t>& data)
{
  const fs::path path = stagingPath(relative_path);
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    throw fs::filesystem_error("writeFile", path, std::error_code(errno, std::generic_category()));
  }

  if (!data.empty()) {
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  }

  return path.string();
}

bool OverwriteManager::rename(const std::string& old_relative,
                              const std::string& new_relative)
{
  std::error_code ec;

  fs::path from = stagingPath(old_relative);
  fs::path to   = stagingPath(new_relative);

  if (!fs::exists(from, ec)) {
    from = overwritePath(old_relative);
    to   = overwritePath(new_relative);
    if (!fs::exists(from, ec)) {
      return false;
    }
  }

  fs::create_directories(to.parent_path(), ec);
  fs::rename(from, to, ec);
  return !ec;
}

bool OverwriteManager::removeFile(const std::string& relative_path)
{
  std::error_code ec;
  fs::path staged = stagingPath(relative_path);
  if (fs::exists(staged, ec)) {
    return fs::remove(staged, ec);
  }

  fs::path overwrite = overwritePath(relative_path);
  if (fs::exists(overwrite, ec)) {
    return fs::remove(overwrite, ec);
  }

  return false;
}

bool OverwriteManager::createDirectory(const std::string& relative_path)
{
  std::error_code ec;
  fs::create_directories(stagingPath(relative_path), ec);
  return !ec;
}

bool OverwriteManager::exists(const std::string& relative_path) const
{
  std::error_code ec;
  return fs::exists(stagingPath(relative_path), ec) ||
         fs::exists(overwritePath(relative_path), ec);
}
