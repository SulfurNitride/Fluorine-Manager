#pragma once

#include <algorithm>
#include <limits>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include <QStringList>

#include "game_feature.h"

namespace MOBase
{
class IPlugin;
}

/**
 * Marker implemented by organizer-created game-feature facades.
 *
 * A facade is a capability tied to the requesting plugin's mutation gate. It
 * must never be accepted as a feature supplied by that plugin: unwrapping it
 * would let the requester claim ownership of a foreign feature and retrieve the
 * unmediated object later.
 */
class ProxiedGameFeature
{
public:
  virtual ~ProxiedGameFeature() = default;
  virtual std::shared_ptr<MOBase::GameFeature> proxiedFeature() const = 0;
};

namespace game_feature_ownership
{
inline bool acceptsCallerSuppliedFeature(
    const std::shared_ptr<MOBase::GameFeature>& feature)
{
  return feature != nullptr &&
         std::dynamic_pointer_cast<ProxiedGameFeature>(feature) == nullptr;
}

/**
 * Registration inventory with immutable per-registration ownership.
 *
 * Keeping the owner check in the inventory makes pointer-based unregistration
 * fail closed even if a caller obtains a foreign raw shared_ptr by some route
 * other than the organizer facade.
 */
class RegistrationStore
{
public:
  class Entry
  {
  public:
    Entry(std::shared_ptr<MOBase::GameFeature> feature, MOBase::IPlugin* plugin,
          QStringList games, int priority)
        : m_Feature(std::move(feature)), m_Plugin(plugin), m_Games(std::move(games)),
          m_Priority(priority)
    {}

    const auto& feature() const { return m_Feature; }
    auto* plugin() const { return m_Plugin; }
    const auto& games() const { return m_Games; }
    auto priority() const { return m_Priority; }

  private:
    std::shared_ptr<MOBase::GameFeature> m_Feature;
    MOBase::IPlugin* m_Plugin;
    QStringList m_Games;
    int m_Priority;
  };

  using Entries = std::vector<Entry>;
  using Inventory = std::unordered_map<std::type_index, Entries>;

  bool registerFeature(MOBase::IPlugin* plugin, const QStringList& games,
                       const std::shared_ptr<MOBase::GameFeature>& feature,
                       int priority, bool gamePlugin)
  {
    if (plugin == nullptr || feature == nullptr) {
      return false;
    }

    auto& features = m_Inventory[feature->typeInfo()];
    if (std::find_if(features.begin(), features.end(),
                     [&feature](const auto& data) {
                       return data.feature() == feature;
                     }) != features.end()) {
      return false;
    }

    auto position = features.end();
    if (!gamePlugin) {
      position = std::lower_bound(features.begin(), features.end(), priority,
                                  [](const auto& current, int requestedPriority) {
                                    return current.priority() > requestedPriority;
                                  });
    }

    // Game plugins are always placed last. Preserve the existing stored-priority
    // behavior; insertion order is determined above.
    features.emplace(position, feature, plugin, games,
                     std::numeric_limits<int>::min());
    return true;
  }

  bool unregisterFeature(MOBase::IPlugin* plugin,
                         const std::shared_ptr<MOBase::GameFeature>& feature)
  {
    if (plugin == nullptr || feature == nullptr) {
      return false;
    }

    const auto inventory = m_Inventory.find(feature->typeInfo());
    if (inventory == m_Inventory.end()) {
      return false;
    }

    auto& features = inventory->second;
    const auto registration =
        std::find_if(features.begin(), features.end(), [&](const auto& data) {
          return data.plugin() == plugin && data.feature() == feature;
        });
    if (registration == features.end()) {
      return false;
    }

    features.erase(registration);
    return true;
  }

  int unregisterFeatures(MOBase::IPlugin* plugin, const std::type_info& info)
  {
    const auto inventory = m_Inventory.find(info);
    if (inventory == m_Inventory.end()) {
      return 0;
    }

    auto& features = inventory->second;
    const auto initialSize = features.size();
    std::erase_if(features,
                  [plugin](const auto& feature) {
                    return feature.plugin() == plugin;
                  });
    return static_cast<int>(initialSize - features.size());
  }

  void unregisterPlugin(MOBase::IPlugin* plugin)
  {
    for (auto& [_, features] : m_Inventory) {
      std::erase_if(features,
                    [plugin](const auto& feature) {
                      return feature.plugin() == plugin;
                    });
    }
  }

  bool ownedBy(const std::type_info& info,
               const std::shared_ptr<MOBase::GameFeature>& feature,
               MOBase::IPlugin* plugin) const
  {
    const auto inventory = m_Inventory.find(info);
    if (inventory == m_Inventory.end()) {
      return false;
    }

    return std::find_if(inventory->second.begin(), inventory->second.end(),
                        [&](const auto& candidate) {
                          return candidate.feature() == feature &&
                                 candidate.plugin() == plugin;
                        }) != inventory->second.end();
  }

  Entries& features(const std::type_info& info) { return m_Inventory[info]; }
  Entries& features(const std::type_index& info) { return m_Inventory[info]; }
  const Inventory& inventory() const { return m_Inventory; }
  Inventory& inventory() { return m_Inventory; }

private:
  Inventory m_Inventory;
};
}  // namespace game_feature_ownership
