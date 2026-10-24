/*
 * fluorine-fuse-helper: registers FUSE passthrough backing files.
 *
 * The kernel only lets a process with CAP_SYS_ADMIN (in the initial user
 * namespace) attach a backing file to a FUSE connection. Fluorine itself must
 * not run with that capability, so a root-owned copy of this tiny program is
 * given it with `setcap cap_sys_admin+ep` and spawned once per mount. It does
 * exactly two things for the Fluorine process that spawned it:
 *
 *   open  : register an already-open regular file with the FUSE connection
 *           and return the backing id
 *   close : release a backing id previously returned by `open`
 *
 * Protocol: SOCK_SEQPACKET on fd 3. Each request is a struct request with the
 * file descriptors it needs attached as SCM_RIGHTS; each reply is one int32
 * (a backing id > 0, 0 for a successful close, or -errno).
 *
 * The helper never opens paths and never touches a descriptor it was not
 * handed, so it cannot reach anything the caller could not already open. It
 * only accepts a /dev/fuse descriptor and a regular file, and exits when the
 * caller closes the socket.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#define HELPER_PROTOCOL_VERSION 1
#define HELPER_SOCKET_FD 3

#define OP_OPEN 1
#define OP_CLOSE 2

/* Older kernel headers (the build image) predate these definitions. */
#define FUSE_DEV_IOC_MAGIC_ 229
struct backing_map {
  uint32_t fd;
  uint32_t flags;
  uint64_t padding;
};
#define FUSE_DEV_IOC_BACKING_OPEN_ _IOW(FUSE_DEV_IOC_MAGIC_, 1, struct backing_map)
#define FUSE_DEV_IOC_BACKING_CLOSE_ _IOW(FUSE_DEV_IOC_MAGIC_, 2, uint32_t)

struct request {
  uint32_t op;
  uint32_t backing_id; /* OP_CLOSE only */
};

#define MAX_FDS 2

static int is_fuse_device(int fd)
{
  struct stat st;
  /* /dev/fuse is the misc device 10:229. */
  return fstat(fd, &st) == 0 && S_ISCHR(st.st_mode) &&
         major(st.st_rdev) == 10 && minor(st.st_rdev) == 229;
}

static int is_regular_file(int fd)
{
  struct stat st;
  return fstat(fd, &st) == 0 && S_ISREG(st.st_mode);
}

static void close_all(int* fds, int n)
{
  for (int i = 0; i < n; ++i) {
    if (fds[i] >= 0) close(fds[i]);
  }
}

/* Returns the number of descriptors received, or -1 on EOF/error. */
static int receive(struct request* req, int* fds)
{
  union {
    char buf[CMSG_SPACE(sizeof(int) * MAX_FDS)];
    struct cmsghdr align;
  } control;
  struct iovec iov = {.iov_base = req, .iov_len = sizeof(*req)};
  struct msghdr msg = {
      .msg_iov = &iov,
      .msg_iovlen = 1,
      .msg_control = control.buf,
      .msg_controllen = sizeof(control.buf),
  };

  for (int i = 0; i < MAX_FDS; ++i) fds[i] = -1;

  ssize_t n;
  do {
    n = recvmsg(HELPER_SOCKET_FD, &msg, MSG_CMSG_CLOEXEC);
  } while (n < 0 && errno == EINTR);
  if (n != (ssize_t)sizeof(*req) || (msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC))) {
    if (n > 0) {
      /* Malformed request: drop whatever descriptors came with it. */
      for (struct cmsghdr* c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c)) {
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
          int count = (int)((c->cmsg_len - CMSG_LEN(0)) / sizeof(int));
          int* data = (int*)CMSG_DATA(c);
          for (int i = 0; i < count; ++i) close(data[i]);
        }
      }
      return -2;
    }
    return -1;
  }

  int count = 0;
  for (struct cmsghdr* c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c)) {
    if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS) continue;
    int n_fds = (int)((c->cmsg_len - CMSG_LEN(0)) / sizeof(int));
    int* data = (int*)CMSG_DATA(c);
    for (int i = 0; i < n_fds; ++i) {
      if (count < MAX_FDS) {
        fds[count++] = data[i];
      } else {
        close(data[i]);
      }
    }
  }
  return count;
}

static int reply(int32_t value)
{
  ssize_t n;
  do {
    n = send(HELPER_SOCKET_FD, &value, sizeof(value), MSG_NOSIGNAL);
  } while (n < 0 && errno == EINTR);
  return n == (ssize_t)sizeof(value) ? 0 : -1;
}

static int32_t handle_open(const int* fds, int count)
{
  if (count != 2) return -EINVAL;
  if (!is_fuse_device(fds[0])) return -ENOTTY;
  if (!is_regular_file(fds[1])) return -EBADF;

  struct backing_map map = {.fd = (uint32_t)fds[1], .flags = 0, .padding = 0};
  int id = ioctl(fds[0], FUSE_DEV_IOC_BACKING_OPEN_, &map);
  return id < 0 ? -errno : id;
}

static int32_t handle_close(const struct request* req, const int* fds, int count)
{
  if (count != 1 || req->backing_id == 0) return -EINVAL;
  if (!is_fuse_device(fds[0])) return -ENOTTY;

  uint32_t id = req->backing_id;
  return ioctl(fds[0], FUSE_DEV_IOC_BACKING_CLOSE_, &id) < 0 ? -errno : 0;
}

int main(int argc, char** argv)
{
  if (argc == 2 && strcmp(argv[1], "--version") == 0) {
    printf("fluorine-fuse-helper %d\n", HELPER_PROTOCOL_VERSION);
    return 0;
  }

  /* No other process may attach to this one, and the helper must not gain
   * anything through exec; it never execs. It exits on socket EOF, so a
   * crashed Fluorine cannot leave a privileged process behind. */
  prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
  prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);

  struct stat st;
  if (fstat(HELPER_SOCKET_FD, &st) != 0 || !S_ISSOCK(st.st_mode)) {
    fprintf(stderr, "fluorine-fuse-helper: expected a socket on fd %d\n",
            HELPER_SOCKET_FD);
    return 2;
  }

  for (;;) {
    struct request req;
    int fds[MAX_FDS];
    int count = receive(&req, fds);
    if (count == -1) return 0; /* caller closed the socket */
    if (count == -2) {
      if (reply(-EPROTO) != 0) return 0;
      continue;
    }

    int32_t result;
    switch (req.op) {
    case OP_OPEN:
      result = handle_open(fds, count);
      break;
    case OP_CLOSE:
      result = handle_close(&req, fds, count);
      break;
    default:
      result = -ENOSYS;
      break;
    }
    close_all(fds, MAX_FDS);
    if (reply(result) != 0) return 0;
  }
}
