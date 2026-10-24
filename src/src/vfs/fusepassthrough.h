#ifndef FLUORINE_VFS_FUSEPASSTHROUGH_H
#define FLUORINE_VFS_FUSEPASSTHROUGH_H

#include <cerrno>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <unistd.h>

// Client for fluorine-fuse-helper (docker/fluorine-fuse-helper.c), the tiny
// root-installed program that holds CAP_SYS_ADMIN so Fluorine itself doesn't
// have to. The kernel only lets such a process attach a backing file to a FUSE
// connection; once attached, reads of that file bypass the userspace daemon.
// See the helper's header comment for the protocol.
namespace fusepassthrough
{

inline constexpr int kProtocolVersion = 1;

// Only files at least this large use passthrough: registering a backing file
// costs a socket round trip, which small files never win back.
inline constexpr uint64_t kDefaultMinFileSize = 4ull * 1024 * 1024;

inline constexpr auto kInstalledHelperPath =
    "/usr/local/libexec/fluorine/fluorine-fuse-helper";

enum class HelperStatus
{
  Ready,
  NotInstalled,
  MissingCapability,  // present, but setcap was not applied (or was cleared)
  Outdated,           // differs from the helper shipped with this Fluorine
};

inline bool readFile(const std::string& path, std::string& out)
{
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}

// `bundledPath` is the helper shipped next to the Fluorine binary. An empty
// path skips the freshness check.
inline HelperStatus helperStatus(const std::string& installedPath,
                                 const std::string& bundledPath)
{
  struct stat st;
  if (stat(installedPath.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
    return HelperStatus::NotInstalled;
  }
  if (!bundledPath.empty()) {
    std::string installed;
    std::string bundled;
    if (readFile(bundledPath, bundled) &&
        (!readFile(installedPath, installed) || installed != bundled)) {
      return HelperStatus::Outdated;
    }
  }
  // File capabilities live in this xattr; a rewrite of the file clears it.
  if (getxattr(installedPath.c_str(), "security.capability", nullptr, 0) <= 0) {
    return HelperStatus::MissingCapability;
  }
  return HelperStatus::Ready;
}

class Helper
{
public:
  Helper() = default;
  Helper(const Helper&) = delete;
  Helper& operator=(const Helper&) = delete;
  ~Helper() { stop(); }

  // Spawns the helper. `fuseFd` is the session's /dev/fuse descriptor; every
  // backing file is registered on that connection.
  bool start(const std::string& helperPath, int fuseFd)
  {
    std::scoped_lock lock(m_mutex);
    if (m_socket >= 0) return true;
    if (access(helperPath.c_str(), X_OK) != 0) return false;

    int pair[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) != 0) {
      return false;
    }

    // fork/exec rather than posix_spawn: <spawn.h> resolves to Fluorine's own
    // spawn.h through the project include path.
    const std::string path = helperPath;
    const pid_t pid        = fork();
    if (pid < 0) {
      ::close(pair[0]);
      ::close(pair[1]);
      return false;
    }
    if (pid == 0) {
      // Child: only async-signal-safe calls until exec. dup2 clears
      // close-on-exec on the target descriptor.
      if (pair[1] == 3) {
        if (fcntl(3, F_SETFD, 0) < 0) _exit(127);
      } else if (dup2(pair[1], 3) < 0) {
        _exit(127);
      }
      char* argv[] = {const_cast<char*>(path.c_str()), nullptr};
      execv(path.c_str(), argv);
      _exit(127);
    }
    ::close(pair[1]);

    m_socket = pair[0];
    m_pid    = pid;
    m_fuseFd = fuseFd;
    return true;
  }

  // Uses an already-connected helper socket (tests, or a helper started by
  // other means). Takes ownership of `socketFd`.
  bool attach(int socketFd, int fuseFd)
  {
    std::scoped_lock lock(m_mutex);
    if (m_socket >= 0) return false;
    m_socket = socketFd;
    m_fuseFd = fuseFd;
    return true;
  }

  bool running() const
  {
    std::scoped_lock lock(m_mutex);
    return m_socket >= 0;
  }

  // Registers `backingFd` (a regular file) with the connection. Returns the
  // backing id (> 0) to put in fuse_file_info::backing_id, or -errno.
  int open(int backingFd)
  {
    std::scoped_lock lock(m_mutex);
    const int fds[] = {m_fuseFd, backingFd};
    return roundTrip(1, 0, fds, 2);
  }

  // Releases an id returned by open(). Call it once the FUSE file that uses
  // the id has been released.
  void close(int backingId)
  {
    if (backingId <= 0) return;
    std::scoped_lock lock(m_mutex);
    const int fds[] = {m_fuseFd};
    roundTrip(2, static_cast<uint32_t>(backingId), fds, 1);
  }

  void stop()
  {
    std::scoped_lock lock(m_mutex);
    shutdownLocked();
  }

private:
  struct Request
  {
    uint32_t op;
    uint32_t backingId;
  };

  int roundTrip(uint32_t op, uint32_t backingId, const int* fds, int fdCount)
  {
    if (m_socket < 0) return -ENODEV;

    Request request{op, backingId};
    struct iovec iov{&request, sizeof(request)};
    union
    {
      char buf[CMSG_SPACE(sizeof(int) * 2)];
      struct cmsghdr align;
    } control{};
    struct msghdr msg{};
    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = control.buf;
    msg.msg_controllen = CMSG_SPACE(sizeof(int) * fdCount);
    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level     = SOL_SOCKET;
    cmsg->cmsg_type      = SCM_RIGHTS;
    cmsg->cmsg_len       = CMSG_LEN(sizeof(int) * fdCount);
    for (int i = 0; i < fdCount; ++i) {
      reinterpret_cast<int*>(CMSG_DATA(cmsg))[i] = fds[i];
    }

    ssize_t n;
    do {
      n = sendmsg(m_socket, &msg, MSG_NOSIGNAL);
    } while (n < 0 && errno == EINTR);
    if (n != static_cast<ssize_t>(sizeof(request))) {
      shutdownLocked();
      return -EPIPE;
    }

    int32_t result = 0;
    do {
      n = recv(m_socket, &result, sizeof(result), 0);
    } while (n < 0 && errno == EINTR);
    if (n != static_cast<ssize_t>(sizeof(result))) {
      shutdownLocked();
      return -EPIPE;
    }
    return result;
  }

  void shutdownLocked()
  {
    if (m_socket >= 0) {
      ::close(m_socket);  // the helper exits on EOF
      m_socket = -1;
    }
    if (m_pid > 0) {
      int status = 0;
      waitpid(m_pid, &status, 0);
      m_pid = -1;
    }
  }

  mutable std::mutex m_mutex;
  int m_socket = -1;
  int m_fuseFd = -1;
  pid_t m_pid  = -1;
};

}  // namespace fusepassthrough

#endif
