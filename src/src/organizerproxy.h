#ifndef ORGANIZERPROXY_H
#define ORGANIZERPROXY_H

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

#include <imoinfo.h>
#include <iplugin.h>

#include "applicationrunnerregistry.h"
#include "organizercore.h"
#include "plugincallgate.h"

class GameFeaturesProxy;
class ApplicationCompletion;
class PluginContainer;
class DownloadManagerProxy;
class ExecutablesListProxy;
class ModListProxy;
class PluginListProxy;

/**
 * Shared, fail-closed access to the OrganizerCore mutation barrier.
 *
 * Plugin-created helper objects can outlive OrganizerProxy. Keeping this state
 * separate from the proxy lets those objects reject work after proxy destruction
 * without retaining a raw OrganizerProxy pointer.
 */
class OrganizerProxyMutationGate : public PluginCallGate
{
public:
  using PluginCallGate::PluginCallGate;
};

class OrganizerProxy : public MOBase::IOrganizer,
                       public MOBase::IOrganizerApplicationHandles
{
  Q_OBJECT
  Q_INTERFACES(MOBase::IOrganizerApplicationHandles)

public:
  OrganizerProxy(OrganizerCore* organizer, PluginContainer* pluginContainer,
                 MOBase::IPlugin* plugin, const QString& pluginIdentifier,
                 const QString& instancePluginDirectory);
  ~OrganizerProxy() override;

public:
  /**
   * @return the plugin corresponding to this proxy.
   */
  MOBase::IPlugin* plugin() const { return m_Plugin; }

  template <typename Mutation>
  bool runMutationIfAllowed(Mutation&& mutation) const
  {
    return m_MutationGate->runIfAllowed(std::forward<Mutation>(mutation));
  }

  std::shared_ptr<OrganizerProxyMutationGate> mutationGate() const
  {
    return m_MutationGate;
  }

  template <typename Callback>
  bool runPluginCallIfAllowed(Callback&& callback) const
  {
    return m_MutationGate->runIfAllowed(std::forward<Callback>(callback));
  }

public:  // IOrganizer interface
  MOBase::IModRepositoryBridge* createNexusBridge() const override;
  QString instanceName() const override;
  QString profileName() const override;
  QString profilePath() const override;
  QString downloadsPath() const override;
  QString overwritePath() const override;
  QString basePath() const override;
  QString modsPath() const override;
  MOBase::Version version() const override;
  MOBase::VersionInfo appVersion() const override;
  MOBase::IPluginGame* getGame(const QString& gameName) const override;
  MOBase::IModInterface* createMod(MOBase::GuessedValue<QString>& name) override;
  void modDataChanged(MOBase::IModInterface* mod) override;
  QVariant persistent(const QString& pluginName, const QString& key,
                      const QVariant& def = QVariant()) const override;
  void setPersistent(const QString& pluginName, const QString& key,
                     const QVariant& value, bool sync = true) override;
  QString pluginDataPath() const override;
  MOBase::IModInterface* installMod(const QString& fileName,
                                    const QString& nameSuggestion = QString()) override;
  QString resolvePath(const QString& fileName) const override;
  QStringList listDirectories(const QString& directoryName) const override;
  QStringList
  findFiles(const QString& path,
            const std::function<bool(const QString&)>& filter) const override;
  QStringList findFiles(const QString& path,
                        const QStringList& globFilters) const override;
  QStringList getFileOrigins(const QString& fileName) const override;
  QList<FileInfo>
  findFileInfos(const QString& path,
                const std::function<bool(const FileInfo&)>& filter) const override;
  std::shared_ptr<const MOBase::IFileTree> virtualFileTree() const override;

  MOBase::IDownloadManager* downloadManager() const override;
  MOBase::IPluginList* pluginList() const override;
  MOBase::IModList* modList() const override;
  MOBase::IExecutablesList* executablesList() const override;
  std::shared_ptr<MOBase::IProfile> profile() const override;
  QStringList profileNames() const override;
  std::shared_ptr<const MOBase::IProfile>
  getProfile(const QString& name) const override;

  MOBase::IGameFeatures* gameFeatures() const override;
  MOBase::IInstanceManager* instanceManager() const override { return nullptr; }

  bool previewFileData(QWidget* parent, const QString& fileName,
                       const QByteArray& fileData) override;

  HANDLE startApplication(const QString& executable,
                          const QStringList& args = QStringList(),
                          const QString& cwd = "", const QString& profile = "",
                          const QString& forcedCustomOverwrite = "",
                          bool ignoreCustomOverwrite           = false) override;
  bool waitForApplication(HANDLE handle, bool refresh = true,
                          LPDWORD exitCode = nullptr) const override;
  bool releaseApplicationHandle(HANDLE handle) override;
  void refresh(bool saveChanges) override;

  bool onAboutToRun(const std::function<bool(const QString&)>& func) override;
  bool onAboutToRun(const std::function<bool(const QString&, const QDir&,
                                             const QString&)>& func) override;
  bool
  onFinishedRun(const std::function<void(const QString&, unsigned int)>& func) override;
  bool
  onUserInterfaceInitialized(std::function<void(QMainWindow*)> const& func) override;
  bool onNextRefresh(const std::function<void()>& func,
                     bool immediateIfPossible) override;
  bool onProfileCreated(std::function<void(MOBase::IProfile*)> const& func) override;
  bool onProfileRenamed(std::function<void(MOBase::IProfile*, QString const&,
                                           QString const&)> const& func) override;
  bool onProfileRemoved(std::function<void(QString const&)> const& func) override;
  bool onProfileChanged(
      std::function<void(MOBase::IProfile*, MOBase::IProfile*)> const& func) override;

  // Plugin related:
  bool isPluginEnabled(QString const& pluginName) const override;
  bool isPluginEnabled(MOBase::IPlugin* plugin) const override;
  QVariant pluginSetting(const QString& pluginName,
                                 const QString& key) const override;
  void setPluginSetting(const QString& pluginName, const QString& key,
                                const QVariant& value) override;
  bool onPluginSettingChanged(
      std::function<void(QString const&, const QString& key, const QVariant&,
                         const QVariant&)> const& func) override;
  bool
  onPluginEnabled(std::function<void(const MOBase::IPlugin*)> const& func) override;
  bool onPluginEnabled(const QString& pluginName,
                               std::function<void()> const& func) override;
  bool
  onPluginDisabled(std::function<void(const MOBase::IPlugin*)> const& func) override;
  bool onPluginDisabled(const QString& pluginName,
                                std::function<void()> const& func) override;

  MOBase::IPluginGame const* managedGame() const override;

protected:
  // The container needs access to some callbacks to simulate startup.
  friend class PluginContainer;

  /**
   * @brief Connect the signals from this proxy and all the child proxies (plugin list,
   * mod list, etc.) to the actual implementation. Before this call, plugins can
   * register signals but they won't be triggered.
   */
  void connectSignals();

  /**
   * @brief Disconnect the signals from this proxy and all the child proxies (plugin
   * list, mod list, etc.) from the actual implementation.
   */
  void disconnectSignals();

  bool retainConnection(boost::signals2::connection connection);

private:
  using ApplicationRegistry =
      ApplicationRunnerRegistry<std::shared_ptr<ApplicationCompletion>>;

  void pruneApplicationHandles() const;
  bool waitForApplicationImpl(HANDLE handle, bool refresh,
                              LPDWORD exitCode) const;
  bool releaseApplicationHandleImpl(HANDLE handle);

  OrganizerCore* m_Proxied;
  PluginContainer* m_PluginContainer;

  MOBase::IPlugin* m_Plugin;
  QString m_PluginDataPath;

  std::shared_ptr<OrganizerProxyMutationGate> m_MutationGate;

  OrganizerCore::SignalAboutToRunApplication m_AboutToRun;
  OrganizerCore::SignalFinishedRunApplication m_FinishedRun;
  OrganizerCore::SignalUserInterfaceInitialized m_UserInterfaceInitialized;
  OrganizerCore::SignalProfileCreated m_ProfileCreated;
  OrganizerCore::SignalProfileRenamed m_ProfileRenamed;
  OrganizerCore::SignalProfileRemoved m_ProfileRemoved;
  OrganizerCore::SignalProfileChanged m_ProfileChanged;
  OrganizerCore::SignalPluginSettingChanged m_PluginSettingChanged;
  OrganizerCore::SignalPluginEnabled m_PluginEnabled;
  OrganizerCore::SignalPluginEnabled m_PluginDisabled;

  std::mutex m_ConnectionsMutex;
  bool m_ConnectionsClosed{false};
  std::vector<boost::signals2::connection> m_Connections;

  // Lifetime tracking and launch-owned cleanup proceed independently of the
  // optional public wait. Running states are retained; completed states use a
  // bounded grace cache and may also be explicitly released.
  mutable std::shared_ptr<ApplicationRegistry> m_ApplicationRunners =
      std::make_shared<ApplicationRegistry>();

  std::unique_ptr<DownloadManagerProxy> m_DownloadManagerProxy;
  std::unique_ptr<ModListProxy> m_ModListProxy;
  std::unique_ptr<PluginListProxy> m_PluginListProxy;
  std::unique_ptr<ExecutablesListProxy> m_ExecutablesListProxy;
  std::unique_ptr<GameFeaturesProxy> m_GameFeaturesProxy;
};

#endif  // ORGANIZERPROXY_H
