#ifndef FUSEPERMISSIONS_H
#define FUSEPERMISSIONS_H

#include "fusemountoptions.h"

#include <QFile>
#include <functional>

enum class FusePermissionStatus { Enabled, Cancelled, Failed };

struct FusePermissionResult
{
  FusePermissionStatus status;
  QString error;
};

// Keep the file checks separate from authorization so they can be exercised
// against temporary files without changing the host or requesting root access.
inline FusePermissionResult ensureFuseUserAllowOther(
    const QString& configPath,
    const std::function<FusePermissionResult(const QByteArray&)>& authorizedAppend)
{
  QFile config(configPath);
  if (config.exists()) {
    if (!config.open(QIODevice::ReadOnly)) {
      return {FusePermissionStatus::Failed, config.errorString()};
    }
    const QByteArray contents = config.readAll();
    if (config.error() != QFileDevice::NoError) {
      return {FusePermissionStatus::Failed, config.errorString()};
    }
    config.close();
    if (fuseUserAllowOtherEnabled(contents)) {
      return {FusePermissionStatus::Enabled, {}};
    }
  }

  // Append rather than replacing the file: preserve comments, other settings,
  // ownership and permissions. The initial newline also handles a missing EOF
  // newline (including a final comment).
  const auto result = authorizedAppend(QByteArray("\nuser_allow_other\n"));
  if (result.status != FusePermissionStatus::Enabled) {
    return result;
  }
  if (!config.open(QIODevice::ReadOnly)) {
    return {FusePermissionStatus::Failed, config.errorString()};
  }
  const QByteArray contents = config.readAll();
  if (config.error() != QFileDevice::NoError) {
    return {FusePermissionStatus::Failed, config.errorString()};
  }
  return {fuseUserAllowOtherEnabled(contents) ? FusePermissionStatus::Enabled
                                             : FusePermissionStatus::Failed,
          {}};
}

#endif
