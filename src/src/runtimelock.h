#ifndef FLUORINE_RUNTIMELOCK_H
#define FLUORINE_RUNTIMELOCK_H

#include <QString>

// Owns the shared publication lease inherited from the packaged launcher.
// Direct developer invocations have no inherited descriptor and remain valid.
class RuntimeLockLease
{
public:
  RuntimeLockLease() = default;
  ~RuntimeLockLease();

  RuntimeLockLease(const RuntimeLockLease&)            = delete;
  RuntimeLockLease& operator=(const RuntimeLockLease&) = delete;

  RuntimeLockLease(RuntimeLockLease&& other) noexcept;
  RuntimeLockLease& operator=(RuntimeLockLease&& other) noexcept;

  static bool adoptFromEnvironment(const QString& expectedPath,
                                   RuntimeLockLease* lease, QString* error);

  static bool requiredForExecutable(const QString& expectedPath,
                                    const QString& executablePath);

  static QString managedLauncherForExecutable(const QString& expectedPath,
                                              const QString& executablePath);

  bool inherited() const noexcept { return m_fd >= 0; }
  int descriptor() const noexcept { return m_fd; }

private:
  explicit RuntimeLockLease(int fd) : m_fd(fd) {}
  int m_fd{-1};
};

#endif
