#ifndef ROOTPROCESSCOMPLETION_H
#define ROOTPROCESSCOMPLETION_H

#include <QProcess>

#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>

#include <sys/types.h>

namespace process_lifetime
{

inline std::optional<std::uint32_t>
normalizeQProcessExit(int exitCode, QProcess::ExitStatus exitStatus)
{
  if (exitCode < 0) {
    return std::nullopt;
  }
  if (exitStatus == QProcess::CrashExit) {
    // Qt 6.11 on Unix reports the terminating signal (not 128 + signal) in
    // QProcess::finished's exitCode. Zero and values outside the POSIX signal
    // range cannot be represented safely and are rejected.
    if (exitCode == 0 || exitCode >= 128) {
      return std::nullopt;
    }
    return static_cast<std::uint32_t>(128 + exitCode);
  }
  if (exitStatus != QProcess::NormalExit) {
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(exitCode);
}

// QProcess is the sole waitpid owner for processes it starts. This compact
// state transfers its terminal result to lifetime observers without letting a
// second component race QProcess for the child status.
class RootProcessCompletion
{
public:
  enum class State
  {
    Running,
    Exited,
    Failed,
  };

  static constexpr std::uint32_t InvalidExitCode =
      std::numeric_limits<std::uint32_t>::max();

  struct Snapshot
  {
    State state = State::Running;
    std::uint32_t exitCode = InvalidExitCode;
    QProcess::ProcessError lastError = QProcess::UnknownError;
  };

  void finish(int exitCode, QProcess::ExitStatus exitStatus)
  {
    const auto normalized = normalizeQProcessExit(exitCode, exitStatus);
    {
      std::lock_guard lock(m_Mutex);
      if (m_State != State::Running) {
        return;
      }

      // The finished signal is authoritative proof of terminal lifetime even
      // when Qt supplies an unusable crash detail. Preserve an unknown code
      // rather than turning a safely-terminal launch into an endless retry.
      m_State = State::Exited;
      m_ExitCode = normalized.value_or(InvalidExitCode);
    }
  }

  void markStarted()
  {
    std::lock_guard lock(m_Mutex);
    m_Started = true;
  }

  void reportError(QProcess::ProcessError error)
  {
    {
      std::lock_guard lock(m_Mutex);
      m_LastError = error;
      if (error == QProcess::FailedToStart && !m_Started &&
          m_State == State::Running) {
        m_State = State::Failed;
      }
    }
  }

  Snapshot snapshot() const
  {
    std::lock_guard lock(m_Mutex);
    return {m_State, m_ExitCode, m_LastError};
  }

private:
  mutable std::mutex m_Mutex;
  State m_State = State::Running;
  std::uint32_t m_ExitCode = InvalidExitCode;
  QProcess::ProcessError m_LastError = QProcess::UnknownError;
  bool m_Started = false;
};

struct LaunchReceipt
{
  pid_t pid = -1;
  std::uint64_t startTime = std::numeric_limits<std::uint64_t>::max();
  std::shared_ptr<RootProcessCompletion> completion;

  explicit operator bool() const { return pid > 0 && completion != nullptr; }
};

inline void observeQProcessRoot(
    QProcess& process, const std::shared_ptr<RootProcessCompletion>& completion)
{
  if (!completion) {
    return;
  }

  QObject::connect(
      &process, &QProcess::started, &process,
      [completion]() { completion->markStarted(); });
  QObject::connect(
      &process, &QProcess::errorOccurred, &process,
      [completion](QProcess::ProcessError error) {
        completion->reportError(error);
      });
  QObject::connect(
      &process,
      QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &process,
      [completion](int exitCode, QProcess::ExitStatus exitStatus) {
        completion->finish(exitCode, exitStatus);
      });
}

}  // namespace process_lifetime

#endif  // ROOTPROCESSCOMPLETION_H
