#ifndef _SKYRIMSESAVEGAME_H
#define _SKYRIMSESAVEGAME_H

#include "gamebryosavegame.h"
#include "gameskyrimse.h"
#include "windows_compat.h"

namespace MOBase
{
class IPluginGame;
}

class SkyrimSESaveGame : public GamebryoSaveGame
{
public:
  SkyrimSESaveGame(QString const& fileName, GameSkyrimSE const* game);

protected:
  // Fetch easy-to-access information.
  void fetchInformationFields(FileWrapper& wrapper, uint32_t& version,
                              QString& playerName, unsigned short& playerLevel,
                              QString& playerLocation, uint32_t& saveNumber,
                              FILETIME& creationTime) const;

  std::unique_ptr<DataFields> fetchDataFields() const override;
};

#endif  // _SKYRIMSESAVEGAME_H
