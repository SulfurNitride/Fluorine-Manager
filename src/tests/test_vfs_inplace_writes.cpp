// Mounts the real mo2_* FUSE handlers and checks where writes to existing files
// land. Mirrors MO2/USVFS: existing mod/profile files are edited in place, only
// new files go to staging/overwrite, and base-game files stay untouched (COW).

#include "vfs/mo2filesystem.h"

#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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
  // Path of a fluorine-fuse-helper to enable kernel passthrough with; files of
  // at least passthroughMinSize use it.
  std::string passthroughHelper;
  uint64_t passthroughMinSize = 1;
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
    if (!passthroughHelper.empty()) {
      m_ctx->passthrough = std::make_shared<fusepassthrough::Helper>();
      m_ctx->passthrough_helper_path = passthroughHelper;
      m_ctx->passthrough_min_size    = passthroughMinSize;
    }
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

// ---------------------------------------------------------------------------
// Kernel passthrough
// ---------------------------------------------------------------------------

TEST(FusePassthroughHelper, StatusReflectsInstallState)
{
  char tmpl[] = "/tmp/fluorine-passthrough-status-XXXXXX";
  ASSERT_NE(mkdtemp(tmpl), nullptr);
  const fs::path dir = tmpl;
  const std::string missing = (dir / "missing").string();
  const std::string installed = (dir / "installed").string();
  const std::string bundled   = (dir / "bundled").string();

  using fusepassthrough::HelperStatus;
  EXPECT_EQ(fusepassthrough::helperStatus(missing, ""), HelperStatus::NotInstalled);

  writeText(installed, "helper v1");
  // A plain file never carries the capability xattr.
  EXPECT_EQ(fusepassthrough::helperStatus(installed, ""),
            HelperStatus::MissingCapability);

  writeText(bundled, "helper v2");
  EXPECT_EQ(fusepassthrough::helperStatus(installed, bundled),
            HelperStatus::Outdated);

  std::error_code ec;
  fs::remove_all(dir, ec);
}

TEST(FusePassthroughHelper, ClientSpeaksTheHelperProtocol)
{
  int pair[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair), 0);

  struct Seen
  {
    uint32_t op = 0;
    uint32_t backingId = 0;
    int fdCount = 0;
  };
  std::vector<Seen> seen;
  std::thread fake([&, sock = pair[1]] {
    int nextId = 7;
    for (;;) {
      uint32_t request[2] = {};
      struct iovec iov{request, sizeof(request)};
      char control[CMSG_SPACE(sizeof(int) * 2)];
      struct msghdr msg{};
      msg.msg_iov        = &iov;
      msg.msg_iovlen     = 1;
      msg.msg_control    = control;
      msg.msg_controllen = sizeof(control);
      const ssize_t n = recvmsg(sock, &msg, 0);
      if (n != static_cast<ssize_t>(sizeof(request))) break;  // EOF
      Seen entry;
      entry.op = request[0];
      entry.backingId = request[1];
      for (cmsghdr* c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c)) {
        if (c->cmsg_type != SCM_RIGHTS) continue;
        const int count = static_cast<int>((c->cmsg_len - CMSG_LEN(0)) / sizeof(int));
        for (int i = 0; i < count; ++i) close(reinterpret_cast<int*>(CMSG_DATA(c))[i]);
        entry.fdCount = count;
      }
      seen.push_back(entry);
      const int32_t result = entry.op == 1 ? nextId++ : (entry.backingId == 7 ? 0 : -EINVAL);
      send(sock, &result, sizeof(result), 0);
    }
    close(sock);
  });

  {
    fusepassthrough::Helper helper;
    int devnull = ::open("/dev/null", O_RDONLY);
    int backing = ::open("/dev/null", O_RDONLY);
    ASSERT_TRUE(helper.attach(pair[0], devnull));
    EXPECT_EQ(helper.open(backing), 7);
    EXPECT_EQ(helper.open(backing), 8);
    helper.close(7);
    helper.stop();
    EXPECT_FALSE(helper.running());
    EXPECT_EQ(helper.open(backing), -ENODEV);
    ::close(devnull);
    ::close(backing);
  }
  fake.join();

  ASSERT_EQ(seen.size(), 3u);
  EXPECT_EQ(seen[0].op, 1u);
  EXPECT_EQ(seen[0].fdCount, 2);
  EXPECT_EQ(seen[2].op, 2u);
  EXPECT_EQ(seen[2].backingId, 7u);
  EXPECT_EQ(seen[2].fdCount, 1);
}

TEST(VfsPassthrough, MissingHelperFallsBackToNormalReads)
{
  MountedVfs vfs;
  ASSERT_TRUE(vfs.valid());
  vfs.passthroughHelper = "/nonexistent/fluorine-fuse-helper";
  writeText(vfs.modA() / "big.bin", std::string(64 * 1024, 'x'));
  vfs.addModFile(vfs.modA() / "big.bin", "big.bin");
  MOUNT_OR_SKIP(vfs);

  EXPECT_EQ(readText(vfs.mountPoint() / "big.bin").size(), 64u * 1024);
  EXPECT_FALSE(vfs.context()->passthrough_active.load());
  EXPECT_EQ(vfs.context()->passthrough_opens.load(), 0u);
  vfs.unmount();
}

// Needs a helper that holds CAP_SYS_ADMIN (installed through Settings, or built
// and `setcap`ed by hand) and a kernel with FUSE passthrough:
//   FLUORINE_TEST_PASSTHROUGH_HELPER=/path/to/fluorine-fuse-helper
TEST(VfsPassthrough, LargeReadOnlyFilesBypassUserspace)
{
  const char* helper = std::getenv("FLUORINE_TEST_PASSTHROUGH_HELPER");
  if (helper == nullptr) GTEST_SKIP() << "FLUORINE_TEST_PASSTHROUGH_HELPER not set";

  MountedVfs vfs;
  ASSERT_TRUE(vfs.valid());
  vfs.passthroughHelper  = helper;
  vfs.passthroughMinSize = 1024 * 1024;

  std::string big(6 * 1024 * 1024, '\0');
  for (size_t i = 0; i < big.size(); ++i) big[i] = static_cast<char>((i * 31 + i / 4096) & 0xff);
  writeText(vfs.modA() / "big.bin", big);
  writeText(vfs.modA() / "small.txt", "tiny");
  vfs.addModFile(vfs.modA() / "big.bin", "big.bin");
  vfs.addModFile(vfs.modA() / "small.txt", "small.txt");
  MOUNT_OR_SKIP(vfs);
  // Capabilities are negotiated when the first request is served.
  struct stat rootStat {};
  ASSERT_EQ(stat(vfs.mountPoint().c_str(), &rootStat), 0);
  if (!vfs.context()->passthrough_active.load()) {
    GTEST_SKIP() << "kernel or helper did not enable passthrough";
  }

  const auto reads = [&] { return vfs.context()->read_count.load(); };
  EXPECT_EQ(readText(vfs.mountPoint() / "small.txt"), "tiny");
  const uint64_t readsAfterSmall = reads();
  EXPECT_GE(readsAfterSmall, 1u);  // small files still go through userspace

  EXPECT_EQ(readText(vfs.mountPoint() / "big.bin"), big);
  EXPECT_EQ(vfs.context()->passthrough_opens.load(), 1u);
  EXPECT_EQ(reads(), readsAfterSmall) << "reads of a passthrough file reached userspace";

  // A write open of the same file must not use passthrough.
  ASSERT_TRUE(writeFd(vfs.mountPoint() / "big.bin", O_WRONLY, "Z"));
  EXPECT_EQ(vfs.context()->passthrough_opens.load(), 1u);
  vfs.unmount();
  EXPECT_EQ(readText(vfs.modA() / "big.bin").substr(0, 1), "Z");
}

// How Wine loads DLLs and maps data: private (copy-on-write) and executable
// mappings, shared read mappings, and several handles reading one file at once.
TEST(VfsPassthrough, MappingsAndConcurrentReadsMatchTheRealFile)
{
  const char* helper = std::getenv("FLUORINE_TEST_PASSTHROUGH_HELPER");
  if (helper == nullptr) GTEST_SKIP() << "FLUORINE_TEST_PASSTHROUGH_HELPER not set";

  MountedVfs vfs;
  ASSERT_TRUE(vfs.valid());
  vfs.passthroughHelper  = helper;
  vfs.passthroughMinSize = 1024 * 1024;

  const size_t size = 6 * 1024 * 1024;
  std::string data(size, '\0');
  for (size_t i = 0; i < size; ++i) data[i] = static_cast<char>((i * 131 + i / 997) & 0xff);
  writeText(vfs.modA() / "image.dll", data);
  vfs.addModFile(vfs.modA() / "image.dll", "image.dll");
  MOUNT_OR_SKIP(vfs);
  struct stat rootStat {};
  ASSERT_EQ(stat(vfs.mountPoint().c_str(), &rootStat), 0);
  if (!vfs.context()->passthrough_active.load()) {
    GTEST_SKIP() << "kernel or helper did not enable passthrough";
  }

  const std::string path = (vfs.mountPoint() / "image.dll").string();
  const int fd = ::open(path.c_str(), O_RDONLY);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(vfs.context()->passthrough_opens.load(), 1u);

  // Read-only private mapping, as for a data file.
  void* ro = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
  ASSERT_NE(ro, MAP_FAILED) << "private read mapping: " << strerror(errno);
  EXPECT_EQ(std::memcmp(ro, data.data(), size), 0);

  // Executable private mapping, as the PE loader maps a DLL image.
  void* exec = mmap(nullptr, size, PROT_READ | PROT_EXEC, MAP_PRIVATE, fd, 0);
  ASSERT_NE(exec, MAP_FAILED) << "executable mapping: " << strerror(errno);
  EXPECT_EQ(std::memcmp(exec, data.data(), size), 0);

  // Writable private mapping (relocations): writes stay in memory.
  void* cow = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
  ASSERT_NE(cow, MAP_FAILED) << "copy-on-write mapping: " << strerror(errno);
  static_cast<char*>(cow)[4096] ^= 0x5a;
  EXPECT_NE(static_cast<char*>(cow)[4096], data[4096]);
  EXPECT_EQ(std::memcmp(ro, data.data(), size), 0) << "private write leaked";

  // Shared read mapping.
  void* shared = mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
  ASSERT_NE(shared, MAP_FAILED) << "shared read mapping: " << strerror(errno);
  EXPECT_EQ(std::memcmp(shared, data.data(), size), 0);

  // Several handles, each reading the whole file in odd-sized chunks at once.
  std::mutex failureMutex;
  std::vector<std::string> failures;
  const auto fail = [&](const std::string& what) {
    std::scoped_lock lock(failureMutex);
    failures.push_back(what);
  };
  std::vector<std::thread> readers;
  for (int t = 0; t < 4; ++t) {
    readers.emplace_back([&, t] {
      const int h = ::open(path.c_str(), O_RDONLY);
      if (h < 0) {
        fail("reader " + std::to_string(t) + ": open failed: " + strerror(errno));
        return;
      }
      std::string chunk(7919 + t * 1000, '\0');
      for (size_t off = 0; off < size; off += chunk.size()) {
        const size_t want = std::min(chunk.size(), size - off);
        const ssize_t n = pread(h, chunk.data(), chunk.size(), static_cast<off_t>(off));
        if (n != static_cast<ssize_t>(want)) {
          fail("reader " + std::to_string(t) + ": pread at " + std::to_string(off) +
               " returned " + std::to_string(n) + ", wanted " + std::to_string(want) +
               " (errno " + std::to_string(errno) + ")");
          break;
        }
        if (std::memcmp(chunk.data(), data.data() + off, want) != 0) {
          fail("reader " + std::to_string(t) + ": data differs at " + std::to_string(off));
          break;
        }
      }
      ::close(h);
    });
  }
  for (auto& r : readers) r.join();
  for (const auto& f : failures) ADD_FAILURE() << f;

  munmap(ro, size);
  munmap(exec, size);
  munmap(cow, size);
  munmap(shared, size);
  ::close(fd);
  vfs.unmount();
}

// The kernel allows one backing file per inode and refuses to mix passthrough
// and normal opens of it, so opens of one file must share a backing id and a
// writer must not poison later readers.
TEST(VfsPassthrough, OpensOfOneFileShareABackingIdAndWritersStayNormal)
{
  const char* helper = std::getenv("FLUORINE_TEST_PASSTHROUGH_HELPER");
  if (helper == nullptr) GTEST_SKIP() << "FLUORINE_TEST_PASSTHROUGH_HELPER not set";

  MountedVfs vfs;
  ASSERT_TRUE(vfs.valid());
  vfs.passthroughHelper  = helper;
  vfs.passthroughMinSize = 1024 * 1024;
  const std::string data(3 * 1024 * 1024, 'q');
  writeText(vfs.modA() / "Big.esp", data);
  vfs.addModFile(vfs.modA() / "Big.esp", "Big.esp");
  MOUNT_OR_SKIP(vfs);
  struct stat rootStat {};
  ASSERT_EQ(stat(vfs.mountPoint().c_str(), &rootStat), 0);
  if (!vfs.context()->passthrough_active.load()) {
    GTEST_SKIP() << "kernel or helper did not enable passthrough";
  }
  const std::string path = (vfs.mountPoint() / "Big.esp").string();
  const auto registrations = [&] { return vfs.context()->passthrough_opens.load(); };

  // Games open the same plugin several times (wineserver, then the process).
  const int a = ::open(path.c_str(), O_RDONLY);
  const int b = ::open(path.c_str(), O_RDONLY);
  const int c = ::open(path.c_str(), O_RDONLY);
  ASSERT_GE(a, 0) << strerror(errno);
  ASSERT_GE(b, 0) << "second open: " << strerror(errno);
  ASSERT_GE(c, 0) << "third open: " << strerror(errno);
  EXPECT_EQ(registrations(), 1u) << "opens must share one backing id";
  std::string buf(4096, '\0');
  EXPECT_EQ(pread(b, buf.data(), buf.size(), 1000), 4096);
  EXPECT_EQ(buf, data.substr(1000, 4096));
  ::close(a);
  // Still readable after the first handle closes: the shared id stays alive.
  EXPECT_EQ(pread(c, buf.data(), buf.size(), 2000), 4096);
  EXPECT_EQ(buf, data.substr(2000, 4096));
  ::close(b);
  ::close(c);

  // After every passthrough handle is gone a writer can open the file...
  {
    const int w = ::open(path.c_str(), O_RDWR);
    ASSERT_GE(w, 0) << "writer open after readers closed: " << strerror(errno);
    // ...and while it is open, readers fall back to normal reads.
    const uint64_t before = registrations();
    const int r = ::open(path.c_str(), O_RDONLY);
    ASSERT_GE(r, 0) << "reader open beside a writer: " << strerror(errno);
    EXPECT_EQ(registrations(), before) << "reader must not use passthrough beside a writer";
    EXPECT_EQ(pread(r, buf.data(), buf.size(), 0), 4096);
    EXPECT_EQ(buf, data.substr(0, 4096));
    ::close(r);
    ::close(w);
  }

  // Once the writer is gone, passthrough is available again. The kernel sends
  // RELEASE asynchronously after close(), so give it a moment to arrive.
  const uint64_t before = registrations();
  for (int attempt = 0; attempt < 100 && registrations() == before; ++attempt) {
    const int again = ::open(path.c_str(), O_RDONLY);
    ASSERT_GE(again, 0) << strerror(errno);
    ::close(again);
    if (registrations() == before) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
  EXPECT_EQ(registrations(), before + 1)
      << "passthrough never became available again after the writer closed";
  vfs.unmount();
}
