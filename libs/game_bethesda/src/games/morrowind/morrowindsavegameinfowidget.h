#ifndef MORROWINDSAVEGAMEINFOWIDGET_H
#define MORROWINDSAVEGAMEINFOWIDGET_H

#include "isavegameinfowidget.h"
#include "morrowindsavegameinfo.h"

#include <QObject>

class GamebryoGame;

namespace Ui
{
class MorrowindSaveGameInfoWidget;
}

class MorrowindSaveGameInfoWidget : public MOBase::ISaveGameInfoWidget
{
  Q_OBJECT

public:
  MorrowindSaveGameInfoWidget(MorrowindSaveGameInfo const* info, QWidget* parent);
  ~MorrowindSaveGameInfoWidget();

  virtual void setSave(MOBase::ISaveGame const&) override;

private:
  Ui::MorrowindSaveGameInfoWidget* ui;
  MorrowindSaveGameInfo const* m_Info;
};

#endif  // MORROWINDSAVEGAMEINFOWIDGET_H
