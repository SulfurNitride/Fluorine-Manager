#ifndef GAMEBRYO_MODDATACONTENT_H
#define GAMEBRYO_MODDATACONTENT_H

#include <ifiletree.h>
#include <moddatacontent.h>

namespace MOBase
{
class IGameFeatures;
}

/**
 * @brief ModDataContent for GameBryo games.
 *
 */
class GamebryoModDataContent : public MOBase::ModDataContent
{
protected:
  /**
   * Note: These are used to index m_Enabled so should have standard
   * enum values, not custom ones.
   */
  enum EContent
  {
    CONTENT_PLUGIN,
    CONTENT_OPTIONAL,
    CONTENT_TEXTURE,
    CONTENT_MESH,
    CONTENT_BSA,
    CONTENT_INTERFACE,
    CONTENT_SOUND,
    CONTENT_SCRIPT,
    CONTENT_SKSE,
    CONTENT_SKSE_FILES,
    CONTENT_SKYPROC,
    CONTENT_MCM,
    CONTENT_INI,
    CONTENT_FACEGEN,
    CONTENT_MODGROUP
  };

  /**
   * This is the first value that can be used for game-specific contents.
   */
  constexpr static auto CONTENT_NEXT_VALUE = CONTENT_MODGROUP + 1;

public:
  /**
   *
   */
  GamebryoModDataContent(const MOBase::IGameFeatures* gameFeatures);

  /**
   * @return the list of all possible contents for the corresponding game.
   */
  virtual std::vector<Content> getAllContents() const override;

  /**
   * @brief Retrieve the list of contents in the given tree.
   *
   * @param fileTree The tree corresponding to the mod to retrieve contents for.
   *
   * @return the IDs of the content in the given tree.
   */
  virtual std::vector<int>
  getContentsFor(std::shared_ptr<const MOBase::IFileTree> fileTree) const override;

protected:
  MOBase::IGameFeatures const* const m_GameFeatures;

  // List of enabled contents:
  std::vector<bool> m_Enabled;
};

#endif  // GAMEBRYO_MODDATACONTENT_H
