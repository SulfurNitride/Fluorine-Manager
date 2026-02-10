#include <uibase/linklabel.h>
#include <QApplication>

QColor LinkLabel::m_linkColor;

LinkLabel::LinkLabel(QWidget* parent) : QLabel(parent) {}

QColor LinkLabel::linkColor() const
{
  return m_linkColor;
}

void LinkLabel::setLinkColor(const QColor& c)
{
  if (m_linkColor != c) {
    m_linkColor = c;

    // setting link color on the global palette; qt doesn't seem to support
    // per-widget colors for links
    if (qApp) {
      auto p = qApp->palette();
      p.setColor(QPalette::Link, c);
      p.setColor(QPalette::LinkVisited, c);
      qApp->setPalette(p);
    }
  }
}
