// Mounts the real mo2_* FUSE handlers and checks where writes to existing files
// land. Mirrors MO2/USVFS: existing mod/profile files are edited in place, only
// new files go to staging/overwrite, and base-game files stay untouched (COW).

#include "vfs/mo2filesystem.h"

#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace
{

std::string readText(const fs::path& path)
{
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

void writeText(const fs::path& path, const std::string& text)
{
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
}

bool writeFd(const fs::path& path, int flags, const std::string& text, off_t off = 0)
{
  const int fd = open(path.c_str(), flags, 0644);
  if (fd < 0) return false;
  const ssize_t n = pwrite(fd, text.data(), text.size(), off);
  const bool ok = n == static_cast<ssize_t>(text.size()) && close(fd) == 0;
  return ok;
}

size_t countFiles(const fs::path& dir)
{
  size_t n = 0;
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(dir, ec);
       it != fs::recursive_directory_iterator(); ++it) {
    if (it->is_regular_file(ec)) ++n;
  }
  return n;
}

void ctxCloseHandle(Mo2FsContext* ctx)
{
  mo2CloseOpenFiles(ctx);
}

class MountedVfs
{
public:
  MountedVfs()
  {
    char tmpl[] = "/tmp/fluorine-vfs-inplace-XXXXXX";
    const char* d = mkdtemp(tmpl);
    if (d == nullptr) return;
    m_root = d;
    m_modA = m_root / "mods" / "ModA";
    m_modB = m_root / "mods" / "ModB";
    m_game = m_root / "game";
    m_overwrite = m_root / "overwrite";
    m_staging = m_root / "VFS_staging";
    m_mount = m_root / "mnt";
    for (const auto& p : {m_modA, m_modB, m_game, m_overwrite, m_staging, m_mount}) {
      fs::create_directories(p);
    }
  }

  ~MountedVfs()
  {
    unmount();
    if (m_backingFd >= 0) close(m_backingFd);
    if (!m_root.empty()) {
      std::error_code ec;
      fs::remove_all(m_root, ec);
    }
  }

  const fs::path& modA() const { return m_modA; }
  const fs::path& modB() const { return m_modB; }
  const fs::path& game() const { return m_game; }
  const fs::path& overwrite() const { return m_overwrite; }
  const fs::path& staging() const { return m_staging; }
  const fs::path& mountPoint() const { return m_mount; }
  // Applied by mount(); set before calling it.
  bool advertiseCaseInsensitive = false;
  Mo2FsContext* context() const { return m_ctx.get(); }
  bool valid() const { return !m_root.empty(); }

  // Registers an existing mod/profile file in the VFS tree.
  void addModFile(const fs::path& real, const std::string& relative)
  {
    struct stat st {};
    ASSERT_EQ(stat(real.c_str(), &st), 0);
    m_tree.root.insertFile(split(relative), real.string(),
                           static_cast<uint64_t>(st.st_size),
                           std::chrono::system_clock::now(), "Mod",
                           /*is_backing=*/false, st.st_mode & 07777);
  }

  // Registers a base-game file; real_path is relative to the game dir fd.
  void addBackingFile(const std::string& relative)
  {
    struct stat st {};
    ASSERT_EQ(stat((m_game / relative).c_str(), &st), 0);
    m_tree.root.insertFile(split(relative), relative,
                           static_cast<uint64_t>(st.st_size),
                           std::chrono::system_clock::now(), "_base_game",
                           /*is_backing=*/true, st.st_mode & 07777);
  }

  bool mount()
  {
    m_tree.recount();
    m_ctx = std::make_shared<Mo2FsContext>();
    m_ctx->tree = std::make_shared<VfsTree>(std::move(m_tree));
    m_ctx->inodes = std::make_unique<InodeTable>();
    m_ctx->overwrite = std::make_unique<OverwriteManager>(m_staging.string(),
                                                          m_overwrite.string());
    m_backingFd = open(m_game.c_str(), O_RDONLY | O_DIRECTORY);
    m_ctx->backing_dir_fd = m_backingFd;
    m_ctx->default_catalog_root = m_overwrite.string();
    m_ctx->catalog_providers = {{m_modA.string(), "ModA"},
                                {m_modB.string(), "ModB"},
                                {m_overwrite.string(), "Overwrite"}};
    m_ctx->uid = ::getuid();
    m_ctx->gid = ::getgid();
    m_ctx->advertise_case_insensitive = advertiseCaseInsensitive;
    mo2PrewarmLookupIndex(m_ctx.get());

    fuse_lowlevel_ops ops {};
    ops.init = mo2_init;
    ops.lookup = mo2_lookup;
    ops.forget = mo2_forget;
    ops.getattr = mo2_getattr;
    ops.opendir = mo2_opendir;
    ops.readdir = mo2_readdir;
    ops.open = mo2_open;
    ops.read = mo2_read;
    ops.write = mo2_write;
    ops.create = mo2_create;
    ops.setattr = mo2_setattr;
    ops.unlink = mo2_unlink;
    ops.mkdir = mo2_mkdir;
    ops.release = mo2_release;
    ops.releasedir = mo2_releasedir;
    ops.flush = mo2_flush;
    ops.access = mo2_access;
    ops.statfs = mo2_statfs;
    ops.ioctl = mo2_ioctl;

    std::string arg0 = "mo2fuse", o = "-o", fsname = "fsname=mo2test",
                maxRead = "max_read=1048576";
    char* argv[] = {arg0.data(), o.data(), fsname.data(), o.data(), maxRead.data()};
    fuse_args args = FUSE_ARGS_INIT(5, argv);
    m_session = fuse_session_new(&args, &ops, sizeof(ops), m_ctx.get());
    if (m_session == nullptr) return false;
    m_ctx->session = m_session;
    if (fuse_session_mount(m_session, m_mount.c_str()) != 0) {
      fuse_session_destroy(m_session);
      m_session = nullptr;
      m_ctx->session = nullptr;
      return false;
    }
    m_mounted = true;
    m_invalidations = std::thread([ctx = m_ctx] { mo2RunKernelInvalidations(ctx.get()); });
    m_loop = std::thread([this] {
      fuse_loop_config* cfg = fuse_loop_cfg_create();
      fuse_loop_cfg_set_max_threads(cfg, 4);
      fuse_session_loop_mt(m_session, cfg);
      fuse_loop_cfg_destroy(cfg);
    });
    return true;
  }

  void unmount()
  {
    if (!m_mounted) return;
    m_mounted = false;
    fuse_session_exit(m_session);
    fuse_session_unmount(m_session);
    if (m_loop.joinable()) m_loop.join();
    mo2StopKernelInvalidations(m_ctx.get());
    if (m_invalidations.joinable()) m_invalidations.join();
    ctxCloseHandle(m_ctx.get());
    fuse_session_destroy(m_session);
    m_session = nullptr;
  }

  bool dirtyContains(const fs::path& providerRoot, const std::string& relative) const
  {
    std::scoped_lock lock(m_ctx->dirty_paths_mutex);
    auto it = m_ctx->dirty_provider_paths.find(providerRoot.string());
    return it != m_ctx->dirty_provider_paths.end() && it->second.count(relative) > 0;
  }

private:
  static std::vector<std::string> split(const std::string& relative)
  {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= relative.size()) {
      const size_t slash = relative.find('/', start);
      out.push_back(relative.substr(
          start, slash == std::string::npos ? std::string::npos : slash - start));
      if (slash == std::string::npos) break;
      start = slash + 1;
    }
    return out;
  }

  fs::path m_root, m_modA, m_modB, m_game, m_overwrite, m_staging, m_mount;
  VfsTree m_tree;
  std::shared_ptr<Mo2FsContext> m_ctx;
  fuse_session* m_session = nullptr;
  std::thread m_loop;
  std::thread m_invalidations;
  int m_backingFd = -1;
  bool m_mounted = false;
};

#define MOUNT_OR_SKIP(vfs)                                                    \
  do {                                                                        \
    ASSERT_TRUE((vfs).valid());                                               \
    if (!(vfs).mount()) GTEST_SKIP() << "FUSE mount unavailable here";        \
  } while (0)

}  // namespace

TEST(VfsInPlaceWrites, TruncateAndRewriteEditsTheModFile)
{
  MountedVfs vfs;
  ASSERT_TRUE(vfs.valid());
  writeText(vfs.modA() / "Meshes/outfit.nif", "old-content");
  vfs.addModFile(vfs.modA() / "Meshes/outfit.nif", "Meshes/outfit.nif");
  MOUNT_OR_SKIP(vfs);

  const fs::path virt = vfs.mountPoint() / "Meshes/outfit.nif";
  ASSERT_EQ(readText(virt), "old-content");
  ASSERT_TRUE(writeFd(virt, O_WRONLY | O_TRUNC, "new"));

  vfs.unmount();
  EXPECT_EQ(readText(vfs.modA() / "Meshes/outfit.nif"), "new");
  EXPECT_EQ(countFiles(vfs.overwrite()), 0u);
  EXPECT_EQ(countFiles(vfs.staging()), 0u);
  EXPECT_TRUE(vfs.dirtyContains(vfs.modA(), "Meshes/outfit.nif"));
}

TEST(VfsInPlaceWrites, PartialWriteWithoutTruncateEditsTheModFile)
{
  MountedVfs vfs;
  ASSERT_TRUE(vfs.valid());
  writeText(vfs.modA() / "SKSE/Plugins/settings.json", "0123456789");
  vfs.addModFile(vfs.modA() / "SKSE/Plugins/settings.json", "SKSE/Plugins/settings.json");
  MOUNT_OR_SKIP(vfs);

  ASSERT_TRUE(writeFd(vfs.mountPoint() / "SKSE/Plugins/settings.json", O_WRONLY, "AB", 3));

  vfs.unmount();
  EXPECT_EQ(readText(vfs.modA() / "SKSE/Plugins/settings.json"), "012AB56789");
  EXPECT_EQ(countFiles(vfs.overwrite()), 0u);
  EXPECT_EQ(countFiles(vfs.staging()), 0u);
}

TEST(VfsInPlaceWrites, PathTruncateEditsTheModFile)
{
  MountedVfs vfs;
  ASSERT_TRUE(vfs.valid());
  writeText(vfs.modA() / "a.ini", "abcdef");
  vfs.addModFile(vfs.modA() / "a.ini", "a.ini");
  MOUNT_OR_SKIP(vfs);

  ASSERT_EQ(truncate((vfs.mountPoint() / "a.ini").c_str(), 2), 0);

  vfs.unmount();
  EXPECT_EQ(readText(vfs.modA() / "a.ini"), "ab");
  EXPECT_EQ(countFiles(vfs.staging()), 0u);
}

TEST(VfsInPlaceWrites, WinningProviderIsTheOneEdited)
{
  MountedVfs vfs;
  ASSERT_TRUE(vfs.valid());
  writeText(vfs.modA() / "cfg/x.json", "from-a");
  writeText(vfs.modB() / "cfg/x.json", "from-b");
  vfs.addModFile(vfs.modA() / "cfg/x.json", "cfg/x.json");
  vfs.addModFile(vfs.modB() / "cfg/x.json", "cfg/x.json");  // higher priority
  MOUNT_OR_SKIP(vfs);

  ASSERT_TRUE(writeFd(vfs.mountPoint() / "cfg/x.json", O_WRONLY | O_TRUNC, "edited"));

  vfs.unmount();
  EXPECT_EQ(readText(vfs.modB() / "cfg/x.json"), "edited");
  EXPECT_EQ(readText(vfs.modA() / "cfg/x.json"), "from-a");
  EXPECT_EQ(countFiles(vfs.overwrite()), 0u);
}

TEST(VfsInPlaceWrites, DifferentCaseLookupStillEditsTheModFile)
{
  MountedVfs vfs;
  ASSERT_TRUE(vfs.valid());
  writeText(vfs.modA() / "Clothes/outfit.json", "old");
  vfs.addModFile(vfs.modA() / "Clothes/outfit.json", "Clothes/outfit.json");
  MOUNT_OR_SKIP(vfs);

  ASSERT_TRUE(writeFd(vfs.mountPoint() / "clothes/OUTFIT.json", O_WRONLY | O_TRUNC, "new"));

  vfs.unmount();
  EXPECT_EQ(readText(vfs.modA() / "Clothes/outfit.json"), "new");
  EXPECT_FALSE(fs::exists(vfs.modA() / "clothes"));
  EXPECT_EQ(countFiles(vfs.overwrite()), 0u);
  EXPECT_EQ(countFiles(vfs.staging()), 0u);
}

TEST(VfsInPlaceWrites, NewFilesStillGoToStaging)
{
  MountedVfs vfs;
  ASSERT_TRUE(vfs.valid());
  writeText(vfs.modA() / "keep.txt", "x");
  vfs.addModFile(vfs.modA() / "keep.txt", "keep.txt");
  MOUNT_OR_SKIP(vfs);

  ASSERT_TRUE(writeFd(vfs.mountPoint() / "brand-new.txt", O_WRONLY | O_CREAT, "hello"));

  vfs.unmount();
  EXPECT_EQ(readText(vfs.staging() / "brand-new.txt"), "hello");
  EXPECT_FALSE(fs::exists(vfs.modA() / "brand-new.txt"));
}

TEST(VfsInPlaceWrites, BaseGameFilesAreCopiedOnWrite)
{
  MountedVfs vfs;
  ASSERT_TRUE(vfs.valid());
  writeText(vfs.game() / "Data/Skyrim.ini", "original");
  vfs.addBackingFile("Data/Skyrim.ini");
  MOUNT_OR_SKIP(vfs);

  ASSERT_TRUE(writeFd(vfs.mountPoint() / "Data/Skyrim.ini", O_WRONLY | O_TRUNC, "changed"));

  vfs.unmount();
  EXPECT_EQ(readText(vfs.game() / "Data/Skyrim.ini"), "original");
  EXPECT_EQ(readText(vfs.staging() / "Data/Skyrim.ini"), "changed");
}

TEST(VfsInPlaceWrites, BaseGamePartialWriteWithoutTruncateIsCopiedOnWrite)
{
  MountedVfs vfs;
  ASSERT_TRUE(vfs.valid());
  writeText(vfs.game() / "Data/Fallout.ini", "0123456789");
  vfs.addBackingFile("Data/Fallout.ini");
  MOUNT_OR_SKIP(vfs);

  // First write on a lazily-COW'd handle: used to invalidate the kernel page
  // cache from inside the write request and deadlock against the folio lock.
  ASSERT_TRUE(writeFd(vfs.mountPoint() / "Data/Fallout.ini", O_WRONLY, "AB", 3));

  vfs.unmount();
  EXPECT_EQ(readText(vfs.game() / "Data/Fallout.ini"), "0123456789");
  EXPECT_EQ(readText(vfs.staging() / "Data/Fallout.ini"), "012AB56789");
}

TEST(VfsInPlaceWrites, ReadOnlyModFileFallsBackToCopyOnWrite)
{
  if (geteuid() == 0) GTEST_SKIP() << "root bypasses file permissions";
  MountedVfs vfs;
  ASSERT_TRUE(vfs.valid());
  writeText(vfs.modA() / "locked.ini", "orig");
  ASSERT_EQ(chmod((vfs.modA() / "locked.ini").c_str(), 0444), 0);
  vfs.addModFile(vfs.modA() / "locked.ini", "locked.ini");
  MOUNT_OR_SKIP(vfs);

  ASSERT_TRUE(writeFd(vfs.mountPoint() / "locked.ini", O_WRONLY | O_TRUNC, "new"));

  vfs.unmount();
  EXPECT_EQ(readText(vfs.modA() / "locked.ini"), "orig");
  EXPECT_EQ(readText(vfs.staging() / "locked.ini"), "new");
}

namespace
{
// FS_IOC_GETFLAGS on an open fd; returns errno (0 on success).
int getFlags(const fs::path& path, int openFlags, unsigned int* flags)
{
  const int fd = open(path.c_str(), openFlags);
  if (fd < 0) return errno;
  int err = 0;
  int value = 0;
  if (ioctl(fd, FS_IOC_GETFLAGS, &value) != 0) {
    err = errno;
  } else {
    *flags = static_cast<unsigned int>(value);
  }
  close(fd);
  return err;
}
}  // namespace

TEST(VfsCaseInsensitiveHint, DirectoriesReportCaseFoldWhenEnabled)
{
  MountedVfs vfs;
  ASSERT_TRUE(vfs.valid());
  writeText(vfs.modA() / "Meshes/outfit.nif", "x");
  vfs.addModFile(vfs.modA() / "Meshes/outfit.nif", "Meshes/outfit.nif");
  vfs.advertiseCaseInsensitive = true;
  MOUNT_OR_SKIP(vfs);

  unsigned int flags = 0;
  ASSERT_EQ(getFlags(vfs.mountPoint() / "Meshes", O_RDONLY | O_DIRECTORY, &flags), 0);
  EXPECT_NE(flags & kMo2CaseFoldFlag, 0u);
  flags = 0;
  ASSERT_EQ(getFlags(vfs.mountPoint(), O_RDONLY | O_DIRECTORY, &flags), 0);
  EXPECT_NE(flags & kMo2CaseFoldFlag, 0u);
  // Regular files are not directories and keep the plain ENOTTY answer.
  EXPECT_EQ(getFlags(vfs.mountPoint() / "Meshes/outfit.nif", O_RDONLY, &flags), ENOTTY);
  vfs.unmount();
}

TEST(VfsCaseInsensitiveHint, DirectoriesStayPlainWhenDisabled)
{
  MountedVfs vfs;
  ASSERT_TRUE(vfs.valid());
  writeText(vfs.modA() / "Meshes/outfit.nif", "x");
  vfs.addModFile(vfs.modA() / "Meshes/outfit.nif", "Meshes/outfit.nif");
  MOUNT_OR_SKIP(vfs);

  unsigned int flags = 0;
  EXPECT_EQ(getFlags(vfs.mountPoint() / "Meshes", O_RDONLY | O_DIRECTORY, &flags), ENOTTY);
  vfs.unmount();
}

TEST(VfsCaseInsensitiveHint, ReplyPolicy)
{
  EXPECT_EQ(mo2FileFlagsReply(true, false, true), kMo2CaseFoldFlag);
  EXPECT_EQ(mo2FileFlagsReply(true, true, true), kMo2CaseFoldFlag);
  EXPECT_FALSE(mo2FileFlagsReply(true, false, false).has_value());
  EXPECT_EQ(mo2FileFlagsReply(true, true, false), 0u);
  EXPECT_FALSE(mo2FileFlagsReply(false, false, true).has_value());
  EXPECT_EQ(mo2FileFlagsReply(false, true, true), 0u);
}

TEST(VfsNoFlush, ReadOnlyHandlesSkipFlushButWritableHandlesKeepIt)
{
  MountedVfs vfs;
  ASSERT_TRUE(vfs.valid());
  writeText(vfs.modA() / "a.ini", "abc");
  vfs.addModFile(vfs.modA() / "a.ini", "a.ini");
  MOUNT_OR_SKIP(vfs);
  const auto flushes = [&] {
    return vfs.context()->flush_count.load(std::memory_order_relaxed);
  };

  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(readText(vfs.mountPoint() / "a.ini"), "abc");
  }
  // close() waits for FLUSH when the kernel sends one, so the count is exact.
  EXPECT_EQ(flushes(), 0u);

  ASSERT_TRUE(writeFd(vfs.mountPoint() / "a.ini", O_WRONLY, "Z"));
  EXPECT_GE(flushes(), 1u);
  vfs.unmount();
  EXPECT_EQ(readText(vfs.modA() / "a.ini"), "Zbc");
}
