#include "processrunner.h"
#include "processlifetime.h"
#include "env.h"
#include "envmodule.h"
#include "launchenvironment.h"
#include "instancemanager.h"
#include "iuserinterface.h"
#include "organizercore.h"
#include "vfsbackend.h"
#include "steamcloudsync.h"

#include <iplugingame.h>
#include <log.h>
#include <report.h>
#include <uibase/utility.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QSettings>
#include <QThread>

#include <cerrno>
#include <utility>


using namespace MOBase;

void adjustForVirtualized(const IPluginGame* game, spawn::SpawnParameters& sp,
                          const Settings& settings)
{
  const QString modsPath = settings.paths().mods();

  // Check if this is a request with either an executable or a working
  // directory under our mods folder; if so, start the process in a
  // virtualized "environment" with the appropriate paths fixed:
  // (i.e. mods/FNIS/path/exe => game/data/path/exe)
  QString cwdPath         = sp.currentDirectory.absolutePath();
  QString trailedModsPath = modsPath;
  if (!trailedModsPath.endsWith('/')) {
    trailedModsPath = trailedModsPath + '/';
  }
  bool virtualizedCwd = cwdPath.startsWith(trailedModsPath, Qt::CaseInsensitive);
  QString binPath     = sp.binary.absoluteFilePath();
  bool virtualizedBin = binPath.startsWith(trailedModsPath, Qt::CaseInsensitive);
  if (!virtualizedCwd && !virtualizedBin) {
    return;
  }

  if (virtualizedCwd) {
    int cwdOffset       = cwdPath.indexOf('/', trailedModsPath.length());
    QString adjustedCwd = cwdPath.mid(cwdOffset, -1);
    cwdPath             = game->dataDirectory().absolutePath();
    if (cwdOffset >= 0)
      cwdPath += adjustedCwd;
  }

  if (virtualizedBin) {
    int binOffset       = binPath.indexOf('/', trailedModsPath.length());
    QString adjustedBin = binPath.mid(binOffset, -1);
    binPath             = game->dataDirectory().absolutePath();
    if (binOffset >= 0)
      binPath += adjustedBin;
  }

  // FUSE is already mounted on Linux — resolve paths directly without
  // launching through MO2-core (which would fail in the Proton prefix).
  //
  // Root Builder deploys Root/ contents to the game directory root,
  // stripping the "Root/" prefix.  Fix paths that were remapped to
  // <dataDir>/Root/... so they point to <gameDir>/... instead.
  // Also handle direct mods/.../Root/ paths (not just dataDir/Root/).
  const QString gameDir = game->gameDirectory().absolutePath();
  const QString dataDir = game->dataDirectory().absolutePath();

  auto normalizeRootPath = [&](QString& path) {
    const QString rootWithSlash = dataDir + QStringLiteral("/Root/");
    const QString rootExact     = dataDir + QStringLiteral("/Root");
    if (path.startsWith(rootWithSlash, Qt::CaseInsensitive)) {
      const QString after = path.mid(rootWithSlash.length());
      path = after.isEmpty() ? gameDir : gameDir + QStringLiteral("/") + after;
      return true;
    }
    if (path.compare(rootExact, Qt::CaseInsensitive) == 0) {
      path = gameDir;
      return true;
    }
    return false;
  };

  bool binNormalized = normalizeRootPath(binPath);
  bool cwdNormalized = normalizeRootPath(cwdPath);

  if (binNormalized) {
    log::info("Root Builder: rewrote binary -> '{}'", binPath);
  }
  if (cwdNormalized) {
    log::info("Root Builder: rewrote start-in -> '{}'", cwdPath);
  }

  // If neither was caught by the dataDir/Root/ check, the path might still be
  // the original mods/.../Root/ path (not yet remapped). This happens when
  // the first remapping above produced something that didn't match the
  // dataDir/Root pattern.
  if (!binNormalized && binPath.startsWith(trailedModsPath, Qt::CaseInsensitive)) {
    int rootIdx =
        binPath.indexOf("/Root/", trailedModsPath.length(), Qt::CaseInsensitive);
    if (rootIdx < 0)
      rootIdx =
          binPath.indexOf("/Root", trailedModsPath.length(), Qt::CaseInsensitive);
    if (rootIdx >= 0) {
      int afterRootStart = rootIdx + 5;  // skip "/Root"
      if (afterRootStart < binPath.length() && binPath[afterRootStart] == '/')
        ++afterRootStart;
      const QString afterRoot = binPath.mid(afterRootStart);
      const QString modRoot   = binPath.left(rootIdx + 5);

      binPath = afterRoot.isEmpty() ? gameDir
                                    : gameDir + QStringLiteral("/") + afterRoot;
      log::info("Root Builder: rewrote binary (mod path) -> '{}'", binPath);

      if (!cwdNormalized && cwdPath.startsWith(modRoot, Qt::CaseInsensitive)) {
        int cwdAfterStart = modRoot.length();
        if (cwdAfterStart < cwdPath.length() && cwdPath[cwdAfterStart] == '/')
          ++cwdAfterStart;
        const QString cwdAfter = cwdPath.mid(cwdAfterStart);
        cwdPath = cwdAfter.isEmpty() ? gameDir
                                     : gameDir + QStringLiteral("/") + cwdAfter;
        log::info("Root Builder: rewrote start-in (mod path) -> '{}'", cwdPath);
      }
    }
  }

  sp.binary = QFileInfo(binPath);
  sp.currentDirectory.setPath(cwdPath);
}

QStringList buildExpectedExecutables(const QFileInfo& binary, const QString& arguments)
{
  QStringList expected;
  auto addName = [&](QString name) {
    name = name.trimmed().toLower();
    if (!name.isEmpty() && !expected.contains(name)) {
      expected.push_back(name);
    }
  };

  addName(binary.fileName());

  const auto args = QProcess::splitCommand(arguments);
  for (const QString& arg : args) {
    const QFileInfo fi(arg);
    const QString base = fi.fileName();
    if (base.endsWith(".exe", Qt::CaseInsensitive)) {
      addName(base);
    }
  }

  log::debug("buildExpectedExecutables: returning [{}]",
             expected.join(", ").toStdString());
  return expected;
}

namespace
{
ProcessRunner::Results runnerResult(env::ProcessWaitResult result)
{
  switch (result) {
  case env::ProcessWaitResult::Completed: return ProcessRunner::Completed;
  case env::ProcessWaitResult::Cancelled: return ProcessRunner::Cancelled;
  case env::ProcessWaitResult::Unlocked: return ProcessRunner::ForceUnlocked;
  default: return ProcessRunner::Error;
  }
}
}  // namespace

ProcessRunner::ProcessRunner(OrganizerCore& core, IUserInterface* ui)
    : m_core(core), m_ui(ui),
      m_waitFlags(NoFlags)
{
  // all processes started in ProcessRunner are hooked by default
  setHooked(true);
}

ProcessRunner& ProcessRunner::setBinary(const QFileInfo& binary)
{
  m_sp.binary = QFileInfo(MOBase::normalizePathForHost(binary.filePath()));
  return *this;
}

ProcessRunner& ProcessRunner::setArguments(const QString& arguments)
{
  m_sp.arguments = arguments;
  return *this;
}

ProcessRunner& ProcessRunner::setCurrentDirectory(const QDir& directory)
{
  m_sp.currentDirectory.setPath(MOBase::normalizePathForHost(directory.path()));
  return *this;
}

ProcessRunner& ProcessRunner::setSteamID(const QString& steamID)
{
  m_sp.steamAppID = steamID;
  return *this;
}

ProcessRunner& ProcessRunner::setCustomOverwrite(const QString& customOverwrite)
{
  m_customOverwrite = customOverwrite;
  return *this;
}

ProcessRunner& ProcessRunner::setForcedLibraries(const ForcedLibraries& forcedLibraries)
{
  m_forcedLibraries = forcedLibraries;
  return *this;
}

ProcessRunner& ProcessRunner::setProfileName(const QString& profileName)
{
  m_profileName = profileName;
  return *this;
}

ProcessRunner& ProcessRunner::setWaitForCompletion(WaitFlags flags,
                                                   UILocker::Reasons reason)
{
  m_waitFlags  = flags;
  m_lockReason = reason;

  if (m_waitFlags.testFlag(WaitForRefresh) && !m_waitFlags.testFlag(TriggerRefresh)) {
    log::warn("process runner: WaitForRefresh without TriggerRefresh "
              "makes no sense, will be ignored");
  }

  return *this;
}

ProcessRunner& ProcessRunner::setHooked(bool b)
{
  m_sp.hooked = b;
  return *this;
}

ProcessRunner& ProcessRunner::setFromFile(QWidget* parent, const QFileInfo& targetInfo)
{
  if (!parent && m_ui) {
    parent = m_ui->mainWindow();
  }

  // if the file is a .exe, start it directly; if it's anything else, ask the
  // shell to start it
  const auto fec = spawn::getFileExecutionContext(parent, targetInfo);

  switch (fec.type) {
  case spawn::FileExecutionTypes::Executable: {
    setBinary(fec.binary);
    setArguments(fec.arguments);
    setCurrentDirectory(targetInfo.absoluteDir());
    break;
  }

  case spawn::FileExecutionTypes::Other:
  default: {
    m_shellOpen = targetInfo;
    setHooked(false);
    break;
  }
  }

  return *this;
}

ProcessRunner& ProcessRunner::setFromExecutable(const Executable& exe)
{
  const auto profile = m_core.currentProfile();
  if (!profile) {
    throw MyException(QObject::tr("No profile set"));
  }

  const QString customOverwrite =
      profile->setting("custom_overwrites", exe.title()).toString();

  ForcedLibraries forcedLibraries;
  if (profile->forcedLibrariesEnabled(exe.title())) {
    forcedLibraries = profile->determineForcedLibraries(exe.title());
  }

  QString currentDirectory = exe.workingDirectory();
  if (currentDirectory.isEmpty()) {
    currentDirectory = exe.binaryInfo().absolutePath();
  }

  setBinary(exe.binaryInfo());
  setArguments(exe.arguments());
  setCurrentDirectory(currentDirectory);
  setSteamID(exe.steamAppID());
  m_sp.useSteam = exe.useSteam();
  m_sp.wrapperOptions = exe.wrapperOptions();
  setCustomOverwrite(customOverwrite);
  setForcedLibraries(forcedLibraries);

  m_sp.useProton    = exe.useProton();
  m_sp.useTerminal  = exe.useTerminal();

  return *this;
}

ProcessRunner& ProcessRunner::setFromShortcut(const MOShortcut& shortcut)
{
  const auto currentInstance = InstanceManager::singleton().currentInstance();

  if (currentInstance) {
    if (shortcut.hasInstance() && !shortcut.isForInstance(*currentInstance)) {
      MOBase::reportError(QObject::tr("This shortcut is for instance '%1' but "
                                      "Mod Organizer is currently "
                                      "running for '%2'. Exit Mod Organizer "
                                      "before running the shortcut or "
                                      "change the active instance.")
                              .arg(shortcut.instanceDisplayName())
                              .arg(currentInstance->displayName()));

      throw std::exception();
    }
  }

  const auto* exes = m_core.executablesList();
  const auto exe   = exes->find(shortcut.executableName());

  if (exe != exes->end()) {
    setFromExecutable(*exe);
  } else {
    MOBase::reportError(QObject::tr("Executable '%1' does not exist in instance '%2'.")
                            .arg(shortcut.executableName())
                            .arg(currentInstance->displayName()));

    throw std::exception();
  }

  return *this;
}

ProcessRunner& ProcessRunner::setFromFileOrExecutable(
    const QString& executable, const QStringList& args, const QString& cwd,
    const QString& profileOverride, const QString& forcedCustomOverwrite,
    bool ignoreCustomOverwrite)
{
  const auto profile = m_core.currentProfile();
  if (!profile) {
    throw MyException(QObject::tr("No profile set"));
  }

  setBinary(QFileInfo(executable));
  setArguments(args.join(" "));
  setCurrentDirectory(cwd);
  setProfileName(profileOverride);

  if (executable.contains('\\') || executable.contains('/')) {
    if (m_sp.binary.isRelative()) {
      setBinary(QFileInfo(
          m_core.managedGame()->gameDirectory().absoluteFilePath(executable)));
    }

    if (cwd == "") {
      setCurrentDirectory(m_sp.binary.absolutePath());
    }

    try {
      const Executable& exe = m_core.executablesList()->getByBinary(m_sp.binary);

      setSteamID(exe.steamAppID());
      m_sp.useSteam = exe.useSteam();
      m_sp.wrapperOptions = exe.wrapperOptions();
      m_sp.useProton = exe.useProton();
      m_sp.useTerminal = exe.useTerminal();
      setCustomOverwrite(profile->setting("custom_overwrites", exe.title()).toString());

      if (profile->forcedLibrariesEnabled(exe.title())) {
        setForcedLibraries(profile->determineForcedLibraries(exe.title()));
      }
    } catch (const std::runtime_error&) {
      // nop
    }
  } else {
    try {
      const Executable& exe = m_core.executablesList()->get(executable);

      setSteamID(exe.steamAppID());
      m_sp.useSteam = exe.useSteam();
      m_sp.wrapperOptions = exe.wrapperOptions();
      m_sp.useProton = exe.useProton();
      m_sp.useTerminal = exe.useTerminal();
      setCustomOverwrite(profile->setting("custom_overwrites", exe.title()).toString());

      if (profile->forcedLibrariesEnabled(exe.title())) {
        setForcedLibraries(profile->determineForcedLibraries(exe.title()));
      }

      if (args.isEmpty()) {
        setArguments(exe.arguments());
      }

      setBinary(exe.binaryInfo());

      if (cwd == "") {
        setCurrentDirectory(exe.workingDirectory());
      }
    } catch (const std::runtime_error&) {
      log::warn("\"{}\" not set up as executable", executable);
    }
  }

  if (ignoreCustomOverwrite) {
    setCustomOverwrite("");
  } else if (!forcedCustomOverwrite.isEmpty()) {
    setCustomOverwrite(forcedCustomOverwrite);
  }

  return *this;
}

bool ProcessRunner::shouldRunShell() const
{
  return !m_shellOpen.filePath().isEmpty();
}

ProcessRunner::Results ProcessRunner::run()
{
  // check if setHooked() was called after setFromFile(); this needs to modify
  // the settings to run the associated executable instead of using
  // shell::Open()
  if (shouldRunShell() && m_sp.hooked) {
    auto assoc = env::getAssociation(m_shellOpen);
    if (!assoc.executable.filePath().isEmpty()) {
      setBinary(assoc.executable);
      setArguments(assoc.formattedCommandLine);
      setCurrentDirectory(assoc.executable.absoluteDir());
      // XDG desktop entries describe host Linux programs. Run the resolved
      // argv directly so the application can use the mounted host FUSE VFS;
      // wrapping a Linux binary as a Proton game cannot work.
      m_sp.useProton = false;
      m_sp.useSteam = false;
      m_shellOpen = {};
    } else {
      log::error("failed to resolve the native file association for '{}'; "
                 "refusing to launch it outside the hooked VFS",
                 m_shellOpen.absoluteFilePath());
      errno = ENOENT;
      return Error;
    }
  } else if (!shouldRunShell() && !m_sp.hooked) {
    m_shellOpen = m_sp.binary;
  }

  std::optional<Results> r;

  if (shouldRunShell()) {
    r = runShell();
  } else {
    r = runBinary();
  }

  if (r) {
    return *r;
  }

  return postRun();
}

std::optional<ProcessRunner::Results> ProcessRunner::runShell()
{
  const auto file = MOBase::normalizePathForHost(m_shellOpen.absoluteFilePath());

  log::debug("executing from shell: '{}'", file);

  auto r = shell::Open(file);
  if (!r.success()) {
    return Error;
  }

  // xdg-open acknowledges the desktop request but does not return the
  // application process. There is no owned child to wait for here.
  m_process = {};
  return Running;
}

std::optional<ProcessRunner::Results> ProcessRunner::runBinary()
{
  if (m_profileName.isEmpty()) {
    const auto profile = m_core.currentProfile();
    if (!profile) {
      throw MyException(QObject::tr("No profile set"));
    }

    m_profileName = profile->name();
  }

  const auto* game = m_core.managedGame();
  auto& settings   = m_core.settings();

  QWidget* parent = (m_ui ? m_ui->mainWindow() : nullptr);
  const QString appId = m_sp.steamAppID.trimmed().isEmpty()
                            ? game->steamAPPId() : m_sp.steamAppID;
  if (m_sp.useProton && m_sp.useSteam
      && SteamCloud::supported(appId, m_sp.binary.fileName())
      && SteamCloudSync::optIn(parent, settings.filename())) {
    const auto profile = m_core.currentProfile();
    if (!profile || profile->localSavesEnabled() || profile->name() != m_profileName) {
      QMessageBox::warning(parent, QObject::tr("Steam Cloud"),
          QObject::tr("Automatic Cyberpunk cloud sync currently requires the active profile "
                      "with profile-specific saves disabled. Steam Cloud has one save collection "
                      "per account/game; separate profile save collections are not supported yet."));
      return Error;
    }
    m_cloudSync = std::make_shared<SteamCloudSync>(parent, settings.filename(),
        game->gameDirectory().absolutePath(), game->savesDirectory().absolutePath());
    if (!m_cloudSync->prepare()) { m_cloudSync.reset(); return Error; }
    // Hold the UI and the cloud-session lock through process-tree exit and cleanup.
    m_waitFlags |= ForceWait | TriggerRefresh;
    m_lockReason = UILocker::LockUI;
  }

  // FUSE makes an executable stored under mods/ visible at its virtual game
  // path before adjustForVirtualized runs. USVFS is installed by the Windows
  // helper later, so its request must contain that virtual target from the
  // outset.
  const QSettings instanceIni(settings.filename(), QSettings::IniFormat);
  const bool preparingUsvfs = useUsvfsForLaunch(
      parseVfsBackend(
          instanceIni.value(kVfsBackendSetting, QStringLiteral("fuse"))
              .toString()),
      m_sp.useProton, game == nullptr || game->usesVFS());
  if (preparingUsvfs && game != nullptr) {
    adjustForVirtualized(game, m_sp, settings);
  }

  // saves profile, sets up the VFS, notifies plugins, etc.; can return false
  // if a plugin doesn't want the program to run.
  if (!m_core.beforeRun(m_sp.binary, m_sp.currentDirectory, m_sp.arguments,
                        m_profileName, m_customOverwrite, m_forcedLibraries,
                        m_sp.useProton, &m_sp.usvfsRequestPath,
                        &m_sp.saveBindMountSource, &m_sp.saveBindMountTarget,
                        &m_sp.usvfsLogPath)) {
    return Error;
  }

  const auto abortPreparedLaunch = [this]() {
    if (!m_sp.usvfsRequestPath.isEmpty()) {
      QFile::remove(m_sp.usvfsRequestPath);
      m_sp.usvfsRequestPath.clear();
    }
    m_core.unmountVFS();
  };

  m_sp.gameDirectory = game->gameDirectory();
  m_sp.gameLocale.clear();
  if (m_sp.useProton && !game->iniFiles().isEmpty()) {
    // Resolve the profile selected for this launch, including shortcuts that
    // name a different profile. absoluteIniFilePath respects local INIs and
    // resolves imported Windows filename casing on Linux.
    std::shared_ptr<const MOBase::IProfile> profile = m_core.currentProfile();
    if (!profile || profile->name() != m_profileName) {
      profile = m_core.getProfile(m_profileName);
    }
    if (profile) {
      QStringList iniPaths;
      for (const QString& ini : game->iniFiles()) {
        iniPaths.append(profile->absoluteIniFilePath(ini));
      }
      m_sp.gameLocale = gameLocaleFromIniFiles(iniPaths);
      if (!m_sp.gameLocale.isEmpty()) {
        log::debug("Profile '{}' game INIs select fallback locale '{}'",
                   m_profileName, m_sp.gameLocale);
      }
    }
  }

  if (m_sp.steamAppID.trimmed().isEmpty()) {
    const QString gameSteamId = game->steamAPPId().trimmed();
    if (!gameSteamId.isEmpty()) {
      m_sp.steamAppID = gameSteamId;
      log::debug("process runner: using game steam app id '{}' for launch",
                 m_sp.steamAppID);
    }
  }

  if (!checkSteam(parent, m_sp, game->gameDirectory(), m_sp.steamAppID, settings)) {
    abortPreparedLaunch();
    return Error;
  }

  if (!checkBlacklist(parent, m_sp, settings)) {
    abortPreparedLaunch();
    return Error;
  }

  // if the executable is inside the mods folder another instance of
  // ModOrganizer is spawned instead to launch it
  if (!preparingUsvfs) {
    adjustForVirtualized(game, m_sp, settings);
  }

  if (m_cloudSync && !m_cloudSync->markLaunching()) {
    abortPreparedLaunch();
    return Error;
  }
  m_process = startBinary(parent, m_sp);

  if (!m_process) {
    // beforeRun may have deployed Root Builder files for the Wine-side USVFS
    // helper. Use the normal VFS teardown path when process creation fails.
    abortPreparedLaunch();
    return Error;
  }

  return {};
}

bool ProcessRunner::shouldRefresh(Results r) const
{
  // afterRun() is only called with the Refresh flag; it refreshes the
  // directory structure and notifies plugins.
  //
  // Refreshing is not always required and can actually cause problems:
  //
  //  1) running shortcuts doesn't need refreshing because MO closes right
  //     after
  //
  //  2) the mod info dialog is not set up to deal with refreshes, so that it
  //     will crash because the old DirectoryEntry's are still being used in
  //     the list
  if (!m_waitFlags.testFlag(TriggerRefresh)) {
    log::debug("process runner: not refreshing because the flag isn't set");
    return false;
  }

  switch (r) {
  case Completed: {
    log::debug("process runner: refreshing because the process completed");
    return true;
  }

  case ForceUnlocked: {
    // The ForceUnlocked branch in waitForProcessTree has already taken down the
    // game's process tree and wineserver, so by the time we're here no
    // Wine process is still writing under the prefix. Run afterRun() so
    // the FUSE VFS is unmounted, game-dir permissions are restored, and
    // local saves are synced back. The exit code is set non-zero in that
    // branch, which gates plugin sync-back (Plugins.txt may have been
    // half-written when we killed the game).
    log::debug("process runner: running afterRun to unmount VFS after force unlock");
    return true;
  }

  case Error:
  case Cancelled:
  case Running:
  default: {
    return false;
  }
  }
}

ProcessRunner::Results ProcessRunner::postRun()
{
  const bool mustWait = (m_waitFlags & ForceWait);

  if (!m_sp.hooked && !mustWait) {
    return Running;
  }

  if (mustWait && m_lockReason == UILocker::NoReason) {
    log::debug("the ForceWait flag is set but the lock reason wasn't, "
               "defaulting to LockUI");

    m_lockReason = UILocker::LockUI;
  }

  const QStringList expectedExecutables = m_attachedExpectedExecutables
      ? *m_attachedExpectedExecutables : expectedExecutablesForTracking();
  const bool usingUsvfsHelper = !m_sp.usvfsRequestPath.isEmpty();

  if (usingUsvfsHelper) {
    log::debug("process runner: using {} as the USVFS lifetime anchor",
               kUsvfsLauncherExecutable);
  }

  if (!mustWait) {
    if (m_lockReason == UILocker::NoReason) {
      // Main window launches typically use TriggerRefresh without
      // waiting/locking. In that mode we still need post-run refresh/sync once
      // the process exits.
      if (m_waitFlags.testFlag(TriggerRefresh)) {
        const env::NativeProcess process = m_process;
        const pid_t pid = process.pid();
        const QFileInfo binary       = m_sp.binary;
        QPointer<OrganizerCore> core = &m_core;
        auto cloudSync = std::exchange(m_cloudSync, {});

        std::thread([core, binary, process, pid, expectedExecutables, cloudSync]() {
          const auto completion =
              env::waitForProcessTree(process, expectedExecutables);
          const auto result = runnerResult(completion.result);
          int exitCode = completion.exitCode;

          if (result != ProcessRunner::Completed) {
            MOBase::log::warn(
                "process runner: asynchronous lifetime tracking failed for "
                "pid {} (result {})",
                pid, static_cast<int>(result));
            exitCode = 1;
          }

          if (!core) {
            return;
          }

          QMetaObject::invokeMethod(
              core,
              [core, binary, exitCode, result, cloudSync]() {
                if (core) {
                  // An error means lifetime tracking could not prove the
                  // workload has exited. Keep the VFS mounted rather than
                  // unmounting under a possibly active game process.
                  if (result == ProcessRunner::Completed ||
                      result == ProcessRunner::ForceUnlocked) {
                    core->afterRun(binary, exitCode);
                  }
                  if (cloudSync) {
                    cloudSync->finish(result == ProcessRunner::Completed &&
                                      exitCode == 0);
                  }
                }
              },
              Qt::QueuedConnection);
        }).detach();

        log::debug(
            "process runner: scheduled async post-run refresh for pid {} "
            "tracking [{}]",
            pid, expectedExecutables.join(", ").toStdString());
      }
      return Running;
    }
  }

  // Only tear down the launched process tree on force-unlock for games that
  // have a wineprefix/FUSE VFS to clean up. Native, non-VFS games (OpenMW)
  // must keep running when the user releases the lock — same usesVFS() gate as
  // the FUSE mount in OrganizerCore (default true = unchanged for every other
  // game).
  const auto* game        = m_core.managedGame();
  const bool killTreeOnUnlock = (game == nullptr) || game->usesVFS();

  auto r = Error;
  withLock([&](auto& ls) {
    const auto completion = env::waitForProcessTree(
        m_process, expectedExecutables, killTreeOnUnlock,
        [&](pid_t pid, const QString& name) {
          ls.setInfo(pid, name);
          switch (UILocker::Session::result()) {
          case UILocker::StillLocked: return env::ProcessWaitAction::Continue;
          case UILocker::Cancelled: return env::ProcessWaitAction::Cancel;
          case UILocker::ForceUnlocked: return env::ProcessWaitAction::Unlock;
          default: return env::ProcessWaitAction::Abort;
          }
        });
    r = runnerResult(completion.result);
    m_exitCode = completion.exitCode;
  });

  if (shouldRefresh(r)) {
    QEventLoop loop;
    const bool wait = m_waitFlags.testFlag(WaitForRefresh);

    if (wait) {
      QObject::connect(&m_core, &OrganizerCore::directoryStructureReady, &loop,
                       &QEventLoop::quit, Qt::ConnectionType::QueuedConnection);
    }

    m_core.afterRun(m_sp.binary, m_exitCode);

    if (wait) {
      log::debug("process runner: waiting until refresh finishes");
      loop.exec();
      log::debug("process runner: refresh is done");
    }
  }

  if (m_cloudSync) {
    m_cloudSync->finish(r == Completed && m_exitCode == 0);
    m_cloudSync.reset();
  }

  return r;
}

ProcessRunner::Results ProcessRunner::attachToProcess(
    env::NativeProcess process,
    std::optional<QStringList> expectedExecutables)
{
  m_process = std::move(process);
  m_attachedExpectedExecutables = std::move(expectedExecutables);
  return m_process ? postRun() : Error;
}

int ProcessRunner::exitCode() const
{
  return m_exitCode;
}

pid_t ProcessRunner::processId() const
{
  return m_process.pid();
}

env::NativeProcess ProcessRunner::takeProcess()
{
  return std::exchange(m_process, {});
}

QStringList ProcessRunner::expectedExecutablesForTracking() const
{
  return processTrackingExecutables(
      buildExpectedExecutables(m_sp.binary, m_sp.arguments),
      !m_sp.usvfsRequestPath.isEmpty());
}

void ProcessRunner::withLock(std::function<void(UILocker::Session&)> f)
{
  auto ls = UILocker::instance().lock(m_lockReason);
  f(*ls);
}
