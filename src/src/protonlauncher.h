#ifndef PROTONLAUNCHER_H
#define PROTONLAUNCHER_H

#include <QMap>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <utility>

class ProtonLauncher {
public:
  ProtonLauncher();

  ProtonLauncher &setBinary(const QString &path);
  ProtonLauncher &setArguments(const QStringList &args);
  ProtonLauncher &setWorkingDir(const QString &dir);
  ProtonLauncher &setProtonPath(const QString &path);
  ProtonLauncher &setPrefix(const QString &path);
  ProtonLauncher &setSteamAppId(uint32_t id);
  ProtonLauncher &setWrapper(const QString &wrapperCmd);
  ProtonLauncher &setSteamDrm(bool useSteamDrm);
  ProtonLauncher &setSteamOverlay(bool useSteamOverlay);
  ProtonLauncher &setUseSLR(bool useSLR);
  ProtonLauncher &setStoreVariant(const QString &variant);
  ProtonLauncher &addEnvVar(const QString &key, const QString &value);
  ProtonLauncher &addFilesystemPath(const QString &path);
  ProtonLauncher &setUseTerminal(bool useTerminal);

  // Bind-mount `source` over `target` inside a per-launch user+mount
  // namespace so the game's view of `target` redirects to `source`.  Used to
  // make `<prefix>/__MO_Saves` resolve to the profile's saves dir without
  // symlinks (which Wine can accidentally replace with a real directory).
  // Both paths must already exist before launch; the mount is torn down
  // automatically when the game process tree exits.
  ProtonLauncher &setSavesBindMount(const QString &source,
                                    const QString &target);

  // True iff the running kernel supports unprivileged user namespaces with
  // CAP_SYS_ADMIN so that `setSavesBindMount` will actually take effect.
  static bool unprivilegedBindMountSupported();

  // Launch dispatch: Proton -> Direct
  std::pair<bool, qint64> launch() const;

private:
  bool launchWithProton(qint64 &pid) const;
  bool launchDirect(qint64 &pid) const;
  static bool ensureSteamRunning();

  QString m_binary;
  QStringList m_arguments;
  QString m_workingDir;
  QString m_protonPath;
  QString m_prefixPath;
  uint32_t m_steamAppId{0};
  QStringList m_wrapperCommands;
  bool m_useSteamDrm{true};
  bool m_useSteamOverlay = false;
  bool m_useSLR = true;
  QString m_storeVariant; // "GOG", "Epic", or empty for Steam
  QMap<QString, QString> m_envVars;
  QMap<QString, QString> m_wrapperEnvVars;
  QStringList m_extraFilesystemPaths;
  bool m_useTerminal = false;
  QString m_bindMountSource;
  QString m_bindMountTarget;
};

#endif // PROTONLAUNCHER_H
