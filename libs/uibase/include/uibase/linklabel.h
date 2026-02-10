#ifndef UIBASE_LINKLABEL_INCLUDED
#define UIBASE_LINKLABEL_INCLUDED

#include "dllimport.h"
#include <QLabel>

// this is a hack to allow .qss files to change the link color
//
// there's nothing in qt to change link colors from a qss file and the color
// can't even be changed on individual widgets, they all use the _global_
// palette on qApp
//
// so as soon as there's a LinkLabel present on screen, the global link color
// will be set to whatever's in the qss file for:
//
//    LinkLabel { qproperty-linkColor: cssColor; }
//
// this doesn't work for links that are visible, so changing the qss live won't
// change the colors for those, MO has to be restarted
//
// apart from that, `LinkLabel` is just a `QLabel`
//
class QDLLEXPORT LinkLabel : public QLabel
{
  Q_OBJECT;
  Q_PROPERTY(QColor linkColor READ linkColor WRITE setLinkColor);

public:
  LinkLabel(QWidget* parent = nullptr);

  QColor linkColor() const;
  void setLinkColor(const QColor& c);

private:
  static QColor m_linkColor;
};

#endif  // UIBASE_LINKLABEL_INCLUDED
