/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef SPAWN_H
#define SPAWN_H

#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QStringList>

#include <sys/types.h>
#include <unistd.h>

#include "wineprofileinisync.h"
#include <cstdint>

class QProcess;
class Settings;

namespace process_lifetime
{
struct LaunchReceipt;
}

namespace spawn
{

enum class SaveDeploymentMode : std::uint8_t
{
  None,
  LeaseOnly,
  ManagedLinks,
  BindMount,
};

struct SaveDeploymentReceipt
{
  SaveDeploymentMode mode{SaveDeploymentMode::None};
  QString prefixPath;
  QString profileRoot;
  QString livePath;
  QString prefixIni;
  QString ownerId;
  QList<WineProfileIniSync::Deployment> profileIniDeployments;
  WineProfileIniSync::CleanupPhase profileIniCleanupPhase{
      WineProfileIniSync::CleanupPhase::Prepared};
  bool iniPatched{false};
  bool sessionLeasePublished{false};
  bool topologyRestored{false};
  bool deploymentCleanupPending{false};

  bool complete() const noexcept
  {
    if (mode == SaveDeploymentMode::None)
      return true;
    if (prefixPath.isEmpty() || livePath.isEmpty() || ownerId.isEmpty()) {
      return false;
    }
    return mode == SaveDeploymentMode::LeaseOnly || !profileRoot.isEmpty();
  }

  bool needsRollback() const noexcept
  {
    return mode == SaveDeploymentMode::ManagedLinks || deploymentCleanupPending ||
           iniPatched || sessionLeasePublished || !profileIniDeployments.isEmpty();
  }
};

/*
 * @param binary the binary to spawn
 * @param arguments arguments to pass to the binary
 * @param profileName name of the active profile
 * @param currentDirectory the directory to use as the working directory to run
 * in
 * @param logLevel log level to be used by the hook library. Ignored if hooked
 * is false
 * @param hooked if set, the binary is started with mo injected
 * @param stdout if not equal to INVALID_HANDLE_VALUE, this is used as stdout
 * for the process
 * @param stderr if not equal to INVALID_HANDLE_VALUE, this is used as stderr
 * for the process
 */
struct SpawnParameters
{
  QFileInfo binary;
  // Compatibility/raw representation used by diagnostics and the existing
  // about-to-run plugin callback.
  QString arguments;
  // Resolved execution arguments. This is authoritative at every spawn,
  // lifetime-policy and USVFS boundary.
  QStringList argumentList;
  QDir currentDirectory;
  QDir gameDirectory;
  QString steamAppID;
  bool hooked      = false;
  bool useProton   = true;
  bool useTerminal = false;
  int stdOut       = -1;
  int stdErr       = -1;
  // Exact save topology committed by beforeRun. This value, rather than
  // mutable profile settings or empty-string inference, owns rollback and
  // after-run teardown.
  SaveDeploymentReceipt saveDeployment;
  // Versioned request consumed by the Wine-side USVFS controller. Empty means
  // launch the target normally (the FUSE path).
  QString usvfsRequestPath;
  // Unique per-launch provenance inherited by wrappers and detached children.
  // ProcessRunner uses it to adopt companions without name-only global scans.
  QString lifetimeToken;
  // Linux PIDs are reusable. Capture the root's /proc start time immediately
  // after spawn so a delayed wait cannot attach launch metadata to a newer
  // process that happens to receive the same PID.
  std::uint64_t lifetimeRootStartTime = 0;
};

bool checkSteam(QWidget* parent, const SpawnParameters& sp, const QDir& gameDirectory,
                const QString& steamAppID, const Settings& settings);

bool checkBlacklist(QWidget* parent, const SpawnParameters& sp, Settings& settings);

/**
 * @brief spawn a binary, returning the new pid (or -1 on failure)
 **/
process_lifetime::LaunchReceipt startBinary(QWidget* parent, const SpawnParameters& sp);

enum class FileExecutionTypes
{
  Executable = 1,
  Other
};

struct FileExecutionContext
{
  QFileInfo binary;
  QString arguments;
  FileExecutionTypes type;
};

QString findJavaInstallation(const QString& jarFile);

FileExecutionContext getFileExecutionContext(QWidget* parent, const QFileInfo& target);

FileExecutionTypes getFileExecutionType(const QFileInfo& target);

}  // namespace spawn

#endif  // SPAWN_H
