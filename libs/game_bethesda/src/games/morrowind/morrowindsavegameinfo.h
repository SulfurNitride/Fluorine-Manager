#ifndef MORROWINDSAVEGAMEINFO_H
#define MORROWINDSAVEGAMEINFO_H

#include "gamebryosavegameinfo.h"
#include "gamemorrowind.h"

class GameGamebryo;

class MorrowindSaveGameInfo : public GamebryoSaveGameInfo
{
public:
  MorrowindSaveGameInfo(GameGamebryo const* game);
  ~MorrowindSaveGameInfo();

  virtual MOBase::ISaveGameInfoWidget* getSaveGameWidget(QWidget*) const override;

protected:
  friend class MorrowindSaveGameInfoWidget;
  GameMorrowind const* m_Game;
};

#endif  // MORROWINDSAVEGAMEINFO_H
