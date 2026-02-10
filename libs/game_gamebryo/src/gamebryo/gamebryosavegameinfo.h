#ifndef GAMEBRYOSAVEGAMEINFO_H
#define GAMEBRYOSAVEGAMEINFO_H

#include "savegameinfo.h"

class GameGamebryo;

class GamebryoSaveGameInfo : public MOBase::SaveGameInfo
{
public:
  GamebryoSaveGameInfo(GameGamebryo const* game);
  ~GamebryoSaveGameInfo();

  virtual MissingAssets getMissingAssets(MOBase::ISaveGame const& save) const override;

  virtual MOBase::ISaveGameInfoWidget* getSaveGameWidget(QWidget*) const override;

protected:
  friend class GamebryoSaveGameInfoWidget;
  GameGamebryo const* m_Game;
};

#endif  // GAMEBRYOSAVEGAMEINFO_H
