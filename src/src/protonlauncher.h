#ifndef PROTONLAUNCHER_H
#define PROTONLAUNCHER_H

#include "nativeprocess.h"

#include <QMap>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <utility>

class ProtonLauncher
{
public:
  ProtonLauncher();

  ProtonLauncher& setBinary(const QString& path);
  ProtonLauncher& setArguments(const QStringList& args);
  ProtonLauncher& setWorkingDir(const QString& dir);
  ProtonLauncher& setGameDirectory(const QString& dir);
  // Used when the installation has no Steam language metadata.
  ProtonLauncher& setGameLocale(const QString& locale);
  ProtonLauncher& setProtonPath(const QString& path);
  ProtonLauncher& setPrefix(const QString& path);
  ProtonLauncher& setSteamAppId(uint32_t id);
  // Global commands wrap local commands; local variables override global ones.
  ProtonLauncher& setWrapper(const QString& wrapperCmd,
                             const QString& executableOptions = {});
  ProtonLauncher& setSteamDrm(bool useSteamDrm);
  ProtonLauncher& setUseSLR(bool useSLR);
  ProtonLauncher& setStoreVariant(const QString& variant);
  ProtonLauncher& addEnvVar(const QString& key, const QString& value);
  ProtonLauncher& setUseTerminal(bool useTerminal);

  // Bind-mount `source` over `target` inside a per-launch user+mount
  // namespace so the game's view of `target` redirects to `source`.  Used to
  // make `<prefix>/__MO_Saves` resolve to the profile's saves dir without
  // symlinks (which Wine can accidentally replace with a real directory).
  // Both paths must already exist before launch; the mount is torn down
  // automatically when the game process tree exits.
  ProtonLauncher& setSavesBindMount(const QString& source, const QString& target);
  ProtonLauncher& setUsvfsRequest(const QString& requestPath);
  // Diagnostics log for the USVFS helper; passed to it as a second argument
  // so the helper can open the file before parsing the request.
  ProtonLauncher& setUsvfsLog(const QString& logPath);

  // True iff the running kernel supports unprivileged user namespaces with
  // CAP_SYS_ADMIN so that `setSavesBindMount` will actually take effect.
  static bool unprivilegedBindMountSupported();

  // Launch dispatch: Proton -> Direct
  env::NativeProcess launch() const;

private:
  bool launchWithProton(env::NativeProcess& process) const;
  bool launchDirect(env::NativeProcess& process) const;
  static bool ensureSteamRunning();

public:
  // Default for the per-instance "Steam DRM" option when the user has not set
  // it: Steam installs need the Steam bridge, including copies of the game
  // files made outside the Steam library (Stock Game, Game Root) and
  // instances with no recorded edition, because those executables still
  // validate against Steam. An explicit GOG, Epic or other edition must not
  // start (or wait for) Steam unless its files sit in a Steam library or have
  // a steam_appid.txt.
  static bool defaultSteamDrmForGame(const QString& gameDirectory,
                                     const QString& storeVariant = {});

  // The one place that decides the Steam DRM option for an instance, shared by
  // the launch and the library panel so the checkbox always shows what the
  // launch will do. An explicit fluorine/steam_drm in the instance ini wins;
  // otherwise the default is computed from that ini's own game path and
  // edition (not from a separately detected game directory).
  struct SteamDrmChoice
  {
    bool enabled          = true;
    bool explicitSetting  = false;
    QString storeVariant;
    QString gameDirectory;
  };
  static SteamDrmChoice steamDrmForInstance(const QString& iniPath,
                                            const QString& fallbackGameDirectory = {});

private:

  QString m_binary;
  QStringList m_arguments;
  QString m_workingDir;
  QString m_gameDirectory;
  QString m_gameLocale;
  QString m_protonPath;
  QString m_prefixPath;
  uint32_t m_steamAppId{0};
  QStringList m_wrapperCommands;
  bool m_useSteamDrm{true};
  bool m_useSLR = true;
  QString m_storeVariant; // "GOG", "Epic", or empty for Steam
  QMap<QString, QString> m_envVars;
  QMap<QString, QString> m_wrapperEnvVars;
  QMap<QString, QString> m_executableEnvVars;
  QString m_wrapperError;
  bool m_useTerminal = false;
  QString m_bindMountSource;
  QString m_bindMountTarget;
  QString m_usvfsRequestPath;
  QString m_usvfsLogPath;
};

#endif  // PROTONLAUNCHER_H
