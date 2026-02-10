#ifndef FALLOUT76SAVEGAME_H
#define FALLOUT76SAVEGAME_H

#include "gamebryosavegame.h"
#include "windows_compat.h"

class GameFallout76;

class Fallout76SaveGame : public GamebryoSaveGame
{
public:
  Fallout76SaveGame(QString const& fileName, GameFallout76 const* game);

protected:
  // Fetch easy-to-access information.
  void fetchInformationFields(FileWrapper& wrapper, QString playerName,
                              unsigned short playerLevel, QString playerLocation,
                              unsigned long saveNumber, FILETIME& creationTime) const;

  std::unique_ptr<DataFields> fetchDataFields() const override;
};

#endif  // FALLOUT76SAVEGAME_H
