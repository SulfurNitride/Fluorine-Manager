#pragma once

#include <QSettings>
#include <QString>

#include <functional>

namespace InstanceUnregister
{

enum class DisableStatus
{
  Disabled,
  SourceUnavailable,
  SourceUnsafe,
  DestinationExists,
  RenameFailed,
  PublicationUncertain,
};

struct DisableResult
{
  DisableStatus status{DisableStatus::RenameFailed};
  QString sourcePath;
  QString disabledPath;
  QString error;

  explicit operator bool() const noexcept
  {
    return status == DisableStatus::Disabled;
  }
};

// Retires one global instance INI without replacing an existing or dangling
// `.disabled` leaf. On Linux the directory and source identities are retained
// across renameat2(RENAME_NOREPLACE), so a path swap cannot be reported as a
// successful removal.
DisableResult disableGlobalIni(const QString& iniPath);

// Updates the process-global portable-instance registry, synchronizes the
// backend, and verifies the exact persisted list before reporting success.
enum class RegistryStatus
{
  Saved,
  Failed,
  RollbackFailed,
};

struct RegistryResult
{
  RegistryStatus status{RegistryStatus::Failed};
  QString error;

  explicit operator bool() const noexcept
  {
    return status == RegistryStatus::Saved;
  }
};

using RegistryReadHook = std::function<void()>;

RegistryResult updatePortableRegistration(
    QSettings& settings, const QString& path, bool registered,
    RegistryReadHook afterReadForTesting = {});

// Replaces one registered portable path in the same locked, verified list
// transaction. This is used only after the corresponding directory rename has
// committed successfully.
RegistryResult replacePortableRegistration(
    QSettings& settings, const QString& oldPath, const QString& newPath,
    RegistryReadHook afterReadForTesting = {});

}  // namespace InstanceUnregister
