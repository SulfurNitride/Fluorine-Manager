#include "env.h"
#include "envdump.h"
#include "envmetrics.h"
#include "envmodule.h"
#include "envsecurity.h"
#include "envshortcut.h"
#include "envwindows.h"
#include "settings.h"
#include "shared/util.h"
#include <log.h>
#include <utility.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimeZone>

#include <climits>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

namespace
{
QProcessEnvironment hostDesktopEnvironment()
{
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  auto restoreOrStrip = [&env](const QString& var, const QString& origVar) {
    if (env.contains(origVar)) {
      const QString orig = env.value(origVar);
      if (orig.isEmpty()) {
        env.remove(var);
      } else {
        env.insert(var, orig);
      }
      env.remove(origVar);
    }
  };

  restoreOrStrip(QStringLiteral("LD_LIBRARY_PATH"),
                 QStringLiteral("FLUORINE_ORIG_LD_LIBRARY_PATH"));
  restoreOrStrip(QStringLiteral("LD_PRELOAD"),
                 QStringLiteral("FLUORINE_ORIG_LD_PRELOAD"));
  restoreOrStrip(QStringLiteral("PATH"), QStringLiteral("FLUORINE_ORIG_PATH"));
  restoreOrStrip(QStringLiteral("XDG_DATA_DIRS"),
                 QStringLiteral("FLUORINE_ORIG_XDG_DATA_DIRS"));
  restoreOrStrip(QStringLiteral("QT_PLUGIN_PATH"),
                 QStringLiteral("FLUORINE_ORIG_QT_PLUGIN_PATH"));

  QStringList toolDirs;
  auto addToolDir = [&toolDirs](const QString& path) {
    if (!path.isEmpty() && QDir(path).exists() && !toolDirs.contains(path)) {
      toolDirs.append(path);
    }
  };

  const QString baseDir = env.value(QStringLiteral("MO2_BASE_DIR"));
  addToolDir(env.value(QStringLiteral("MO2_LIBS_DIR")));
  if (!baseDir.isEmpty()) {
    addToolDir(QDir(baseDir).filePath(QStringLiteral("lib")));
    addToolDir(baseDir);
  }
  const QString appDir = QCoreApplication::applicationDirPath();
  addToolDir(QDir(appDir).filePath(QStringLiteral("lib")));
  addToolDir(appDir);
  if (!toolDirs.isEmpty()) {
    const QString path = env.value(QStringLiteral("PATH"));
    env.insert(QStringLiteral("PATH"),
               toolDirs.join(QLatin1Char(':')) +
                   (path.isEmpty() ? QString() : QStringLiteral(":") + path));
  }

  env.remove(QStringLiteral("QT_QPA_PLATFORM_PLUGIN_PATH"));
  return env;
}
}

namespace env
{

using namespace MOBase;

Console::Console()  
{
  // stdout/stderr are already attached on Linux.
}

Console::~Console() = default;

ModuleNotification::ModuleNotification(QObject* o, std::function<void(Module)> f)
    :  m_object(o), m_f(std::move(f))
{}

ModuleNotification::~ModuleNotification() = default;

void ModuleNotification::setCookie(void* c)
{
  m_cookie = c;
}

void ModuleNotification::fire(QString path, std::size_t fileSize)
{
  if (m_loaded.contains(path)) {
    return;
  }

  m_loaded.insert(path);

  if (m_f) {
    QMetaObject::invokeMethod(
        m_object,
        [path, fileSize, f = m_f] {
          f(Module(path, fileSize));
        },
        Qt::QueuedConnection);
  }
}

Environment::Environment() = default;

// anchor
Environment::~Environment() = default;

const std::vector<Module>& Environment::loadedModules() const
{
  if (m_modules.empty()) {
    m_modules = getLoadedModules();
  }

  return m_modules;
}

std::vector<Process> Environment::runningProcesses() 
{
  return getRunningProcesses();
}

const WindowsInfo& Environment::windowsInfo() const
{
  if (!m_windows) {
    m_windows.reset(new WindowsInfo);
  }

  return *m_windows;
}

const std::vector<SecurityProduct>& Environment::securityProducts() const
{
  if (m_security.empty()) {
    m_security = getSecurityProducts();
  }

  return m_security;
}

const Metrics& Environment::metrics() const
{
  if (!m_metrics) {
    m_metrics.reset(new Metrics);
  }

  return *m_metrics;
}

QString Environment::timezone() 
{
  QTimeZone const tz = QTimeZone::systemTimeZone();
  if (!tz.isValid()) {
    log::error("failed to get system timezone");
    return "unknown";
  }

  return QString::fromUtf8(tz.id());
}

std::unique_ptr<ModuleNotification>
Environment::onModuleLoaded(QObject*, std::function<void(Module)>)
{
  // Linux's dynamic loader has no equivalent of LdrRegisterDllNotification.
  return {};
}

void Environment::dump(const Settings& s) const
{
  log::debug("os: {}", windowsInfo().toString());
  log::debug("time zone: {}", timezone());

  log::debug("security products:");

  {
    std::set<QString> productNames;
    for (const auto& sp : securityProducts()) {
      productNames.insert(sp.toString());
    }

    for (auto&& name : productNames) {
      log::debug("  . {}", name);
    }
  }

  log::debug("modules loaded in process:");
  for (const auto& m : loadedModules()) {
    if (m.interesting()) {
      log::debug(" . {}", m.toString());
    }
  }

  log::debug("displays:");
  for (const auto& d : metrics().displays()) {
    log::debug(" . {}", d.toString());
  }

  const auto r = metrics().desktopGeometry();
  log::debug("desktop geometry: ({},{})-({},{})", r.left(), r.top(), r.right(),
             r.bottom());

  dumpDisks(s);
}

void Environment::dumpDisks(const Settings& s) 
{
  std::set<QString> rootPaths;

  auto dump = [&](auto&& path) {
    const QFileInfo fi(path);
    const QStorageInfo si(fi.absoluteFilePath());

    if (rootPaths.contains(si.rootPath())) {
      return;
    }

    rootPaths.insert(si.rootPath());

    log::debug("  . {} free={} MB{}", si.rootPath(), (si.bytesFree() / 1000 / 1000),
               (si.isReadOnly() ? " (readonly)" : ""));
  };

  log::debug("drives:");

  dump(QStorageInfo::root().rootPath());
  dump(s.paths().base());
  dump(s.paths().downloads());
  dump(s.paths().mods());
  dump(s.paths().cache());
  dump(s.paths().profiles());
  dump(s.paths().overwrite());
  dump(QCoreApplication::applicationDirPath());
}

QString path()
{
  return get("PATH");
}

QString appendToPath(const QString& s)
{
  auto old = path();
  set("PATH", old + ":" + s);
  return old;
}

QString prependToPath(const QString& s)
{
  auto old = path();
  set("PATH", s + ":" + old);
  return old;
}

void setPath(const QString& s)
{
  set("PATH", s);
}

QString get(const QString& name)
{
  return qEnvironmentVariable(name.toUtf8());
}

void set(const QString& n, const QString& v)
{
  qputenv(n.toUtf8(), v.toUtf8());
}

Service::Service(QString name) : Service(std::move(name), StartType::None, Status::None)
{}

Service::Service(QString name, StartType st, Status s)
    : m_name(std::move(name)), m_startType(st), m_status(s)
{}

const QString& Service::name() const
{
  return m_name;
}

bool Service::isValid() const
{
  return (m_startType != StartType::None) && (m_status != Status::None);
}

Service::StartType Service::startType() const
{
  return m_startType;
}

Service::Status Service::status() const
{
  return m_status;
}

QString Service::toString() const
{
  return QString("service '%1', start=%2, status=%3")
      .arg(m_name)
      .arg(env::toString(m_startType))
      .arg(env::toString(m_status));
}

QString toString(Service::StartType st)
{
  using ST = Service::StartType;

  switch (st) {
  case ST::None:
    return "none";

  case ST::Disabled:
    return "disabled";

  case ST::Enabled:
    return "enabled";

  default:
    return QString("unknown %1").arg(static_cast<int>(st));
  }
}

QString toString(Service::Status st)
{
  using S = Service::Status;

  switch (st) {
  case S::None:
    return "none";

  case S::Stopped:
    return "stopped";

  case S::Running:
    return "running";

  default:
    return QString("unknown %1").arg(static_cast<int>(st));
  }
}

Service getService(const QString& name)
{
  // Linux has no Windows SCM equivalent — return an invalid service.
  return Service(name);
}

Association getAssociation(const QFileInfo& targetInfo)
{
  log::debug("getting association for '{}', extension is '.{}'",
             targetInfo.absoluteFilePath(), targetInfo.suffix());

  const QString mimeType = "application/x-" + targetInfo.suffix();

  QProcess xdgMime;
  xdgMime.setProcessEnvironment(hostDesktopEnvironment());
  xdgMime.start("xdg-mime", QStringList() << "query" << "default" << mimeType);

  if (!xdgMime.waitForFinished(3000)) {
    log::debug("xdg-mime query timed out for '{}'", targetInfo.suffix());
    return {};
  }

  const QString desktopFile =
      QString::fromUtf8(xdgMime.readAllStandardOutput()).trimmed();
  if (desktopFile.isEmpty()) {
    log::debug("no association found for '{}'", targetInfo.suffix());
    return {};
  }

  log::debug("associated desktop file: '{}'", desktopFile);

  return {};
}

void deleteRegistryKeyIfEmpty(const QString&)
{
  // No registry on Linux.
}

bool registryValueExists(const QString&, const QString&)
{
  // No registry on Linux.
  return false;
}

std::filesystem::path thisProcessPath()
{
  char buf[PATH_MAX] = {};
  ssize_t const len        = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (len <= 0) {
    std::cerr << "failed to readlink /proc/self/exe: " << strerror(errno) << "\n";
    return {};
  }

  buf[len] = '\0';
  return std::filesystem::path(buf);
}

pid_t findOtherPid()
{
  std::clog << "looking for the other process...\n";

  const pid_t thisPid = ::getpid();
  std::clog << "this process id is " << thisPid << "\n";

  const std::string targetName = "ModOrganizer";

  for (const auto& entry : std::filesystem::directory_iterator("/proc")) {
    if (!entry.is_directory()) {
      continue;
    }

    const auto pidStr = entry.path().filename().string();

    bool isNumber = true;
    for (char const c : pidStr) {
      if (!std::isdigit(static_cast<unsigned char>(c))) {
        isNumber = false;
        break;
      }
    }
    if (!isNumber) {
      continue;
    }

    pid_t const pid = std::stoi(pidStr);
    if (pid == thisPid) {
      continue;
    }

    std::ifstream commFile(entry.path() / "comm");
    if (!commFile.is_open()) {
      continue;
    }

    std::string comm;
    std::getline(commFile, comm);

    if (comm == targetName) {
      std::clog << "found other process with pid " << pid << "\n";
      return pid;
    }
  }

  std::clog << "no process with this filename\n";
  return 0;
}

CoreDumpTypes coreDumpTypeFromString(const std::string& s)
{
  if (s == "data")
    return env::CoreDumpTypes::Data;
  else if (s == "full")
    return env::CoreDumpTypes::Full;
  else
    return env::CoreDumpTypes::Mini;
}

std::string toString(CoreDumpTypes type)
{
  switch (type) {
  case CoreDumpTypes::Mini:
    return "mini";

  case CoreDumpTypes::Data:
    return "data";

  case CoreDumpTypes::Full:
    return "full";

  default:
    return "?";
  }
}

bool coredump(const char* dir, CoreDumpTypes type)
{
  std::clog << "coredump requested (type: " << toString(type) << ")\n";

  if (dir) {
    std::clog << "dump directory: " << dir << "\n";
  }

  // On Linux, abort() generates a core dump if ulimit -c allows it.
  std::clog << "calling abort() to generate core dump\n";
  std::abort();
}

bool coredumpOther(CoreDumpTypes type)
{
  std::clog << "creating core dump for a running process\n";

  const auto pid = findOtherPid();
  if (pid == 0) {
    std::cerr << "no other process found\n";
    return false;
  }

  std::clog << "found other process with pid " << pid << "\n";

  if (::kill(pid, SIGABRT) != 0) {
    std::cerr << "failed to send SIGABRT to process " << pid << ": "
              << strerror(errno) << "\n";
    return false;
  }

  std::clog << "sent SIGABRT to process " << pid << "\n";
  return true;
}

}  // namespace env
