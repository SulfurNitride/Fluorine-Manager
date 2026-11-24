#ifndef SLRMANAGER_H
#define SLRMANAGER_H

#include <QString>
#include <QStringList>
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

/// Command placed in front of the SLR `run` script (and nothing else), split
/// into argv. Taken from FLUORINE_RUNTIME_WRAPPER, else the
/// fluorine/runtime_wrapper setting. Lets NixOS users run pressure-vessel
/// through `steam-run` while Fluorine and its FUSE mount stay on the host.
__attribute__((visibility("default"))) QStringList slrRuntimeWrapper();

/// Check the remote SLR BUILD_ID without downloading or replacing anything.
__attribute__((visibility("default")))
SlrUpdateInfo checkSlrUpdate(const int* cancelFlag = nullptr);

/// Download and install SteamLinuxRuntime_4 (steamrt4, ~200 MB).
/// Skips if already at the latest version (BUILD_ID check).
/// Returns empty string on success, or an error message.
__attribute__((visibility("default")))
QString downloadSlr(const std::function<void(float)>& progressCb,
                    const std::function<void(const QString&)>& statusCb,
                    const int* cancelFlag);

/// Returns true if our injected xrandr helper is present.
__attribute__((visibility("default"))) bool isXrandrInjected();

/// Ensure xrandr is extracted into the steamrt install dir. Blocking, but
/// small (~150 KB download). Used to back-fill xrandr for users whose SLR
/// was installed before Fluorine started injecting it (issue #49) — pre-
/// existing installs short-circuit downloadSlr() and would otherwise never
/// get xrandr. Safe to call unconditionally.
__attribute__((visibility("default")))
bool ensureXrandrInstalled(
    const int* cancelFlag,
    const std::function<void(const QString&)>& statusCb);

#endif // SLRMANAGER_H
