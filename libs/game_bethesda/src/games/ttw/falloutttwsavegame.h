#ifndef FALLOUTTTWSAVEGAME_H
#define FALLOUTTTWSAVEGAME_H

#include "gamebryosavegame.h"

class GameFalloutTTW;

class FalloutTTWSaveGame : public GamebryoSaveGame
{
public:
  FalloutTTWSaveGame(QString const& fileName, GameFalloutTTW const* game);

protected:
  // Fetch easy-to-access information.
  void fetchInformationFields(FileWrapper& wrapper, uint32_t& width,
                              uint32_t& height, uint32_t& saveNumber,
                              QString& playerName, unsigned short& playerLevel,
                              QString& playerLocation) const;

  std::unique_ptr<DataFields> fetchDataFields() const override;
};

#endif  // FALLOUTTTWSAVEGAME_H
