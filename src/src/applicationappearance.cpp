#include "applicationappearance.h"

#include "stylesheetpath.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QProxyStyle>
#include <QRegularExpression>
#include <QStyleFactory>
#include <QStyleOption>
#include <QTreeView>

#include <uibase/log.h>

#include <memory>
#include <utility>

namespace ApplicationAppearance
{
namespace
{
class ProxyStyle : public QProxyStyle
{
public:
  explicit ProxyStyle(QStyle* baseStyle) : QProxyStyle(baseStyle) {}

  void drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                     QPainter* painter, const QWidget* widget) const override
  {
    if (element != QStyle::PE_IndicatorItemViewItemDrop) {
      QProxyStyle::drawPrimitive(element, option, painter, widget);
      return;
    }

    if (option->rect.height() == 0 &&
        option->rect.bottomRight() == QPoint(-1, -1)) {
      return;
    }

    QRect rect(option->rect);
    if (const auto* view = qobject_cast<const QTreeView*>(widget)) {
      rect.setLeft(view->indentation());
      rect.setRight(widget->width());
    }

    painter->setRenderHint(QPainter::Antialiasing, true);
    QColor color(option->palette.windowText().color());
    QPen pen(color);
    pen.setWidth(2);
    color.setAlpha(50);
    painter->setPen(pen);
    painter->setBrush(QBrush(color));
    if (rect.height() == 0) {
      const QPoint triangle[3] = {
          rect.topLeft(), rect.topLeft() + QPoint(-5, 5),
          rect.topLeft() + QPoint(-5, -5)};
      painter->drawPolygon(triangle, 3);
      painter->drawLine(rect.topLeft(), rect.topRight());
    } else {
      painter->drawRoundedRect(rect, 5, 5);
    }
  }
};

QString applyFontSize(const QString& stylesheet, int fontSize)
{
  if (fontSize <= 0) {
    return stylesheet;
  }

  static const QRegularExpression fontSizeExpression(
      QStringLiteral(R"(font-size\s*:\s*[^;{}]+;)"),
      QRegularExpression::CaseInsensitiveOption);

  QString result = stylesheet;
  result.replace(fontSizeExpression,
                 QStringLiteral("font-size: %1px;").arg(fontSize));
  result += QStringLiteral("\n\n/* Fluorine QSS font size override */\n"
                           "QWidget { font-size: %1px; }\n")
                .arg(fontSize);
  return result;
}

QString baseStyle(const QString& stylesheet, const QString& defaultStyle)
{
  const QStringList factoryStyles = QStyleFactory::keys();
  const QStringList lines         = stylesheet.split('\n');
  for (const QString& untrimmed : lines) {
    const QString line = untrimmed.trimmed();
    if (line.isEmpty()) {
      continue;
    }
    if (!line.startsWith(QStringLiteral("/*")) ||
        !line.endsWith(QStringLiteral("*/"))) {
      break;
    }

    const QString comment = line.mid(2, line.size() - 4).trimmed();
    if (!comment.startsWith(QStringLiteral("mo2-base-style"))) {
      continue;
    }
    const QStringList parts = comment.split(':');
    if (parts.size() != 2) {
      MOBase::log::warn("found invalid top-comment for mo2: {}", comment);
      continue;
    }
    const int index = factoryStyles.indexOf(parts[1].trimmed(), 0,
                                            Qt::CaseInsensitive);
    if (index >= 0) {
      MOBase::log::info("found base style '{}' in stylesheet",
                        factoryStyles[index]);
      return factoryStyles[index];
    }
    MOBase::log::warn("base style '{}' was not found", parts[1].trimmed());
  }
  return defaultStyle;
}

std::unique_ptr<QStyle> makeStyle(const QString& name)
{
  if (QStyle* style = QStyleFactory::create(name)) {
    return std::make_unique<ProxyStyle>(style);
  }
  return {};
}
}  // namespace

Controller::Controller(QApplication& application, QString applicationDirectory,
                       QString defaultStyle, QFont defaultFont)
    : m_Application(application),
      m_ApplicationDirectory(std::move(applicationDirectory)),
      m_DefaultStyle(std::move(defaultStyle)),
      m_DefaultFont(std::move(defaultFont))
{}

bool Controller::apply(const Spec& spec, QString* error)
{
  QString styleSheet;
  QString styleFile;
  QString base = m_DefaultStyle;

  if (!spec.styleName.isEmpty()) {
    const QStringList factoryStyles = QStyleFactory::keys();
    const int factoryIndex =
        factoryStyles.indexOf(spec.styleName, 0, Qt::CaseSensitive);
    if (factoryIndex >= 0) {
      base = factoryStyles[factoryIndex];
    } else {
      const QStringList directories = StyleSheetPath::searchDirectories(
          m_ApplicationDirectory, spec.instanceDirectory);
      styleFile = StyleSheetPath::resolve(spec.styleName, directories);
      QFile file(styleFile);
      if (styleFile.isEmpty() || !file.open(QFile::ReadOnly | QFile::Text)) {
        if (error != nullptr) {
          *error = QStringLiteral("stylesheet '%1' could not be read from [%2]")
                       .arg(spec.styleName, directories.join(", "));
        }
        reset();
        return false;
      }
      const QString raw = QString::fromUtf8(file.readAll());
      base              = baseStyle(raw, m_DefaultStyle);
      styleSheet = applyFontSize(
          StyleSheetPath::resolveAssets(raw, QFileInfo(styleFile).absolutePath()),
          spec.fontSize);
    }
  }

  std::unique_ptr<QStyle> style = makeStyle(base);
  if (!style) {
    if (error != nullptr) {
      *error = QStringLiteral("Qt style '%1' is unavailable").arg(base);
    }
    reset();
    return false;
  }

  QFont font = m_DefaultFont;
  if (!spec.fontFamily.isEmpty()) {
    font.setFamily(spec.fontFamily);
  }
  if (spec.fontSize > 0) {
    font.setPixelSize(spec.fontSize);
  }

  m_Application.setStyle(style.release());
  m_Application.setStyleSheet(styleSheet);
  m_Application.setFont(font);
  m_ActiveStyleFile = styleFile;
  return true;
}

void Controller::reset()
{
  if (std::unique_ptr<QStyle> style = makeStyle(m_DefaultStyle)) {
    m_Application.setStyle(style.release());
  }
  m_Application.setStyleSheet(QString());
  m_Application.setFont(m_DefaultFont);
  m_ActiveStyleFile.clear();
}

}  // namespace ApplicationAppearance
