#include "unixtermination.h"

#include <array>
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <optional>
#include <poll.h>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

namespace
{
static_assert(std::atomic<int>::is_always_lock_free,
              "termination signal state must be lock-free");

std::atomic<int> g_firstTerminationSignal{0};
std::atomic<int> g_terminationWriteFd{-1};

[[noreturn]] void immediateTermination(int signal) noexcept
{
  // _exit closes the FUSE descriptor and the inherited publication lease. Raw
  // staging is intentionally left for the existing next-start recovery path.
  _exit(128 + signal);
}

void terminationSignalHandler(int signal) noexcept
{
  const int savedErrno = errno;
  int expected         = 0;
  if (!g_firstTerminationSignal.compare_exchange_strong(
          expected, signal, std::memory_order_relaxed,
          std::memory_order_relaxed)) {
    immediateTermination(expected);
  }

  const int fd = g_terminationWriteFd.load(std::memory_order_relaxed);
  if (fd >= 0) {
    const unsigned char byte = static_cast<unsigned char>(signal);
    const ssize_t ignored = ::write(fd, &byte, sizeof(byte));
    (void)ignored;
  }
  errno = savedErrno;
}

void closeFd(int& fd) noexcept
{
  if (fd >= 0) {
    ::close(fd);
    fd = -1;
  }
}
}  // namespace

UnixTerminationBridge::UnixTerminationBridge(std::chrono::milliseconds grace)
    : m_grace(grace)
{
  if (grace <= std::chrono::milliseconds::zero()) {
    throw std::invalid_argument("termination grace period must be positive");
  }
  if (::pipe2(m_notificationPipe, O_NONBLOCK | O_CLOEXEC) != 0 ||
      ::pipe2(m_controlPipe, O_NONBLOCK | O_CLOEXEC) != 0) {
    const int error = errno;
    closeFd(m_notificationPipe[0]);
    closeFd(m_notificationPipe[1]);
    closeFd(m_controlPipe[0]);
    closeFd(m_controlPipe[1]);
    throw std::system_error(error, std::generic_category(),
                            "failed to create termination bridge pipes");
  }

  g_firstTerminationSignal.store(0, std::memory_order_relaxed);
  g_terminationWriteFd.store(m_notificationPipe[1], std::memory_order_relaxed);

  struct sigaction action{};
  action.sa_handler = terminationSignalHandler;
  action.sa_flags   = 0;
  sigemptyset(&action.sa_mask);
  sigaddset(&action.sa_mask, SIGINT);
  sigaddset(&action.sa_mask, SIGTERM);

  if (::sigaction(SIGTERM, &action, &m_previousTerm) != 0) {
    const int error = errno;
    g_terminationWriteFd.store(-1, std::memory_order_relaxed);
    closeFd(m_notificationPipe[0]);
    closeFd(m_notificationPipe[1]);
    closeFd(m_controlPipe[0]);
    closeFd(m_controlPipe[1]);
    throw std::system_error(error, std::generic_category(),
                            "failed to install termination handlers");
  }
  if (::sigaction(SIGINT, &action, &m_previousInterrupt) != 0) {
    const int error = errno;
    (void)::sigaction(SIGTERM, &m_previousTerm, nullptr);
    g_terminationWriteFd.store(-1, std::memory_order_relaxed);
    // SIGTERM may already be executing on another thread. The process exits
    // after this constructor failure, so retain the notification descriptors
    // rather than risk an in-flight handler writing through a reused number.
    closeFd(m_controlPipe[0]);
    closeFd(m_controlPipe[1]);
    throw std::system_error(error, std::generic_category(),
                            "failed to install termination handlers");
  }
  m_installed = true;

  try {
    m_watchdog = std::thread([this] { watchdogLoop(); });
  } catch (...) {
    (void)::sigaction(SIGINT, &m_previousInterrupt, nullptr);
    (void)::sigaction(SIGTERM, &m_previousTerm, nullptr);
    m_installed          = false;
    g_terminationWriteFd.store(-1, std::memory_order_relaxed);
    // Both handlers were live and may already have loaded the descriptor.
    // Retain the notification pipe until this failing process terminates.
    closeFd(m_controlPipe[0]);
    closeFd(m_controlPipe[1]);
    throw;
  }
}

UnixTerminationBridge::~UnixTerminationBridge()
{
  complete();
  // The signal handler may already have loaded the notification descriptor on
  // another thread while complete() restores dispositions. Keep both ends
  // process-lifetime so that in-flight writes cannot target a reused fd.
  closeFd(m_controlPipe[0]);
  closeFd(m_controlPipe[1]);
}

void UnixTerminationBridge::drainNotifications() noexcept
{
  std::array<unsigned char, 32> buffer{};
  while (::read(m_notificationPipe[0], buffer.data(), buffer.size()) > 0) {
  }
}

bool UnixTerminationBridge::requested() const noexcept
{
  return g_firstTerminationSignal.load(std::memory_order_relaxed) != 0;
}

int UnixTerminationBridge::signalNumber() const noexcept
{
  return g_firstTerminationSignal.load(std::memory_order_relaxed);
}

int UnixTerminationBridge::exitCode() const noexcept
{
  const int signal = signalNumber();
  return signal == 0 ? 0 : 128 + signal;
}

void UnixTerminationBridge::complete() noexcept
{
  if (m_completed) {
    return;
  }
  m_completed = true;

  const unsigned char byte = 1;
  ssize_t written;
  do {
    written = ::write(m_controlPipe[1], &byte, sizeof(byte));
  } while (written < 0 && errno == EINTR);
  if (m_watchdog.joinable()) {
    m_watchdog.join();
  }

  if (m_installed) {
    // Stop the watchdog first, then restore dispositions. Notification pipe
    // descriptors remain process-lifetime so an already-entered handler can
    // never write through a reused descriptor.
    (void)::sigaction(SIGINT, &m_previousInterrupt, nullptr);
    (void)::sigaction(SIGTERM, &m_previousTerm, nullptr);
    m_installed = false;
  }
  g_terminationWriteFd.store(-1, std::memory_order_relaxed);
}

void UnixTerminationBridge::watchdogLoop() noexcept
{
  using clock = std::chrono::steady_clock;
  std::optional<clock::time_point> deadline;

  for (;;) {
    int timeout = 100;
    if (requested()) {
      if (!deadline) {
        deadline = clock::now() + m_grace;
      }
      const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
          *deadline - clock::now());
      if (remaining <= std::chrono::milliseconds::zero()) {
        immediateTermination(signalNumber());
      }
      timeout = static_cast<int>(
          std::min<std::int64_t>(100, std::max<std::int64_t>(1, remaining.count())));
    }

    struct pollfd control{};
    control.fd     = m_controlPipe[0];
    control.events = POLLIN;
    const int result = ::poll(&control, 1, timeout);
    if (result > 0 && (control.revents & POLLIN) != 0) {
      return;
    }
    if (result < 0 && errno != EINTR) {
      immediateTermination(requested() ? signalNumber() : SIGTERM);
    }
  }
}
