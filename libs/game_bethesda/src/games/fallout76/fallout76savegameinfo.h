#ifndef FALLOUT76SAVEGAMEINFO_H
#define FALLOUT76SAVEGAMEINFO_H

#include "gamebryosavegameinfo.h"

class GameGamebryo;

class Fallout76SaveGameInfo : public GamebryoSaveGameInfo
{
public:
  Fallout76SaveGameInfo(GameGamebryo const* game);
  ~Fallout76SaveGameInfo();
};

#endif  // FALLOUT76SAVEGAMEINFO_H
