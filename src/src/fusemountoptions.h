#ifndef FUSEMOUNTOPTIONS_H
#define FUSEMOUNTOPTIONS_H

#include <QByteArray>
#include <QFile>
#include <algorithm>
#include <string>
#include <vector>

inline constexpr auto kFuseAllowOtherSetting = "fluorine/fuse_allow_other";
// Per-instance. On by default: tells Wine the mount is case-insensitive so it
// skips its userspace directory scan after every failed exact-case lookup.
inline constexpr auto kFuseCaseInsensitiveHintSetting = "fluorine/fuse_case_insensitive_hint";
// Per-instance. Off by default: needs the fuse.enable_uring kernel parameter.
inline constexpr auto kFuseIoUringSetting = "fluorine/fuse_io_uring";
// Per-instance. Off by default: needs the privileged helper installed once
// (Settings > Proton/Wine). Large read-only files are then read by the kernel
// straight from the real file instead of through this process.
inline constexpr auto kFusePassthroughSetting = "fluorine/fuse_passthrough";
inline constexpr auto kFuseIoUringKernelParameter =
    "/sys/module/fuse/parameters/enable_uring";

// The kernel only honours "-o io_uring" when fuse.enable_uring is Y; otherwise
// libfuse silently keeps the classic /dev/fuse path.
inline bool fuseIoUringKernelEnabled(const QString& parameterPath =
                                         QString::fromLatin1(kFuseIoUringKernelParameter))
{
  QFile file(parameterPath);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }
  const QByteArray value = file.readAll().trimmed().toUpper();
  return value == "Y" || value == "1";
}

inline bool fuseUserAllowOtherEnabled(const QByteArray& config)
{
  for (const auto& line : config.split('\n')) {
    if (line.split('#').first().trimmed() == "user_allow_other") {
      return true;
    }
  }
  return false;
}

// Worker threads for the FUSE request loop: one per hardware thread, with a
// floor so a few reads blocked on disk can't starve metadata requests on
// small CPUs. hardware_concurrency() may report 0 when it can't tell.
inline int fuseWorkerThreadCount(unsigned hardwareThreads)
{
  constexpr int kMinWorkers = 4;
  return std::max(kMinWorkers, static_cast<int>(hardwareThreads));
}

inline std::vector<std::string> fuseMountArguments(bool allowOther, bool ioUring = false)
{
  std::vector<std::string> args = {
      "mo2fuse", "-o", "fsname=mo2linux", "-o", "noatime",
      "-o", "max_read=1048576"};
  if (ioUring) {
    args.insert(args.end(), {"-o", "io_uring"});
  }
  if (allowOther) {
    // The filesystem serves requests as the mounting user. Have the kernel
    // enforce inode ownership/modes before allowing access by other users.
    args.insert(args.end(), {"-o", "allow_other", "-o", "default_permissions"});
  }
  return args;
}

#endif
