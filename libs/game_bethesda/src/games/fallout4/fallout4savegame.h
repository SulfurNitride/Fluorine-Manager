#ifndef FALLOUT4SAVEGAME_H
#define FALLOUT4SAVEGAME_H

#include "gamebryosavegame.h"
#include "windows_compat.h"

class GameFallout4;

class Fallout4SaveGame : public GamebryoSaveGame
{
public:
  Fallout4SaveGame(QString const& fileName, GameFallout4 const* game);

protected:
  // Fetch easy-to-access information.
  void fetchInformationFields(FileWrapper& file, uint32_t& saveNumber,
                              QString& playerName, unsigned short& playerLevel,
                              QString& playerLocation, FILETIME& creationTime) const;

  std::unique_ptr<DataFields> fetchDataFields() const override;
};

#endif  // FALLOUT4SAVEGAME_H
