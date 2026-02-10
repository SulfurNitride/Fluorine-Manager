#ifndef VFS_MO2FILESYSTEM_H
#define VFS_MO2FILESYSTEM_H

#include <fuse3/fuse_lowlevel.h>

#include "inodetable.h"
#include "overwritemanager.h"
#include "vfstree.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

struct Mo2FsContext
{
  std::shared_ptr<VfsTree> tree;
  mutable std::shared_mutex tree_mutex;

  std::unique_ptr<InodeTable> inodes;
  mutable std::mutex inode_mutex;

  std::unique_ptr<OverwriteManager> overwrite;

  int backing_dir_fd = -1;

  struct OpenFile
  {
    std::string real_path;
    bool writable    = false;
    bool is_backing  = false;
    std::string relative_path;
  };

  std::unordered_map<uint64_t, OpenFile> open_files;
  mutable std::mutex open_files_mutex;
  std::atomic<uint64_t> next_fh{1};

  uid_t uid = 0;
  gid_t gid = 0;
};

void mo2_lookup(fuse_req_t req, fuse_ino_t parent, const char* name);
void mo2_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi);
void mo2_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                 struct fuse_file_info* fi);
void mo2_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi);
void mo2_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
              struct fuse_file_info* fi);
void mo2_write(fuse_req_t req, fuse_ino_t ino, const char* buf, size_t size,
               off_t off, struct fuse_file_info* fi);
void mo2_create(fuse_req_t req, fuse_ino_t parent, const char* name, mode_t mode,
                struct fuse_file_info* fi);
void mo2_rename(fuse_req_t req, fuse_ino_t parent, const char* name,
                fuse_ino_t newparent, const char* newname, unsigned int flags);
void mo2_setattr(fuse_req_t req, fuse_ino_t ino, struct stat* attr, int to_set,
                 struct fuse_file_info* fi);
void mo2_unlink(fuse_req_t req, fuse_ino_t parent, const char* name);
void mo2_mkdir(fuse_req_t req, fuse_ino_t parent, const char* name, mode_t mode);
void mo2_release(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi);

#endif
