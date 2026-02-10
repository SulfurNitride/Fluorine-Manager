#ifndef UIBASE_GAMEFEATURES_GAMEFEATURE_H
#define UIBASE_GAMEFEATURES_GAMEFEATURE_H

#include <typeindex>

namespace MOBase
{

/**
 * Empty class that is inherit by all game features.
 */
class GameFeature
{
public:
  GameFeature()          = default;
  virtual ~GameFeature() = 0;

  /**
   * @brief Retrieve the type index of the main game feature this feature extends.
   */
  virtual const std::type_info& typeInfo() const = 0;
};

// Pure virtual destructor must still have a definition
inline GameFeature::~GameFeature() {}

namespace details
{

  template <class T>
  class GameFeatureCRTP : public GameFeature
  {
    const std::type_info& typeInfo() const final { return typeid(T); }
  };

}  // namespace details

}  // namespace MOBase

#endif
