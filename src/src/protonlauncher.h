#ifndef PROTONLAUNCHER_H
#define PROTONLAUNCHER_H

#include <QMap>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <cstdint>

namespace process_lifetime
{
struct LaunchReceipt;
}

class ProtonLauncher
{
public:
  ProtonLauncher();

  ProtonLauncher& setBinary(const QString& path);
  ProtonLauncher& setArguments(const QStringList& args);
  ProtonLauncher& setWorkingDir(const QString& dir);
  ProtonLauncher& setGameDirectory(const QString& dir);
  ProtonLauncher& setProtonPath(const QString& path);
  ProtonLauncher& setPrefix(const QString& path);
  ProtonLauncher& setSteamAppId(uint32_t id);
  ProtonLauncher& setWrapper(const QString& wrapperCmd);
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

  // True iff the running kernel supports unprivileged user namespaces with
  // CAP_SYS_ADMIN so that `setSavesBindMount` will actually take effect.
  static bool unprivilegedBindMountSupported();

  // Launch dispatch: Proton -> Direct
  process_lifetime::LaunchReceipt launch() const;

private:
  bool launchWithProton(process_lifetime::LaunchReceipt& receipt) const;
  bool launchDirect(process_lifetime::LaunchReceipt& receipt) const;
  static bool ensureSteamRunning();

  QString m_binary;
  QStringList m_arguments;
  QString m_workingDir;
  QString m_gameDirectory;
  QString m_protonPath;
  QString m_prefixPath;
  uint32_t m_steamAppId{0};
  QStringList m_wrapperCommands;
  bool m_useSteamDrm{true};
  bool m_useSLR = true;
  QString m_storeVariant; // "GOG", "Epic", or empty for Steam
  QMap<QString, QString> m_envVars;
  QMap<QString, QString> m_wrapperEnvVars;
  bool m_useTerminal = false;
  QString m_bindMountSource;
  QString m_bindMountTarget;
  QString m_usvfsRequestPath;
};

#endif  // PROTONLAUNCHER_H
