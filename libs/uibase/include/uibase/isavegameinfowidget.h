#ifndef ISAVEGAMEINFOWIDGET_H
#define ISAVEGAMEINFOWIDGET_H

#include <QWidget>

#include "isavegame.h"

class QFile;

namespace MOBase
{

/** Base class for a save game info widget.
 *
 * This supports something or other
 */
class ISaveGameInfoWidget : public QWidget
{
public:
  ISaveGameInfoWidget(QWidget* parent = 0) : QWidget(parent) {}

  virtual ~ISaveGameInfoWidget() {}

  /** Set the save file to display in the widget */
  virtual void setSave(ISaveGame const&) = 0;
};

}  // namespace MOBase

#endif  // ISAVEGAMEINFOWIDGET_H
