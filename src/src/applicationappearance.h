#ifndef APPLICATIONAPPEARANCE_H
#define APPLICATIONAPPEARANCE_H

#include <QFont>
#include <QString>

class QApplication;

namespace ApplicationAppearance
{

struct Spec
{
  QString styleName;
  QString fontFamily;
  int fontSize{0};
  QString instanceDirectory;
};

class Controller
{
public:
  Controller(QApplication& application, QString applicationDirectory,
             QString defaultStyle, QFont defaultFont);

  // Applies one complete appearance. A rejected or unreadable stylesheet
  // resets the application to its baseline rather than retaining pieces of the
  // previous instance's appearance.
  bool apply(const Spec& spec, QString* error = nullptr);

  void reset();

  const QString& activeStyleFile() const { return m_ActiveStyleFile; }

private:
  QApplication& m_Application;
  QString m_ApplicationDirectory;
  QString m_DefaultStyle;
  QFont m_DefaultFont;
  QString m_ActiveStyleFile;
};

}  // namespace ApplicationAppearance

#endif  // APPLICATIONAPPEARANCE_H
