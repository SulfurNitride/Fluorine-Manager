#pragma once

#include <QString>
#include <QStringList>

namespace nxm_handler_integration
{
inline constexpr auto CurrentDesktopFile =
    "com.fluorine.manager.nxm-handler.desktop";
inline constexpr auto LegacyDesktopFile = "mo2-nxm-handler.desktop";

struct Paths
{
  QString desktop;
  QString historicalDesktop;
  QString legacyDesktop;
  QString legacyWrapper;
  QString mimeApps;
  QStringList legacyMimeApps;
  QString lockFile;
};

enum class Status
{
  Success,
  NoChange,
  Collision,
  IoError,
  Busy,
  InvalidInput,
};

struct Result
{
  Status status{Status::Success};
  bool changed{false};
  QString path;
  QString message;
  bool retiredLegacy{false};

  [[nodiscard]] bool succeeded() const noexcept
  {
    return status == Status::Success || status == Status::NoChange;
  }
};

// Installs a marker-owned desktop entry and updates both supported schemes in
// one atomic mimeapps.list transformation. With forceDefault=false, an
// external first-choice handler remains first.
Result install(const Paths& paths, const QString& launcher,
               bool forceDefault);

// Removes only strictly recognized Fluorine artifacts and associations.
// Missing files are a successful no-op; foreign collisions are preserved.
Result uninstall(const Paths& paths);

// Used only to migrate the pre-preference behavior once. This returns true
// only for a complete, strictly recognized current or historical registration.
bool recognizesCompleteRegistration(const Paths& paths);

// Exposed for focused policy tests and the DBus orchestration layer.
QString desktopEntry(const QString& launcher, QString* error = nullptr);

}  // namespace nxm_handler_integration
