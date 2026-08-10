#include "processlifetime.h"
#include "rootprocesscompletion.h"

#include <uibase/log.h>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QProcess>
#include <QThread>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <deque>
#include <dirent.h>
#include <fcntl.h>
#include <optional>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace process_lifetime
{

namespace log = MOBase::log;

struct ProcStat
{
  char state;
  pid_t parentPid;
  std::uint64_t startTime;
};

enum class ProcState
{
  Available,
  Missing,
  Unavailable
};

template <class T> struct ProcResult
{
  ProcState state{ProcState::Unavailable};
  T value{};
};

struct ProcContext
{
  const ProcFaultInjector* faultInjector{nullptr};
};

bool isMissingProcError(int error)
{
  return error == ENOENT || error == ESRCH;
}

std::optional<ProcFault> injectedFault(const ProcContext& context,
                                       ProcOperation operation, pid_t pid)
{
  if (context.faultInjector == nullptr || !*context.faultInjector) {
    return std::nullopt;
  }
  return (*context.faultInjector)(operation, pid);
}

template <class T> ProcResult<T> failedProcResult(int error)
{
  return {isMissingProcError(error) ? ProcState::Missing
                                    : ProcState::Unavailable,
          {}};
}

ProcResult<QByteArray> readProcFile(const ProcContext& context, pid_t pid,
                                    ProcOperation operation,
                                    const char* fileName)
{
  if (const auto fault = injectedFault(context, operation, pid)) {
    if (fault->replacementData) {
      return {ProcState::Available, *fault->replacementData};
    }
    // bytesBeforeError represents a read that returned some data and then
    // failed. No prefix is safe to use for an identity or matching decision.
    if (fault->bytesBeforeError) {
      return {ProcState::Unavailable, {}};
    }
    return failedProcResult<QByteArray>(fault->error == 0 ? EIO
                                                          : fault->error);
  }

  const QByteArray path =
      QFile::encodeName(QStringLiteral("/proc/%1/%2").arg(pid).arg(fileName));
  const int fd = ::open(path.constData(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return failedProcResult<QByteArray>(errno);
  }

  QByteArray data;
  char buffer[4096];
  while (true) {
    const ssize_t count = ::read(fd, buffer, sizeof(buffer));
    if (count > 0) {
      data.append(buffer, static_cast<qsizetype>(count));
      continue;
    }
    if (count == 0) {
      ::close(fd);
      return {ProcState::Available, std::move(data)};
    }
    if (errno == EINTR) {
      continue;
    }
    ::close(fd);
    return {ProcState::Unavailable, {}};
  }
}

ProcResult<ProcStat> readProcStat(const ProcContext& context, pid_t pid)
{
  const auto result =
      readProcFile(context, pid, ProcOperation::ReadStat, "stat");
  if (result.state != ProcState::Available) {
    return {result.state, {}};
  }

  const QByteArray& stat = result.value;
  const qsizetype commandEnd = stat.lastIndexOf(')');
  if (commandEnd < 0 || commandEnd + 2 >= stat.size()) {
    return {ProcState::Unavailable, {}};
  }

  // Fields after the command begin with state (field 3). PPid is field 4 and
  // starttime is field 22, therefore indices 1 and 19 in this suffix. Reading
  // both from stat gives one coherent process-generation/parent snapshot;
  // combining status with a later stat read can join two PID generations.
  const QList<QByteArray> fields = stat.mid(commandEnd + 2).split(' ');
  if (fields.size() <= 19 || fields[0].size() != 1) {
    return {ProcState::Unavailable, {}};
  }

  bool parentOk = false;
  const qlonglong parsedParent = fields[1].toLongLong(&parentOk);
  bool startOk = false;
  const auto startTime = fields[19].toULongLong(&startOk);
  if (!parentOk || parsedParent < 0 ||
      parsedParent > std::numeric_limits<pid_t>::max() || !startOk) {
    return {ProcState::Unavailable, {}};
  }
  return {ProcState::Available,
          ProcStat{fields[0].at(0), static_cast<pid_t>(parsedParent),
                   startTime}};
}

ProcState processExistence(pid_t pid)
{
  if (pid <= 0) {
    return ProcState::Missing;
  }
  if (::kill(pid, 0) == 0) {
    return ProcState::Available;
  }
  if (errno == ESRCH) {
    return ProcState::Missing;
  }
  return errno == EPERM ? ProcState::Available : ProcState::Unavailable;
}

ProcResult<QString> readProcComm(const ProcContext& context, pid_t pid)
{
  const auto result =
      readProcFile(context, pid, ProcOperation::ReadComm, "comm");
  if (result.state != ProcState::Available) {
    return {result.state, {}};
  }
  const QString comm = QString::fromUtf8(result.value).trimmed();
  return comm.isEmpty() ? ProcResult<QString>{ProcState::Unavailable, {}}
                        : ProcResult<QString>{ProcState::Available, comm};
}

IdentityState processIdentityState(const ProcContext& context, pid_t pid,
                                   std::uint64_t startTime)
{
  const auto existence = processExistence(pid);
  if (existence == ProcState::Missing) {
    return IdentityState::Exited;
  }
  if (existence == ProcState::Unavailable) {
    return IdentityState::Unknown;
  }

  const auto stat = readProcStat(context, pid);
  if (stat.state == ProcState::Missing) {
    // Resolve the ordinary exit race between kill(0) and opening stat. If the
    // PID still exists, its identity is uncertain rather than exited.
    return processExistence(pid) == ProcState::Missing ? IdentityState::Exited
                                                        : IdentityState::Unknown;
  }
  if (stat.state == ProcState::Unavailable) {
    return IdentityState::Unknown;
  }
  if (stat.value.state == 'Z' || stat.value.state == 'X') {
    return IdentityState::Exited;
  }
  if (startTime == UnknownProcessStartTime) {
    return IdentityState::Unknown;
  }
  if (startTime == 0) {
    return IdentityState::Running;
  }
  return stat.value.startTime == startTime ? IdentityState::Running
                                           : IdentityState::Exited;
}

ProcState processAliveState(const ProcContext& context, pid_t pid)
{
  switch (processIdentityState(context, pid, 0)) {
  case IdentityState::Running:
    return ProcState::Available;
  case IdentityState::Exited:
    return ProcState::Missing;
  case IdentityState::Unknown:
    return ProcState::Unavailable;
  }
  return ProcState::Unavailable;
}

std::optional<std::uint64_t> processStartTime(pid_t pid)
{
  const auto stat = readProcStat({}, pid);
  return stat.state == ProcState::Available
             ? std::optional<std::uint64_t>(stat.value.startTime)
             : std::nullopt;
}

IdentityState processIdentityState(pid_t pid, std::uint64_t startTime)
{
  return processIdentityState({}, pid, startTime);
}

bool processIdentityIsAlive(pid_t pid, std::uint64_t startTime)
{
  return processIdentityState(pid, startTime) == IdentityState::Running;
}

// Read /proc/<pid>/cmdline (NUL-separated) and return all argv entries.
ProcResult<QStringList> readProcCmdline(const ProcContext& context, pid_t pid)
{
  const auto result =
      readProcFile(context, pid, ProcOperation::ReadCmdline, "cmdline");
  if (result.state != ProcState::Available) {
    return {result.state, {}};
  }

  QStringList parts;
  for (const QByteArray& part : result.value.split('\0')) {
    if (!part.isEmpty()) {
      parts.push_back(QString::fromUtf8(part));
    }
  }
  return {ProcState::Available, std::move(parts)};
}

enum class ProcEnvState
{
  Found,
  Missing,
  Exited,
  Unreadable
};

struct ProcEnvValue
{
  ProcEnvState state;
  QString value;
};

// Read a specific environment variable from /proc/<pid>/environ while
// preserving the distinction between a missing value and an unreadable proc
// entry. Permission/transient read failures must never be interpreted as
// process exit.
ProcEnvValue readProcEnv(const ProcContext& context, pid_t pid,
                         const char* varName)
{
  const auto result = readProcFile(context, pid, ProcOperation::ReadEnvironment,
                                   "environ");
  if (result.state == ProcState::Missing) {
    return {ProcEnvState::Exited, {}};
  }
  if (result.state == ProcState::Unavailable) {
    return {ProcEnvState::Unreadable, {}};
  }

  const QByteArray prefix = QByteArray(varName) + '=';
  for (const QByteArray& entry : result.value.split('\0')) {
    if (entry.startsWith(prefix)) {
      return {ProcEnvState::Found,
              QString::fromUtf8(entry.mid(prefix.size()))};
    }
  }
  return {ProcEnvState::Missing, {}};
}

QString normalizedPrefixPath(const QString& path)
{
  const QString canonical = QFileInfo(path).canonicalFilePath();
  return canonical.isEmpty()
             ? QDir::cleanPath(QFileInfo(path).absoluteFilePath())
             : canonical;
}

enum class LaunchMatch
{
  Match,
  Mismatch,
  Exited,
  Unreadable
};

LaunchMatch processBelongsToLaunch(const ProcContext& context, pid_t pid,
                                   const QString& launchToken)
{
  const auto alive = processAliveState(context, pid);
  if (alive == ProcState::Missing) {
    return LaunchMatch::Exited;
  }
  if (alive == ProcState::Unavailable) {
    return LaunchMatch::Unreadable;
  }
  if (launchToken.isEmpty()) {
    return LaunchMatch::Match;
  }
  const auto token = readProcEnv(context, pid, LaunchTokenEnvironment);
  if (token.state == ProcEnvState::Unreadable) {
    return LaunchMatch::Unreadable;
  }
  if (token.state == ProcEnvState::Exited) {
    return LaunchMatch::Exited;
  }
  return token.state == ProcEnvState::Found && token.value == launchToken
             ? LaunchMatch::Match
             : LaunchMatch::Mismatch;
}

// Find a wineserver process owned by the current user that belongs to the
// given WINEPREFIX. When expectedPrefix is empty, returns the first
// wineserver owned by us (legacy behaviour).
//
// Wineserver stays alive as long as any Wine process in the prefix is
// running, making it the most reliable way to detect when a game has truly
// exited — even when launcher .exe's (nvse_loader, skse_loader, etc.) exit
// before the actual game.
struct WineserverResult
{
  pid_t pid{0};
  std::uint64_t startTime{UnknownProcessStartTime};
  bool unavailable{false};
};

WineserverResult findWineserver(const ProcContext& context,
                                const QString& expectedPrefix = {})
{
  WineserverResult result;
  std::vector<pid_t> ownedPids;
  std::optional<std::size_t> failAfterEntries;
  if (const auto fault =
          injectedFault(context, ProcOperation::OpenDirectory, 0)) {
    if (!fault->bytesBeforeError) {
      result.unavailable = true;
      return result;
    }
    failAfterEntries = *fault->bytesBeforeError;
  }

  DIR* proc = ::opendir("/proc");
  if (proc == nullptr) {
    result.unavailable = true;
    return result;
  }
  const uid_t myUid = ::getuid();
  std::size_t numericEntries = 0;
  while (true) {
    errno = 0;
    struct dirent* entry = ::readdir(proc);
    if (entry == nullptr) {
      if (errno != 0) {
        result.unavailable = true;
      }
      break;
    }
    const char* name = entry->d_name;
    if ((entry->d_type != DT_DIR && entry->d_type != DT_UNKNOWN) ||
        *name == '\0' ||
        !std::isdigit(static_cast<unsigned char>(*name))) {
      continue;
    }
    if (failAfterEntries && numericEntries++ >= *failAfterEntries) {
      result.unavailable = true;
      break;
    }

    const pid_t entryPid =
        static_cast<pid_t>(std::strtol(name, nullptr, 10));
    if (const auto fault =
            injectedFault(context, ProcOperation::StatEntry, entryPid)) {
      if (!fault->bytesBeforeError &&
          isMissingProcError(fault->error == 0 ? EIO : fault->error)) {
        continue;
      }
      result.unavailable = true;
      continue;
    }
    struct stat st;
    const QByteArray path =
        QFile::encodeName(QStringLiteral("/proc/%1").arg(entryPid));
    if (::stat(path.constData(), &st) != 0) {
      if (!isMissingProcError(errno)) {
        result.unavailable = true;
      }
      continue;
    }
    if (st.st_uid == myUid) {
      ownedPids.push_back(entryPid);
    }
  }
  ::closedir(proc);

  for (pid_t pid : ownedPids) {
    const auto authenticatedStat = readProcStat(context, pid);
    if (authenticatedStat.state == ProcState::Unavailable) {
      result.unavailable = true;
      continue;
    }
    if (authenticatedStat.state == ProcState::Missing ||
        authenticatedStat.value.state == 'Z' ||
        authenticatedStat.value.state == 'X') {
      continue;
    }
    const auto comm = readProcComm(context, pid);
    if (comm.state == ProcState::Unavailable) {
      result.unavailable = true;
      continue;
    }
    if (comm.state != ProcState::Available || comm.value != "wineserver") {
      continue;
    }

    // If a prefix filter was given, verify this wineserver belongs to it. An
    // absent WINEPREFIX means Wine's default ~/.wine prefix; unreadable procfs
    // remains uncertainty and must not be treated as a mismatch.
    if (!expectedPrefix.isEmpty()) {
      const auto wsPrefix = readProcEnv(context, pid, "WINEPREFIX");
      if (wsPrefix.state == ProcEnvState::Unreadable) {
        result.unavailable = true;
        continue;
      }
      if (wsPrefix.state == ProcEnvState::Exited) {
        continue;
      }
      const QString actualPrefix =
          wsPrefix.state == ProcEnvState::Found
              ? wsPrefix.value
              : QDir(QDir::homePath()).absoluteFilePath(QStringLiteral(".wine"));
      if (normalizedPrefixPath(actualPrefix) !=
          normalizedPrefixPath(expectedPrefix)) {
        log::debug("skipping wineserver {} (prefix '{}' != expected '{}')", pid,
                   actualPrefix.toStdString(), expectedPrefix.toStdString());
        continue;
      }
    }

    const auto verifiedStat = readProcStat(context, pid);
    if (verifiedStat.state == ProcState::Missing) {
      continue;
    }
    if (verifiedStat.state == ProcState::Unavailable ||
        verifiedStat.value.startTime != authenticatedStat.value.startTime) {
      result.unavailable = true;
      continue;
    }
    result.pid = pid;
    result.startTime = authenticatedStat.value.startTime;
    return result;
  }
  return result;
}

// Check whether any of the expected executable names appear in a process's
// comm or cmdline. Wine processes often show "wine64-preload" or "start.exe"
// in /proc/comm while the actual game executable only appears in cmdline.
// Also handles the 15-char TASK_COMM_LEN truncation in /proc/comm.
enum class ExpectedMatch
{
  Match,
  Mismatch,
  Exited,
  Unavailable
};

ExpectedMatch processMatchesExpected(const ProcContext& context, pid_t pid,
                                     const QStringList& expected,
                                     QString* matchedNameOut)
{
  const auto alive = processAliveState(context, pid);
  if (alive == ProcState::Missing) {
    return ExpectedMatch::Exited;
  }
  if (alive == ProcState::Unavailable) {
    return ExpectedMatch::Unavailable;
  }

  // 1. Check /proc/comm (fast path).
  const auto comm = readProcComm(context, pid);
  if (comm.state == ProcState::Available) {
    const QString lower = comm.value.toLower();
    for (const QString& exp : expected) {
      if (lower == exp) {
        if (matchedNameOut)
          *matchedNameOut = comm.value;
        return ExpectedMatch::Match;
      }
      // Handle TASK_COMM_LEN truncation (15 chars): if the expected name is
      // longer than 15 chars, check if comm matches its first 15 chars.
      if (exp.size() > 15 && lower == exp.left(15)) {
        if (matchedNameOut)
          *matchedNameOut = exp;
        return ExpectedMatch::Match;
      }
    }
  }

  // 2. Check /proc/cmdline — Wine/Proton processes carry the .exe name here
  //    even when comm shows wine64-preloader or start.exe.
  const auto cmdline = readProcCmdline(context, pid);
  if (cmdline.state == ProcState::Available) {
    for (const QString& arg : cmdline.value) {
      const QString normalized = QString(arg).replace('\\', '/');
      const QString base = QFileInfo(normalized).fileName().toLower();
      if (expected.contains(base)) {
        if (matchedNameOut)
          *matchedNameOut = QFileInfo(normalized).fileName();
        return ExpectedMatch::Match;
      }
    }
  }

  if (comm.state == ProcState::Unavailable ||
      cmdline.state == ProcState::Unavailable) {
    // A process can disappear between reads. Only report a normal exit when
    // that is positively observable; otherwise the unread field might have
    // contained the expected executable.
    return processAliveState(context, pid) == ProcState::Missing
               ? ExpectedMatch::Exited
               : ExpectedMatch::Unavailable;
  }
  return ExpectedMatch::Mismatch;
}

struct ProcPidScan
{
  std::vector<pid_t> pids;
  bool unavailable{false};
};

ProcPidScan scanOwnedProcesses(const ProcContext& context)
{
  ProcPidScan scan;
  std::optional<std::size_t> failAfterEntries;
  if (const auto fault =
          injectedFault(context, ProcOperation::OpenDirectory, 0)) {
    if (!fault->bytesBeforeError) {
      scan.unavailable = true;
      return scan;
    }
    failAfterEntries = *fault->bytesBeforeError;
  }

  DIR* proc = ::opendir("/proc");
  if (!proc) {
    scan.unavailable = true;
    return scan;
  }

  const uid_t myUid = ::getuid();
  std::size_t numericEntries = 0;
  while (true) {
    errno = 0;
    struct dirent* entry = ::readdir(proc);
    if (entry == nullptr) {
      if (errno != 0) {
        scan.unavailable = true;
      }
      break;
    }
    if (entry->d_type != DT_DIR && entry->d_type != DT_UNKNOWN) {
      continue;
    }

    const char* name = entry->d_name;
    if (*name == '\0' || !std::isdigit(static_cast<unsigned char>(*name))) {
      continue;
    }

    if (failAfterEntries && numericEntries++ >= *failAfterEntries) {
      scan.unavailable = true;
      break;
    }

    const pid_t pid = static_cast<pid_t>(std::strtol(name, nullptr, 10));
    if (const auto fault =
            injectedFault(context, ProcOperation::StatEntry, pid)) {
      if (!fault->bytesBeforeError &&
          isMissingProcError(fault->error == 0 ? EIO : fault->error)) {
        continue;
      }
      scan.unavailable = true;
      continue;
    }

    struct stat st;
    const QByteArray path =
        QFile::encodeName(QStringLiteral("/proc/%1").arg(pid));
    if (::stat(path.constData(), &st) != 0) {
      if (!isMissingProcError(errno)) {
        scan.unavailable = true;
      }
      continue;
    }
    if (st.st_uid == myUid) {
      scan.pids.push_back(pid);
    }
  }

  ::closedir(proc);
  return scan;
}

struct ProcChildrenMap
{
  struct Identity
  {
    pid_t pid{0};
    std::uint64_t startTime{UnknownProcessStartTime};
  };

  std::unordered_map<pid_t, std::vector<Identity>> children;
  bool unavailable{false};
};

using ProcIdentity = ProcChildrenMap::Identity;

struct ProcLineage
{
  // Ordered root -> candidate. Each identity comes from the same stat read as
  // that process's PPid edge.
  std::vector<ProcIdentity> identities;

  const ProcIdentity& process() const { return identities.back(); }
};

enum class LineageState
{
  Valid,
  Missing,
  Changed,
  Unavailable
};

ProcChildrenMap buildProcChildrenMap(const ProcContext& context = {})
{
  ProcChildrenMap result;
  const auto scan = scanOwnedProcesses(context);
  result.unavailable = scan.unavailable;

  for (pid_t pid : scan.pids) {
    const auto stat = readProcStat(context, pid);
    if (stat.state == ProcState::Missing) {
      continue;
    }
    if (stat.state == ProcState::Unavailable) {
      result.unavailable = true;
      continue;
    }
    if (stat.value.state == 'Z' || stat.value.state == 'X') {
      continue;
    }
    if (stat.value.parentPid > 0) {
      result.children[stat.value.parentPid].push_back(
          {pid, stat.value.startTime});
    }
  }
  return result;
}

std::vector<ProcLineage> collectDescendants(
    const ProcIdentity& root,
    const std::unordered_map<pid_t, std::vector<ProcIdentity>>& children)
{
  std::vector<ProcLineage> out;
  std::deque<ProcLineage> q;
  q.push_back({{root}});
  std::unordered_set<pid_t> seen{root.pid};

  while (!q.empty()) {
    ProcLineage current = std::move(q.front());
    q.pop_front();

    const auto it = children.find(current.process().pid);
    if (it == children.end()) {
      continue;
    }

    for (const ProcIdentity& child : it->second) {
      if (seen.insert(child.pid).second) {
        ProcLineage lineage = current;
        lineage.identities.push_back(child);
        out.push_back(lineage);
        q.push_back(std::move(lineage));
      }
    }
  }

  return out;
}

LineageState validateLineage(const ProcContext& context,
                             const ProcLineage& lineage)
{
  if (lineage.identities.empty()) {
    return LineageState::Changed;
  }

  const auto exactStat = [&](const ProcIdentity& identity,
                             bool candidate) -> LineageState {
    const auto stat = readProcStat(context, identity.pid);
    if (stat.state == ProcState::Missing ||
        (stat.state == ProcState::Available &&
         (stat.value.state == 'Z' || stat.value.state == 'X'))) {
      return candidate ? LineageState::Missing : LineageState::Changed;
    }
    if (stat.state == ProcState::Unavailable) {
      return LineageState::Unavailable;
    }
    return stat.value.startTime == identity.startTime
               ? LineageState::Valid
               : LineageState::Changed;
  };

  if (lineage.identities.size() == 1) {
    return exactStat(lineage.identities.front(), true);
  }

  // For every edge, bracket the child's coherent (PPid,starttime) stat read
  // with exact-generation reads of its parent. A parent PID that is recycled
  // during the scan therefore cannot lend its replacement's children to the
  // old lineage.
  for (std::size_t index = 1; index < lineage.identities.size(); ++index) {
    const ProcIdentity& parent = lineage.identities[index - 1];
    const ProcIdentity& child  = lineage.identities[index];
    const auto parentBefore = exactStat(parent, false);
    if (parentBefore != LineageState::Valid) {
      return parentBefore;
    }

    const auto childStat = readProcStat(context, child.pid);
    if (childStat.state == ProcState::Missing ||
        (childStat.state == ProcState::Available &&
         (childStat.value.state == 'Z' || childStat.value.state == 'X'))) {
      return index + 1 == lineage.identities.size()
                 ? LineageState::Missing
                 : LineageState::Changed;
    }
    if (childStat.state == ProcState::Unavailable) {
      return LineageState::Unavailable;
    }
    if (childStat.value.startTime != child.startTime ||
        childStat.value.parentPid != parent.pid) {
      return LineageState::Changed;
    }

    const auto parentAfter = exactStat(parent, false);
    if (parentAfter != LineageState::Valid) {
      return parentAfter;
    }
  }
  return LineageState::Valid;
}

enum class ExactSignalResult
{
  Signalled,
  Exited,
  Unavailable
};

ExactSignalResult signalExactProcess(const ProcContext& context,
                                     const ProcIdentity& identity,
                                     int signal)
{
#if defined(SYS_pidfd_open) && defined(SYS_pidfd_send_signal)
  int pidfd = -1;
  do {
    pidfd = static_cast<int>(::syscall(SYS_pidfd_open, identity.pid, 0));
  } while (pidfd < 0 && errno == EINTR);
  if (pidfd < 0) {
    return errno == ESRCH ? ExactSignalResult::Exited
                          : ExactSignalResult::Unavailable;
  }

  // Opening first binds the kernel handle to one generation. Authenticate
  // that bound generation by starttime before signalling through the handle;
  // a raw kill(pid) after a stat check would retain a PID-reuse TOCTOU.
  const auto stat = readProcStat(context, identity.pid);
  if (stat.state == ProcState::Missing ||
      (stat.state == ProcState::Available &&
       (stat.value.state == 'Z' || stat.value.state == 'X' ||
        stat.value.startTime != identity.startTime))) {
    ::close(pidfd);
    return ExactSignalResult::Exited;
  }
  if (stat.state == ProcState::Unavailable) {
    ::close(pidfd);
    return ExactSignalResult::Unavailable;
  }

  int sent = -1;
  do {
    sent = static_cast<int>(
        ::syscall(SYS_pidfd_send_signal, pidfd, signal, nullptr, 0));
  } while (sent < 0 && errno == EINTR);
  const int sendError = errno;
  ::close(pidfd);
  if (sent == 0) {
    return ExactSignalResult::Signalled;
  }
  return sendError == ESRCH ? ExactSignalResult::Exited
                            : ExactSignalResult::Unavailable;
#else
  Q_UNUSED(context);
  Q_UNUSED(identity);
  Q_UNUSED(signal);
  // Destructive cleanup fails closed on kernels/toolchains without pidfds.
  return ExactSignalResult::Unavailable;
#endif
}

QStringList buildExpectedExecutables(const QFileInfo& binary, const QString& arguments,
                                     const QStringList& additional)
{
  QStringList expected;
  auto addName = [&](QString name) {
    name = name.trimmed().toLower();
    if (!name.isEmpty() && !expected.contains(name)) {
      expected.push_back(name);
    }
  };

  addName(binary.fileName());

  const auto args = QProcess::splitCommand(arguments);
  for (const QString& arg : args) {
    const QFileInfo fi(arg);
    const QString base = fi.fileName();
    if (base.endsWith(".exe", Qt::CaseInsensitive)) {
      addName(base);
    }
  }

  for (const QString& name : additional) {
    addName(QFileInfo(QString(name).replace('\\', '/')).fileName());
  }

  log::debug("buildExpectedExecutables: returning [{}]",
             expected.join(", ").toStdString());
  return expected;
}

struct DiscoveryResult
{
  pid_t pid{0};
  std::uint64_t startTime{UnknownProcessStartTime};
  QString name;
  bool unavailable{false};
};

bool captureDiscoveredIdentity(const ProcContext& context,
                               DiscoveryResult& result, pid_t pid,
                               std::uint64_t expectedStartTime,
                               const QString& name)
{
  const auto stat = readProcStat(context, pid);
  if (stat.state == ProcState::Unavailable) {
    result.unavailable = true;
    return false;
  }
  if (stat.state == ProcState::Missing || stat.value.state == 'Z' ||
      stat.value.state == 'X') {
    return false;
  }
  if (stat.value.startTime != expectedStartTime) {
    // Name/provenance was checked on a different PID generation. Retry a
    // later scan instead of authenticating the replacement process.
    result.unavailable = true;
    return false;
  }
  result.pid = pid;
  result.startTime = stat.value.startTime;
  result.name = name;
  return true;
}

DiscoveryResult findTrackedProcess(const ProcContext& context, pid_t rootPid,
                                   std::uint64_t rootStartTime,
                                   const QStringList& expected,
                                   const QString& launchToken)
{
  DiscoveryResult result;
  if (expected.isEmpty()) {
    return result;
  }

  const auto children = buildProcChildrenMap(context);
  result.unavailable = children.unavailable;
  const auto descendants = collectDescendants(
      {rootPid, rootStartTime}, children.children);

  const bool trustedLineage =
      processIdentityState(context, rootPid, rootStartTime) ==
      IdentityState::Running;
  for (const ProcLineage& lineage : descendants) {
    const pid_t pid = lineage.process().pid;
    const auto candidateStat = readProcStat(context, pid);
    if (candidateStat.state == ProcState::Unavailable) {
      result.unavailable = true;
      continue;
    }
    if (candidateStat.state == ProcState::Missing ||
        candidateStat.value.state == 'Z' ||
        candidateStat.value.state == 'X') {
      continue;
    }
    if (candidateStat.value.startTime != lineage.process().startTime) {
      result.unavailable = true;
      continue;
    }
    QString matched;
    const auto expectedMatch =
        processMatchesExpected(context, pid, expected, &matched);
    if (expectedMatch == ExpectedMatch::Unavailable) {
      result.unavailable = true;
      continue;
    }
    if (expectedMatch != ExpectedMatch::Match) {
      continue;
    }
    if (trustedLineage) {
      const auto lineageState = validateLineage(context, lineage);
      if (lineageState == LineageState::Valid) {
        if (captureDiscoveredIdentity(context, result, pid,
                                      lineage.process().startTime, matched)) {
          return result;
        }
      } else if (lineageState != LineageState::Missing) {
        result.unavailable = true;
      }
      continue;
    }
    if (launchToken.isEmpty()) {
      // Without an exact root generation, lineage is not provenance: the PID
      // may have been reused and this can be the replacement's child. A token
      // or the separately scoped global scan is required before adoption.
      if (processAliveState(context, pid) != ProcState::Missing) {
        result.unavailable = true;
      }
      continue;
    }
    const auto launchMatch = processBelongsToLaunch(context, pid, launchToken);
    if (launchMatch == LaunchMatch::Unreadable) {
      // The root identity was not trustworthy enough to authenticate this
      // descendant by lineage, and the token is unreadable. Preserve the
      // uncertainty rather than adopting or reporting completion.
      if (processAliveState(context, pid) != ProcState::Missing) {
        result.unavailable = true;
      }
      continue;
    }
    if (launchMatch == LaunchMatch::Match) {
      if (captureDiscoveredIdentity(context, result, pid,
                                    candidateStat.value.startTime, matched)) {
        return result;
      }
    }
  }
  return result;
}

// Scan every process owned by the current user for one whose comm or cmdline
// matches an expected game executable (e.g. FalloutNV.exe, SkyrimSE.exe).
// Used as a fallback after the immediate descendant tree loses the game —
// Proton's session manager can reparent game processes outside of our root
// PID's subtree, so a plain-descendant walk misses them. A per-launch token is
// preferred; the WINEPREFIX is retained only as a compatibility fallback for
// callers that do not have a token.
DiscoveryResult findGameProcessForLaunch(const ProcContext& context,
                                         const QStringList& expected,
                                         const QString& winePrefix,
                                         const QString& launchToken,
                                         pid_t rootPid,
                                         std::uint64_t rootStartTime)
{
  DiscoveryResult result;
  if (expected.isEmpty() || (launchToken.isEmpty() && winePrefix.isEmpty())) {
    return result;
  }

  QString expectedPrefixCanon;
  if (!winePrefix.isEmpty()) {
    expectedPrefixCanon = normalizedPrefixPath(winePrefix);
  }

  const auto scan = scanOwnedProcesses(context);
  result.unavailable = scan.unavailable;
  for (pid_t pid : scan.pids) {
    if (pid == rootPid) {
      continue;
    }

    const auto authenticatedStat = readProcStat(context, pid);
    if (authenticatedStat.state == ProcState::Unavailable) {
      result.unavailable = true;
      continue;
    }
    if (authenticatedStat.state == ProcState::Missing ||
        authenticatedStat.value.state == 'Z' ||
        authenticatedStat.value.state == 'X') {
      continue;
    }

    QString matched;
    const auto expectedMatch =
        processMatchesExpected(context, pid, expected, &matched);
    if (expectedMatch == ExpectedMatch::Unavailable) {
      result.unavailable = true;
      continue;
    }
    if (expectedMatch != ExpectedMatch::Match) {
      continue;
    }

    // A unique inherited token is the primary provenance. It survives native
    // QProcess::startDetached and Flatpak/Proton reparenting without admitting
    // an unrelated same-name process owned by the user.
    if (!launchToken.isEmpty()) {
      const auto token =
          readProcEnv(context, pid, LaunchTokenEnvironment);
      if (token.state == ProcEnvState::Unreadable) {
        // A pre-existing unrelated same-name process must not hold this
        // launch forever merely because its environment is unreadable. Only
        // a process born no earlier than the captured root is a plausible
        // unresolved detached companion.
        const auto candidateStat = readProcStat(context, pid);
        const bool plausiblyNew =
            rootStartTime == UnknownProcessStartTime ||
            rootStartTime == 0 ||
            (candidateStat.state == ProcState::Available &&
             (candidateStat.value.startTime > rootStartTime ||
              (candidateStat.value.startTime == rootStartTime &&
               pid > rootPid)));
        if (candidateStat.state == ProcState::Unavailable ||
            (plausiblyNew &&
             processAliveState(context, pid) != ProcState::Missing)) {
          result.unavailable = true;
        }
        continue;
      }
      if (token.state == ProcEnvState::Exited) {
        continue;
      }
      if (token.state != ProcEnvState::Found || token.value != launchToken) {
        continue;
      }
    }

    // Constrain to the same WINEPREFIX so we don't latch onto an unrelated
    // Wine process (another instance, winetricks, etc.).
    if (launchToken.isEmpty() && !expectedPrefixCanon.isEmpty()) {
      const auto pidPrefix = readProcEnv(context, pid, "WINEPREFIX");
      if (pidPrefix.state == ProcEnvState::Unreadable) {
        result.unavailable = true;
        continue;
      }
      if (pidPrefix.state == ProcEnvState::Exited)
        continue;
      const QString actualPrefix =
          pidPrefix.state == ProcEnvState::Found
              ? pidPrefix.value
              : QDir(QDir::homePath()).absoluteFilePath(QStringLiteral(".wine"));
      if (normalizedPrefixPath(actualPrefix) != expectedPrefixCanon)
        continue;
    }

    if (captureDiscoveredIdentity(context, result, pid,
                                  authenticatedStat.value.startTime,
                                  matched)) {
      return result;
    }
  }
  return result;
}

struct LaunchTokenScan
{
  std::vector<ProcIdentity> processes;
  bool unavailable{false};
};

// Destructive cleanup cannot use the ordinary expected-executable filter:
// launchers may leave helper/audio/crash workers whose names were never part
// of the game policy. Enumerate the unique inherited launch token instead and
// authenticate the environment read to one exact PID generation.
LaunchTokenScan findProcessesForLaunchToken(
    const ProcContext& context, const QString& launchToken,
    std::uint64_t rootStartTime)
{
  LaunchTokenScan result;
  if (launchToken.isEmpty()) {
    return result;
  }

  const auto scan = scanOwnedProcesses(context);
  result.unavailable = scan.unavailable;
  for (pid_t candidate : scan.pids) {
    const auto initial = readProcStat(context, candidate);
    if (initial.state == ProcState::Unavailable) {
      result.unavailable = true;
      continue;
    }
    if (initial.state == ProcState::Missing || initial.value.state == 'Z' ||
        initial.value.state == 'X') {
      continue;
    }

    const bool plausiblyFromLaunch =
        rootStartTime == 0 || rootStartTime == UnknownProcessStartTime ||
        initial.value.startTime >= rootStartTime;
    const auto token =
        readProcEnv(context, candidate, LaunchTokenEnvironment);
    if (token.state == ProcEnvState::Unreadable) {
      // An unreadable process born during this launch might hold the token.
      // Treat the root's whole start-time tick as in-scope regardless of PID:
      // PID ordering is not reliable across allocator wrap. Older unrelated
      // processes need not poison every force-unlock on the system.
      if (plausiblyFromLaunch &&
          processIdentityState(context, candidate,
                               initial.value.startTime) !=
              IdentityState::Exited) {
        result.unavailable = true;
      }
      continue;
    }
    if (token.state == ProcEnvState::Exited ||
        token.state != ProcEnvState::Found || token.value != launchToken) {
      continue;
    }

    const auto verified = readProcStat(context, candidate);
    if (verified.state == ProcState::Missing ||
        (verified.state == ProcState::Available &&
         (verified.value.state == 'Z' || verified.value.state == 'X'))) {
      continue;
    }
    if (verified.state == ProcState::Unavailable ||
        verified.value.startTime != initial.value.startTime) {
      result.unavailable = true;
      continue;
    }
    result.processes.push_back({candidate, initial.value.startTime});
  }
  return result;
}

bool killProcessesForLaunchToken(const ProcContext& context,
                                 const QString& launchToken,
                                 std::uint64_t rootStartTime)
{
  if (launchToken.isEmpty()) {
    return true;
  }

  // A terminating helper can race by creating one last worker. Rescan a
  // bounded number of times and only certify cleanup after a complete empty
  // token scan.
  for (int round = 0; round < 3; ++round) {
    const auto scan = findProcessesForLaunchToken(
        context, launchToken, rootStartTime);
    if (scan.unavailable) {
      return false;
    }
    if (scan.processes.empty()) {
      return true;
    }

    bool complete = true;
    for (const ProcIdentity& identity : scan.processes) {
      const auto state = processIdentityState(context, identity.pid,
                                              identity.startTime);
      if (state == IdentityState::Exited) {
        continue;
      }
      if (state == IdentityState::Unknown) {
        complete = false;
        continue;
      }
      log::info("sending SIGTERM to launch-token process {}", identity.pid);
      if (signalExactProcess(context, identity, SIGTERM) ==
          ExactSignalResult::Unavailable) {
        complete = false;
      }
    }

    for (int retry = 0; retry < 5; ++retry) {
      bool anyRunning = false;
      for (const ProcIdentity& identity : scan.processes) {
        if (processIdentityState(context, identity.pid, identity.startTime) !=
            IdentityState::Exited) {
          anyRunning = true;
          break;
        }
      }
      if (!anyRunning) {
        break;
      }
      QThread::msleep(100);
    }

    for (const ProcIdentity& identity : scan.processes) {
      const auto state = processIdentityState(context, identity.pid,
                                              identity.startTime);
      if (state == IdentityState::Running) {
        log::warn("launch-token process {} did not exit on SIGTERM, sending "
                  "SIGKILL",
                  identity.pid);
        if (signalExactProcess(context, identity, SIGKILL) ==
            ExactSignalResult::Unavailable) {
          complete = false;
        }
      } else if (state == IdentityState::Unknown) {
        complete = false;
      }
    }

    for (int retry = 0; retry < 10; ++retry) {
      bool allExited = true;
      for (const ProcIdentity& identity : scan.processes) {
        if (processIdentityState(context, identity.pid, identity.startTime) !=
            IdentityState::Exited) {
          allExited = false;
          break;
        }
      }
      if (allExited) {
        break;
      }
      QThread::msleep(50);
    }
    for (const ProcIdentity& identity : scan.processes) {
      if (processIdentityState(context, identity.pid, identity.startTime) !=
          IdentityState::Exited) {
        complete = false;
      }
    }
    if (!complete) {
      return false;
    }
  }

  const auto remaining = findProcessesForLaunchToken(
      context, launchToken, rootStartTime);
  return !remaining.unavailable && remaining.processes.empty();
}

// Best-effort "hard kill" of the wineserver (and every Wine process it owns)
// for the given prefix. Used when the user clicks Unlock in the lock dialog —
// Proton's session manager can keep wineserver alive for tens of seconds
// after the game exits, and that's exactly what the user is asking us to
// short-circuit.
bool killWineserverForPrefix(const QString& winePrefix,
                             const ProcContext& context)
{
  if (winePrefix.isEmpty()) {
    log::debug("killWineserverForPrefix: skipping (no WINEPREFIX resolved)");
    return true;
  }

  const auto found = findWineserver(context, winePrefix);
  const pid_t ws = found.pid;
  if (ws > 0) {
    const auto initialState =
        processIdentityState(context, ws, found.startTime);
    if (initialState == IdentityState::Exited) {
      return !found.unavailable;
    }
    if (initialState == IdentityState::Unknown) {
      return false;
    }
    log::info("sending SIGTERM to wineserver {} for prefix '{}'", ws,
              winePrefix.toStdString());
    if (signalExactProcess(context, {ws, found.startTime}, SIGTERM) ==
        ExactSignalResult::Unavailable) {
      log::warn("SIGTERM on exact wineserver identity {} failed", ws);
      return false;
    }
    // Give wineserver a short window to tear down cleanly, then SIGKILL if
    // it's still hanging around.
    for (int i = 0; i < 10; ++i) {
      if (processIdentityState(context, ws, found.startTime) ==
          IdentityState::Exited) {
        break;
      }
      QThread::msleep(100);
    }
    if (processIdentityState(context, ws, found.startTime) ==
        IdentityState::Running) {
      log::warn("wineserver {} did not exit on SIGTERM, sending SIGKILL", ws);
      if (signalExactProcess(context, {ws, found.startTime}, SIGKILL) ==
          ExactSignalResult::Unavailable) {
        return false;
      }
    }
    for (int i = 0; i < 10; ++i) {
      const auto state =
          processIdentityState(context, ws, found.startTime);
      if (state == IdentityState::Exited) {
        return !found.unavailable;
      }
      QThread::msleep(50);
    }
    return false;
  } else {
    log::debug("killWineserverForPrefix: no wineserver found for '{}'",
               winePrefix.toStdString());
  }
  return !found.unavailable;
}

// SIGTERM every descendant of |root| (and root itself), wait briefly, then
// SIGKILL any survivor. Used on force-unlock so launcher .exe grandchildren
// (skse_loader → SkyrimSE.exe → audio/physics workers) all go down even when
// wineserver wasn't reachable.
bool killProcessTree(pid_t root, std::uint64_t rootStartTime,
                     const ProcContext& context)
{
  if (root <= 0 || rootStartTime == 0 ||
      rootStartTime == UnknownProcessStartTime) {
    return false;
  }
  if (processIdentityState(context, root, rootStartTime) !=
      IdentityState::Running) {
    return false;
  }

  const auto children = buildProcChildrenMap(context);
  auto descendants =
      collectDescendants({root, rootStartTime}, children.children);
  bool complete = !children.unavailable;
  std::vector<ProcLineage> authenticated;
  std::unordered_map<pid_t, std::uint64_t> identities;
  const auto verifiedRoot = readProcStat(context, root);
  if (verifiedRoot.state != ProcState::Available ||
      verifiedRoot.value.startTime != rootStartTime ||
      verifiedRoot.value.state == 'Z' || verifiedRoot.value.state == 'X') {
    // The tree map cannot be attributed to the expected root generation.
    return false;
  }
  authenticated.push_back({{{root, rootStartTime}}});
  identities.emplace(root, rootStartTime);
  for (const ProcLineage& lineage : descendants) {
    const auto state = validateLineage(context, lineage);
    if (state == LineageState::Missing) {
      continue;
    }
    if (state != LineageState::Valid) {
      complete = false;
      continue;
    }
    authenticated.push_back(lineage);
    identities.emplace(lineage.process().pid, lineage.process().startTime);
  }

  // Signal leaves before their parents. This keeps each saved ancestry intact
  // until the candidate's final pre-signal validation and avoids treating a
  // legitimate reparenting caused by our own teardown as authentication.
  std::sort(authenticated.begin(), authenticated.end(),
            [](const ProcLineage& left, const ProcLineage& right) {
              return left.identities.size() > right.identities.size();
            });
  for (const ProcLineage& lineage : authenticated) {
    const pid_t p = lineage.process().pid;
    const auto lineageState = validateLineage(context, lineage);
    if (lineageState == LineageState::Missing) {
      continue;
    }
    if (lineageState != LineageState::Valid) {
      complete = false;
      continue;
    }
    if (signalExactProcess(context, lineage.process(), SIGTERM) ==
        ExactSignalResult::Unavailable) {
      log::debug("SIGTERM on exact process identity {} failed", p);
      complete = false;
    }
  }

  for (int i = 0; i < 5; ++i) {
    bool anyAlive = false;
    for (const auto& [p, startTime] : identities) {
      const auto state = processIdentityState(context, p, startTime);
      if (state != IdentityState::Exited) {
        anyAlive = true;
        break;
      }
    }
    if (!anyAlive) {
      return complete;
    }
    QThread::msleep(100);
  }

  for (const auto& [p, startTime] : identities) {
    const auto state = processIdentityState(context, p, startTime);
    if (state == IdentityState::Running) {
      log::warn("process {} did not exit on SIGTERM, sending SIGKILL", p);
      if (signalExactProcess(context, {p, startTime}, SIGKILL) ==
          ExactSignalResult::Unavailable) {
        complete = false;
      }
    } else if (state == IdentityState::Unknown) {
      complete = false;
    }
  }

  for (int i = 0; i < 10; ++i) {
    bool allExited = true;
    for (const auto& [p, startTime] : identities) {
      if (processIdentityState(context, p, startTime) !=
          IdentityState::Exited) {
        allExited = false;
        break;
      }
    }
    if (allExited) {
      return complete;
    }
    QThread::msleep(50);
  }
  return false;
}

std::uint32_t exitCodeFromWaitStatus(int status)
{
  if (WIFEXITED(status)) {
    return static_cast<std::uint32_t>(WEXITSTATUS(status));
  }

  if (WIFSIGNALED(status)) {
    return static_cast<std::uint32_t>(128 + WTERMSIG(status));
  }

  return 0;
}

// killTreeOnUnlock: when the user force-unlocks/cancels the lock dialog,
// SIGKILL the launched process tree (and the wineserver) so the wineprefix/FUSE
// VFS can be torn down cleanly. This is correct for Proton games and for native
// games that still rely on the FUSE VFS (e.g. Stardew Valley). It must be FALSE
// for native, non-VFS games like OpenMW (usesVFS()==false): there is nothing to
// tear down, and killing the tree would take down a launcher-spawned engine the
// user is actively using. See OrganizerCore::managedGame()->usesVFS() at the
// caller.
Result waitForPid(pid_t pid, std::uint32_t* exitCode, const Callbacks& callbacks,
                  const QStringList& expected, bool killTreeOnUnlock,
                  const QString& launchToken, bool expectDetachedCompanion,
                  std::uint64_t rootStartTime,
                  const std::shared_ptr<RootProcessCompletion>& rootCompletion)
{
  const ProcContext procContext{&callbacks.procFaultInjector};
  const auto started = std::chrono::steady_clock::now();
  constexpr auto initialCompanionGrace = std::chrono::milliseconds(5000);
  constexpr auto transitionCompanionGrace = std::chrono::milliseconds(750);
  constexpr auto discoveryInterval = std::chrono::milliseconds(100);
  constexpr auto invalidExitCode = static_cast<std::uint32_t>(-1);
  if (exitCode != nullptr) {
    *exitCode = invalidExitCode;
  }

  pid_t lastTrackedPid = 0;
  std::uint64_t lastTrackedStartTime = 0;
  QString lastTrackedName;
  const auto finish = [&](Result result) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();
    const auto rawCode = exitCode != nullptr ? *exitCode : invalidExitCode;
    const auto code = rawCode == invalidExitCode ? -1 : static_cast<int>(rawCode);
    const char* resultName = "error";
    switch (result) {
    case Result::Completed:
      resultName = "completed";
      break;
    case Result::Cancelled:
      resultName = "cancelled";
      break;
    case Result::ForceUnlocked:
      resultName = "force-unlocked";
      break;
    case Result::Error:
      break;
    }

    log::info("process runner: root pid {} lifetime ended; adopted pid {} ({}), "
              "exit code {}, elapsed {} ms, result {}",
              pid, lastTrackedPid,
              lastTrackedName.isEmpty() ? "none" : lastTrackedName.toStdString(), code,
              elapsed, resultName);
    return result;
  };

  if (pid <= 0) {
    return finish(Result::Error);
  }
  // UnknownProcessStartTime means the caller could not capture a generation.
  // Raw roots may retry capture only after waitpid(WNOHANG)==0 proves this PID
  // is still our live child. Managed QProcess roots may retry only while their
  // shared completion is Running, with a second state check after /proc. A
  // detached/unowned or terminal root must remain generation-unknown so Force
  // Unlock can never signal a reused process by PID alone.
  bool rootStartCapturePending =
      rootStartTime == UnknownProcessStartTime;
  if (rootStartTime == 0) {
    const auto rootStat = readProcStat(procContext, pid);
    if (rootStat.state == ProcState::Available) {
      rootStartTime = rootStat.value.startTime;
    } else {
      rootStartTime = UnknownProcessStartTime;
      rootStartCapturePending =
          rootStat.state == ProcState::Unavailable;
    }
  }

  // Capture the WINEPREFIX from the launched process so we can filter
  // wineserver lookups to the correct prefix. Without this, Fluorine would
  // track ANY wineserver owned by the user (e.g. one running winecfg under
  // ~/.wine while the game uses a different prefix).
  const bool initialRootIdentityExact =
      rootStartTime != 0 && rootStartTime != UnknownProcessStartTime &&
      processIdentityState(procContext, pid, rootStartTime) ==
          IdentityState::Running;
  const auto rootPrefix = readProcEnv(procContext, pid, "WINEPREFIX");
  const bool initialPrefixTrusted =
      initialRootIdentityExact &&
      processIdentityState(procContext, pid, rootStartTime) ==
          IdentityState::Running &&
      rootPrefix.state != ProcEnvState::Exited;
  QString winePrefix =
      initialPrefixTrusted && rootPrefix.state == ProcEnvState::Found
          ? rootPrefix.value
          : QString{};
  bool rootPrefixUnavailable =
      !initialPrefixTrusted || rootPrefix.state == ProcEnvState::Unreadable;
  bool rootScopeUnavailable =
      rootPrefixUnavailable && expected.size() > 0 &&
      launchToken.isEmpty();
  if (!winePrefix.isEmpty()) {
    log::debug("process {} has WINEPREFIX='{}'", pid, winePrefix.toStdString());
  }

  bool seenTrackedProcess = false;
  bool sawInaccessibleCandidate = false;
  std::optional<std::chrono::steady_clock::time_point> unavailableSince;
  std::optional<std::chrono::steady_clock::time_point> companionDeadline;
  const auto waitForCompanionGrace = [&](auto duration) {
    if (!expectDetachedCompanion) {
      return false;
    }
    const auto now = std::chrono::steady_clock::now();
    if (!companionDeadline) {
      companionDeadline = now + duration;
    }
    return now < *companionDeadline;
  };
  const auto findScopedProcess = [&](QString* name,
                                     std::uint64_t* startTime) {
    const auto found = findGameProcessForLaunch(
        procContext, expected, winePrefix, launchToken, pid, rootStartTime);
    sawInaccessibleCandidate =
        sawInaccessibleCandidate || found.unavailable;
    if (name != nullptr && found.pid > 0) {
      *name = found.name;
    }
    if (startTime != nullptr && found.pid > 0) {
      *startTime = found.startTime;
    }
    return found.pid;
  };
  const auto procUnavailableExpired = [&]() {
    const auto now = std::chrono::steady_clock::now();
    if (!unavailableSince) {
      unavailableSince = now;
    }
    const auto timeout =
        std::max(callbacks.procUnavailableTimeout,
                 std::chrono::milliseconds::zero());
    return now - *unavailableSince >= timeout;
  };
  const auto clearProcUnavailable = [&]() { unavailableSince.reset(); };
  const auto finishCompleted = [&]() { return finish(Result::Completed); };
  const auto refreshKnownRunningRootStart = [&]() {
    if (!rootStartCapturePending) {
      return;
    }
    if (rootCompletion &&
        rootCompletion->snapshot().state !=
            RootProcessCompletion::State::Running) {
      return;
    }
    const auto refreshed = readProcStat(procContext, pid);
    if (refreshed.state == ProcState::Available &&
        refreshed.value.state != 'Z' && refreshed.value.state != 'X' &&
        (!rootCompletion ||
         rootCompletion->snapshot().state ==
             RootProcessCompletion::State::Running)) {
      rootStartTime = refreshed.value.startTime;
      rootStartCapturePending = false;
    }
  };
  const auto refreshRootScope = [&]() {
    if (!rootPrefixUnavailable && !rootScopeUnavailable) {
      return;
    }
    if (rootStartTime == 0 ||
        rootStartTime == UnknownProcessStartTime ||
        processIdentityState(procContext, pid, rootStartTime) !=
            IdentityState::Running) {
      return;
    }
    const auto refreshed = readProcEnv(procContext, pid, "WINEPREFIX");
    if (processIdentityState(procContext, pid, rootStartTime) !=
        IdentityState::Running) {
      return;
    }
    if (refreshed.state == ProcEnvState::Found ||
        refreshed.state == ProcEnvState::Missing) {
      winePrefix = refreshed.state == ProcEnvState::Found
                       ? refreshed.value
                       : QString{};
      rootPrefixUnavailable = false;
      rootScopeUnavailable = false;
    }
  };

  // Give a transient failure a chance to recover before waitpid can reap a
  // very short-lived root and make its environment permanently unavailable.
  refreshRootScope();

  QString initialTrackedName;
  const auto initialTracked = findTrackedProcess(
      procContext, pid, rootStartTime, expected, launchToken);
  lastTrackedPid = initialTracked.pid;
  lastTrackedStartTime = initialTracked.startTime;
  initialTrackedName = initialTracked.name;
  sawInaccessibleCandidate = initialTracked.unavailable;
  if (lastTrackedPid == 0) {
    lastTrackedPid =
        findScopedProcess(&initialTrackedName, &lastTrackedStartTime);
  }
  if (lastTrackedPid > 0) {
    seenTrackedProcess = true;
    sawInaccessibleCandidate = false;
    companionDeadline.reset();
    lastTrackedName = initialTrackedName;
    log::info("process runner: root pid {} adopted lifetime pid {} ({})", pid,
              lastTrackedPid, lastTrackedName.toStdString());
  }
  auto nextDiscoveryAt =
      std::chrono::steady_clock::now() + discoveryInterval;
  const auto discoverProcess = [&](QString* name,
                                   std::uint64_t* startTime) {
    sawInaccessibleCandidate = false;
    const auto descendant = findTrackedProcess(
        procContext, pid, rootStartTime, expected, launchToken);
    pid_t found = descendant.pid;
    if (name != nullptr) {
      *name = descendant.name;
    }
    if (startTime != nullptr) {
      *startTime = descendant.startTime;
    }
    sawInaccessibleCandidate = descendant.unavailable;
    if (found == 0) {
      found = findScopedProcess(name, startTime);
    }
    nextDiscoveryAt =
        std::chrono::steady_clock::now() + discoveryInterval;
    return found;
  };
  bool rootExitTransitionScanned = false;

  // QProcess owns and reaps roots that it starts. Such roots always use exact
  // identity polling plus the shared QProcess result; calling waitpid here
  // would race the QProcess notifier for the one child status. Raw/detached
  // callers retain the legacy ownership probe.
  bool useKillPoll = rootCompletion != nullptr;
  if (rootCompletion) {
    const auto root = rootCompletion->snapshot();
    if (root.state == RootProcessCompletion::State::Failed) {
      return finish(Result::Error);
    }
    if (root.state == RootProcessCompletion::State::Exited) {
      if (exitCode != nullptr) {
        *exitCode = root.exitCode;
      }
    } else {
      refreshKnownRunningRootStart();
      refreshRootScope();
    }
  } else {
    int status = 0;
    if (callbacks.waitpidAttempted) {
      callbacks.waitpidAttempted(pid);
    }
    const pid_t probe = ::waitpid(pid, &status, WNOHANG);
    if (probe == 0) {
      // waitpid(0) is a generation-safe proof that this is still our live
      // child, so a failed initial stat can now be reacquired safely.
      refreshKnownRunningRootStart();
      refreshRootScope();
    }
    if (probe == pid) {
      if (exitCode != nullptr) {
        *exitCode = exitCodeFromWaitStatus(status);
      }
      if (seenTrackedProcess &&
          processIdentityState(procContext, lastTrackedPid,
                               lastTrackedStartTime) !=
              IdentityState::Exited) {
        useKillPoll = true;
        log::debug("root process {} completed immediately but adopted process {} "
                   "is still alive",
                   pid, lastTrackedPid);
      } else if ((!seenTrackedProcess &&
                  waitForCompanionGrace(initialCompanionGrace)) ||
                 sawInaccessibleCandidate || rootScopeUnavailable) {
        useKillPoll = true;
        log::debug("root process {} completed before its detached companion "
                   "was visible; waiting briefly for launch token '{}'",
                   pid, launchToken.toStdString());
      } else {
        log::debug("process {} completed immediately", pid);
        return finishCompleted();
      }
    }
    if (probe < 0 && errno == ECHILD) {
      useKillPoll = true;
      log::debug("process {} is detached, using kill(0) polling", pid);
    }
  }

  const auto handleControl =
      [&](pid_t displayPid, const QString& displayName) -> std::optional<Result> {
    if (!callbacks.control) {
      return std::nullopt;
    }

    const Control control = callbacks.control();
    switch (control) {
    case Control::Continue:
      return std::nullopt;

    case Control::ForceUnlock:
    case Control::Cancel: {
      const bool cancelled = (control == Control::Cancel);

      // The teardown below exists only to clean up the wineprefix and let the
      // FUSE VFS unmount. A native, non-VFS game (OpenMW, usesVFS()==false)
      // has neither, and its launcher spawns the real engine as a child the
      // user is still playing — so force-unlock here must release the lock
      // without killing the process tree.
      if (!killTreeOnUnlock) {
        log::debug("waiting for {} {} by user; non-VFS game, releasing lock "
                   "but leaving the process tree running",
                   displayPid, cancelled ? "cancelled" : "force unlocked");
        return cancelled ? Result::Cancelled : Result::ForceUnlocked;
      }

      log::debug("waiting for {} {} by user, terminating", displayPid,
                 cancelled ? "cancelled" : "force unlocked");

      // The root pid (Proton wrapper) often doesn't carry WINEPREFIX in its
      // environ even though the actual game process below it does. Fall
      // through the known PIDs until we find one that has it set.
      QString effectivePrefix = winePrefix;
      bool scopeResolved = !rootPrefixUnavailable;
      std::unordered_set<pid_t> checkedScopePids;
      for (pid_t candidate : {displayPid, lastTrackedPid, pid}) {
        if (!effectivePrefix.isEmpty()) {
          break;
        }
        if (candidate > 0 && checkedScopePids.insert(candidate).second) {
          const std::uint64_t candidateStartTime =
              candidate == lastTrackedPid ? lastTrackedStartTime
              : candidate == pid          ? rootStartTime
                                          : UnknownProcessStartTime;
          if (candidateStartTime == 0 ||
              candidateStartTime == UnknownProcessStartTime ||
              processIdentityState(procContext, candidate,
                                   candidateStartTime) !=
                  IdentityState::Running) {
            continue;
          }
          const auto candidatePrefix =
              readProcEnv(procContext, candidate, "WINEPREFIX");
          if (processIdentityState(procContext, candidate,
                                   candidateStartTime) !=
              IdentityState::Running) {
            continue;
          }
          if (candidatePrefix.state == ProcEnvState::Found) {
            effectivePrefix = candidatePrefix.value;
            scopeResolved = true;
            break;
          } else if (candidatePrefix.state == ProcEnvState::Missing) {
            // A generation-checked process with no WINEPREFIX uses Wine's
            // default prefix when it is a Wine process.
            scopeResolved = true;
            break;
          }
        }
      }
      const bool killScopeUnavailable = !scopeResolved;

      const bool likelyDefaultWinePrefix =
          std::any_of(expected.cbegin(), expected.cend(), [](const QString& name) {
            return name.endsWith(QStringLiteral(".exe"),
                                 Qt::CaseInsensitive);
          }) ||
          displayName.contains(QStringLiteral("wine"), Qt::CaseInsensitive) ||
          displayName.contains(QStringLiteral("proton"), Qt::CaseInsensitive) ||
          lastTrackedName.endsWith(QStringLiteral(".exe"),
                                   Qt::CaseInsensitive);
      if (effectivePrefix.isEmpty() && !killScopeUnavailable &&
          likelyDefaultWinePrefix) {
        // Wine without WINEPREFIX uses ~/.wine. Resolve that scope explicitly
        // so Force Unlock does not certify cleanup while its wineserver lives.
        effectivePrefix =
            QDir(QDir::homePath()).absoluteFilePath(QStringLiteral(".wine"));
      }

      // Take down the whole descendant tree first, then SIGKILL wineserver so
      // Proton's session manager cannot keep the prefix open. Never report a
      // successful unlock when procfs prevented us from identifying or
      // verifying the processes that keep the VFS in use.
      bool terminated = !killScopeUnavailable;
      const auto rootState =
          processIdentityState(procContext, pid, rootStartTime);
      if (rootState == IdentityState::Running) {
        terminated =
            killProcessTree(pid, rootStartTime, procContext) && terminated;
      } else if (rootState == IdentityState::Unknown) {
        terminated = false;
      }

      if (lastTrackedPid > 0 && lastTrackedPid != pid) {
        const auto trackedState = processIdentityState(
            procContext, lastTrackedPid, lastTrackedStartTime);
        if (trackedState == IdentityState::Running) {
          terminated =
              killProcessTree(lastTrackedPid, lastTrackedStartTime,
                              procContext) &&
              terminated;
        } else if (trackedState == IdentityState::Unknown) {
          terminated = false;
        }
      }
      terminated =
          killWineserverForPrefix(effectivePrefix, procContext) && terminated;

      // Expected names are a discovery policy, not a destructive-cleanup
      // boundary. Tear down every exact-token process, including detached
      // differently named workers, before allowing the VFS to unmount.
      terminated =
          killProcessesForLaunchToken(procContext, launchToken,
                                      rootStartTime) &&
          terminated;

      auto remainingTree = findTrackedProcess(
          procContext, pid, rootStartTime, expected, launchToken);
      auto remainingScoped = findGameProcessForLaunch(
          procContext, expected, effectivePrefix, launchToken, pid,
          rootStartTime);
      if (remainingTree.pid > 0) {
        terminated =
            killProcessTree(remainingTree.pid, remainingTree.startTime,
                            procContext) &&
            terminated;
      }
      if (remainingScoped.pid > 0 &&
          remainingScoped.pid != remainingTree.pid) {
        terminated =
            killProcessTree(remainingScoped.pid, remainingScoped.startTime,
                            procContext) &&
            terminated;
      }

      remainingTree = findTrackedProcess(procContext, pid, rootStartTime,
                                         expected, launchToken);
      remainingScoped = findGameProcessForLaunch(
          procContext, expected, effectivePrefix, launchToken, pid,
          rootStartTime);
      const auto remainingToken = findProcessesForLaunchToken(
          procContext, launchToken, rootStartTime);
      if (remainingTree.pid > 0 || remainingScoped.pid > 0 ||
          !remainingToken.processes.empty() || remainingTree.unavailable ||
          remainingScoped.unavailable || remainingToken.unavailable) {
        terminated = false;
      }

      if (!terminated) {
        log::error("could not verify termination of the process lifetime for "
                   "root {}; keeping VFS cleanup blocked",
                   pid);
        return Result::Error;
      }

      if (exitCode != nullptr) {
        *exitCode = 1;
      }
      return cancelled ? Result::Cancelled : Result::ForceUnlocked;
    }

    case Control::Error:
      log::debug("unexpected lock result while waiting for {}", pid);
      return Result::Error;
    }

    return Result::Error;
  };

  while (true) {
    if (rootCompletion) {
      // Synchronous waits may run on the QProcess-owning thread. Pump that
      // event loop before reading the shared state even on procfs-error paths
      // whose early continue would otherwise starve the queued finished
      // signal and lose the terminal status until after this wait returns.
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      const auto root = rootCompletion->snapshot();
      if (root.state == RootProcessCompletion::State::Failed) {
        return finish(Result::Error);
      }
      if (root.state == RootProcessCompletion::State::Exited &&
          exitCode != nullptr && *exitCode == invalidExitCode) {
        *exitCode = root.exitCode;
      }
    }
    refreshRootScope();

    // Once a detached or adopted process moves us to kill-polling, continue
    // attempting to reap an owned root so its real exit status is retained.
    // ECHILD means another owner reaped it; leave the status explicitly
    // unknown rather than fabricating success.
    if (!rootCompletion && useKillPoll && exitCode != nullptr &&
        *exitCode == invalidExitCode) {
      int rootStatus = 0;
      if (callbacks.waitpidAttempted) {
        callbacks.waitpidAttempted(pid);
      }
      const pid_t rootWait = ::waitpid(pid, &rootStatus, WNOHANG);
      if (rootWait == pid) {
        *exitCode = exitCodeFromWaitStatus(rootStatus);
      }
    }

    QString trackedName;
    pid_t displayPid = pid;
    const auto rootComm = readProcComm(procContext, pid);
    QString displayName = rootComm.state == ProcState::Available
                              ? rootComm.value
                              : QString{};
    pid_t tracked = 0;
    std::uint64_t trackedStartTime = UnknownProcessStartTime;
    bool retainedStableIdentity = false;
    const bool hasExactTrackedIdentity =
        lastTrackedPid > 0 && lastTrackedStartTime != 0 &&
        lastTrackedStartTime != UnknownProcessStartTime;
    IdentityState directTrackedState = IdentityState::Exited;
    if (hasExactTrackedIdentity) {
      directTrackedState = processIdentityState(
          procContext, lastTrackedPid, lastTrackedStartTime);
      if (directTrackedState == IdentityState::Running) {
        // An exact PID/starttime pair is a complete lifetime anchor. Poll it
        // directly instead of enumerating all of /proc every 50 ms.
        tracked = lastTrackedPid;
        trackedStartTime = lastTrackedStartTime;
        trackedName = lastTrackedName;
        retainedStableIdentity = true;
        sawInaccessibleCandidate = false;
      } else if (directTrackedState == IdentityState::Unknown) {
        sawInaccessibleCandidate = true;
      }
    } else if (lastTrackedPid > 0) {
      // A discovered process without a captured generation is not stable yet.
      sawInaccessibleCandidate = true;
    }

    const auto discoveryNow = std::chrono::steady_clock::now();
    const bool transitionScan =
        hasExactTrackedIdentity &&
        directTrackedState == IdentityState::Exited;
    const bool discoveryDue = discoveryNow >= nextDiscoveryAt;
    if (tracked == 0 && (transitionScan || discoveryDue)) {
      // Unavailability belongs to the newest complete acquisition attempt.
      // Preserve it while scans are deliberately skipped so it cannot become
      // a false "no match" decision between cadence ticks.
      tracked = discoverProcess(&trackedName, &trackedStartTime);
    }
    if (tracked > 0) {
      sawInaccessibleCandidate = false;
      if (!seenTrackedProcess || tracked != lastTrackedPid) {
        log::info("process runner: root pid {} adopted lifetime pid {} ({})", pid,
                  tracked, trackedName.toStdString());
      }
      seenTrackedProcess = true;
      lastTrackedPid = tracked;
      if (!retainedStableIdentity) {
        lastTrackedStartTime = trackedStartTime;
        if (trackedStartTime == UnknownProcessStartTime) {
          sawInaccessibleCandidate = true;
        }
      }
      lastTrackedName = trackedName;
      companionDeadline.reset();
      clearProcUnavailable();
      displayPid = tracked;
      displayName = trackedName;
    } else if (seenTrackedProcess) {
      // The tracked process is no longer a descendant of the root PID. This
      // can happen when:
      //  a) The root (proton) exits and wine/game processes get reparented
      //  b) A launcher .exe (nvse_loader, skse_loader, f4se_loader) exits
      //     after spawning the actual game (FalloutNV.exe, SkyrimSE.exe,
      //     Fallout4.exe)
      //
      // If the last tracked PID is still alive, keep polling it directly.
      const auto trackedState =
          processIdentityState(procContext, lastTrackedPid,
                               lastTrackedStartTime);
      if (trackedState != IdentityState::Exited) {
        displayPid = lastTrackedPid;
        const auto trackedComm = readProcComm(procContext, lastTrackedPid);
        displayName = trackedComm.state == ProcState::Available
                          ? trackedComm.value
                          : QString{};
        if (trackedState == IdentityState::Running) {
          clearProcUnavailable();
        }
      } else {
        // A companion extends the selected executable's lifetime; it never
        // replaces it. If the root still exists (or procfs cannot currently
        // prove otherwise), return to tracking the root and give a later
        // phase a fresh discovery window only after the root also exits.
        lastTrackedPid = 0;
        lastTrackedStartTime = 0;
        const auto rootState =
            processIdentityState(procContext, pid, rootStartTime);
        if (rootState == IdentityState::Running) {
          companionDeadline.reset();
          clearProcUnavailable();
          useKillPoll = true;
          displayPid = pid;
          const auto currentRootComm = readProcComm(procContext, pid);
          displayName = currentRootComm.state == ProcState::Available
                            ? currentRootComm.value
                            : QString{};
        } else if (rootState == IdentityState::Unknown) {
          sawInaccessibleCandidate = true;
          useKillPoll = true;
          displayPid = pid;
        } else if (waitForCompanionGrace(transitionCompanionGrace)) {
          useKillPoll = true;
          displayPid = pid;
          const auto currentRootComm = readProcComm(procContext, pid);
          displayName = currentRootComm.state == ProcState::Available
                            ? currentRootComm.value
                            : QString{};
        } else {
          if (sawInaccessibleCandidate || rootScopeUnavailable) {
            if (procUnavailableExpired()) {
              log::error("procfs remained unavailable while resolving the "
                         "lifetime of root {}",
                         pid);
              return finish(Result::Error);
            }
            useKillPoll = true;
            QThread::msleep(50);
            continue;
          }
          clearProcUnavailable();
          log::debug("game processes for root {} exited; releasing lock "
                     "(wineserver may linger in background)",
                     pid);
          return finishCompleted();
        }
      }
    }

    if (callbacks.updateProcess) {
      callbacks.updateProcess(std::max<pid_t>(0, displayPid), displayName);
    }

    // Poll controls before any bounded companion-discovery continuation. In
    // particular, an already-exited launcher can spend several seconds
    // waiting for its detached child to appear; Cancel and Force Unlock must
    // remain responsive throughout that window.
    if (const auto controlled = handleControl(displayPid, displayName)) {
      return finish(*controlled);
    }

    if (useKillPoll) {
      // Poll for process existence via kill(pid, 0). When we have a tracked
      // game PID, monitor that instead of the root (proton) PID which may
      // have already exited.
      const pid_t pollPid =
          (seenTrackedProcess && lastTrackedPid > 0) ? lastTrackedPid : pid;
      const std::uint64_t pollStartTime =
          (seenTrackedProcess && lastTrackedPid > 0) ? lastTrackedStartTime
                                                     : rootStartTime;
      const auto pollState =
          processIdentityState(procContext, pollPid, pollStartTime);
      if (pollState == IdentityState::Unknown) {
        // A transient procfs/stat failure is uncertainty, not completion.
        if (procUnavailableExpired()) {
          log::error("procfs remained unavailable while polling process {}",
                     pollPid);
          return finish(Result::Error);
        }
        QThread::msleep(50);
        continue;
      }
      if (pollState == IdentityState::Running) {
        if (pollPid == pid) {
          rootExitTransitionScanned = false;
        }
        clearProcUnavailable();
      }
      if (pollState == IdentityState::Exited) {
        if (pollPid == pid && rootCompletion) {
          const auto root = rootCompletion->snapshot();
          if (root.state == RootProcessCompletion::State::Failed) {
            return finish(Result::Error);
          }
          if (root.state == RootProcessCompletion::State::Running) {
            // QProcess has observed SIGCHLD but its owning event loop has not
            // delivered finished yet. /proc disappearance alone cannot
            // authorize completion or lose the real child status.
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(10);
            continue;
          }
          if (exitCode != nullptr && *exitCode == invalidExitCode) {
            *exitCode = root.exitCode;
          }
        }
        // The polled process exited. Rescan the prefix for any other matching
        // game executable (launcher .exe's like f4se_loader exit after
        // spawning the real game binary, and Proton can reparent that binary
        // out of our root's subtree).
        if (lastTrackedPid > 0) {
          QString rescanName;
          std::uint64_t rescannedStartTime = UnknownProcessStartTime;
          const pid_t rescanned =
              discoverProcess(&rescanName, &rescannedStartTime);
          if (rescanned > 0 && rescanned != pollPid) {
            sawInaccessibleCandidate = false;
            log::info("polled process {} exited, resumed tracking game {}: {}", pollPid,
                      rescanned, rescanName.toStdString());
            lastTrackedPid = rescanned;
            lastTrackedStartTime = rescannedStartTime;
            lastTrackedName = rescanName;
            companionDeadline.reset();
            continue;
          }
        } else if (pollPid == pid && !rootExitTransitionScanned) {
          // The root can exit between cadence scans. Force one fresh complete
          // acquisition attempt before treating that anchor exit as terminal.
          QString transitionName;
          std::uint64_t transitionStartTime = UnknownProcessStartTime;
          const pid_t transition =
              discoverProcess(&transitionName, &transitionStartTime);
          rootExitTransitionScanned = true;
          if (transition > 0) {
            seenTrackedProcess = true;
            lastTrackedPid = transition;
            lastTrackedStartTime = transitionStartTime;
            lastTrackedName = transitionName;
            companionDeadline.reset();
            continue;
          }
        }
        if (lastTrackedPid > 0) {
          lastTrackedPid = 0;
          lastTrackedStartTime = 0;
          const auto rootState =
              processIdentityState(procContext, pid, rootStartTime);
          if (rootState == IdentityState::Running) {
            companionDeadline.reset();
            clearProcUnavailable();
            QThread::msleep(50);
            continue;
          }
          if (rootState == IdentityState::Unknown) {
            sawInaccessibleCandidate = true;
          }
        }

        const bool waitingForInitial = !seenTrackedProcess;
        if (waitForCompanionGrace(waitingForInitial ? initialCompanionGrace
                                                     : transitionCompanionGrace)) {
          QThread::msleep(50);
          continue;
        }
        if (sawInaccessibleCandidate || rootScopeUnavailable) {
          if (procUnavailableExpired()) {
            log::error("procfs remained unavailable while resolving the "
                       "lifetime of root {}",
                       pid);
            return finish(Result::Error);
          }
          QThread::msleep(50);
          continue;
        }
        clearProcUnavailable();
        // No game process remains — do NOT block on wineserver. Preserve an
        // unknown root status as unknown when somebody else reaped it.
        log::debug("process {} completed", pollPid);
        return finishCompleted();
      }
    } else if (!rootCompletion) {
      int status = 0;
      if (callbacks.waitpidAttempted) {
        callbacks.waitpidAttempted(pid);
      }
      const pid_t waitResult = ::waitpid(pid, &status, WNOHANG);

      if (waitResult == 0) {
        rootExitTransitionScanned = false;
        refreshKnownRunningRootStart();
        refreshRootScope();
        clearProcUnavailable();
      }

      if (waitResult == pid) {
        if (exitCode != nullptr) {
          *exitCode = exitCodeFromWaitStatus(status);
        }
        // Root process (proton) exited. If we have a tracked game process
        // that is still alive, switch to polling the game PID directly
        // rather than declaring the game finished.
        if (seenTrackedProcess && lastTrackedPid > 0 &&
            processIdentityState(procContext, lastTrackedPid,
                                 lastTrackedStartTime) !=
                IdentityState::Exited) {
          log::debug("root process {} exited but tracked game {} still alive, "
                     "switching to kill-poll",
                     pid, lastTrackedPid);
          useKillPoll = true;
          continue;
        }
        QString detachedName;
        std::uint64_t detachedStartTime = UnknownProcessStartTime;
        const pid_t detached =
            discoverProcess(&detachedName, &detachedStartTime);
        rootExitTransitionScanned = true;
        if (detached > 0) {
          sawInaccessibleCandidate = false;
          seenTrackedProcess = true;
          lastTrackedPid     = detached;
          lastTrackedStartTime = detachedStartTime;
          lastTrackedName    = detachedName;
          companionDeadline.reset();
          useKillPoll = true;
          log::info("root process {} exited; adopted detached lifetime pid {} ({})",
                    pid, detached, detachedName.toStdString());
          continue;
        }
        if (!seenTrackedProcess &&
            waitForCompanionGrace(initialCompanionGrace)) {
          useKillPoll = true;
          continue;
        }
        if (sawInaccessibleCandidate || rootScopeUnavailable) {
          useKillPoll = true;
          if (procUnavailableExpired()) {
            log::error("procfs remained unavailable while resolving the "
                       "lifetime of root {}",
                       pid);
            return finish(Result::Error);
          }
          continue;
        }
        clearProcUnavailable();
        log::debug("process {} completed", pid);
        return finishCompleted();
      }

      if (waitResult < 0) {
        if (errno == EINTR) {
          continue;
        }
        if (errno == ECHILD) {
          // Process was reparented, switch to kill polling.
          useKillPoll = true;
          continue;
        }
        log::error("failed waiting for {}, errno={}", pid, errno);
        return finish(Result::Error);
      }
    }

    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(50);
  }
}

Result waitForPidAndNotify(pid_t pid, const Callbacks& callbacks,
                           const QStringList& expected, bool killTreeOnUnlock,
                           const Completion& completion,
                           const QString& launchToken,
                           bool expectDetachedCompanion,
                           std::uint64_t rootStartTime,
                           const std::shared_ptr<RootProcessCompletion>&
                               rootCompletion)
{
  std::uint32_t exitCode = static_cast<std::uint32_t>(-1);
  const Result result = waitForPid(pid, &exitCode, callbacks, expected,
                                   killTreeOnUnlock, launchToken,
                                   expectDetachedCompanion, rootStartTime,
                                   rootCompletion);
  if (completion) {
    completion(result, exitCode);
  }
  return result;
}

} // namespace process_lifetime
