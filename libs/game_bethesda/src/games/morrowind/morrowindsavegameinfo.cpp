#include "morrowindsavegameinfo.h"
#include "gamegamebryo.h"
#include "morrowindsavegame.h"
#include "morrowindsavegameinfowidget.h"

MorrowindSaveGameInfo::MorrowindSaveGameInfo(GameGamebryo const* game)
    : GamebryoSaveGameInfo(game)
{
  m_Game = dynamic_cast<GameMorrowind const*>(game);
}

MorrowindSaveGameInfo::~MorrowindSaveGameInfo() {}

MOBase::ISaveGameInfoWidget*
MorrowindSaveGameInfo::getSaveGameWidget(QWidget* parent) const
{
  return new MorrowindSaveGameInfoWidget(this, parent);
}
