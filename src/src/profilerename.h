#pragma once

#include <QDir>
#include <QSettings>
#include <QString>

#include <memory>

namespace ProfileRename
{

enum class Status
{
  Renamed,
  NoChange,
  InvalidName,
  SourceUnavailable,
  SettingsMismatch,
  SettingsSyncFailed,
  DestinationExists,
  RenameFailed,
  RebindFailed,
  RollbackFailed,
};

struct Result
{
  Status status{Status::RenameFailed};
  QString sourcePath;
  QString targetPath;
  QString error;
  std::unique_ptr<QSettings> replacementSettings;

  bool succeeded() const noexcept
  {
    return status == Status::Renamed || status == Status::NoChange;
  }
};

// Synchronizes the current settings generation, renames the profile directory
// without replacing another profile, and prepares a settings backend bound to
// the new directory. The caller keeps the old backend alive until it adopts the
// returned replacement.
Result apply(const QDir& directory, QSettings& currentSettings,
             const QString& requestedName);

}  // namespace ProfileRename
