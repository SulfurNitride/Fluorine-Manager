#include "processrunner.h"
#include "env.h"
#include "envmodule.h"
#include "instancemanager.h"
#include "iuserinterface.h"
#include "organizercore.h"

#include <iplugingame.h>
#include <log.h>
#include <report.h>
#include <uibase/utility.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QProcess>
#include <QThread>

#include <cerrno>
#include <deque>
#include <dirent.h>
#include <fstream>
#include <csignal>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unordered_map>
#include <unordered_set>

using namespace MOBase;

void adjustForVirtualized(const IPluginGame* game, spawn::SpawnParameters& sp,
                          const Settings& settings)
{
  const QString modsPath = settings.paths().mods();

  // Check if this is a request with either an executable or a working
  // directory under our mods folder; if so, start the process in a
  // virtualized "environment" with the appropriate paths fixed:
  // (i.e. mods/FNIS/path/exe => game/data/path/exe)
  QString cwdPath         = sp.currentDirectory.absolutePath();
  QString trailedModsPath = modsPath;
  if (!trailedModsPath.endsWith('/')) {
    trailedModsPath = trailedModsPath + '/';
  }
  bool virtualizedCwd = cwdPath.startsWith(trailedModsPath, Qt::CaseInsensitive);
  QString binPath     = sp.binary.absoluteFilePath();
  bool virtualizedBin = binPath.startsWith(trailedModsPath, Qt::CaseInsensitive);
  if (!virtualizedCwd && !virtualizedBin) {
    return;
  }

  if (virtualizedCwd) {
    int cwdOffset       = cwdPath.indexOf('/', trailedModsPath.length());
    QString adjustedCwd = cwdPath.mid(cwdOffset, -1);
    cwdPath             = game->dataDirectory().absolutePath();
    if (cwdOffset >= 0)
      cwdPath += adjustedCwd;
  }

  if (virtualizedBin) {
    int binOffset       = binPath.indexOf('/', trailedModsPath.length());
    QString adjustedBin = binPath.mid(binOffset, -1);
    binPath             = game->dataDirectory().absolutePath();
    if (binOffset >= 0)
      binPath += adjustedBin;
  }

  // FUSE is already mounted on Linux — resolve paths directly without
  // launching through MO2-core (which would fail in the Proton prefix).
  //
  // Root Builder deploys Root/ contents to the game directory root,
  // stripping the "Root/" prefix.  Fix paths that were remapped to
  // <dataDir>/Root/... so they point to <gameDir>/... instead.
  // Also handle direct mods/.../Root/ paths (not just dataDir/Root/).
  const QString gameDir = game->gameDirectory().absolutePath();
  const QString dataDir = game->dataDirectory().absolutePath();

  auto normalizeRootPath = [&](QString& path) {
    const QString rootWithSlash = dataDir + QStringLiteral("/Root/");
    const QString rootExact     = dataDir + QStringLiteral("/Root");
    if (path.startsWith(rootWithSlash, Qt::CaseInsensitive)) {
      const QString after = path.mid(rootWithSlash.length());
      path = after.isEmpty() ? gameDir : gameDir + QStringLiteral("/") + after;
      return true;
    }
    if (path.compare(rootExact, Qt::CaseInsensitive) == 0) {
      path = gameDir;
      return true;
    }
    return false;
  };

  bool binNormalized = normalizeRootPath(binPath);
  bool cwdNormalized = normalizeRootPath(cwdPath);

  if (binNormalized) {
    log::info("Root Builder: rewrote binary -> '{}'", binPath);
  }
  if (cwdNormalized) {
    log::info("Root Builder: rewrote start-in -> '{}'", cwdPath);
  }

  // If neither was caught by the dataDir/Root/ check, the path might still be
  // the original mods/.../Root/ path (not yet remapped). This happens when
  // the first remapping above produced something that didn't match the
  // dataDir/Root pattern.
  if (!binNormalized && binPath.startsWith(trailedModsPath, Qt::CaseInsensitive)) {
    int rootIdx =
        binPath.indexOf("/Root/", trailedModsPath.length(), Qt::CaseInsensitive);
    if (rootIdx < 0)
      rootIdx =
          binPath.indexOf("/Root", trailedModsPath.length(), Qt::CaseInsensitive);
    if (rootIdx >= 0) {
      int afterRootStart = rootIdx + 5;  // skip "/Root"
      if (afterRootStart < binPath.length() && binPath[afterRootStart] == '/')
        ++afterRootStart;
      const QString afterRoot = binPath.mid(afterRootStart);
      const QString modRoot   = binPath.left(rootIdx + 5);

      binPath = afterRoot.isEmpty() ? gameDir
                                    : gameDir + QStringLiteral("/") + afterRoot;
      log::info("Root Builder: rewrote binary (mod path) -> '{}'", binPath);

      if (!cwdNormalized && cwdPath.startsWith(modRoot, Qt::CaseInsensitive)) {
        int cwdAfterStart = modRoot.length();
        if (cwdAfterStart < cwdPath.length() && cwdPath[cwdAfterStart] == '/')
          ++cwdAfterStart;
        const QString cwdAfter = cwdPath.mid(cwdAfterStart);
        cwdPath = cwdAfter.isEmpty() ? gameDir
                                     : gameDir + QStringLiteral("/") + cwdAfter;
        log::info("Root Builder: rewrote start-in (mod path) -> '{}'", cwdPath);
      }
    }
  }

  sp.binary = QFileInfo(binPath);
  sp.currentDirectory.setPath(cwdPath);
}

enum class Interest
{
  None = 0,
  Weak,
  Strong
};

QString toString(Interest i)
{
  switch (i) {
  case Interest::Weak:
    return "weak";

  case Interest::Strong:
    return "strong";

  case Interest::None:
  default:
    return "no";
  }
}

pid_t handleToPid(HANDLE h)
{
  return static_cast<pid_t>(reinterpret_cast<intptr_t>(h));
}

QString readProcComm(pid_t pid)
{
  QFile f(QString("/proc/%1/comm").arg(pid));
  if (!f.open(QIODevice::ReadOnly)) {
    return {};
  }

  return QString::fromUtf8(f.readAll()).trimmed();
}

// Read /proc/<pid>/cmdline (NUL-separated) and return all argv entries.
QStringList readProcCmdline(pid_t pid)
{
  QFile f(QString("/proc/%1/cmdline").arg(pid));
  if (!f.open(QIODevice::ReadOnly)) {
    return {};
  }

  const QByteArray data = f.readAll();
  QStringList parts;
  for (const QByteArray& part : data.split('\0')) {
    if (!part.isEmpty()) {
      parts.push_back(QString::fromUtf8(part));
    }
  }
  return parts;
}

// Read a specific environment variable from /proc/<pid>/environ.
// The environ file is NUL-separated KEY=VALUE pairs.
QString readProcEnvVar(pid_t pid, const char* varName)
{
  QFile f(QString("/proc/%1/environ").arg(pid));
  if (!f.open(QIODevice::ReadOnly)) {
    return {};
  }

  const QByteArray data   = f.readAll();
  const QByteArray prefix = QByteArray(varName) + '=';
  for (const QByteArray& entry : data.split('\0')) {
    if (entry.startsWith(prefix)) {
      return QString::fromUtf8(entry.mid(prefix.size()));
    }
  }
  return {};
}

// Find a wineserver process owned by the current user that belongs to the
// given WINEPREFIX. When expectedPrefix is empty, returns the first
// wineserver owned by us (legacy behaviour).
//
// Wineserver stays alive as long as any Wine process in the prefix is
// running, making it the most reliable way to detect when a game has truly
// exited — even when launcher .exe's (nvse_loader, skse_loader, etc.) exit
// before the actual game.
pid_t findWineserver(const QString& expectedPrefix = {})
{
  const uid_t myUid = ::getuid();
  DIR* proc         = opendir("/proc");
  if (!proc)
    return 0;

  pid_t result         = 0;
  struct dirent* entry = nullptr;
  while ((entry = readdir(proc)) != nullptr) {
    if (entry->d_type != DT_DIR && entry->d_type != DT_UNKNOWN)
      continue;
    const char* name = entry->d_name;
    if (*name == '\0' || !std::isdigit(static_cast<unsigned char>(*name)))
      continue;

    const pid_t pid    = static_cast<pid_t>(std::strtol(name, nullptr, 10));
    const QString comm = readProcComm(pid);
    if (comm != "wineserver")
      continue;

    struct stat st;
    if (::stat(QString("/proc/%1").arg(pid).toStdString().c_str(), &st) != 0 ||
        st.st_uid != myUid) {
      continue;
    }

    // If a prefix filter was given, verify this wineserver belongs to it.
    // A wineserver without WINEPREFIX in its environ is using the default
    // ~/.wine prefix, which is never the game prefix — skip it too.
    if (!expectedPrefix.isEmpty()) {
      const QString wsPrefix = readProcEnvVar(pid, "WINEPREFIX");
      if (wsPrefix.isEmpty() ||
          QDir(wsPrefix).canonicalPath() != QDir(expectedPrefix).canonicalPath()) {
        log::debug("skipping wineserver {} (prefix '{}' != expected '{}')", pid,
                   wsPrefix.toStdString(), expectedPrefix.toStdString());
        continue;
      }
    }

    result = pid;
    break;
  }
  closedir(proc);
  return result;
}

// Check whether any of the expected executable names appear in a process's
// comm or cmdline. Wine processes often show "wine64-preload" or "start.exe"
// in /proc/comm while the actual game executable only appears in cmdline.
// Also handles the 15-char TASK_COMM_LEN truncation in /proc/comm.
bool processMatchesExpected(pid_t pid, const QStringList& expected,
                            QString* matchedNameOut)
{
  // 1. Check /proc/comm (fast path).
  const QString comm = readProcComm(pid);
  if (!comm.isEmpty()) {
    const QString lower = comm.toLower();
    for (const QString& exp : expected) {
      if (lower == exp) {
        if (matchedNameOut)
          *matchedNameOut = comm;
        return true;
      }
      // Handle TASK_COMM_LEN truncation (15 chars): if the expected name is
      // longer than 15 chars, check if comm matches its first 15 chars.
      if (exp.size() > 15 && lower == exp.left(15)) {
        if (matchedNameOut)
          *matchedNameOut = exp;
        return true;
      }
    }
  }

  // 2. Check /proc/cmdline — Wine/Proton processes carry the .exe name here
  //    even when comm shows wine64-preloader or start.exe.
  const QStringList cmdline = readProcCmdline(pid);
  for (const QString& arg : cmdline) {
    const QString normalized = QString(arg).replace('\\', '/');
    const QString base       = QFileInfo(normalized).fileName().toLower();
    if (expected.contains(base)) {
      if (matchedNameOut)
        *matchedNameOut = QFileInfo(normalized).fileName();
      return true;
    }
  }

  return false;
}

std::unordered_map<pid_t, std::vector<pid_t>> buildProcChildrenMap()
{
  std::unordered_map<pid_t, std::vector<pid_t>> children;
  DIR* proc = opendir("/proc");
  if (!proc) {
    return children;
  }

  struct dirent* entry = nullptr;
  while ((entry = readdir(proc)) != nullptr) {
    if (entry->d_type != DT_DIR && entry->d_type != DT_UNKNOWN) {
      continue;
    }

    const char* name = entry->d_name;
    if (*name == '\0' || !std::isdigit(static_cast<unsigned char>(*name))) {
      continue;
    }

    const pid_t pid = static_cast<pid_t>(std::strtol(name, nullptr, 10));
    std::ifstream status(QString("/proc/%1/status").arg(pid).toStdString());
    if (!status.is_open()) {
      continue;
    }

    std::string line;
    pid_t ppid = 0;
    while (std::getline(status, line)) {
      if (line.rfind("PPid:", 0) == 0) {
        ppid = static_cast<pid_t>(std::strtol(line.c_str() + 5, nullptr, 10));
        break;
      }
    }

    if (ppid > 0) {
      children[ppid].push_back(pid);
    }
  }

  closedir(proc);
  return children;
}

std::unordered_set<pid_t> collectDescendants(
    pid_t root, const std::unordered_map<pid_t, std::vector<pid_t>>& children)
{
  std::unordered_set<pid_t> out;
  std::deque<pid_t> q;
  q.push_back(root);

  while (!q.empty()) {
    const pid_t cur = q.front();
    q.pop_front();

    const auto it = children.find(cur);
    if (it == children.end()) {
      continue;
    }

    for (pid_t child : it->second) {
      if (out.insert(child).second) {
        q.push_back(child);
      }
    }
  }

  return out;
}

QStringList buildExpectedExecutables(const QFileInfo& binary, const QString& arguments)
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

  log::debug("buildExpectedExecutables: returning [{}]",
             expected.join(", ").toStdString());
  return expected;
}

pid_t findTrackedProcess(pid_t rootPid, const QStringList& expected,
                         QString* trackedNameOut)
{
  if (expected.isEmpty()) {
    return 0;
  }

  const auto children    = buildProcChildrenMap();
  const auto descendants = collectDescendants(rootPid, children);
  if (descendants.empty()) {
    return 0;
  }

  pid_t best = 0;
  QString bestName;
  for (pid_t pid : descendants) {
    QString matched;
    if (processMatchesExpected(pid, expected, &matched)) {
      best     = pid;
      bestName = matched;
      break;
    }
  }

  if (best > 0 && trackedNameOut) {
    *trackedNameOut = bestName;
  }
  return best;
}

// Scan every process owned by the current user for one whose comm or cmdline
// matches an expected game executable (e.g. FalloutNV.exe, SkyrimSE.exe).
// Used as a fallback after the immediate descendant tree loses the game —
// Proton's session manager can reparent game processes outside of our root
// PID's subtree, so a plain-descendant walk misses them. Only processes
// inside the given WINEPREFIX are considered so we don't latch onto an
// unrelated Wine/Proton session.
pid_t findGameProcessInPrefix(const QStringList& expected, const QString& winePrefix,
                              QString* matchedNameOut)
{
  if (expected.isEmpty()) {
    return 0;
  }

  const uid_t myUid = ::getuid();
  DIR* proc         = opendir("/proc");
  if (!proc) {
    return 0;
  }

  QString expectedPrefixCanon;
  if (!winePrefix.isEmpty()) {
    expectedPrefixCanon = QDir(winePrefix).canonicalPath();
  }

  pid_t best           = 0;
  struct dirent* entry = nullptr;
  while ((entry = readdir(proc)) != nullptr) {
    if (entry->d_type != DT_DIR && entry->d_type != DT_UNKNOWN)
      continue;
    const char* name = entry->d_name;
    if (*name == '\0' || !std::isdigit(static_cast<unsigned char>(*name)))
      continue;

    const pid_t pid = static_cast<pid_t>(std::strtol(name, nullptr, 10));

    struct stat st;
    if (::stat(QString("/proc/%1").arg(pid).toStdString().c_str(), &st) != 0 ||
        st.st_uid != myUid) {
      continue;
    }

    QString matched;
    if (!processMatchesExpected(pid, expected, &matched)) {
      continue;
    }

    // Constrain to the same WINEPREFIX so we don't latch onto an unrelated
    // Wine process (another instance, winetricks, etc.).
    if (!expectedPrefixCanon.isEmpty()) {
      const QString pidPrefix = readProcEnvVar(pid, "WINEPREFIX");
      if (pidPrefix.isEmpty())
        continue;
      if (QDir(pidPrefix).canonicalPath() != expectedPrefixCanon)
        continue;
    }

    best = pid;
    if (matchedNameOut)
      *matchedNameOut = matched;
    break;
  }
  closedir(proc);
  return best;
}

// Best-effort "hard kill" of the wineserver (and every Wine process it owns)
// for the given prefix. Used when the user clicks Unlock in the lock dialog —
// Proton's session manager can keep wineserver alive for tens of seconds
// after the game exits, and that's exactly what the user is asking us to
// short-circuit.
void killWineserverForPrefix(const QString& winePrefix)
{
  if (winePrefix.isEmpty()) {
    log::debug("killWineserverForPrefix: skipping (no WINEPREFIX resolved)");
    return;
  }

  const pid_t ws = findWineserver(winePrefix);
  if (ws > 0) {
    log::info("sending SIGTERM to wineserver {} for prefix '{}'", ws,
              winePrefix.toStdString());
    if (::kill(ws, SIGTERM) != 0 && errno != ESRCH) {
      log::warn("SIGTERM on wineserver {} failed, errno={}", ws, errno);
    }
    // Give wineserver a short window to tear down cleanly, then SIGKILL if
    // it's still hanging around.
    for (int i = 0; i < 10; ++i) {
      if (::kill(ws, 0) != 0 && errno == ESRCH) {
        break;
      }
      QThread::msleep(100);
    }
    if (::kill(ws, 0) == 0) {
      log::warn("wineserver {} did not exit on SIGTERM, sending SIGKILL", ws);
      ::kill(ws, SIGKILL);
    }
  } else {
    log::debug("killWineserverForPrefix: no wineserver found for '{}'",
               winePrefix.toStdString());
  }
}

// SIGTERM every descendant of |root| (and root itself), wait briefly, then
// SIGKILL any survivor. Used on force-unlock so launcher .exe grandchildren
// (skse_loader → SkyrimSE.exe → audio/physics workers) all go down even when
// wineserver wasn't reachable.
void killProcessTree(pid_t root)
{
  if (root <= 0) {
    return;
  }

  const auto children    = buildProcChildrenMap();
  auto descendants       = collectDescendants(root, children);
  descendants.insert(root);

  for (pid_t p : descendants) {
    if (::kill(p, SIGTERM) != 0 && errno != ESRCH) {
      log::debug("SIGTERM on {} failed, errno={}", p, errno);
    }
  }

  for (int i = 0; i < 5; ++i) {
    bool anyAlive = false;
    for (pid_t p : descendants) {
      if (::kill(p, 0) == 0) {
        anyAlive = true;
        break;
      }
    }
    if (!anyAlive) {
      return;
    }
    QThread::msleep(100);
  }

  for (pid_t p : descendants) {
    if (::kill(p, 0) == 0) {
      log::warn("process {} did not exit on SIGTERM, sending SIGKILL", p);
      ::kill(p, SIGKILL);
    }
  }
}

DWORD exitCodeFromWaitStatus(int status)
{
  if (WIFEXITED(status)) {
    return static_cast<DWORD>(WEXITSTATUS(status));
  }

  if (WIFSIGNALED(status)) {
    return static_cast<DWORD>(128 + WTERMSIG(status));
  }

  return 0;
}

ProcessRunner::Results waitForPid(pid_t pid, LPDWORD exitCode,
                                  UILocker::Session* ls, const QStringList& expected)
{
  if (pid <= 0) {
    return ProcessRunner::Error;
  }

  // Capture the WINEPREFIX from the launched process so we can filter
  // wineserver lookups to the correct prefix. Without this, Fluorine would
  // track ANY wineserver owned by the user (e.g. one running winecfg under
  // ~/.wine while the game uses a different prefix).
  const QString winePrefix = readProcEnvVar(pid, "WINEPREFIX");
  if (!winePrefix.isEmpty()) {
    log::debug("process {} has WINEPREFIX='{}'", pid, winePrefix.toStdString());
  }

  // startDetached() creates a non-child process, so waitpid() will fail with
  // ECHILD. Detect this on the first call and switch to kill(pid, 0) polling
  // which works for any process owned by the same user.
  bool useKillPoll = false;
  {
    int status        = 0;
    const pid_t probe = ::waitpid(pid, &status, WNOHANG);
    if (probe == pid) {
      if (exitCode != nullptr) {
        *exitCode = exitCodeFromWaitStatus(status);
      }
      log::debug("process {} completed immediately", pid);
      return ProcessRunner::Completed;
    }
    if (probe < 0 && errno == ECHILD) {
      useKillPoll = true;
      log::debug("process {} is detached, using kill(0) polling", pid);
    }
  }

  bool seenTrackedProcess = false;
  pid_t lastTrackedPid    = 0;

  while (true) {
    QString trackedName;
    pid_t displayPid       = pid;
    QString displayName    = readProcComm(pid);
    const pid_t tracked    = findTrackedProcess(pid, expected, &trackedName);
    if (tracked > 0) {
      if (!seenTrackedProcess || tracked != lastTrackedPid) {
        log::info("tracking game process {}: {}", tracked, trackedName.toStdString());
      }
      seenTrackedProcess = true;
      lastTrackedPid     = tracked;
      displayPid         = tracked;
      displayName        = trackedName;
    } else if (seenTrackedProcess) {
      // The tracked process is no longer a descendant of the root PID. This
      // can happen when:
      //  a) The root (proton) exits and wine/game processes get reparented
      //  b) A launcher .exe (nvse_loader, skse_loader, f4se_loader) exits
      //     after spawning the actual game (FalloutNV.exe, SkyrimSE.exe,
      //     Fallout4.exe)
      //
      // If the last tracked PID is still alive, keep polling it directly.
      if (lastTrackedPid > 0 && ::kill(lastTrackedPid, 0) == 0) {
        displayPid  = lastTrackedPid;
        displayName = readProcComm(lastTrackedPid);
      } else {
        // The previously tracked process is gone. Rescan the user's processes
        // for any of the expected game executables in the same WINEPREFIX —
        // Proton's session manager can reparent the game out of our
        // descendant tree. If we find one, track that.
        QString rescanName;
        const pid_t rescanned =
            findGameProcessInPrefix(expected, winePrefix, &rescanName);
        if (rescanned > 0 && rescanned != lastTrackedPid) {
          log::info("tracked process exited, resumed tracking game {}: {}",
                    rescanned, rescanName.toStdString());
          lastTrackedPid = rescanned;
          useKillPoll    = true;
          displayName    = rescanName;
          continue;
        }

        // No matching game process remains. Do NOT fall back to waiting for
        // wineserver — Proton's session manager keeps wineserver alive for
        // the prefix idle timeout (several seconds on modern Proton, much
        // longer under load), and blocking MO2's lock on that is
        // indistinguishable from a hang from the user's POV.
        if (exitCode != nullptr) {
          *exitCode = 0;
        }
        log::debug("game processes for root {} exited; releasing lock "
                   "(wineserver may linger in background)",
                   pid);
        return ProcessRunner::Completed;
      }
    }

    if (ls != nullptr) {
      ls->setInfo(static_cast<DWORD>(std::max<pid_t>(0, displayPid)), displayName);
    }

    if (useKillPoll) {
      // Poll for process existence via kill(pid, 0). When we have a tracked
      // game PID, monitor that instead of the root (proton) PID which may
      // have already exited.
      const pid_t pollPid =
          (seenTrackedProcess && lastTrackedPid > 0) ? lastTrackedPid : pid;
      if (::kill(pollPid, 0) != 0) {
        if (errno == ESRCH) {
          // The polled process exited. Rescan the prefix for any other
          // matching game executable (launcher .exe's like f4se_loader exit
          // after spawning the real game binary, and Proton can reparent
          // that binary out of our root's subtree).
          if (seenTrackedProcess) {
            QString rescanName;
            const pid_t rescanned =
                findGameProcessInPrefix(expected, winePrefix, &rescanName);
            if (rescanned > 0 && rescanned != pollPid) {
              log::info("polled process {} exited, resumed tracking game {}: {}",
                        pollPid, rescanned, rescanName.toStdString());
              lastTrackedPid = rescanned;
              continue;
            }
          }
          // No game process remains — do NOT block on wineserver.
          if (exitCode != nullptr) {
            *exitCode = 0;
          }
          log::debug("process {} completed", pollPid);
          return ProcessRunner::Completed;
        }
        // EPERM means the process exists but we can't signal it; keep
        // waiting.
        else if (errno != EPERM) {
          log::error("failed checking process {}, errno={}", pollPid, errno);
          return ProcessRunner::Error;
        }
      }
    } else {
      int status             = 0;
      const pid_t waitResult = ::waitpid(pid, &status, WNOHANG);

      if (waitResult == pid) {
        // Root process (proton) exited. If we have a tracked game process
        // that is still alive, switch to polling the game PID directly
        // rather than declaring the game finished.
        if (seenTrackedProcess && lastTrackedPid > 0 &&
            ::kill(lastTrackedPid, 0) == 0) {
          log::debug("root process {} exited but tracked game {} still alive, "
                     "switching to kill-poll",
                     pid, lastTrackedPid);
          useKillPoll = true;
          continue;
        }
        if (exitCode != nullptr) {
          *exitCode = exitCodeFromWaitStatus(status);
        }
        log::debug("process {} completed", pid);
        return ProcessRunner::Completed;
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
        return ProcessRunner::Error;
      }
    }

    if (ls != nullptr) {
      switch (UILocker::Session::result()) {
      case UILocker::StillLocked:
        break;

      case UILocker::ForceUnlocked:
      case UILocker::Cancelled: {
        const bool cancelled = (UILocker::Session::result() == UILocker::Cancelled);
        log::debug("waiting for {} {} by user, terminating", displayPid,
                   cancelled ? "cancelled" : "force unlocked");

        // The root pid (Proton wrapper) often doesn't carry WINEPREFIX in
        // its environ even though the actual game process below it does.
        // Fall through the known PIDs until we find one that has it set.
        QString effectivePrefix = winePrefix;
        for (pid_t candidate : {displayPid, lastTrackedPid, pid}) {
          if (!effectivePrefix.isEmpty()) {
            break;
          }
          if (candidate > 0) {
            effectivePrefix = readProcEnvVar(candidate, "WINEPREFIX");
          }
        }

        // Take down the whole descendant tree first — launcher .exe's
        // (skse_loader, nvse_loader, f4se_loader) often exit before the real
        // game does, leaving grandchildren that a single SIGTERM on
        // displayPid wouldn't reach. Then SIGKILL wineserver so Proton's
        // session manager can't keep the prefix open and the next launch
        // starts from a clean state.
        killProcessTree(pid);
        killWineserverForPrefix(effectivePrefix);

        // Signal abnormal termination so afterRun()'s plugin-sync gate
        // skips the prefix (we may have killed the game mid-write).
        if (exitCode != nullptr) {
          *exitCode = 1;
        }
        return cancelled ? ProcessRunner::Cancelled
                         : ProcessRunner::ForceUnlocked;
      }

      case UILocker::NoResult:
      default:
        log::debug("unexpected lock result while waiting for {}", pid);
        return ProcessRunner::Error;
      }
    }

    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(50);
  }
}

ProcessRunner::Results waitForProcess(HANDLE initialProcess, LPDWORD exitCode,
                                      UILocker::Session* ls,
                                      const QStringList& expected)
{
  return waitForPid(handleToPid(initialProcess), exitCode, ls, expected);
}

ProcessRunner::Results waitForProcesses(const std::vector<HANDLE>& initialProcesses,
                                        UILocker::Session* ls,
                                        const QStringList& expected)
{
  if (initialProcesses.empty()) {
    return ProcessRunner::Completed;
  }

  for (HANDLE h : initialProcesses) {
    DWORD ignored = 0;
    const auto r  = waitForPid(handleToPid(h), &ignored, ls, expected);
    if (r != ProcessRunner::Completed) {
      return r;
    }
  }

  return ProcessRunner::Completed;
}

ProcessRunner::ProcessRunner(OrganizerCore& core, IUserInterface* ui)
    : m_core(core), m_ui(ui),
      m_waitFlags(NoFlags), m_handle(INVALID_HANDLE_VALUE)
{
  // all processes started in ProcessRunner are hooked by default
  setHooked(true);
}

ProcessRunner& ProcessRunner::setBinary(const QFileInfo& binary)
{
  m_sp.binary = QFileInfo(MOBase::normalizePathForHost(binary.filePath()));
  return *this;
}

ProcessRunner& ProcessRunner::setArguments(const QString& arguments)
{
  m_sp.arguments = arguments;
  return *this;
}

ProcessRunner& ProcessRunner::setCurrentDirectory(const QDir& directory)
{
  m_sp.currentDirectory.setPath(MOBase::normalizePathForHost(directory.path()));
  return *this;
}

ProcessRunner& ProcessRunner::setSteamID(const QString& steamID)
{
  m_sp.steamAppID = steamID;
  return *this;
}

ProcessRunner& ProcessRunner::setCustomOverwrite(const QString& customOverwrite)
{
  m_customOverwrite = customOverwrite;
  return *this;
}

ProcessRunner& ProcessRunner::setForcedLibraries(const ForcedLibraries& forcedLibraries)
{
  m_forcedLibraries = forcedLibraries;
  return *this;
}

ProcessRunner& ProcessRunner::setProfileName(const QString& profileName)
{
  m_profileName = profileName;
  return *this;
}

ProcessRunner& ProcessRunner::setWaitForCompletion(WaitFlags flags,
                                                   UILocker::Reasons reason)
{
  m_waitFlags  = flags;
  m_lockReason = reason;

  if (m_waitFlags.testFlag(WaitForRefresh) && !m_waitFlags.testFlag(TriggerRefresh)) {
    log::warn("process runner: WaitForRefresh without TriggerRefresh "
              "makes no sense, will be ignored");
  }

  return *this;
}

ProcessRunner& ProcessRunner::setHooked(bool b)
{
  m_sp.hooked = b;
  return *this;
}

ProcessRunner& ProcessRunner::setFromFile(QWidget* parent, const QFileInfo& targetInfo)
{
  if (!parent && m_ui) {
    parent = m_ui->mainWindow();
  }

  // if the file is a .exe, start it directly; if it's anything else, ask the
  // shell to start it
  const auto fec = spawn::getFileExecutionContext(parent, targetInfo);

  switch (fec.type) {
  case spawn::FileExecutionTypes::Executable: {
    setBinary(fec.binary);
    setArguments(fec.arguments);
    setCurrentDirectory(targetInfo.absoluteDir());
    break;
  }

  case spawn::FileExecutionTypes::Other:
  default: {
    m_shellOpen = targetInfo;
    setHooked(false);
    break;
  }
  }

  return *this;
}

ProcessRunner& ProcessRunner::setFromExecutable(const Executable& exe)
{
  const auto profile = m_core.currentProfile();
  if (!profile) {
    throw MyException(QObject::tr("No profile set"));
  }

  const QString customOverwrite =
      profile->setting("custom_overwrites", exe.title()).toString();

  ForcedLibraries forcedLibraries;
  if (profile->forcedLibrariesEnabled(exe.title())) {
    forcedLibraries = profile->determineForcedLibraries(exe.title());
  }

  QString currentDirectory = exe.workingDirectory();
  if (currentDirectory.isEmpty()) {
    currentDirectory = exe.binaryInfo().absolutePath();
  }

  setBinary(exe.binaryInfo());
  setArguments(exe.arguments());
  setCurrentDirectory(currentDirectory);
  setSteamID(exe.steamAppID());
  setCustomOverwrite(customOverwrite);
  setForcedLibraries(forcedLibraries);

  m_sp.useProton    = exe.useProton();
  m_sp.useTerminal  = exe.useTerminal();

  return *this;
}

ProcessRunner& ProcessRunner::setFromShortcut(const MOShortcut& shortcut)
{
  const auto currentInstance = InstanceManager::singleton().currentInstance();

  if (currentInstance) {
    if (shortcut.hasInstance() && !shortcut.isForInstance(*currentInstance)) {
      MOBase::reportError(QObject::tr("This shortcut is for instance '%1' but "
                                      "Mod Organizer is currently "
                                      "running for '%2'. Exit Mod Organizer "
                                      "before running the shortcut or "
                                      "change the active instance.")
                              .arg(shortcut.instanceDisplayName())
                              .arg(currentInstance->displayName()));

      throw std::exception();
    }
  }

  const auto* exes = m_core.executablesList();
  const auto exe   = exes->find(shortcut.executableName());

  if (exe != exes->end()) {
    setFromExecutable(*exe);
  } else {
    MOBase::reportError(QObject::tr("Executable '%1' does not exist in instance '%2'.")
                            .arg(shortcut.executableName())
                            .arg(currentInstance->displayName()));

    throw std::exception();
  }

  return *this;
}

ProcessRunner& ProcessRunner::setFromFileOrExecutable(
    const QString& executable, const QStringList& args, const QString& cwd,
    const QString& profileOverride, const QString& forcedCustomOverwrite,
    bool ignoreCustomOverwrite)
{
  const auto profile = m_core.currentProfile();
  if (!profile) {
    throw MyException(QObject::tr("No profile set"));
  }

  setBinary(QFileInfo(executable));
  setArguments(args.join(" "));
  setCurrentDirectory(cwd);
  setProfileName(profileOverride);

  if (executable.contains('\\') || executable.contains('/')) {
    if (m_sp.binary.isRelative()) {
      setBinary(QFileInfo(
          m_core.managedGame()->gameDirectory().absoluteFilePath(executable)));
    }

    if (cwd == "") {
      setCurrentDirectory(m_sp.binary.absolutePath());
    }

    try {
      const Executable& exe = m_core.executablesList()->getByBinary(m_sp.binary);

      setSteamID(exe.steamAppID());
      setCustomOverwrite(profile->setting("custom_overwrites", exe.title()).toString());

      if (profile->forcedLibrariesEnabled(exe.title())) {
        setForcedLibraries(profile->determineForcedLibraries(exe.title()));
      }
    } catch (const std::runtime_error&) {
      // nop
    }
  } else {
    try {
      const Executable& exe = m_core.executablesList()->get(executable);

      setSteamID(exe.steamAppID());
      setCustomOverwrite(profile->setting("custom_overwrites", exe.title()).toString());

      if (profile->forcedLibrariesEnabled(exe.title())) {
        setForcedLibraries(profile->determineForcedLibraries(exe.title()));
      }

      if (args.isEmpty()) {
        setArguments(exe.arguments());
      }

      setBinary(exe.binaryInfo());

      if (cwd == "") {
        setCurrentDirectory(exe.workingDirectory());
      }
    } catch (const std::runtime_error&) {
      log::warn("\"{}\" not set up as executable", executable);
    }
  }

  if (ignoreCustomOverwrite) {
    setCustomOverwrite("");
  } else if (!forcedCustomOverwrite.isEmpty()) {
    setCustomOverwrite(forcedCustomOverwrite);
  }

  return *this;
}

bool ProcessRunner::shouldRunShell() const
{
  return !m_shellOpen.filePath().isEmpty();
}

ProcessRunner::Results ProcessRunner::run()
{
  // check if setHooked() was called after setFromFile(); this needs to modify
  // the settings to run the associated executable instead of using
  // shell::Open()
  if (shouldRunShell() && m_sp.hooked) {
    auto assoc = env::getAssociation(m_shellOpen);
    if (!assoc.executable.filePath().isEmpty()) {
      setBinary(assoc.executable);
      setArguments(assoc.formattedCommandLine);
      setCurrentDirectory(assoc.executable.absoluteDir());
      m_shellOpen = {};
    } else {
      log::error("failed to get the associated executable, running unhooked");
      m_sp.hooked = false;
    }
  } else if (!shouldRunShell() && !m_sp.hooked) {
    m_shellOpen = m_sp.binary;
  }

  std::optional<Results> r;

  if (shouldRunShell()) {
    r = runShell();
  } else {
    r = runBinary();
  }

  if (r) {
    return *r;
  }

  return postRun();
}

std::optional<ProcessRunner::Results> ProcessRunner::runShell()
{
  const auto file = MOBase::normalizePathForHost(m_shellOpen.absoluteFilePath());

  log::debug("executing from shell: '{}'", file);

  auto r = shell::Open(file);
  if (!r.success()) {
    return Error;
  }

  m_handle.reset(r.stealProcessHandle());

  // not all files will return a valid handle even if opening them was
  // successful, such as inproc handlers (like the photo viewer); in that case
  // it's impossible to determine the status, so just say it's still running.
  if (m_handle.get() == INVALID_HANDLE_VALUE) {
    log::debug("shell didn't report an error, but no handle is available");
    return Running;
  }

  return {};
}

std::optional<ProcessRunner::Results> ProcessRunner::runBinary()
{
  if (m_profileName.isEmpty()) {
    const auto profile = m_core.currentProfile();
    if (!profile) {
      throw MyException(QObject::tr("No profile set"));
    }

    m_profileName = profile->name();
  }

  // saves profile, sets up the VFS, notifies plugins, etc.; can return false
  // if a plugin doesn't want the program to run.
  if (!m_core.beforeRun(m_sp.binary, m_sp.currentDirectory, m_sp.arguments,
                        m_profileName, m_customOverwrite, m_forcedLibraries,
                        &m_sp.saveBindMountSource, &m_sp.saveBindMountTarget,
                        &m_sp.usvfsManifestPath, &m_sp.usvfsLauncherPath)) {
    return Error;
  }

  QWidget* parent = (m_ui ? m_ui->mainWindow() : nullptr);

  const auto* game = m_core.managedGame();
  auto& settings   = m_core.settings();

  if (m_sp.steamAppID.trimmed().isEmpty()) {
    const QString gameSteamId = game->steamAPPId().trimmed();
    if (!gameSteamId.isEmpty()) {
      m_sp.steamAppID = gameSteamId;
      log::debug("process runner: using game steam app id '{}' for launch",
                 m_sp.steamAppID);
    }
  }

  if (!checkSteam(parent, m_sp, game->gameDirectory(), m_sp.steamAppID, settings)) {
    return Error;
  }

  if (!checkBlacklist(parent, m_sp, settings)) {
    return Error;
  }

  // if the executable is inside the mods folder another instance of
  // ModOrganizer is spawned instead to launch it
  adjustForVirtualized(game, m_sp, settings);

  m_handle.reset(reinterpret_cast<HANDLE>(
      static_cast<intptr_t>(startBinary(parent, m_sp))));

  if (m_handle.get() == INVALID_HANDLE_VALUE) {
    return Error;
  }

  return {};
}

bool ProcessRunner::shouldRefresh(Results r) const
{
  // afterRun() is only called with the Refresh flag; it refreshes the
  // directory structure and notifies plugins.
  //
  // Refreshing is not always required and can actually cause problems:
  //
  //  1) running shortcuts doesn't need refreshing because MO closes right
  //     after
  //
  //  2) the mod info dialog is not set up to deal with refreshes, so that it
  //     will crash because the old DirectoryEntry's are still being used in
  //     the list
  if (!m_waitFlags.testFlag(TriggerRefresh)) {
    log::debug("process runner: not refreshing because the flag isn't set");
    return false;
  }

  switch (r) {
  case Completed: {
    log::debug("process runner: refreshing because the process completed");
    return true;
  }

  case ForceUnlocked: {
    // The ForceUnlocked branch in waitForPid has already taken down the
    // game's process tree and wineserver, so by the time we're here no
    // Wine process is still writing under the prefix. Run afterRun() so
    // the FUSE VFS is unmounted, game-dir permissions are restored, and
    // local saves are synced back. The exit code is set non-zero in that
    // branch, which gates plugin sync-back (Plugins.txt may have been
    // half-written when we killed the game).
    log::debug("process runner: running afterRun to unmount VFS after force unlock");
    return true;
  }

  case Error:
  case Cancelled:
  case Running:
  default: {
    return false;
  }
  }
}

ProcessRunner::Results ProcessRunner::postRun()
{
  const bool mustWait = (m_waitFlags & ForceWait);

  if (!m_sp.hooked && !mustWait) {
    return Running;
  }

  if (mustWait && m_lockReason == UILocker::NoReason) {
    log::debug("the ForceWait flag is set but the lock reason wasn't, "
               "defaulting to LockUI");

    m_lockReason = UILocker::LockUI;
  }

  const QStringList expectedExecutables  =
      buildExpectedExecutables(m_sp.binary, m_sp.arguments);

  if (!mustWait) {
    if (m_lockReason == UILocker::NoReason) {
      // Main window launches typically use TriggerRefresh without
      // waiting/locking. In that mode we still need post-run refresh/sync once
      // the process exits.
      if (m_waitFlags.testFlag(TriggerRefresh)) {
        const pid_t pid =
            static_cast<pid_t>(reinterpret_cast<intptr_t>(m_handle.get()));
        const QFileInfo binary       = m_sp.binary;
        QPointer<OrganizerCore> core = &m_core;

        std::thread([core, binary, pid]() {
          int status     = 0;
          pid_t waited   = -1;
          do {
            waited = ::waitpid(pid, &status, 0);
          } while (waited == -1 && errno == EINTR);

          DWORD exitCode = 0;
          if (waited == pid) {
            if (WIFEXITED(status)) {
              exitCode = static_cast<DWORD>(WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
              exitCode = static_cast<DWORD>(128 + WTERMSIG(status));
            }
          } else if (errno == ECHILD) {
            // Detached process — poll with kill(0) until gone.
            while (::kill(pid, 0) == 0 || errno == EPERM) {
              usleep(200000);
            }
          } else {
            MOBase::log::warn("process runner: waitpid failed for pid {}: {}", pid,
                              errno);
          }

          if (!core) {
            return;
          }

          QMetaObject::invokeMethod(
              core,
              [core, binary, exitCode]() {
                if (core) {
                  core->afterRun(binary, exitCode);
                }
              },
              Qt::QueuedConnection);
        }).detach();

        log::debug("process runner: scheduled async post-run refresh for pid {}", pid);
      }
      return Running;
    }
  }

  auto r = Error;
  withLock([&](auto& ls) {
    r = waitForProcess(m_handle.get(), &m_exitCode, &ls, expectedExecutables);
  });

  if (shouldRefresh(r)) {
    QEventLoop loop;
    const bool wait = m_waitFlags.testFlag(WaitForRefresh);

    if (wait) {
      QObject::connect(&m_core, &OrganizerCore::directoryStructureReady, &loop,
                       &QEventLoop::quit, Qt::ConnectionType::QueuedConnection);
    }

    m_core.afterRun(m_sp.binary, m_exitCode);

    if (wait) {
      log::debug("process runner: waiting until refresh finishes");
      loop.exec();
      log::debug("process runner: refresh is done");
    }
  }

  return r;
}

ProcessRunner::Results ProcessRunner::attachToProcess(pid_t pid)
{
  m_handle.reset(reinterpret_cast<HANDLE>(static_cast<intptr_t>(pid)));
  return postRun();
}

DWORD ProcessRunner::exitCode() const
{
  return m_exitCode;
}

pid_t ProcessRunner::getProcessHandle() const
{
  return static_cast<pid_t>(reinterpret_cast<intptr_t>(m_handle.get()));
}

env::HandlePtr ProcessRunner::stealProcessHandle()
{
  auto *h = m_handle.release();
  m_handle.reset(INVALID_HANDLE_VALUE);
  return env::HandlePtr(h);
}

void ProcessRunner::withLock(std::function<void(UILocker::Session&)> f)
{
  auto ls = UILocker::instance().lock(m_lockReason);
  f(*ls);
}
