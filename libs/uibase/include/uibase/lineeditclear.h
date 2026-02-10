/****************************************************************************
**
** Copyright (c) 2007 Trolltech ASA <info@trolltech.com>
**
** Use, modification and distribution is allowed without limitation,
** warranty, liability or support of any kind.
**
****************************************************************************/

#ifndef LINEEDITCLEAR_H
#define LINEEDITCLEAR_H

#include "dllimport.h"
#include <QLineEdit>

class QToolButton;

namespace MOBase
{

class QDLLEXPORT LineEditClear : public QLineEdit
{
  Q_OBJECT

public:
  LineEditClear(QWidget* parent = 0);

protected:
  void resizeEvent(QResizeEvent*);

private slots:
  void updateCloseButton(const QString& text);

private:
  QToolButton* clearButton;
};

}  // namespace MOBase

#endif  // LINEEDITCLEAR_H
