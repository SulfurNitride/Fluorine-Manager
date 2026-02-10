#ifndef _SKYRIMVRSAVEGAME_H
#define _SKYRIMVRSAVEGAME_H

#include "gamebryosavegame.h"
#include "windows_compat.h"

class GameSkyrimVR;

class SkyrimVRSaveGame : public GamebryoSaveGame
{
public:
  SkyrimVRSaveGame(QString const& fileName, GameSkyrimVR const* game);

protected:
  // Fetch easy-to-access information.
  void fetchInformationFields(FileWrapper& wrapper, uint32_t& version,
                              QString& playerName, unsigned short& playerLevel,
                              QString& playerLocation, uint32_t& saveNumber,
                              FILETIME& creationTime) const;

  std::unique_ptr<DataFields> fetchDataFields() const override;
};

#endif  // _SKYRIMVRSAVEGAME_H
