#include "runtimelock.h"
#include "unixtermination.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>

namespace
{
using namespace std::chrono_literals;

class ScopedEnvironment
{
public:
  explicit ScopedEnvironment(const char* name) : m_name(name)
  {
    m_hadValue = qEnvironmentVariableIsSet(name);
    if (m_hadValue) {
      m_value = qgetenv(name);
    }
  }
  ~ScopedEnvironment()
  {
    if (m_hadValue) {
      qputenv(m_name, m_value);
    } else {
      qunsetenv(m_name);
    }
  }

private:
  const char* m_name;
  bool m_hadValue{false};
  QByteArray m_value;
};

int makeLockFile(const QString& path)
{
  const QByteArray encoded = QFile::encodeName(path);
  return ::open(encoded.constData(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
}

TEST(RuntimeLockLeaseTest, MissingEnvironmentIsAnAllowedDeveloperInvocation)
{
  ScopedEnvironment environment("FLUORINE_RUNTIME_LOCK_FD");
  qunsetenv("FLUORINE_RUNTIME_LOCK_FD");

  RuntimeLockLease lease;
  QString error;
  EXPECT_TRUE(RuntimeLockLease::adoptFromEnvironment(
      QStringLiteral("/does/not/matter"), &lease, &error));
  EXPECT_FALSE(lease.inherited());
  EXPECT_TRUE(error.isEmpty());
}

TEST(RuntimeLockLeaseTest, InstalledCoreRequiresTheInheritedLease)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QDir root(temporary.path());
  ASSERT_TRUE(root.mkpath(QStringLiteral("bin")));
  QFile executable(root.filePath(QStringLiteral("bin/ModOrganizer-core")));
  ASSERT_TRUE(executable.open(QIODevice::WriteOnly));
  executable.close();

  EXPECT_TRUE(RuntimeLockLease::requiredForExecutable(
      root.filePath(QStringLiteral("runtime.lock")), executable.fileName()));

  QFile launcher(root.filePath(QStringLiteral("bin/fluorine-manager")));
  ASSERT_TRUE(launcher.open(QIODevice::WriteOnly));
  launcher.close();
  ASSERT_TRUE(launcher.setPermissions(QFileDevice::ReadOwner |
                                      QFileDevice::WriteOwner |
                                      QFileDevice::ExeOwner));
  EXPECT_EQ(RuntimeLockLease::managedLauncherForExecutable(
                root.filePath(QStringLiteral("runtime.lock")),
                executable.fileName()),
            QFileInfo(launcher).canonicalFilePath());

  ASSERT_TRUE(root.mkpath(QStringLiteral("build")));
  QFile developer(root.filePath(QStringLiteral("build/ModOrganizer")));
  ASSERT_TRUE(developer.open(QIODevice::WriteOnly));
  developer.close();
  EXPECT_FALSE(RuntimeLockLease::requiredForExecutable(
      root.filePath(QStringLiteral("runtime.lock")), developer.fileName()));
}

TEST(RuntimeLockLeaseTest, AdoptsExactSharedLeaseAndPreventsExecInheritance)
{
  ScopedEnvironment environment("FLUORINE_RUNTIME_LOCK_FD");
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath(QStringLiteral("runtime.lock"));
  const int fd       = makeLockFile(path);
  ASSERT_GE(fd, 3);
  ASSERT_EQ(::flock(fd, LOCK_SH | LOCK_NB), 0);
  qputenv("FLUORINE_RUNTIME_LOCK_FD", QByteArray::number(fd));

  RuntimeLockLease lease;
  QString error;
  ASSERT_TRUE(RuntimeLockLease::adoptFromEnvironment(path, &lease, &error))
      << error.toStdString();
  ASSERT_TRUE(lease.inherited());
  EXPECT_EQ(lease.descriptor(), fd);
  EXPECT_NE(::fcntl(fd, F_GETFD) & FD_CLOEXEC, 0);
  EXPECT_FALSE(qEnvironmentVariableIsSet("FLUORINE_RUNTIME_LOCK_FD"));

  const int competitor = makeLockFile(path);
  ASSERT_GE(competitor, 0);
  EXPECT_EQ(::flock(competitor, LOCK_EX | LOCK_NB), -1);
  EXPECT_TRUE(errno == EWOULDBLOCK || errno == EAGAIN);
  ::close(competitor);

  const pid_t child = ::fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    const QByteArray fdArgument = QByteArray::number(fd);
    ::execl("/bin/sh", "sh", "-c", "test ! -e /proc/self/fd/$1", "sh",
            fdArgument.constData(), static_cast<char*>(nullptr));
    _exit(125);
  }
  int status = 0;
  ASSERT_EQ(::waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(RuntimeLockLeaseTest, RejectsWrongFileIdentityAndSymlink)
{
  ScopedEnvironment environment("FLUORINE_RUNTIME_LOCK_FD");
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString actual = temporary.filePath(QStringLiteral("actual.lock"));
  const QString other  = temporary.filePath(QStringLiteral("other.lock"));
  const QString alias  = temporary.filePath(QStringLiteral("alias.lock"));
  const int fd         = makeLockFile(actual);
  ASSERT_GE(fd, 3);
  const int otherFd = makeLockFile(other);
  ASSERT_GE(otherFd, 0);
  ::close(otherFd);
  ASSERT_TRUE(QFile::link(actual, alias));

  RuntimeLockLease lease;
  QString error;
  qputenv("FLUORINE_RUNTIME_LOCK_FD", QByteArray::number(fd));
  EXPECT_FALSE(RuntimeLockLease::adoptFromEnvironment(other, &lease, &error));
  EXPECT_FALSE(error.isEmpty());

  qputenv("FLUORINE_RUNTIME_LOCK_FD", QByteArray::number(fd));
  error.clear();
  EXPECT_FALSE(RuntimeLockLease::adoptFromEnvironment(alias, &lease, &error));
  EXPECT_FALSE(error.isEmpty());
  ::close(fd);
}

struct ChildBridge
{
  pid_t pid{-1};
  int observed{-1};
};

ChildBridge startBridgeChild(std::chrono::milliseconds grace, bool complete)
{
  int observed[2];
  if (::pipe2(observed, O_CLOEXEC) != 0) {
    return {};
  }
  const pid_t child = ::fork();
  if (child == 0) {
    ::close(observed[0]);
    UnixTerminationBridge bridge(grace);
    const unsigned char ready = 1;
    const ssize_t readyWritten = ::write(observed[1], &ready, sizeof(ready));
    if (readyWritten != sizeof(ready)) {
      _exit(123);
    }

    struct pollfd notification{};
    notification.fd     = bridge.notificationFd();
    notification.events = POLLIN;
    int pollResult = 0;
    do {
      pollResult = ::poll(&notification, 1, 5000);
    } while (pollResult < 0 && errno == EINTR);
    if (pollResult <= 0) {
      _exit(124);
    }
    bridge.drainNotifications();
    const unsigned char signal =
        static_cast<unsigned char>(bridge.signalNumber());
    const ssize_t signalWritten = ::write(observed[1], &signal, sizeof(signal));
    if (signalWritten != sizeof(signal)) {
      _exit(123);
    }
    if (complete) {
      bridge.complete();
      ::usleep(static_cast<useconds_t>((grace + 100ms).count() * 1000));
      _exit(42);
    }
    for (;;) {
      ::pause();
    }
  }
  ::close(observed[1]);
  return {child, observed[0]};
}

unsigned char readByte(int fd)
{
  unsigned char value = 0;
  EXPECT_EQ(::read(fd, &value, sizeof(value)), 1);
  return value;
}

TEST(UnixTerminationBridgeTest, FirstSignalAllowsGracefulCompletion)
{
  const auto child = startBridgeChild(500ms, true);
  ASSERT_GT(child.pid, 0);
  ASSERT_EQ(readByte(child.observed), 1);
  ASSERT_EQ(::kill(child.pid, SIGTERM), 0);
  EXPECT_EQ(readByte(child.observed), SIGTERM);

  int status = 0;
  ASSERT_EQ(::waitpid(child.pid, &status, 0), child.pid);
  ::close(child.observed);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 42);
}

TEST(UnixTerminationBridgeTest, SecondSignalEscalatesImmediately)
{
  const auto child = startBridgeChild(5s, false);
  ASSERT_GT(child.pid, 0);
  ASSERT_EQ(readByte(child.observed), 1);
  ASSERT_EQ(::kill(child.pid, SIGINT), 0);
  EXPECT_EQ(readByte(child.observed), SIGINT);
  const auto start = std::chrono::steady_clock::now();
  ASSERT_EQ(::kill(child.pid, SIGTERM), 0);

  int status = 0;
  ASSERT_EQ(::waitpid(child.pid, &status, 0), child.pid);
  ::close(child.observed);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 128 + SIGINT);
  EXPECT_LT(std::chrono::steady_clock::now() - start, 2s);
}

TEST(UnixTerminationBridgeTest, DeadlineEscalatesWhenCleanupCannotFinish)
{
  const auto child = startBridgeChild(150ms, false);
  ASSERT_GT(child.pid, 0);
  ASSERT_EQ(readByte(child.observed), 1);
  ASSERT_EQ(::kill(child.pid, SIGTERM), 0);
  EXPECT_EQ(readByte(child.observed), SIGTERM);

  int status = 0;
  ASSERT_EQ(::waitpid(child.pid, &status, 0), child.pid);
  ::close(child.observed);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 128 + SIGTERM);
}
}  // namespace
