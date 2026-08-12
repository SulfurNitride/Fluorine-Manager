#include "organizerproxy.h"

#include "applicationcompletion.h"
#include "downloadmanagerproxy.h"
#include "executableslistproxy.h"
#include "gamefeaturesproxy.h"
#include "glob_matching.h"
#include "instancemanager.h"
#include "modlistproxy.h"
#include "organizercore.h"
#include "plugincontainer.h"
#include "plugindatapath.h"
#include "pluginlistproxy.h"
#include "proxyutils.h"
#include "settings.h"
#include "shared/appconfig.h"
#include "shared/util.h"

#include <QApplication>
#include <QDir>
#include <QObject>

#include <algorithm>
#include <memory>

using namespace MOBase;
using namespace MOShared;

namespace
{
using CompletionRegistry =
    ApplicationRunnerRegistry<std::shared_ptr<ApplicationCompletion>>;
constexpr auto ApplicationHandleGrace = std::chrono::minutes(5);
static_assert(CompletionRegistry::CompletedCapacity == 64);

void pruneCompletionRegistry(const std::shared_ptr<CompletionRegistry>& registry)
{
  if (!registry) {
    return;
  }
  const auto retired = registry->prune(
      [](const CompletionRegistry::Entry& entry) {
        return entry.payload && entry.payload->canRetire();
      },
      [](const CompletionRegistry::Entry& entry) {
        return entry.payload &&
               entry.payload->retentionExpired(ApplicationHandleGrace);
      });
  if (!retired.empty()) {
    log::debug("retired {} completed unconsumed application handle(s)",
               retired.size());
  }
}
}  // namespace

OrganizerProxy::OrganizerProxy(OrganizerCore* organizer,
                               PluginContainer* pluginContainer,
                               MOBase::IPlugin* plugin,
                               const QString& pluginIdentifier,
                               const QString& instancePluginDirectory)
    : m_Proxied(organizer), m_PluginContainer(pluginContainer), m_Plugin(plugin),
      m_PluginDataPath(PluginDataPath::select(OrganizerCore::pluginDataPath(),
                                              instancePluginDirectory,
                                              pluginIdentifier)),
      m_MutationGate(std::make_shared<OrganizerProxyMutationGate>(
          organizer->pluginMutationBarrier())),
      m_DownloadManagerProxy(
          std::make_unique<DownloadManagerProxy>(this, organizer->downloadManager())),
      m_ModListProxy(std::make_unique<ModListProxy>(this, organizer->modList())),
      m_ExecutablesListProxy(
          std::make_unique<ExecutablesListProxy>(organizer->executablesList())),
      m_PluginListProxy(
          std::make_unique<PluginListProxy>(this, organizer->pluginList())),
      m_GameFeaturesProxy(
          std::make_unique<GameFeaturesProxy>(this, pluginContainer->gameFeatures()))
{}

OrganizerProxy::~OrganizerProxy()
{
  m_MutationGate->close();
  disconnectSignals();
}

void OrganizerProxy::connectSignals()
{
  retainConnection(
      m_Proxied->onAboutToRun(callSignalIfPluginActive(this, m_AboutToRun, true)));
  retainConnection(
      m_Proxied->onFinishedRun(callSignalIfPluginActive(this, m_FinishedRun)));
  retainConnection(
      m_Proxied->onProfileCreated(callSignalIfPluginActive(this, m_ProfileCreated)));
  retainConnection(
      m_Proxied->onProfileRenamed(callSignalIfPluginActive(this, m_ProfileRenamed)));
  retainConnection(
      m_Proxied->onProfileRemoved(callSignalIfPluginActive(this, m_ProfileRemoved)));
  retainConnection(
      m_Proxied->onProfileChanged(callSignalIfPluginActive(this, m_ProfileChanged)));

  retainConnection(m_Proxied->onUserInterfaceInitialized(
      callSignalAlways(this, m_UserInterfaceInitialized)));
  retainConnection(
      m_Proxied->onPluginSettingChanged(callSignalAlways(this, m_PluginSettingChanged)));
  retainConnection(
      m_Proxied->onPluginEnabled(callSignalAlways(this, m_PluginEnabled)));
  retainConnection(
      m_Proxied->onPluginDisabled(callSignalAlways(this, m_PluginDisabled)));

  // Connect the child proxies.
  m_DownloadManagerProxy->connectSignals();
  m_ModListProxy->connectSignals();
  m_PluginListProxy->connectSignals();
}

void OrganizerProxy::disconnectSignals()
{
  std::vector<boost::signals2::connection> connections;
  {
    const std::lock_guard lock(m_ConnectionsMutex);
    m_ConnectionsClosed = true;
    connections.swap(m_Connections);
  }

  // Disconnect the child proxies.
  m_DownloadManagerProxy->disconnectSignals();
  m_ModListProxy->disconnectSignals();
  m_PluginListProxy->disconnectSignals();

  for (auto& conn : connections) {
    conn.disconnect();
  }
}

bool OrganizerProxy::retainConnection(boost::signals2::connection connection)
{
  const bool connected = connection.connected();
  bool retained = false;
  if (connected) {
    const std::lock_guard lock(m_ConnectionsMutex);
    if (!m_ConnectionsClosed) {
      std::erase_if(m_Connections,
                    [](const auto& current) { return !current.connected(); });
      m_Connections.push_back(connection);
      retained = true;
    }
  }
  if (connected && !retained) {
    connection.disconnect();
  }
  return retained;
}

IModRepositoryBridge* OrganizerProxy::createNexusBridge() const
{
  IModRepositoryBridge* bridge = nullptr;
  runMutationIfAllowed(
      [&] { bridge = new NexusBridge(m_MutationGate, m_Plugin->name()); });
  return bridge;
}

QString OrganizerProxy::instanceName() const
{
  QString result;
  runPluginCallIfAllowed([&] {
    result = InstanceManager::singleton().currentInstance()->displayName();
  });
  return result;
}

QString OrganizerProxy::profileName() const
{
  QString result;
  runPluginCallIfAllowed([&] { result = m_Proxied->profileName(); });
  return result;
}

QString OrganizerProxy::profilePath() const
{
  QString result;
  runPluginCallIfAllowed([&] { result = m_Proxied->profilePath(); });
  return result;
}

QString OrganizerProxy::downloadsPath() const
{
  QString result;
  runPluginCallIfAllowed([&] { result = m_Proxied->downloadsPath(); });
  return result;
}

QString OrganizerProxy::overwritePath() const
{
  QString result;
  runPluginCallIfAllowed([&] { result = m_Proxied->overwritePath(); });
  return result;
}

QString OrganizerProxy::basePath() const
{
  QString result;
  runPluginCallIfAllowed([&] { result = m_Proxied->basePath(); });
  return result;
}

QString OrganizerProxy::modsPath() const
{
  QString result;
  runPluginCallIfAllowed([&] { result = m_Proxied->modsPath(); });
  return result;
}

Version OrganizerProxy::version() const
{
  Version result(0, 0, 0);
  runPluginCallIfAllowed([&] { result = m_Proxied->version(); });
  return result;
}

VersionInfo OrganizerProxy::appVersion() const
{
  Version version(0, 0, 0);
  if (!runPluginCallIfAllowed([&] { version = m_Proxied->version(); })) {
    return {};
  }
  const int major = version.major();
  const int minor = version.minor();
  const int subminor                       = version.patch();
  int subsubminor                          = 0;
  VersionInfo::ReleaseType infoReleaseType = VersionInfo::RELEASE_FINAL;

  // make a copy
  auto prereleases = version.preReleases();

  if (!prereleases.empty()) {
    // check if the first pre-release entry is a number
    if (prereleases.front().index() == 0) {
      subsubminor = std::get<int>(prereleases.front());
      prereleases.erase(prereleases.begin());
    }

    if (!prereleases.empty()) {
      const auto releaseType = std::get<Version::ReleaseType>(prereleases.front());
      switch (releaseType) {
      case Version::Development:
        infoReleaseType = VersionInfo::RELEASE_PREALPHA;
        break;
      case Version::Alpha:
        infoReleaseType = VersionInfo::RELEASE_ALPHA;
        break;
      case Version::Beta:
        infoReleaseType = VersionInfo::RELEASE_BETA;
        break;
      case Version::ReleaseCandidate:
        infoReleaseType = VersionInfo::RELEASE_CANDIDATE;
        break;
      default:
        infoReleaseType = VersionInfo::RELEASE_PREALPHA;
      }
    }

    // there is no way to differentiate two pre-releases?
  }

  return {major, minor, subminor, subsubminor, infoReleaseType};
}

IPluginGame* OrganizerProxy::getGame(const QString& gameName) const
{
  IPluginGame* game = nullptr;
  runPluginCallIfAllowed([&] { game = m_Proxied->getGame(gameName); });
  return game;
}

IModInterface* OrganizerProxy::createMod(MOBase::GuessedValue<QString>& name)
{
  IModInterface* mod = nullptr;
  runMutationIfAllowed([&] { mod = m_Proxied->createMod(name); });
  return mod;
}

void OrganizerProxy::modDataChanged(IModInterface* mod)
{
  runMutationIfAllowed([&] { m_Proxied->modDataChanged(mod); });
}

bool OrganizerProxy::isPluginEnabled(QString const& pluginName) const
{
  bool enabled = false;
  runPluginCallIfAllowed(
      [&] { enabled = m_PluginContainer->isEnabled(pluginName); });
  return enabled;
}

bool OrganizerProxy::isPluginEnabled(IPlugin* plugin) const
{
  bool enabled = false;
  runPluginCallIfAllowed([&] { enabled = m_PluginContainer->isEnabled(plugin); });
  return enabled;
}

QVariant OrganizerProxy::pluginSetting(const QString& pluginName,
                                       const QString& key) const
{
  QVariant result;
  runPluginCallIfAllowed(
      [&] { result = m_Proxied->pluginSetting(pluginName, key); });
  return result;
}

void OrganizerProxy::setPluginSetting(const QString& pluginName, const QString& key,
                                      const QVariant& value)
{
  runMutationIfAllowed(
      [&] { m_Proxied->setPluginSetting(pluginName, key, value); });
}

QVariant OrganizerProxy::persistent(const QString& pluginName, const QString& key,
                                    const QVariant& def) const
{
  QVariant result = def;
  runPluginCallIfAllowed(
      [&] { result = m_Proxied->persistent(pluginName, key, def); });
  return result;
}

void OrganizerProxy::setPersistent(const QString& pluginName, const QString& key,
                                   const QVariant& value, bool sync)
{
  runMutationIfAllowed(
      [&] { m_Proxied->setPersistent(pluginName, key, value, sync); });
}

QString OrganizerProxy::pluginDataPath() const
{
  QString result;
  runPluginCallIfAllowed([&] { result = m_PluginDataPath; });
  return result;
}

HANDLE OrganizerProxy::startApplication(const QString& exe, const QStringList& args,
                                        const QString& cwd, const QString& profile,
                                        const QString& overwrite, bool ignoreOverwrite)
{
  HANDLE handle = INVALID_HANDLE_VALUE;
  runMutationIfAllowed([&] {
    log::debug("a plugin has requested to start an application:\n"
               " . executable: '{}'\n"
               " . args: '{}'\n"
               " . cwd: '{}'\n"
               " . profile: '{}'\n"
               " . overwrite: '{}'\n"
               " . ignore overwrite: {}",
               exe, args.join(" "), cwd, profile, overwrite, ignoreOverwrite);

    auto runner = m_Proxied->processRunner();

    // don't wait for completion
    const auto result =
        runner.setFromFileOrExecutable(exe, args, cwd, profile, overwrite,
                                       ignoreOverwrite)
        .run();

    if (result == ProcessRunner::Error) {
      return;
    }

    // Linux exposes HANDLE as an opaque API token rather than an operating
    // system handle. Its one monitor starts now, not when/if the plugin waits,
    // so launch-owned cleanup is independent of public handle consumption.
    auto completion = runner.monitorApplication();
    if (completion) {
      const auto retainedCompletion = completion;
      const auto opaque = m_ApplicationRunners->insert(completion->rootPid(),
                                                       std::move(completion));
      const std::weak_ptr<CompletionRegistry> weakRegistry = m_ApplicationRunners;
      retainedCompletion->onCleanupFinished([weakRegistry]() {
        if (const auto registry = weakRegistry.lock()) {
          pruneCompletionRegistry(registry);
        }
      });
      pruneApplicationHandles();
      handle = reinterpret_cast<HANDLE>(opaque);
    }
  });
  return handle;
}

bool OrganizerProxy::waitForApplication(HANDLE handle, bool refresh,
                                        LPDWORD exitCode) const
{
  bool result = false;
  if (!runPluginCallIfAllowed(
          [&] { result = waitForApplicationImpl(handle, refresh, exitCode); })) {
    if (exitCode) {
      *exitCode = static_cast<DWORD>(-1);
    }
  }
  return result;
}

bool OrganizerProxy::waitForApplicationImpl(HANDLE handle, bool refresh,
                                            LPDWORD exitCode) const
{
  const auto handleValue = reinterpret_cast<std::uintptr_t>(handle);
  pid_t pid = static_cast<pid_t>(handleValue);
  std::shared_ptr<ApplicationCompletion> completion;
  if (ApplicationRegistry::isOpaqueHandle(handleValue)) {
    pruneApplicationHandles();
    auto retained = m_ApplicationRunners->take(handleValue);
    if (!retained) {
      log::error("unknown, consumed, released, or retired startApplication "
                 "handle {}",
                 handleValue);
      if (exitCode) {
        *exitCode = static_cast<DWORD>(-1);
      }
      return false;
    }
    pid = retained->pid;
    completion = std::move(retained->payload);
  }

  log::debug("a plugin wants to wait for an application to complete, pid {}", pid);

  auto runner = m_Proxied->processRunner();

  ProcessRunner::WaitFlags waitFlags = ProcessRunner::ForceWait;

  if (refresh) {
    waitFlags |= ProcessRunner::TriggerRefresh | ProcessRunner::WaitForRefresh;
  }

  runner.setWaitForCompletion(waitFlags, UILocker::OutputRequired);
  const auto r = completion
                     ? runner.waitForApplicationCompletion(completion)
                     // Preserve compatibility with handles not created by
                     // startApplication(). The legacy path captures and
                     // validates the raw PID's current generation before it
                     // begins waiting.
                     : runner.attachToProcess(pid);

  if (exitCode) {
    *exitCode = runner.exitCode();
  }

  switch (r) {
  case ProcessRunner::Completed:
    return true;

  case ProcessRunner::Cancelled:  // fall-through
  case ProcessRunner::ForceUnlocked:
    // this is always an error because the application should have run to
    // completion
    return false;

  case ProcessRunner::Error:  // fall-through
  default:
    return false;
  }
}

bool OrganizerProxy::releaseApplicationHandle(HANDLE handle)
{
  bool result = false;
  runPluginCallIfAllowed(
      [&] { result = releaseApplicationHandleImpl(handle); });
  return result;
}

bool OrganizerProxy::releaseApplicationHandleImpl(HANDLE handle)
{
  const auto handleValue = reinterpret_cast<std::uintptr_t>(handle);
  if (!ApplicationRegistry::isOpaqueHandle(handleValue)) {
    return false;
  }

  pruneApplicationHandles();
  const bool released = m_ApplicationRunners->release(handleValue);
  if (!released) {
    log::debug("application handle {} was already consumed, released, or retired",
               handleValue);
  }
  return released;
}

void OrganizerProxy::pruneApplicationHandles() const
{
  pruneCompletionRegistry(m_ApplicationRunners);
}

void OrganizerProxy::refresh(bool saveChanges)
{
  runMutationIfAllowed([&] { m_Proxied->refresh(saveChanges); });
}

IModInterface* OrganizerProxy::installMod(const QString& fileName,
                                          const QString& nameSuggestion)
{
  IModInterface* mod = nullptr;
  runMutationIfAllowed([&] {
    mod = m_Proxied->installMod(fileName, -1, false, nullptr, nameSuggestion);
  });
  return mod;
}

QString OrganizerProxy::resolvePath(const QString& fileName) const
{
  QString result;
  runPluginCallIfAllowed([&] { result = m_Proxied->resolvePath(fileName); });
  return result;
}

QStringList OrganizerProxy::listDirectories(const QString& directoryName) const
{
  QStringList result;
  runPluginCallIfAllowed(
      [&] { result = m_Proxied->listDirectories(directoryName); });
  return result;
}

QStringList
OrganizerProxy::findFiles(const QString& path,
                          const std::function<bool(const QString&)>& filter) const
{
  QStringList result;
  runPluginCallIfAllowed([&] { result = m_Proxied->findFiles(path, filter); });
  return result;
}

QStringList OrganizerProxy::findFiles(const QString& path,
                                      const QStringList& globFilters) const
{
  QList<GlobPattern<QChar>> patterns;
  for (const auto& gfilter : globFilters) {
    patterns.append(GlobPattern(gfilter));
  }
  return findFiles(path, [&patterns](const QString& filename) {
    for (auto& p : patterns) {
      if (p.match(filename)) {
        return true;
      }
    }
    return false;
  });
}

QStringList OrganizerProxy::getFileOrigins(const QString& fileName) const
{
  QStringList result;
  runPluginCallIfAllowed(
      [&] { result = m_Proxied->getFileOrigins(fileName); });
  return result;
}

QList<MOBase::IOrganizer::FileInfo> OrganizerProxy::findFileInfos(
    const QString& path,
    const std::function<bool(const MOBase::IOrganizer::FileInfo&)>& filter) const
{
  QList<MOBase::IOrganizer::FileInfo> result;
  runPluginCallIfAllowed(
      [&] { result = m_Proxied->findFileInfos(path, filter); });
  return result;
}

std::shared_ptr<const MOBase::IFileTree> OrganizerProxy::virtualFileTree() const
{
  std::shared_ptr<const MOBase::IFileTree> result;
  runPluginCallIfAllowed([&] { result = m_Proxied->m_VirtualFileTree.value(); });
  return result;
}

MOBase::IDownloadManager* OrganizerProxy::downloadManager() const
{
  MOBase::IDownloadManager* result = nullptr;
  runPluginCallIfAllowed([&] { result = m_DownloadManagerProxy.get(); });
  return result;
}

MOBase::IPluginList* OrganizerProxy::pluginList() const
{
  MOBase::IPluginList* result = nullptr;
  runPluginCallIfAllowed([&] { result = m_PluginListProxy.get(); });
  return result;
}

MOBase::IModList* OrganizerProxy::modList() const
{
  MOBase::IModList* result = nullptr;
  runPluginCallIfAllowed([&] { result = m_ModListProxy.get(); });
  return result;
}

MOBase::IExecutablesList* OrganizerProxy::executablesList() const
{
  MOBase::IExecutablesList* result = nullptr;
  runPluginCallIfAllowed([&] { result = m_ExecutablesListProxy.get(); });
  return result;
}

MOBase::IGameFeatures* OrganizerProxy::gameFeatures() const
{
  MOBase::IGameFeatures* result = nullptr;
  runPluginCallIfAllowed([&] { result = m_GameFeaturesProxy.get(); });
  return result;
}

bool OrganizerProxy::previewFileData(QWidget* parent, const QString& fileName,
                                     const QByteArray& fileData)
{
  bool result = false;
  runMutationIfAllowed(
      [&] { result = m_Proxied->previewFileData(parent, fileName, fileData); });
  return result;
}

std::shared_ptr<MOBase::IProfile> OrganizerProxy::profile() const
{
  std::shared_ptr<MOBase::IProfile> result;
  runPluginCallIfAllowed([&] { result = m_Proxied->currentProfile(); });
  return result;
}

QStringList OrganizerProxy::profileNames() const
{
  QStringList result;
  runPluginCallIfAllowed([&] { result = m_Proxied->profileNames(); });
  return result;
}

std::shared_ptr<const MOBase::IProfile>
OrganizerProxy::getProfile(const QString& name) const
{
  std::shared_ptr<const MOBase::IProfile> profile;
  runMutationIfAllowed([&] { profile = m_Proxied->getProfile(name); });
  return profile;
}

MOBase::IPluginGame const* OrganizerProxy::managedGame() const
{
  MOBase::IPluginGame const* result = nullptr;
  runPluginCallIfAllowed([&] { result = m_Proxied->managedGame(); });
  return result;
}

// CALLBACKS

bool OrganizerProxy::onAboutToRun(const std::function<bool(const QString&)>& func)
{
  bool connected = false;
  runMutationIfAllowed([&] {
    connected = retainConnection(m_Proxied->onAboutToRun(
        MOShared::callIfPluginActive(
            this,
            [func](const QString& binary, const QDir&, const QString&) {
              return func(binary);
            },
            true)));
  });
  return connected;
}

bool OrganizerProxy::onAboutToRun(
    const std::function<bool(const QString&, const QDir&, const QString&)>& func)
{
  bool connected = false;
  runMutationIfAllowed([&] {
    connected = retainConnection(
        m_Proxied->onAboutToRun(MOShared::callIfPluginActive(this, func, true)));
  });
  return connected;
}

bool OrganizerProxy::onFinishedRun(
    const std::function<void(const QString&, unsigned int)>& func)
{
  bool connected = false;
  runMutationIfAllowed([&] {
    connected = retainConnection(
        m_Proxied->onFinishedRun(MOShared::callIfPluginActive(this, func)));
  });
  return connected;
}

bool OrganizerProxy::onUserInterfaceInitialized(
    std::function<void(QMainWindow*)> const& func)
{
  // Always call this one to allow plugin to initialize themselves even when not active:
  bool connected = false;
  runPluginCallIfAllowed(
      [&] { connected = m_UserInterfaceInitialized.connect(func).connected(); });
  return connected;
}

bool OrganizerProxy::onNextRefresh(const std::function<void()>& func,
                                   bool immediateIfPossible)
{
  using enum OrganizerCore::RefreshCallbackMode;
  bool connected = false;
  runMutationIfAllowed([&] {
    connected = retainConnection(
        m_Proxied->onNextRefresh(MOShared::callIfPluginActive(this, func),
                                 OrganizerCore::RefreshCallbackGroup::EXTERNAL,
                                 immediateIfPossible ? RUN_NOW_IF_POSSIBLE
                                                     : FORCE_WAIT_FOR_REFRESH));
  });
  return connected;
}

bool OrganizerProxy::onProfileCreated(std::function<void(IProfile*)> const& func)
{
  bool connected = false;
  runPluginCallIfAllowed(
      [&] { connected = m_ProfileCreated.connect(func).connected(); });
  return connected;
}

bool OrganizerProxy::onProfileRenamed(
    std::function<void(IProfile*, QString const&, QString const&)> const& func)
{
  bool connected = false;
  runPluginCallIfAllowed(
      [&] { connected = m_ProfileRenamed.connect(func).connected(); });
  return connected;
}

bool OrganizerProxy::onProfileRemoved(std::function<void(QString const&)> const& func)
{
  bool connected = false;
  runPluginCallIfAllowed(
      [&] { connected = m_ProfileRemoved.connect(func).connected(); });
  return connected;
}

bool OrganizerProxy::onProfileChanged(
    std::function<void(MOBase::IProfile*, MOBase::IProfile*)> const& func)
{
  bool connected = false;
  runPluginCallIfAllowed(
      [&] { connected = m_ProfileChanged.connect(func).connected(); });
  return connected;
}
// Always call these one, otherwise plugin cannot detect they are being enabled /
// disabled:
bool OrganizerProxy::onPluginSettingChanged(
    std::function<void(QString const&, const QString& key, const QVariant&,
                       const QVariant&)> const& func)
{
  bool connected = false;
  runPluginCallIfAllowed(
      [&] { connected = m_PluginSettingChanged.connect(func).connected(); });
  return connected;
}

bool OrganizerProxy::onPluginEnabled(std::function<void(const IPlugin*)> const& func)
{
  bool connected = false;
  runPluginCallIfAllowed(
      [&] { connected = m_PluginEnabled.connect(func).connected(); });
  return connected;
}

bool OrganizerProxy::onPluginEnabled(const QString& pluginName,
                                     std::function<void()> const& func)
{
  return onPluginEnabled([=](const IPlugin* plugin) {
    if (plugin->name().compare(pluginName, Qt::CaseInsensitive) == 0) {
      func();
    }
  });
}

bool OrganizerProxy::onPluginDisabled(std::function<void(const IPlugin*)> const& func)
{
  bool connected = false;
  runPluginCallIfAllowed(
      [&] { connected = m_PluginDisabled.connect(func).connected(); });
  return connected;
}

bool OrganizerProxy::onPluginDisabled(const QString& pluginName,
                                      std::function<void()> const& func)
{
  return onPluginDisabled([=](const IPlugin* plugin) {
    if (plugin->name().compare(pluginName, Qt::CaseInsensitive) == 0) {
      func();
    }
  });
}
