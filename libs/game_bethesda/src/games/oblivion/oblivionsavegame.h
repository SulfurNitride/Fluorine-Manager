#ifndef OBLIVIONSAVEGAME_H
#define OBLIVIONSAVEGAME_H

#include "gamebryosavegame.h"
#include "gameoblivion.h"

class OblivionSaveGame : public GamebryoSaveGame
{
public:
  OblivionSaveGame(QString const& fileName, GameOblivion const* game);

protected:
  // Fetch easy-to-access information.
  void fetchInformationFields(FileWrapper& wrapper, uint32_t& saveNumber,
                              QString& playerName, unsigned short& playerLevel,
                              QString& playerLocation, _SYSTEMTIME& creationTime) const;

  std::unique_ptr<DataFields> fetchDataFields() const override;
};

#endif  // OBLIVIONSAVEGAME_H
