#include "nxmhandler_linux.h"

#include <log.h>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>

using namespace MOBase;

namespace
{
constexpr int ToolStartTimeoutMs  = 1000;
constexpr int ToolFinishTimeoutMs = 3000;

QString stableLauncher()
{
  const QString appDir = QCoreApplication::applicationDirPath();
  const QFileInfo launcher(QDir(appDir).filePath(QStringLiteral("fluorine-manager")));
  if (launcher.exists() && launcher.isFile() && launcher.isExecutable()) {
    return launcher.absoluteFilePath();
  }
  return QCoreApplication::applicationFilePath();
}

void runDesktopCommand(const QString& program, const QStringList& arguments)
{
  QProcess proc;
  proc.setProgram(program);
  proc.setArguments(arguments);
  proc.setProcessChannelMode(QProcess::ForwardedChannels);

  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  auto restoreOrStrip = [&env](const QString& var, const QString& original) {
    if (!env.contains(original)) {
      return;
    }
    const QString value = env.value(original);
    if (value.isEmpty()) {
      env.remove(var);
    } else {
      env.insert(var, value);
    }
    env.remove(original);
  };
  restoreOrStrip(QStringLiteral("LD_LIBRARY_PATH"),
                 QStringLiteral("FLUORINE_ORIG_LD_LIBRARY_PATH"));
  restoreOrStrip(QStringLiteral("LD_PRELOAD"),
                 QStringLiteral("FLUORINE_ORIG_LD_PRELOAD"));
  restoreOrStrip(QStringLiteral("PATH"),
                 QStringLiteral("FLUORINE_ORIG_PATH"));
  restoreOrStrip(QStringLiteral("XDG_DATA_DIRS"),
                 QStringLiteral("FLUORINE_ORIG_XDG_DATA_DIRS"));
  restoreOrStrip(QStringLiteral("QT_PLUGIN_PATH"),
                 QStringLiteral("FLUORINE_ORIG_QT_PLUGIN_PATH"));
  env.remove(QStringLiteral("QT_QPA_PLATFORM_PLUGIN_PATH"));
  proc.setProcessEnvironment(env);

  proc.start();
  if (!proc.waitForStarted(ToolStartTimeoutMs)) {
    log::debug("{} is not available", program);
    return;
  }
  if (!proc.waitForFinished(ToolFinishTimeoutMs)) {
    proc.kill();
    proc.waitForFinished(ToolStartTimeoutMs);
    log::warn("{} timed out while refreshing desktop integration", program);
    return;
  }
  if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
    log::warn("{} exited with code {}", program, proc.exitCode());
  }
}

void refreshDesktopAssociationCaches(const QString& applicationsDir)
{
  runDesktopCommand(QStringLiteral("update-desktop-database"),
                    {applicationsDir});
  runDesktopCommand(QStringLiteral("xdg-desktop-menu"),
                    {QStringLiteral("forceupdate")});
}

void clearOwnedPortalChoices(bool includeCurrent, bool includeLegacy)
{
  const QStringList schemes = {
      QStringLiteral("x-scheme-handler/nxm"),
      QStringLiteral("x-scheme-handler/modl"),
  };
  // These are Fluorine's own current handler and the old main desktop ID that
  // accidentally claimed NXM. Never clear Vortex or another manager's choice.
  QStringList appIds{QStringLiteral("com.fluorine.manager")};
  if (includeCurrent) {
    appIds.append(QStringLiteral("com.fluorine.manager.nxm-handler"));
  }
  if (includeLegacy) {
    appIds.append(QStringLiteral("mo2-nxm-handler"));
  }
  for (const QString& scheme : schemes) {
    for (const QString& appId : appIds) {
      QDBusMessage message = QDBusMessage::createMethodCall(
          QStringLiteral("org.freedesktop.impl.portal.PermissionStore"),
          QStringLiteral("/org/freedesktop/impl/portal/PermissionStore"),
          QStringLiteral("org.freedesktop.impl.portal.PermissionStore"),
          QStringLiteral("DeletePermission"));
      message << QStringLiteral("desktop-used-apps") << scheme << appId;
      QDBusConnection::sessionBus().call(message, QDBus::NoBlock);
    }
  }
}

nxm_handler_integration::Result invalidPaths(const QString& message)
{
  return {nxm_handler_integration::Status::IoError, false, {}, message};
}

}  // namespace

nxm_handler_integration::Paths NxmHandlerLinux::paths()
{
  using namespace nxm_handler_integration;
  const QString applicationsDir =
      QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
  const QString configDir =
      QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
  const QString home = QDir::homePath();
  const QString historicalApplications =
      QDir(home).filePath(QStringLiteral(".local/share/applications"));
  const QString historicalConfig =
      QDir(home).filePath(QStringLiteral(".config"));

  Paths result;
  if (!applicationsDir.isEmpty()) {
    result.desktop = QDir(applicationsDir)
                         .filePath(QString::fromLatin1(CurrentDesktopFile));
  }
  if (!home.isEmpty()) {
    const QString historicalCurrent =
        QDir(historicalApplications)
            .filePath(QString::fromLatin1(CurrentDesktopFile));
    if (result.desktop.isEmpty() ||
        QDir::cleanPath(historicalCurrent) != QDir::cleanPath(result.desktop)) {
      result.historicalDesktop = historicalCurrent;
    }
    result.legacyDesktop = QDir(historicalApplications)
                               .filePath(QString::fromLatin1(LegacyDesktopFile));
    result.legacyWrapper =
        QDir(home).filePath(QStringLiteral(".local/bin/mo2-nxm-handler"));
  }
  if (!configDir.isEmpty()) {
    result.mimeApps =
        QDir(configDir).filePath(QStringLiteral("mimeapps.list"));
    result.lockFile =
        QDir(configDir).filePath(QStringLiteral("fluorine-nxm-handler.lock"));
  }

  if (!home.isEmpty()) {
    const QString oldConfigMime =
        QDir(historicalConfig).filePath(QStringLiteral("mimeapps.list"));
    if (result.mimeApps.isEmpty() ||
        QDir::cleanPath(oldConfigMime) != QDir::cleanPath(result.mimeApps)) {
      result.legacyMimeApps.append(oldConfigMime);
    }
    result.legacyMimeApps.append(
        QDir(historicalApplications).filePath(QStringLiteral("mimeapps.list")));
  }
  result.legacyMimeApps.removeDuplicates();
  return result;
}

bool NxmHandlerLinux::recognizesCompleteRegistration()
{
  return nxm_handler_integration::recognizesCompleteRegistration(paths());
}

nxm_handler_integration::Result NxmHandlerLinux::registerHandler(
    bool forceDefault)
{
  const auto integrationPaths = paths();
  if (integrationPaths.desktop.isEmpty() ||
      integrationPaths.mimeApps.isEmpty() ||
      integrationPaths.lockFile.isEmpty()) {
    return invalidPaths(QStringLiteral(
        "cannot resolve the XDG desktop integration directories"));
  }
  if (!QDir().mkpath(QFileInfo(integrationPaths.lockFile).absolutePath())) {
    return invalidPaths(QStringLiteral(
        "cannot create the desktop integration configuration directory"));
  }

  auto result = nxm_handler_integration::install(
      integrationPaths, stableLauncher(), forceDefault);
  if (!result.succeeded()) {
    return result;
  }
  if (result.changed) {
    refreshDesktopAssociationCaches(QFileInfo(integrationPaths.desktop).absolutePath());
  }
  // The old main desktop ID never handled URL arguments correctly. Clear
  // that Fluorine-owned stale chooser result even when an already-complete
  // registration made this filesystem pass a no-op. A strictly proven legacy
  // retirement additionally authorizes clearing the old handler ID.
  clearOwnedPortalChoices(false, result.retiredLegacy);
  return result;
}

nxm_handler_integration::Result NxmHandlerLinux::unregisterHandler()
{
  const auto integrationPaths = paths();
  if (integrationPaths.desktop.isEmpty() ||
      integrationPaths.mimeApps.isEmpty() ||
      integrationPaths.lockFile.isEmpty()) {
    return invalidPaths(QStringLiteral(
        "cannot resolve the XDG desktop integration directories"));
  }
  if (!QFileInfo::exists(QFileInfo(integrationPaths.lockFile).absolutePath())) {
    // Nothing can have been atomically published at the current XDG paths,
    // but historical paths may still exist. Avoid creating directories merely
    // to remove an association; use a temporary lock beside an existing path.
    auto existsOrSymlink = [](const QString& path) {
      const QFileInfo info(path);
      return info.exists() || info.isSymLink();
    };
    bool historicalExists = existsOrSymlink(integrationPaths.desktop) ||
                            existsOrSymlink(integrationPaths.historicalDesktop) ||
                            existsOrSymlink(integrationPaths.legacyDesktop) ||
                            existsOrSymlink(integrationPaths.legacyWrapper) ||
                            existsOrSymlink(integrationPaths.mimeApps);
    for (const QString& legacyMime : integrationPaths.legacyMimeApps) {
      historicalExists = historicalExists || existsOrSymlink(legacyMime);
    }
    if (!historicalExists) {
      // The files may already be gone while the desktop portal still remembers
      // Fluorine as its chooser result. An explicit Remove must clear that
      // process-external state even when filesystem cleanup is a no-op.
      clearOwnedPortalChoices(true, false);
      return {nxm_handler_integration::Status::NoChange};
    }
    if (!QDir().mkpath(QFileInfo(integrationPaths.lockFile).absolutePath())) {
      return invalidPaths(QStringLiteral(
          "cannot create the desktop integration lock directory"));
    }
  }

  auto result = nxm_handler_integration::uninstall(integrationPaths);
  if (result.succeeded() && result.changed) {
    refreshDesktopAssociationCaches(
        QFileInfo(integrationPaths.desktop).absolutePath());
  }
  if (result.succeeded()) {
    clearOwnedPortalChoices(true, result.retiredLegacy);
  }
  return result;
}
