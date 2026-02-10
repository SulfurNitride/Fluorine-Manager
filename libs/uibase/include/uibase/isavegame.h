#ifndef ISAVEGAMEINFO_H
#define ISAVEGAMEINFO_H

#include <QMetaType>

class QString;
class QDateTime;

namespace MOBase
{

/** Base class for information about what is in a save game */
class ISaveGame
{
public:
  virtual ~ISaveGame() {}

  /**
   * @return the path of the (main) save file, either as an absolute file or
   *     relative to the save folder for which this save was created.
   */
  virtual QString getFilepath() const = 0;

  /**
   * @brief Retrieve the creation time of the save.
   *
   * Note that this might not be the same as the creation time of the file.
   */
  virtual QDateTime getCreationTime() const = 0;

  /**
   * @brief Retrieve the name of this save.
   *
   * @return the name of this save.
   */
  virtual QString getName() const = 0;

  /**
   * @brief Get a name which can be used to identify sets of saves.
   *
   * This is usually the PC name for RPG games. The name can contain '/' that
   * are considered separate section for better visualization (not yet implemented).
   */
  virtual QString getSaveGroupIdentifier() const = 0;

  /**
   * @brief Gets all the files related to this save
   *
   * Note: This must return the actual list, not the potential list.
   */
  virtual QStringList allFiles() const = 0;
};

}  // namespace MOBase

Q_DECLARE_METATYPE(MOBase::ISaveGame const*)

#endif  // SAVEGAMEINFO_H
