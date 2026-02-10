#ifndef GAMEBRYO_MODATACHECKER_H
#define GAMEBRYO_MODATACHECKER_H

#include <ifiletree.h>
#include <moddatachecker.h>

class GameGamebryo;

/**
 * @brief ModDataChecker for GameBryo games that look at folder and files in the "data"
 *     directory.
 *
 * The default implementation is game-agnostic and uses the list of folders and file
 * extensions that were used before the ModDataChecker feature was added. It is possible
 * to inherit the class to provide custom list of folders or filenames.
 */
class GamebryoModDataChecker : public MOBase::ModDataChecker
{
public:
  /**
   * @brief Construct a new mod-data checker for GameBryo games.
   */
  GamebryoModDataChecker(const GameGamebryo* game);

  virtual CheckReturn
  dataLooksValid(std::shared_ptr<const MOBase::IFileTree> fileTree) const override;

protected:
  GameGamebryo const* const m_Game;

  using FileNameSet = std::set<QString, MOBase::FileNameComparator>;

  const GameGamebryo* game() const { return m_Game; }

  /**
   * @return the list of possible folder names in data.
   */
  virtual const FileNameSet& possibleFolderNames() const;

  /**
   * @return the extensions of possible files in data.
   */
  virtual const FileNameSet& possibleFileExtensions() const;
};

#endif  // GAMEBRYO_MODATACHECKER_H
