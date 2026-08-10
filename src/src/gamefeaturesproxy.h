#ifndef GAMEFEATURESPROXY_H
#define GAMEFEATURESPROXY_H

#include "igamefeatures.h"

#include <mutex>
#include <unordered_map>

class GameFeatures;
class OrganizerProxy;

class GameFeaturesProxy : public MOBase::IGameFeatures
{
public:
  GameFeaturesProxy(OrganizerProxy* coreProxy, GameFeatures& gameFeatures);

  bool registerFeature(QStringList const& games,
                       std::shared_ptr<MOBase::GameFeature> feature, int priority,
                       bool replace) override;
  bool registerFeature(MOBase::IPluginGame* game,
                       std::shared_ptr<MOBase::GameFeature> feature, int priority,
                       bool replace) override;
  bool registerFeature(std::shared_ptr<MOBase::GameFeature> feature, int priority,
                       bool replace) override;
  bool unregisterFeature(std::shared_ptr<MOBase::GameFeature> feature) override;

protected:
  std::shared_ptr<MOBase::GameFeature>
  gameFeatureImpl(std::type_info const& info) const override;
  int unregisterFeaturesImpl(std::type_info const& info) override;

private:
  std::shared_ptr<MOBase::GameFeature>
  gameFeatureImplAllowed(std::type_info const& info) const;

  GameFeatures& m_Features;
  OrganizerProxy& m_CoreProxy;
  mutable std::mutex m_FeatureProxyMutex;
  mutable std::unordered_map<MOBase::GameFeature*,
                             std::weak_ptr<MOBase::GameFeature>>
      m_FeatureProxies;
};

#endif
