#include "runtimelock.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
constexpr auto RuntimeLockFdVariable = "FLUORINE_RUNTIME_LOCK_FD";

void closeFd(int& fd) noexcept
{
  if (fd >= 0) {
    ::close(fd);
    fd = -1;
  }
}
}  // namespace

RuntimeLockLease::~RuntimeLockLease()
{
  closeFd(m_fd);
}

RuntimeLockLease::RuntimeLockLease(RuntimeLockLease&& other) noexcept
    : m_fd(other.m_fd)
{
  other.m_fd = -1;
}

RuntimeLockLease& RuntimeLockLease::operator=(RuntimeLockLease&& other) noexcept
{
  if (this != &other) {
    closeFd(m_fd);
    m_fd       = other.m_fd;
    other.m_fd = -1;
  }
  return *this;
}

bool RuntimeLockLease::adoptFromEnvironment(const QString& expectedPath,
                                            RuntimeLockLease* lease,
                                            QString* error)
{
  if (lease == nullptr) {
    return false;
  }
  if (!qEnvironmentVariableIsSet(RuntimeLockFdVariable)) {
    const QString executable = QFileInfo(QStringLiteral("/proc/self/exe"))
                                   .canonicalFilePath();
    if (requiredForExecutable(expectedPath, executable)) {
      if (error != nullptr) {
        *error = QStringLiteral(
            "the installed Fluorine core was started without its runtime lease; "
            "run fluorine-manager instead");
      }
      return false;
    }
    *lease = RuntimeLockLease{};
    return true;
  }

  const QByteArray encodedFd = qgetenv(RuntimeLockFdVariable);
  qunsetenv(RuntimeLockFdVariable);

  bool ok      = false;
  const int fd = QString::fromLatin1(encodedFd).toInt(&ok, 10);
  auto fail = [&](const QString& detail) {
    if (error != nullptr) {
      *error = detail;
    }
    return false;
  };
  if (!ok || fd < 3) {
    return fail(QStringLiteral("invalid inherited runtime lock descriptor"));
  }

  const int descriptorFlags = ::fcntl(fd, F_GETFD);
  if (descriptorFlags < 0) {
    return fail(QStringLiteral("inherited runtime lock descriptor is not open"));
  }

  struct stat inherited{};
  struct stat expected{};
  struct stat expectedLink{};
  const QByteArray path = QFile::encodeName(expectedPath);
  if (::fstat(fd, &inherited) != 0 || !S_ISREG(inherited.st_mode)) {
    return fail(QStringLiteral("inherited runtime lock is not a regular file"));
  }
  if (::lstat(path.constData(), &expectedLink) != 0 ||
      !S_ISREG(expectedLink.st_mode) || ::stat(path.constData(), &expected) != 0) {
    return fail(QStringLiteral("expected runtime lock is missing or unsafe"));
  }
  if (inherited.st_dev != expected.st_dev || inherited.st_ino != expected.st_ino) {
    return fail(QStringLiteral("inherited runtime lock has the wrong identity"));
  }
  if (::flock(fd, LOCK_SH | LOCK_NB) != 0) {
    return fail(QStringLiteral("inherited runtime lock is not held shared"));
  }
  if (::fcntl(fd, F_SETFD, descriptorFlags | FD_CLOEXEC) != 0) {
    return fail(QStringLiteral("cannot prevent runtime lock inheritance"));
  }

  *lease = RuntimeLockLease(fd);
  return true;
}

bool RuntimeLockLease::requiredForExecutable(const QString& expectedPath,
                                             const QString& executablePath)
{
  const QString executable = QFileInfo(executablePath).canonicalFilePath();
  if (executable.isEmpty()) {
    return false;
  }

  const QDir dataRoot = QFileInfo(expectedPath).absoluteDir();
  const QString installedBin =
      QFileInfo(dataRoot.absoluteFilePath(QStringLiteral("bin")))
          .canonicalFilePath();
  return !installedBin.isEmpty() &&
         QFileInfo(executable).absoluteDir().canonicalPath() == installedBin;
}

QString RuntimeLockLease::managedLauncherForExecutable(
    const QString& expectedPath, const QString& executablePath)
{
  if (!requiredForExecutable(expectedPath, executablePath)) {
    return {};
  }
  const QString launcher =
      QFileInfo(executablePath).absoluteDir().absoluteFilePath(
          QStringLiteral("fluorine-manager"));
  const QFileInfo info(launcher);
  return info.isFile() && info.isExecutable() && !info.isSymLink()
             ? info.canonicalFilePath()
             : QString{};
}
