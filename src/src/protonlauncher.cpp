#include "protonlauncher.h"
#include "fluorinepaths.h"

#include <nak_ffi.h>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <log.h>

namespace
{
QString compatDataPathFromPrefix(const QString& prefixPath)
{
  if (prefixPath.isEmpty()) {
    return {};
  }

  QDir prefixDir(prefixPath);
  if (prefixDir.dirName() == "pfx") {
    if (prefixDir.cdUp()) {
      return QDir::cleanPath(prefixDir.absolutePath());
    }
  }

  return QDir::cleanPath(QFileInfo(prefixPath).dir().absolutePath());
}

QString detectSteamPath()
{
  if (char* steamPathRaw = nak_find_steam_path(); steamPathRaw != nullptr) {
    const QString steamPath = QString::fromUtf8(steamPathRaw).trimmed();
    nak_string_free(steamPathRaw);

    if (!steamPath.isEmpty()) {
      return steamPath;
    }
  }

  const QString homeSteam = QDir::home().filePath(".steam/steam");
  if (QFileInfo::exists(homeSteam)) {
    return homeSteam;
  }

  const QString homeRoot = QDir::home().filePath(".steam/root");
  if (QFileInfo::exists(homeRoot)) {
    return homeRoot;
  }

  return {};
}

bool startDetachedWithEnv(const QString& program, const QStringList& arguments,
                          const QString& workingDir,
                          const QProcessEnvironment& environment, qint64& pid)
{
  QProcess process;
  process.setProgram(program);
  process.setArguments(arguments);

  if (!workingDir.isEmpty()) {
    process.setWorkingDirectory(workingDir);
  }

  process.setProcessEnvironment(environment);
  return process.startDetached(&pid);
}

void wrapProgram(const QStringList& wrapperCommands, const QString& program,
                 const QStringList& arguments, QString& wrappedProgram,
                 QStringList& wrappedArguments)
{
  if (wrapperCommands.isEmpty()) {
    wrappedProgram   = program;
    wrappedArguments = arguments;
    return;
  }

  wrappedProgram = wrapperCommands.first();
  wrappedArguments.clear();

  if (wrapperCommands.size() > 1) {
    wrappedArguments.append(wrapperCommands.mid(1));
  }

  wrappedArguments.append(program);
  wrappedArguments.append(arguments);
}

void maybeWrapWithSteamRun(bool useSteamRun, QString& program, QStringList& arguments)
{
  if (!useSteamRun) {
    return;
  }

  QStringList wrappedArgs;
  wrappedArgs.append(program);
  wrappedArgs.append(arguments);
  program   = QStringLiteral("steam-run");
  arguments = wrappedArgs;
}

bool isFlatpak()
{
  return QFileInfo::exists(QStringLiteral("/.flatpak-info"));
}

// In Flatpak, Wine/Proton binaries can't execute inside the sandbox (they need
// the Steam Runtime's linker and 32-bit libs).  Wrap them with flatpak-spawn
// --host so they run on the host system instead.
//
// flatpak-spawn --host runs via the Flatpak portal D-Bus interface, which does
// NOT reliably forward the caller's process environment.  We must pass any
// custom env vars explicitly with --env= flags.
void maybeWrapForFlatpak(QString& program, QStringList& arguments,
                         const QProcessEnvironment& env)
{
  if (!isFlatpak()) {
    return;
  }

  QStringList wrappedArgs;
  wrappedArgs.append(QStringLiteral("--host"));

  // Pass every env var that differs from the inherited system environment.
  const QProcessEnvironment sysEnv = QProcessEnvironment::systemEnvironment();
  for (const QString& key : env.keys()) {
    const QString val = env.value(key);
    if (val != sysEnv.value(key)) {
      wrappedArgs.append(QStringLiteral("--env=%1=%2").arg(key, val));
    }
  }

  wrappedArgs.append(program);
  wrappedArgs.append(arguments);
  program   = QStringLiteral("flatpak-spawn");
  arguments = wrappedArgs;
}

bool isValidEnvKey(const QString& key)
{
  if (key.isEmpty()) {
    return false;
  }

  const QChar first = key.front();
  if (!(first.isLetter() || first == QChar('_'))) {
    return false;
  }

  for (const QChar c : key) {
    if (!(c.isLetterOrNumber() || c == QChar('_'))) {
      return false;
    }
  }

  return true;
}

bool parseEnvAssignment(const QString& token, QString& keyOut, QString& valueOut)
{
  const int eq = token.indexOf('=');
  if (eq <= 0) {
    return false;
  }

  const QString key = token.left(eq);
  if (!isValidEnvKey(key)) {
    return false;
  }

  keyOut   = key;
  valueOut = token.mid(eq + 1);
  return true;
}
}  // namespace

ProtonLauncher::ProtonLauncher()
    : m_steamAppId(0), m_useUmu(false), m_preferSystemUmu(false),
      m_useSteamRun(false)
{}

ProtonLauncher& ProtonLauncher::setBinary(const QString& path)
{
  m_binary = path.trimmed();
  return *this;
}

ProtonLauncher& ProtonLauncher::setArguments(const QStringList& args)
{
  m_arguments = args;
  return *this;
}

ProtonLauncher& ProtonLauncher::setWorkingDir(const QString& dir)
{
  m_workingDir = dir.trimmed();
  return *this;
}

ProtonLauncher& ProtonLauncher::setProtonPath(const QString& path)
{
  m_protonPath = path.trimmed();
  return *this;
}

ProtonLauncher& ProtonLauncher::setPrefix(const QString& path)
{
  m_prefixPath = path.trimmed();
  return *this;
}

ProtonLauncher& ProtonLauncher::setSteamAppId(uint32_t id)
{
  m_steamAppId = id;
  return *this;
}

ProtonLauncher& ProtonLauncher::setWrapper(const QString& wrapperCmd)
{
  m_wrapperCommands.clear();
  m_wrapperEnvVars.clear();

  const QStringList parts = QProcess::splitCommand(wrapperCmd.trimmed());
  for (const QString& part : parts) {
    if (part.compare("%command%", Qt::CaseInsensitive) == 0) {
      continue;
    }
    QString key;
    QString value;
    if (parseEnvAssignment(part, key, value)) {
      m_wrapperEnvVars.insert(key, value);
    } else {
      m_wrapperCommands.push_back(part);
    }
  }

  return *this;
}

ProtonLauncher& ProtonLauncher::setUmu(bool useUmu)
{
  m_useUmu = useUmu;
  return *this;
}

ProtonLauncher& ProtonLauncher::setPreferSystemUmu(bool preferSystemUmu)
{
  m_preferSystemUmu = preferSystemUmu;
  return *this;
}

ProtonLauncher& ProtonLauncher::setUseSteamRun(bool useSteamRun)
{
  m_useSteamRun = useSteamRun;
  return *this;
}

ProtonLauncher& ProtonLauncher::addEnvVar(const QString& key, const QString& value)
{
  if (!key.isEmpty()) {
    m_envVars.insert(key, value);
  }

  return *this;
}

std::pair<bool, qint64> ProtonLauncher::launch() const
{
  qint64 pid = -1;

  if (m_useUmu) {
    if (launchWithUmu(pid)) {
      return {true, pid};
    }
    MOBase::log::warn("UMU launch failed, falling back to Proton");
  }

  if (!m_protonPath.isEmpty()) {
    return {launchWithProton(pid), pid};
  }

  return {launchDirect(pid), pid};
}

bool ProtonLauncher::launchWithProton(qint64& pid) const
{
  if (m_binary.isEmpty() || m_protonPath.isEmpty()) {
    return false;
  }

  ensureSteamRunning();

  QString protonScript = m_protonPath;
  if (QFileInfo(protonScript).isDir()) {
    protonScript = QDir(m_protonPath).filePath("proton");
  }

  const QStringList protonArgs = QStringList() << "run" << m_binary << m_arguments;

  QString program;
  QStringList arguments;
  wrapProgram(m_wrapperCommands, protonScript, protonArgs, program, arguments);
  maybeWrapWithSteamRun(m_useSteamRun, program, arguments);

  // Build environment BEFORE flatpak wrapping (flatpak-spawn needs --env= flags).
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.remove("PYTHONHOME");

  if (!m_prefixPath.isEmpty()) {
    env.insert("WINEPREFIX", m_prefixPath);
  }

  const QString compatDataPath = compatDataPathFromPrefix(m_prefixPath);
  if (!compatDataPath.isEmpty()) {
    env.insert("STEAM_COMPAT_DATA_PATH", compatDataPath);
  }

  const QString steamPath = detectSteamPath();
  if (!steamPath.isEmpty()) {
    env.insert("STEAM_COMPAT_CLIENT_INSTALL_PATH", steamPath);
  }

  if (m_steamAppId != 0) {
    const QString appId = QString::number(m_steamAppId);
    env.insert("SteamAppId", appId);
    env.insert("SteamGameId", appId);
  }

  env.insert("DOTNET_ROOT", "");
  env.insert("DOTNET_MULTILEVEL_LOOKUP", "0");

  for (auto it = m_wrapperEnvVars.cbegin(); it != m_wrapperEnvVars.cend(); ++it) {
    env.insert(it.key(), it.value());
  }

  for (auto it = m_envVars.cbegin(); it != m_envVars.cend(); ++it) {
    env.insert(it.key(), it.value());
  }

  // Set DXVK config if available
  if (char* dxvkPath = nak_get_dxvk_conf_path(); dxvkPath != nullptr) {
    const QString dxvkConf = QString::fromUtf8(dxvkPath);
    nak_string_free(dxvkPath);
    if (QFileInfo::exists(dxvkConf)) {
      env.insert("DXVK_CONFIG_FILE", dxvkConf);
    }
  }

  maybeWrapForFlatpak(program, arguments, env);

  MOBase::log::info("Proton launch: '{}' run '{}'", protonScript, m_binary);
  return startDetachedWithEnv(program, arguments, m_workingDir, env, pid);
}

bool ProtonLauncher::launchWithUmu(qint64& pid) const
{
  if (m_binary.isEmpty()) {
    return false;
  }

  // Steam must be running for games with Steamworks DRM (Application Load
  // Error 5:0000065434 occurs otherwise).
  ensureSteamRunning();

  // Resolve umu-run according to user preference (bundled vs system).
  // In Flatpak, umu-run must run on the host (it needs Steam Runtime).
  // Use the full path to our copied umu-run since the host PATH won't include it.
  QString umuRun;
  if (isFlatpak()) {
    const QString flatpakUmu = fluorineDataDir() + QStringLiteral("/umu-run");
    if (QFileInfo::exists(flatpakUmu)) {
      umuRun = flatpakUmu;
    } else {
      // Fall back to bare name (requires host to have umu-run in PATH)
      umuRun = QStringLiteral("umu-run");
    }
  } else {
    const QString bundled =
        QCoreApplication::applicationDirPath() + QStringLiteral("/umu-run");
    const QString system = QStandardPaths::findExecutable(QStringLiteral("umu-run"));

    if (m_preferSystemUmu) {
      if (!system.isEmpty()) {
        umuRun = system;
      } else if (QFileInfo::exists(bundled)) {
        umuRun = bundled;
        MOBase::log::warn(
            "System umu-run preferred but not found in PATH, falling back to bundled");
      }
    } else {
      if (QFileInfo::exists(bundled)) {
        umuRun = bundled;
      } else if (!system.isEmpty()) {
        umuRun = system;
      }
    }
  }

  if (umuRun.isEmpty()) {
    MOBase::log::warn("umu-run not found (bundled or in PATH)");
    return false;
  }

  const QStringList umuArgs = QStringList() << m_binary << m_arguments;

  QString program;
  QStringList arguments;
  wrapProgram(m_wrapperCommands, umuRun, umuArgs, program, arguments);
  maybeWrapWithSteamRun(m_useSteamRun, program, arguments);

  // Build environment BEFORE flatpak wrapping (flatpak-spawn needs --env= flags).
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.remove("PYTHONHOME");

  if (!m_prefixPath.isEmpty()) {
    env.insert("WINEPREFIX", m_prefixPath);
  }

  if (!m_protonPath.isEmpty()) {
    env.insert("PROTONPATH", m_protonPath);
  }

  // umu-run sets STEAM_COMPAT_DATA_PATH internally from WINEPREFIX, so we
  // do NOT set it here.  However, the game's Steamworks DRM still needs
  // STEAM_COMPAT_CLIENT_INSTALL_PATH to locate the Steam client libraries.
  const QString steamPath = detectSteamPath();
  if (!steamPath.isEmpty()) {
    env.insert("STEAM_COMPAT_CLIENT_INSTALL_PATH", steamPath);
  }

  uint32_t effectiveSteamAppId = m_steamAppId;
  if (effectiveSteamAppId == 0) {
    bool ok = false;
    const QString inheritedSteamAppId =
        env.value("SteamAPPId", env.value("SteamAppId")).trimmed();
    const uint32_t parsed = inheritedSteamAppId.toUInt(&ok);
    if (ok) {
      effectiveSteamAppId = parsed;
    }
  }

  if (effectiveSteamAppId != 0) {
    // umu-run expects GAMEID in "umu-<AppID>" format to extract SteamAppId.
    env.insert("GAMEID", QStringLiteral("umu-") + QString::number(effectiveSteamAppId));
    env.insert("SteamAppId", QString::number(effectiveSteamAppId));
    env.insert("SteamGameId", QString::number(effectiveSteamAppId));
  }

  for (auto it = m_wrapperEnvVars.cbegin(); it != m_wrapperEnvVars.cend(); ++it) {
    env.insert(it.key(), it.value());
  }

  for (auto it = m_envVars.cbegin(); it != m_envVars.cend(); ++it) {
    env.insert(it.key(), it.value());
  }

  // Set DXVK config if available
  if (char* dxvkPath = nak_get_dxvk_conf_path(); dxvkPath != nullptr) {
    const QString dxvkConf = QString::fromUtf8(dxvkPath);
    nak_string_free(dxvkPath);
    if (QFileInfo::exists(dxvkConf)) {
      env.insert("DXVK_CONFIG_FILE", dxvkConf);
    }
  }

  maybeWrapForFlatpak(program, arguments, env);

  MOBase::log::info("UMU launch: '{}' '{}' (game id: {}, steam: '{}')", umuRun,
                    m_binary,
                    (effectiveSteamAppId == 0
                         ? QStringLiteral("<unset>")
                         : QStringLiteral("umu-") +
                               QString::number(effectiveSteamAppId)),
                    steamPath);
  return startDetachedWithEnv(program, arguments, m_workingDir, env, pid);
}

bool ProtonLauncher::launchDirect(qint64& pid) const
{
  if (m_binary.isEmpty()) {
    return false;
  }

  QString program;
  QStringList arguments;
  wrapProgram(m_wrapperCommands, m_binary, m_arguments, program, arguments);
  maybeWrapWithSteamRun(m_useSteamRun, program, arguments);

  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.remove("PYTHONHOME");
  for (auto it = m_wrapperEnvVars.cbegin(); it != m_wrapperEnvVars.cend(); ++it) {
    env.insert(it.key(), it.value());
  }
  for (auto it = m_envVars.cbegin(); it != m_envVars.cend(); ++it) {
    env.insert(it.key(), it.value());
  }

  maybeWrapForFlatpak(program, arguments, env);

  return startDetachedWithEnv(program, arguments, m_workingDir, env, pid);
}

bool ProtonLauncher::ensureSteamRunning()
{
  QProcess pgrep;
  if (isFlatpak()) {
    // In Flatpak, check for Steam on the host.
    pgrep.start("flatpak-spawn", {"--host", "pgrep", "-x", "steam"});
  } else {
    pgrep.start("pgrep", {"-x", "steam"});
  }
  if (pgrep.waitForFinished(2000) && pgrep.exitCode() == 0) {
    return true;
  }

  qint64 pid = -1;
  if (isFlatpak()) {
    if (QProcess::startDetached("flatpak-spawn",
                                {"--host", "steam", "-silent"}, QString(), &pid)) {
      MOBase::log::warn("Steam was not running, started it on host in silent mode");
      return true;
    }
  } else {
    if (QProcess::startDetached("steam", {"-silent"}, QString(), &pid)) {
      MOBase::log::warn("Steam was not running, started it in silent mode");
      return true;
    }
  }

  return false;
}
