#include "mo2filesystem.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace
{
namespace fs = std::filesystem;

constexpr double TTL_SECONDS = 60.0 * 60.0 * 24.0 * 365.0;

struct NodeSnapshot
{
  bool found        = false;
  bool is_directory = false;
  bool is_backing   = false;
  uint64_t size     = 0;
  std::chrono::system_clock::time_point mtime{};
  std::string real_path;
};

Mo2FsContext* getContext(fuse_req_t req)
{
  return static_cast<Mo2FsContext*>(fuse_req_userdata(req));
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

std::string joinPath(const std::string& base, const std::string& name)
{
  if (base.empty()) {
    return name;
  }
  return base + "/" + name;
}

std::string inodeToPath(const Mo2FsContext* ctx, fuse_ino_t ino, bool* ok)
{
  std::scoped_lock lock(ctx->inode_mutex);
  const std::string path = ctx->inodes->getPath(ino);

  if (ino == 1) {
    *ok = true;
    return "";
  }

  *ok = !path.empty();
  return path;
}

NodeSnapshot snapshotForPath(const Mo2FsContext* ctx, const std::string& path)
{
  NodeSnapshot snap;
  std::shared_lock lock(ctx->tree_mutex);

  const VfsNode* node = path.empty() ? &ctx->tree->root : ctx->tree->root.resolve(splitPath(path));
  if (node == nullptr) {
    return snap;
  }

  snap.found        = true;
  snap.is_directory = node->is_directory;
  if (!node->is_directory) {
    snap.real_path  = node->file_info.real_path;
    snap.size       = node->file_info.size;
    snap.mtime      = node->file_info.mtime;
    snap.is_backing = node->file_info.is_backing;
  }

  return snap;
}

std::vector<std::pair<std::string, bool>> listChildrenSnapshot(
    const Mo2FsContext* ctx, const std::string& path, bool* ok)
{
  std::vector<std::pair<std::string, bool>> out;
  std::shared_lock lock(ctx->tree_mutex);

  const VfsNode* node = path.empty() ? &ctx->tree->root : ctx->tree->root.resolve(splitPath(path));
  if (node == nullptr || !node->is_directory) {
    *ok = false;
    return out;
  }

  *ok = true;
  for (const auto& [name, child] : node->listChildren()) {
    out.emplace_back(name, child->is_directory);
  }

  return out;
}

void fillStatForDir(struct stat* st, fuse_ino_t ino, uid_t uid, gid_t gid)
{
  std::memset(st, 0, sizeof(struct stat));
  st->st_ino   = ino;
  st->st_mode  = S_IFDIR | 0755;
  st->st_nlink = 2;
  st->st_uid   = uid;
  st->st_gid   = gid;
}

void fillStatForFile(struct stat* st, fuse_ino_t ino, uid_t uid, gid_t gid,
                     uint64_t size,
                     const std::chrono::system_clock::time_point& mtime)
{
  std::memset(st, 0, sizeof(struct stat));
  st->st_ino   = ino;
  st->st_mode  = S_IFREG | 0644;
  st->st_nlink = 1;
  st->st_uid   = uid;
  st->st_gid   = gid;
  st->st_size  = static_cast<off_t>(size);

  const auto secs = std::chrono::duration_cast<std::chrono::seconds>(
      mtime.time_since_epoch());
  st->st_mtim.tv_sec = secs.count();
  st->st_ctim.tv_sec = secs.count();
  st->st_atim.tv_sec = secs.count();
}

void replyEntryFromSnapshot(fuse_req_t req, const Mo2FsContext* ctx, fuse_ino_t ino,
                            const NodeSnapshot& snap)
{
  struct fuse_entry_param e;
  std::memset(&e, 0, sizeof(e));
  e.ino           = ino;
  e.attr_timeout  = TTL_SECONDS;
  e.entry_timeout = TTL_SECONDS;

  if (snap.is_directory) {
    fillStatForDir(&e.attr, ino, ctx->uid, ctx->gid);
  } else {
    fillStatForFile(&e.attr, ino, ctx->uid, ctx->gid, snap.size, snap.mtime);
  }

  fuse_reply_entry(req, &e);
}

bool isWritableOpen(int flags)
{
  return (flags & O_WRONLY) != 0 || (flags & O_RDWR) != 0;
}

std::chrono::system_clock::time_point fileMtimeOrNow(const std::string& path)
{
  std::error_code ec;
  const auto mtime = fs::last_write_time(path, ec);
  if (ec) {
    return std::chrono::system_clock::now();
  }

  const auto nowFs  = fs::file_time_type::clock::now();
  const auto nowSys = std::chrono::system_clock::now();
  return nowSys + std::chrono::duration_cast<std::chrono::system_clock::duration>(
                      mtime - nowFs);
}

void updateFileNode(Mo2FsContext* ctx, const std::string& relative,
                    const std::string& realPath, const std::string& origin)
{
  std::error_code ec;
  const uint64_t size = static_cast<uint64_t>(fs::file_size(realPath, ec));
  const auto mtime    = fileMtimeOrNow(realPath);

  std::unique_lock lock(ctx->tree_mutex);
  ctx->tree->root.insertFile(splitPath(relative), realPath, ec ? 0 : size, mtime,
                             origin);
}

}  // namespace

void mo2_lookup(fuse_req_t req, fuse_ino_t parent, const char* name)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || name == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  bool ok = false;
  const std::string parentPath = inodeToPath(ctx, parent, &ok);
  if (!ok) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  const std::string childPath = joinPath(parentPath, name);
  const auto snap             = snapshotForPath(ctx, childPath);
  if (!snap.found) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  fuse_ino_t childIno;
  {
    std::scoped_lock lock(ctx->inode_mutex);
    childIno = ctx->inodes->getOrCreate(childPath);
  }

  replyEntryFromSnapshot(req, ctx, childIno, snap);
}

void mo2_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* /*fi*/)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  if (ino == 1) {
    struct stat st;
    fillStatForDir(&st, 1, ctx->uid, ctx->gid);
    fuse_reply_attr(req, &st, TTL_SECONDS);
    return;
  }

  bool ok = false;
  const std::string path = inodeToPath(ctx, ino, &ok);
  if (!ok) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  const auto snap = snapshotForPath(ctx, path);
  if (!snap.found) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  struct stat st;
  if (snap.is_directory) {
    fillStatForDir(&st, ino, ctx->uid, ctx->gid);
  } else {
    fillStatForFile(&st, ino, ctx->uid, ctx->gid, snap.size, snap.mtime);
  }

  fuse_reply_attr(req, &st, TTL_SECONDS);
}

void mo2_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                 struct fuse_file_info* /*fi*/)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || off < 0) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  bool ok = false;
  const std::string path = inodeToPath(ctx, ino, &ok);
  if (!ok) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  bool listOk = false;
  auto children = listChildrenSnapshot(ctx, path, &listOk);
  if (!listOk) {
    fuse_reply_err(req, ENOTDIR);
    return;
  }

  struct Entry
  {
    fuse_ino_t ino;
    std::string name;
    bool isDir;
  };

  std::vector<Entry> entries;
  entries.reserve(children.size() + 2);
  entries.push_back({ino, ".", true});
  entries.push_back({1, "..", true});

  {
    std::scoped_lock lock(ctx->inode_mutex);
    for (const auto& [name, isDir] : children) {
      const std::string childPath = joinPath(path, name);
      entries.push_back({ctx->inodes->getOrCreate(childPath), name, isDir});
    }
  }

  std::vector<char> buf(size);
  size_t used = 0;

  for (size_t i = static_cast<size_t>(off); i < entries.size(); ++i) {
    struct stat st;
    std::memset(&st, 0, sizeof(st));
    st.st_ino  = entries[i].ino;
    st.st_mode = entries[i].isDir ? (S_IFDIR | 0755) : (S_IFREG | 0644);

    const size_t ent = fuse_add_direntry(req, buf.data() + used, size - used,
                                         entries[i].name.c_str(), &st,
                                         static_cast<off_t>(i + 1));
    if (ent > size - used) {
      break;
    }
    used += ent;
  }

  fuse_reply_buf(req, buf.data(), used);
}

void mo2_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || fi == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  bool ok = false;
  const std::string path = inodeToPath(ctx, ino, &ok);
  if (!ok) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  const auto snap = snapshotForPath(ctx, path);
  if (!snap.found || snap.is_directory) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  std::string realPath = snap.real_path;
  const bool writable  = isWritableOpen(fi->flags);
  bool isBacking       = snap.is_backing;

  if (writable) {
    try {
      if (isBacking && ctx->backing_dir_fd >= 0) {
        realPath = ctx->overwrite->copyOnWriteFromFd(ctx->backing_dir_fd, path);
      } else {
        realPath = ctx->overwrite->copyOnWrite(realPath, path);
      }
      isBacking = false;
      updateFileNode(ctx, path, realPath, "Staging");
    } catch (...) {
      fuse_reply_err(req, EIO);
      return;
    }
  }

  const uint64_t fh = ctx->next_fh.fetch_add(1, std::memory_order_relaxed);
  {
    std::scoped_lock lock(ctx->open_files_mutex);
    Mo2FsContext::OpenFile of;
    of.real_path     = realPath;
    of.writable      = writable;
    of.is_backing    = isBacking;
    of.relative_path = path;
    ctx->open_files[fh] = std::move(of);
  }

  fi->fh         = fh;
  fi->keep_cache = 1;
  fuse_reply_open(req, fi);
}

void mo2_read(fuse_req_t req, fuse_ino_t /*ino*/, size_t size, off_t off,
              struct fuse_file_info* fi)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || fi == nullptr || off < 0) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  std::string realPath;
  bool isBacking = false;
  {
    std::scoped_lock lock(ctx->open_files_mutex);
    auto it = ctx->open_files.find(fi->fh);
    if (it == ctx->open_files.end()) {
      fuse_reply_err(req, EBADF);
      return;
    }
    realPath   = it->second.real_path;
    isBacking  = it->second.is_backing;
  }

  int fd = -1;
  if (isBacking && ctx->backing_dir_fd >= 0) {
    fd = openat(ctx->backing_dir_fd, realPath.c_str(), O_RDONLY);
  } else {
    fd = open(realPath.c_str(), O_RDONLY);
  }

  if (fd < 0) {
    fuse_reply_err(req, EIO);
    return;
  }

  std::vector<char> out(size);
  const ssize_t n = pread(fd, out.data(), size, off);
  close(fd);

  if (n < 0) {
    fuse_reply_err(req, EIO);
    return;
  }

  fuse_reply_buf(req, out.data(), static_cast<size_t>(n));
}

void mo2_write(fuse_req_t req, fuse_ino_t /*ino*/, const char* buf, size_t size,
               off_t off, struct fuse_file_info* fi)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || fi == nullptr || off < 0 || (buf == nullptr && size > 0)) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  Mo2FsContext::OpenFile open;
  {
    std::scoped_lock lock(ctx->open_files_mutex);
    auto it = ctx->open_files.find(fi->fh);
    if (it == ctx->open_files.end()) {
      fuse_reply_err(req, EBADF);
      return;
    }
    open = it->second;
  }

  if (!open.writable) {
    fuse_reply_err(req, EACCES);
    return;
  }

  std::fstream io(open.real_path, std::ios::binary | std::ios::in | std::ios::out);
  if (!io) {
    io.open(open.real_path, std::ios::binary | std::ios::out);
    io.close();
    io.open(open.real_path, std::ios::binary | std::ios::in | std::ios::out);
  }

  if (!io) {
    fuse_reply_err(req, EIO);
    return;
  }

  io.seekp(off, std::ios::beg);
  io.write(buf, static_cast<std::streamsize>(size));
  io.flush();
  if (!io) {
    fuse_reply_err(req, EIO);
    return;
  }

  updateFileNode(ctx, open.relative_path, open.real_path, "Staging");
  fuse_reply_write(req, size);
}

void mo2_create(fuse_req_t req, fuse_ino_t parent, const char* name, mode_t /*mode*/,
                struct fuse_file_info* fi)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || fi == nullptr || name == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  bool ok = false;
  const std::string parentPath = inodeToPath(ctx, parent, &ok);
  if (!ok) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  const std::string relative = joinPath(parentPath, name);

  std::string realPath;
  try {
    realPath = ctx->overwrite->writeFile(relative, {});
  } catch (...) {
    fuse_reply_err(req, EIO);
    return;
  }

  updateFileNode(ctx, relative, realPath, "Staging");
  {
    std::unique_lock lock(ctx->tree_mutex);
    ++ctx->tree->file_count;
  }

  fuse_ino_t newIno;
  {
    std::scoped_lock lock(ctx->inode_mutex);
    newIno = ctx->inodes->getOrCreate(relative);
  }

  const auto snap = snapshotForPath(ctx, relative);
  if (!snap.found || snap.is_directory) {
    fuse_reply_err(req, EIO);
    return;
  }

  const uint64_t fh = ctx->next_fh.fetch_add(1, std::memory_order_relaxed);
  {
    std::scoped_lock lock(ctx->open_files_mutex);
    Mo2FsContext::OpenFile of;
    of.real_path     = realPath;
    of.writable      = true;
    of.is_backing    = false;
    of.relative_path = relative;
    ctx->open_files[fh] = std::move(of);
  }

  fi->fh         = fh;
  fi->keep_cache = 1;

  struct fuse_entry_param e;
  std::memset(&e, 0, sizeof(e));
  e.ino           = newIno;
  e.attr_timeout  = TTL_SECONDS;
  e.entry_timeout = TTL_SECONDS;
  fillStatForFile(&e.attr, newIno, ctx->uid, ctx->gid, snap.size, snap.mtime);

  fuse_reply_create(req, &e, fi);
}

void mo2_rename(fuse_req_t req, fuse_ino_t parent, const char* name,
                fuse_ino_t newparent, const char* newname, unsigned int flags)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || name == nullptr || newname == nullptr || flags != 0) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  bool okParent = false;
  bool okNewParent = false;
  const std::string parentPath = inodeToPath(ctx, parent, &okParent);
  const std::string newParentPath = inodeToPath(ctx, newparent, &okNewParent);
  if (!okParent || !okNewParent) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  const std::string oldRelative = joinPath(parentPath, name);
  const std::string newRelative = joinPath(newParentPath, newname);

  const auto oldSnap = snapshotForPath(ctx, oldRelative);
  if (!oldSnap.found) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  if (!ctx->overwrite->rename(oldRelative, newRelative)) {
    fuse_reply_err(req, EACCES);
    return;
  }

  {
    std::unique_lock lock(ctx->tree_mutex);
    ctx->tree->root.removeFromTree(splitPath(oldRelative));

    if (oldSnap.is_directory) {
      ctx->tree->root.insertDirectory(splitPath(newRelative));
    } else {
      const std::string staged = ctx->overwrite->stagingPath(newRelative);
      const std::string over   = ctx->overwrite->overwritePath(newRelative);
      const std::string real   = fs::exists(staged) ? staged : over;
      ctx->tree->root.insertFile(splitPath(newRelative), real, oldSnap.size,
                                 std::chrono::system_clock::now(), "Staging");
    }
  }

  {
    std::scoped_lock lock(ctx->inode_mutex);
    ctx->inodes->rename(oldRelative, newRelative);
  }

  fuse_reply_err(req, 0);
}

void mo2_setattr(fuse_req_t req, fuse_ino_t ino, struct stat* attr, int to_set,
                 struct fuse_file_info* fi)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  if (ino == 1) {
    struct stat st;
    fillStatForDir(&st, 1, ctx->uid, ctx->gid);
    fuse_reply_attr(req, &st, TTL_SECONDS);
    return;
  }

  bool ok = false;
  const std::string path = inodeToPath(ctx, ino, &ok);
  if (!ok) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  if ((to_set & FUSE_SET_ATTR_SIZE) != 0 && attr != nullptr) {
    std::string target;
    bool targetIsBacking = false;
    uint64_t fh = 0;

    if (fi != nullptr) {
      fh = fi->fh;
      std::scoped_lock lock(ctx->open_files_mutex);
      auto it = ctx->open_files.find(fh);
      if (it != ctx->open_files.end()) {
        target          = it->second.real_path;
        targetIsBacking = it->second.is_backing;
      }
    }

    if (target.empty()) {
      const auto snap = snapshotForPath(ctx, path);
      if (!snap.found || snap.is_directory) {
        fuse_reply_err(req, ENOENT);
        return;
      }
      target          = snap.real_path;
      targetIsBacking = snap.is_backing;
    }

    const std::string stagedPath = ctx->overwrite->stagingPath(path);
    if (fs::path(target).lexically_normal().string() !=
        fs::path(stagedPath).lexically_normal().string()) {
      try {
        if (targetIsBacking && ctx->backing_dir_fd >= 0) {
          target = ctx->overwrite->copyOnWriteFromFd(ctx->backing_dir_fd, path);
        } else {
          target = ctx->overwrite->copyOnWrite(target, path);
        }
      } catch (...) {
        fuse_reply_err(req, EIO);
        return;
      }

      if (fi != nullptr) {
        std::scoped_lock lock(ctx->open_files_mutex);
        auto it = ctx->open_files.find(fh);
        if (it != ctx->open_files.end()) {
          it->second.real_path = target;
          it->second.writable  = true;
        }
      }
    }

    std::error_code ec;
    fs::resize_file(target, static_cast<uint64_t>(attr->st_size), ec);
    if (ec) {
      fuse_reply_err(req, EIO);
      return;
    }

    updateFileNode(ctx, path, target, "Staging");
  }

  const auto snap = snapshotForPath(ctx, path);
  if (!snap.found) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  struct stat st;
  if (snap.is_directory) {
    fillStatForDir(&st, ino, ctx->uid, ctx->gid);
  } else {
    fillStatForFile(&st, ino, ctx->uid, ctx->gid, snap.size, snap.mtime);
  }
  fuse_reply_attr(req, &st, TTL_SECONDS);
}

void mo2_unlink(fuse_req_t req, fuse_ino_t parent, const char* name)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || name == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  bool ok = false;
  const std::string parentPath = inodeToPath(ctx, parent, &ok);
  if (!ok) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  const std::string relative = joinPath(parentPath, name);
  if (!ctx->overwrite->removeFile(relative)) {
    fuse_reply_err(req, EACCES);
    return;
  }

  {
    std::unique_lock lock(ctx->tree_mutex);
    if (ctx->tree->root.removeFromTree(splitPath(relative))) {
      ctx->tree->file_count = ctx->tree->file_count > 0 ? ctx->tree->file_count - 1 : 0;
    }
  }

  fuse_reply_err(req, 0);
}

void mo2_mkdir(fuse_req_t req, fuse_ino_t parent, const char* name, mode_t /*mode*/)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || name == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  bool ok = false;
  const std::string parentPath = inodeToPath(ctx, parent, &ok);
  if (!ok) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  const std::string relative = joinPath(parentPath, name);
  if (!ctx->overwrite->createDirectory(relative)) {
    fuse_reply_err(req, EIO);
    return;
  }

  {
    std::unique_lock lock(ctx->tree_mutex);
    ctx->tree->root.insertDirectory(splitPath(relative));
    ++ctx->tree->dir_count;
  }

  fuse_ino_t dirIno;
  {
    std::scoped_lock lock(ctx->inode_mutex);
    dirIno = ctx->inodes->getOrCreate(relative);
  }

  const auto snap = snapshotForPath(ctx, relative);
  if (!snap.found) {
    fuse_reply_err(req, EIO);
    return;
  }

  replyEntryFromSnapshot(req, ctx, dirIno, snap);
}

void mo2_release(fuse_req_t req, fuse_ino_t /*ino*/, struct fuse_file_info* fi)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || fi == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  {
    std::scoped_lock lock(ctx->open_files_mutex);
    ctx->open_files.erase(fi->fh);
  }

  fuse_reply_err(req, 0);
}
