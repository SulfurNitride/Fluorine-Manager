#ifndef UIBASE_GAMEFEATURES_IGAMEFEATURES_H
#define UIBASE_GAMEFEATURES_IGAMEFEATURES_H

#include <memory>
#include <tuple>

#include <QStringList>

#include "game_feature.h"

namespace MOBase
{

class IPluginGame;

// top-level game features
class BSAInvalidation;
class DataArchives;
class GamePlugins;
class LocalSavegames;
class ModDataChecker;
class ModDataContent;
class SaveGameInfo;
class ScriptExtender;
class UnmanagedMods;

namespace details
{

  // use pointers in the tuple since are only forward-declaring the features here
  using BaseGameFeaturesP =
      std::tuple<BSAInvalidation*, DataArchives*, GamePlugins*, LocalSavegames*,
                 ModDataChecker*, ModDataContent*, SaveGameInfo*, ScriptExtender*,
                 UnmanagedMods*>;

}  // namespace details

// simple concept that only restricting template function that should take a game
// feature to actually viable game feature types
//
template <class T>
concept BaseGameFeature = requires(T) {
  {
    std::get<T*>(std::declval<details::BaseGameFeaturesP>())
  } -> std::convertible_to<T*>;
};

/**
 * @brief Interface to game features.
 *
 */
class IGameFeatures
{
public:
  /**
   * @brief Register game feature for the specified game.
   *
   * This method register a game feature to combine or replace the same feature provided
   * by the game. Some features are merged (e.g., ModDataContent, ModDataChecker), while
   * other override previous features (e.g., SaveGameInfo).
   *
   * For features that can be combined, the priority argument indicates the order of
   * priority (e.g., the order of the checks for ModDataChecker). For other features,
   * the feature with the highest priority will be used. The features provided by the
   * game plugin itself always have lowest priority.
   *
   * The feature is associated to the plugin that registers it, if the plugin is
   * disabled, the feature will not be available.
   *
   * This function will return True if the feature was registered, even if the feature
   * is not used du to its low priority.
   *
   * @param games Names of the game to enable the feature for.
   * @param feature Game feature to register.
   * @param priority Priority of the game feature. If the plugin registering the feature
   * is a game plugin, this parameter is ignored.
   * @param replace If true, remove features of the same kind registered by the current
   * plugin, otherwise add the feature alongside existing ones.
   *
   * @return true if the game feature was properly registered, false otherwise.
   */
  virtual bool registerFeature(QStringList const& games,
                               std::shared_ptr<GameFeature> feature, int priority,
                               bool replace = false) = 0;

  /**
   * @brief Register game feature for the specified game.
   *
   * See first overload for more details.
   *
   * @param game Game to enable the feature for.
   * @param feature Game feature to register.
   * @param priority Priority of the game feature.
   * @param replace If true, remove features of the same kind registered by the current
   * plugin, otherwise add the feature alongside existing ones.
   *
   * @return true if the game feature was properly registered, false otherwise.
   */
  virtual bool registerFeature(IPluginGame* game, std::shared_ptr<GameFeature> feature,
                               int priority, bool replace = false) = 0;

  /**
   * @brief Register game feature for all games.
   *
   * See first overload for more details.
   *
   * @param feature Game feature to register.
   * @param priority Priority of the game feature.
   * @param replace If true, remove features of the same kind registered by the current
   * plugin, otherwise add the feature alongside existing ones.
   *
   * @return true if the game feature was properly registered, false otherwise.
   */
  virtual bool registerFeature(std::shared_ptr<GameFeature> feature, int priority,
                               bool replace = false) = 0;

  /**
   * @brief Unregister the given game feature.
   *
   * This function is safe to use even if the feature was not registered before.
   *
   * @param feature Feature to unregister.
   */
  virtual bool unregisterFeature(std::shared_ptr<GameFeature> feature) = 0;

  /**
   * @brief Unregister all features of the given type registered by the calling plugin.
   *
   * This function is safe to use even if the plugin has no feature of the given type
   * register.
   *
   * @return the number of features unregistered.
   *
   * @tparam Feature Type of game feature to remove.
   */
  template <BaseGameFeature Feature>
  int unregisterFeatures()
  {
    return unregisterFeaturesImpl(typeid(Feature));
  }

  /**
   * Retrieve the given game feature, if one exists.
   *
   * @return the feature of the given type, if one exists, otherwise a null pointer.
   */
  template <BaseGameFeature T>
  std::shared_ptr<T> gameFeature() const
  {
    // gameFeatureImpl ensure that the returned pointer is of the right type (or
    // nullptr), so reinterpret_cast should be fine here
    return std::dynamic_pointer_cast<T>(gameFeatureImpl(typeid(T)));
  }

public:
  virtual ~IGameFeatures() = default;

protected:
  virtual std::shared_ptr<GameFeature>
  gameFeatureImpl(std::type_info const& info) const              = 0;
  virtual int unregisterFeaturesImpl(std::type_info const& info) = 0;
};

}  // namespace MOBase

#endif
