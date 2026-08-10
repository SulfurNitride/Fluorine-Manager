#ifndef SLRMANAGER_H
#define SLRMANAGER_H

#include "slroperationcontext.h"

#include <QString>
#include <functional>

struct SlrUpdateInfo
{
  bool installed = false;
  bool updateAvailable = false;
  QString localBuildId;
  QString remoteBuildId;
  QString error;
};

/// Returns true if the SLR `run` script is present and executable.
__attribute__((visibility("default"))) bool isSlrInstalled();

/// Returns the path to the SLR `run` script, or empty if not installed.
__attribute__((visibility("default"))) QString getSlrRunScript();

/// Check the remote SLR BUILD_ID without downloading or replacing anything.
__attribute__((visibility("default"))) SlrUpdateInfo
checkSlrUpdate(SlrCancellationToken cancellation = {});

/// Download and install SteamLinuxRuntime_4 (steamrt4, ~200 MB).
/// Skips if already at the latest version (BUILD_ID check).
/// Returns empty string on success, or an error message.
__attribute__((visibility("default"))) QString
downloadSlr(const std::function<void(float)>& progressCb,
            const std::function<void(const QString&)>& statusCb,
            SlrCancellationToken cancellation = {});

/// Returns true if our injected xrandr helper is present.
__attribute__((visibility("default"))) bool isXrandrInjected();

/// Ensure xrandr is extracted into the steamrt install dir. Blocking, but
/// small (~150 KB download). Used to back-fill xrandr for users whose SLR
/// was installed before Fluorine started injecting it (issue #49) — pre-
/// existing installs short-circuit downloadSlr() and would otherwise never
/// get xrandr. Safe to call unconditionally.
__attribute__((visibility("default"))) bool
ensureXrandrInstalled(SlrCancellationToken cancellation,
                      const std::function<void(const QString&)>& statusCb);

/// Close process-wide SLR operation admission and cancel admitted work. This
/// is irreversible and is used only by failed-startup fail-stop.
__attribute__((visibility("default"))) void
suppressSlrOperationsForFailedRollback() noexcept;

/// True once no admitted SLR network, extraction, or install work remains.
__attribute__((visibility("default"))) bool
slrOperationsDrainedForFailedRollback() noexcept;

/// Used by delayed UI callbacks to reject prompts/installs after fail-stop.
__attribute__((visibility("default"))) bool slrOperationAdmissionSuppressed() noexcept;

/// Run a short SLR-owned UI persistence action under the same admission and
/// drain accounting as installer commits.
__attribute__((visibility("default"))) bool
runSlrUiCommitIfAllowed(const std::function<void()>& mutation);

#endif // SLRMANAGER_H
