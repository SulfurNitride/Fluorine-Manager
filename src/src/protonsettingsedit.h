#ifndef PROTONSETTINGSEDIT_H
#define PROTONSETTINGSEDIT_H

#include "fluorineconfig.h"

#include <QSettings>
#include <QVariant>

#include <optional>

namespace ProtonSettingsEdit
{
inline const QString LaunchWrapperKey =
    QStringLiteral("fluorine/launch_wrapper");
inline const QString DisableVfsCacheKey =
    QStringLiteral("fluorine/disable_vfs_cache");

struct Value
{
  bool present{false};
  QVariant value;

  friend bool operator==(const Value&, const Value&) = default;
};

struct ConfigState
{
  bool present{false};
  FluorineConfig config;
};

struct Snapshot
{
  Value launchWrapper;
  Value disableVfsCache;
  ConfigState config;
};

// Captures only the settings owned by the Proton tab. A present but unreadable
// or malformed config.json is a capture failure, not an absent configuration.
std::optional<Snapshot> capture(QSettings& defaultSettings,
                                int lockTimeoutMs = 5000);
std::optional<Snapshot> capture(int lockTimeoutMs = 5000);

// Persists ordinary Proton-tab edits on Settings acceptance. Prefix lifecycle
// buttons are separate, explicitly destructive transactions and are not
// performed here.
bool persist(QSettings& defaultSettings, const QString& launchWrapper,
             bool disableVfsCache, const QString& protonName,
             const QString& protonPath, int lockTimeoutMs = 5000);
bool persist(const QString& launchWrapper, bool disableVfsCache,
             const QString& protonName, const QString& protonPath,
             int lockTimeoutMs = 5000);

// Restores keys still equal to this dialog's rejected state. Proton name/path
// are restored only if app_id/prefix_path/created are unchanged. This retains
// explicit create/delete/recreate results and never attempts filesystem undo.
bool restore(QSettings& defaultSettings, const Snapshot& original,
             const Snapshot& rejected, int lockTimeoutMs = 5000);
bool restore(const Snapshot& original, const Snapshot& rejected,
             int lockTimeoutMs = 5000);

bool verify(QSettings& defaultSettings, const Snapshot& original,
            const Snapshot& rejected, int lockTimeoutMs = 5000);
bool verify(const Snapshot& original, const Snapshot& rejected,
            int lockTimeoutMs = 5000);

void suppressWritesForFailedRollback() noexcept;
bool failedRollbackWritesDrained() noexcept;
}  // namespace ProtonSettingsEdit

#endif  // PROTONSETTINGSEDIT_H
