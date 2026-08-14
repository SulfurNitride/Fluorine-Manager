#ifndef FLUORINE_UNIXTERMINATION_H
#define FLUORINE_UNIXTERMINATION_H

#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>

// Bridges SIGINT/SIGTERM into the Qt event loop without doing lifecycle or
// filesystem work in an asynchronous signal handler. The first signal becomes
// a graceful-shutdown notification. A repeated signal, or expiry of the hard
// deadline, terminates immediately so a wedged event loop/unmount cannot hang
// service shutdown forever.
class UnixTerminationBridge
{
public:
  explicit UnixTerminationBridge(
      std::chrono::milliseconds grace = std::chrono::seconds(30));
  ~UnixTerminationBridge();

  UnixTerminationBridge(const UnixTerminationBridge&)            = delete;
  UnixTerminationBridge& operator=(const UnixTerminationBridge&) = delete;

  int notificationFd() const noexcept { return m_notificationPipe[0]; }
  void drainNotifications() noexcept;

  bool requested() const noexcept;
  int signalNumber() const noexcept;
  int exitCode() const noexcept;

  // Call only after MOApplication and all of its cleanup-owning members have
  // been destroyed. This is deliberately later than QCoreApplication::aboutToQuit.
  void complete() noexcept;

private:
  void watchdogLoop() noexcept;

  int m_notificationPipe[2]{-1, -1};
  int m_controlPipe[2]{-1, -1};
  std::chrono::milliseconds m_grace;
  std::thread m_watchdog;
  bool m_installed{false};
  bool m_completed{false};

  struct sigaction m_previousTerm{};
  struct sigaction m_previousInterrupt{};
};

#endif
