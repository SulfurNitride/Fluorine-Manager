#include "gamefeaturesproxy.h"

#include "game_features.h"
#include "gamefeatureownership.h"
#include "organizerproxy.h"

#include <bsainvalidation.h>
#include <dataarchives.h>
#include <gameplugins.h>
#include <localsavegames.h>

#include <algorithm>

namespace
{
class BSAInvalidationProxy final : public MOBase::BSAInvalidation,
                                   public ProxiedGameFeature
{
public:
  BSAInvalidationProxy(
      std::shared_ptr<MOBase::BSAInvalidation> proxied,
      std::shared_ptr<OrganizerProxyMutationGate> mutationGate)
      : m_Proxied(std::move(proxied)), m_MutationGate(std::move(mutationGate))
  {}

  bool isInvalidationBSA(const QString& name) override
  {
    return m_Proxied->isInvalidationBSA(name);
  }
  void deactivate(MOBase::IProfile* profile) override
  {
    m_MutationGate->runIfAllowed([&] { m_Proxied->deactivate(profile); });
  }
  void activate(MOBase::IProfile* profile) override
  {
    m_MutationGate->runIfAllowed([&] { m_Proxied->activate(profile); });
  }
  bool prepareProfile(MOBase::IProfile* profile) override
  {
    bool result = false;
    m_MutationGate->runIfAllowed(
        [&] { result = m_Proxied->prepareProfile(profile); });
    return result;
  }
  std::shared_ptr<MOBase::GameFeature> proxiedFeature() const override
  {
    return m_Proxied;
  }

private:
  std::shared_ptr<MOBase::BSAInvalidation> m_Proxied;
  std::shared_ptr<OrganizerProxyMutationGate> m_MutationGate;
};

class DataArchivesProxy final : public MOBase::DataArchives,
                                public ProxiedGameFeature
{
public:
  DataArchivesProxy(std::shared_ptr<MOBase::DataArchives> proxied,
                    std::shared_ptr<OrganizerProxyMutationGate> mutationGate)
      : m_Proxied(std::move(proxied)), m_MutationGate(std::move(mutationGate))
  {}

  QStringList vanillaArchives() const override
  {
    return m_Proxied->vanillaArchives();
  }
  QStringList archives(const MOBase::IProfile* profile) const override
  {
    return m_Proxied->archives(profile);
  }
  void addArchive(MOBase::IProfile* profile, int index,
                  const QString& name) override
  {
    m_MutationGate->runIfAllowed(
        [&] { m_Proxied->addArchive(profile, index, name); });
  }
  void removeArchive(MOBase::IProfile* profile, const QString& name) override
  {
    m_MutationGate->runIfAllowed(
        [&] { m_Proxied->removeArchive(profile, name); });
  }
  std::shared_ptr<MOBase::GameFeature> proxiedFeature() const override
  {
    return m_Proxied;
  }

private:
  std::shared_ptr<MOBase::DataArchives> m_Proxied;
  std::shared_ptr<OrganizerProxyMutationGate> m_MutationGate;
};

class GamePluginsProxy final : public MOBase::GamePlugins,
                               public ProxiedGameFeature
{
public:
  GamePluginsProxy(std::shared_ptr<MOBase::GamePlugins> proxied,
                   std::shared_ptr<OrganizerProxyMutationGate> mutationGate)
      : m_Proxied(std::move(proxied)), m_MutationGate(std::move(mutationGate))
  {}

  void writePluginLists(const MOBase::IPluginList* plugins) override
  {
    m_MutationGate->runIfAllowed(
        [&] { m_Proxied->writePluginLists(plugins); });
  }
  void readPluginLists(MOBase::IPluginList* plugins) override
  {
    m_MutationGate->runIfAllowed(
        [&] { m_Proxied->readPluginLists(plugins); });
  }
  QStringList getLoadOrder() override { return m_Proxied->getLoadOrder(); }
  bool lightPluginsAreSupported() override
  {
    return m_Proxied->lightPluginsAreSupported();
  }
  bool mediumPluginsAreSupported() override
  {
    return m_Proxied->mediumPluginsAreSupported();
  }
  bool blueprintPluginsAreSupported() override
  {
    return m_Proxied->blueprintPluginsAreSupported();
  }
  std::shared_ptr<MOBase::GameFeature> proxiedFeature() const override
  {
    return m_Proxied;
  }

private:
  std::shared_ptr<MOBase::GamePlugins> m_Proxied;
  std::shared_ptr<OrganizerProxyMutationGate> m_MutationGate;
};

class LocalSavegamesProxy final : public MOBase::LocalSavegames,
                                  public MOBase::LocalSavegamesRouting,
                                  public MOBase::LocalSavegamesTopology,
                                  public ProxiedGameFeature
{
public:
  LocalSavegamesProxy(
      std::shared_ptr<MOBase::LocalSavegames> proxied,
      std::shared_ptr<OrganizerProxyMutationGate> mutationGate)
      : m_Proxied(std::move(proxied)), m_MutationGate(std::move(mutationGate))
  {}

  MappingType mappings(const QDir& profileSaveDir) const override
  {
    return m_Proxied->mappings(profileSaveDir);
  }
  bool prepareProfile(MOBase::IProfile* profile) override
  {
    bool result = false;
    m_MutationGate->runIfAllowed(
        [&] { result = m_Proxied->prepareProfile(profile); });
    return result;
  }
  QString routingIniName() const override
  {
    const auto* routing =
        dynamic_cast<const MOBase::LocalSavegamesRouting*>(m_Proxied.get());
    return routing != nullptr ? routing->routingIniName() : QString{};
  }
  QByteArray routingPath() const override
  {
    const auto* routing =
        dynamic_cast<const MOBase::LocalSavegamesRouting*>(m_Proxied.get());
    return routing != nullptr ? routing->routingPath() : QByteArray{};
  }
  bool usesFixedGameDirectory() const override
  {
    const auto* topology =
        dynamic_cast<const MOBase::LocalSavegamesTopology*>(m_Proxied.get());
    return topology != nullptr && topology->usesFixedGameDirectory();
  }
  std::shared_ptr<MOBase::GameFeature> proxiedFeature() const override
  {
    return m_Proxied;
  }

private:
  std::shared_ptr<MOBase::LocalSavegames> m_Proxied;
  std::shared_ptr<OrganizerProxyMutationGate> m_MutationGate;
};

}  // namespace

GameFeaturesProxy::GameFeaturesProxy(OrganizerProxy* coreProxy,
                                     GameFeatures& gameFeatures)
    : m_CoreProxy(*coreProxy), m_Features(gameFeatures)
{}

bool GameFeaturesProxy::registerFeature(QStringList const& games,
                                        std::shared_ptr<MOBase::GameFeature> feature,
                                        int priority, bool replace)
{
  if (!game_feature_ownership::acceptsCallerSuppliedFeature(feature)) {
    return false;
  }
  bool result = false;
  m_CoreProxy.runMutationIfAllowed([&] {
    if (replace) {
      m_Features.unregisterGameFeatures(m_CoreProxy.plugin(), feature->typeInfo());
    }
    result =
        m_Features.registerGameFeature(m_CoreProxy.plugin(), games, feature, priority);
  });
  return result;
}

bool GameFeaturesProxy::registerFeature(MOBase::IPluginGame* game,
                                        std::shared_ptr<MOBase::GameFeature> feature,
                                        int priority, bool replace)
{
  return registerFeature({game->gameName()}, feature, priority, replace);
}

bool GameFeaturesProxy::registerFeature(std::shared_ptr<MOBase::GameFeature> feature,
                                        int priority, bool replace)
{
  return registerFeature(QStringList(), feature, priority, replace);
}

bool GameFeaturesProxy::unregisterFeature(std::shared_ptr<MOBase::GameFeature> feature)
{
  if (!game_feature_ownership::acceptsCallerSuppliedFeature(feature)) {
    return false;
  }
  bool result = false;
  m_CoreProxy.runMutationIfAllowed([&] {
    result = m_Features.unregisterGameFeature(m_CoreProxy.plugin(), feature);
  });
  return result;
}

std::shared_ptr<MOBase::GameFeature>
GameFeaturesProxy::gameFeatureImpl(std::type_info const& info) const
{
  std::shared_ptr<MOBase::GameFeature> feature;
  m_CoreProxy.runMutationIfAllowed(
      [&] { feature = gameFeatureImplAllowed(info); });
  return feature;
}

std::shared_ptr<MOBase::GameFeature>
GameFeaturesProxy::gameFeatureImplAllowed(std::type_info const& info) const
{
  auto feature = m_Features.gameFeature(info);
  if (!feature) {
    return nullptr;
  }

  // A plugin already owns capabilities it registered itself. Returning its exact
  // shared_ptr preserves concrete-type casts and pointer identity for that owner;
  // only features obtained from another plugin (or the core) need mediation.
  if (m_Features.m_allFeatures.ownedBy(info, feature, m_CoreProxy.plugin())) {
    return feature;
  }

  std::lock_guard lock(m_FeatureProxyMutex);
  const auto cached = m_FeatureProxies.find(feature.get());
  if (cached != m_FeatureProxies.end()) {
    if (auto proxy = cached->second.lock()) {
      return proxy;
    }
    m_FeatureProxies.erase(cached);
  }

  const auto gate = m_CoreProxy.mutationGate();
  std::shared_ptr<MOBase::GameFeature> proxy;
  if (info == typeid(MOBase::BSAInvalidation)) {
    const auto typed =
        std::dynamic_pointer_cast<MOBase::BSAInvalidation>(feature);
    if (!typed) {
      return nullptr;
    }
    proxy = std::make_shared<BSAInvalidationProxy>(typed, gate);
  } else if (info == typeid(MOBase::DataArchives)) {
    const auto typed =
        std::dynamic_pointer_cast<MOBase::DataArchives>(feature);
    if (!typed) {
      return nullptr;
    }
    proxy = std::make_shared<DataArchivesProxy>(typed, gate);
  } else if (info == typeid(MOBase::GamePlugins)) {
    const auto typed =
        std::dynamic_pointer_cast<MOBase::GamePlugins>(feature);
    if (!typed) {
      return nullptr;
    }
    proxy = std::make_shared<GamePluginsProxy>(typed, gate);
  } else if (info == typeid(MOBase::LocalSavegames)) {
    const auto typed =
        std::dynamic_pointer_cast<MOBase::LocalSavegames>(feature);
    if (!typed) {
      return nullptr;
    }
    proxy = std::make_shared<LocalSavegamesProxy>(typed, gate);
  } else {
    return feature;
  }

  m_FeatureProxies.emplace(feature.get(), proxy);
  return proxy;
}

int GameFeaturesProxy::unregisterFeaturesImpl(std::type_info const& info)
{
  int result = 0;
  m_CoreProxy.runMutationIfAllowed(
      [&] { result = m_Features.unregisterGameFeatures(m_CoreProxy.plugin(), info); });
  return result;
}
