#ifndef PROCESSLIFETIME_H
#define PROCESSLIFETIME_H

#include <QByteArray>
#include <QFileInfo>
#include <QStringList>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <sys/types.h>

namespace process_lifetime
{

class RootProcessCompletion;

inline constexpr auto LaunchTokenEnvironment = "FLUORINE_LAUNCH_TOKEN";
inline constexpr std::uint64_t UnknownProcessStartTime =
    std::numeric_limits<std::uint64_t>::max();

enum class Result
{
  Completed,
  Error,
  Cancelled,
  ForceUnlocked
};

enum class Control
{
  Continue,
  Cancel,
  ForceUnlock,
  Error
};

enum class IdentityState
{
  Running,
  Exited,
  Unknown
};

// Test seam for controlled procfs reads. Production callers leave this unset.
// A fault without bytesBeforeError fails the operation before any data is
// returned; with bytesBeforeError it simulates a short read followed by the
// supplied error. replacementData models a successful read.
enum class ProcOperation
{
  OpenDirectory,
  StatEntry,
  ReadStat,
  ReadStatus,
  ReadComm,
  ReadCmdline,
  ReadEnvironment
};

struct ProcFault
{
  int error{0};
  std::optional<std::size_t> bytesBeforeError;
  // Deterministic test seam for a successful procfs read. This lets tests
  // model a PID changing generation between two reads without depending on
  // the host's PID allocator. Production callers leave this unset.
  std::optional<QByteArray> replacementData;
};

using ProcFaultInjector =
    std::function<std::optional<ProcFault>(ProcOperation, pid_t)>;

struct Callbacks
{
  std::function<void(pid_t, const QString&)> updateProcess;
  std::function<Control()> control;
  // Test/diagnostic seam invoked immediately before this lifetime observer
  // attempts to own a child status with waitpid(). Managed QProcess roots
  // must never invoke it.
  std::function<void(pid_t)> waitpidAttempted;
  ProcFaultInjector procFaultInjector;
  std::chrono::milliseconds procUnavailableTimeout{
      std::chrono::seconds(2)};
};

using Completion = std::function<void(Result result, std::uint32_t exitCode)>;

QStringList buildExpectedExecutables(const QFileInfo& binary,
                                     const QStringList& arguments,
                                     const QStringList& additional = {});

std::optional<std::uint64_t> processStartTime(pid_t pid);
IdentityState processIdentityState(pid_t pid, std::uint64_t startTime);
bool processIdentityIsAlive(pid_t pid, std::uint64_t startTime);

Result waitForPid(pid_t pid, std::uint32_t* exitCode, const Callbacks& callbacks,
                  const QStringList& expected, bool killTreeOnUnlock,
                  const QString& launchToken = {},
                  bool expectDetachedCompanion = false,
                  std::uint64_t rootStartTime = 0,
                  const std::shared_ptr<RootProcessCompletion>&
                      rootCompletion = {});

Result waitForPidAndNotify(pid_t pid, const Callbacks& callbacks,
                           const QStringList& expected, bool killTreeOnUnlock,
                           const Completion& completion,
                           const QString& launchToken = {},
                           bool expectDetachedCompanion = false,
                           std::uint64_t rootStartTime = 0,
                           const std::shared_ptr<RootProcessCompletion>&
                               rootCompletion = {});

} // namespace process_lifetime

#endif // PROCESSLIFETIME_H
