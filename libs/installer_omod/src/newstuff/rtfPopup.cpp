#include "rtfPopup.h"

#include <QGridLayout>
#include <QLabel>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTextDocument>

#include "../interop/QtDotNetConverters.h"

RtfPopup::RtfPopup(System::String^ rtfText, QWidget* parent, Qt::WindowFlags f) : QDialog(parent, f)
{
  QString text = rtfText->StartsWith("{\\rtf") ? toQString(RtfPipe::Rtf::ToHtml(rtfText)) : Qt::convertFromPlainText(toQString(rtfText), Qt::WhiteSpaceNormal);
  QRegularExpression urlFinder(R"REGEX((?<!(?:href="))((?:(?:https?|ftp|file)://|www\.|ftp\.)(?:\([-A-Z0-9+@#/%=~_|$?!:,.]|(?:&amp;)*\)|[-A-Z0-9+@#/%=~_|$?!:,.]|(?:&amp;))*(?:\([-A-Z0-9+@#/%=~+|$?!:,.]|(?:&amp;)*\)|[A-Z0-9+@#/%=~_|$]|(?:&amp;))))REGEX", QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
  text.replace(urlFinder, R"(<a href="\1">\1</a>)");

  QLayout* layout = new QGridLayout(this);
  setLayout(layout);
  QScrollArea* scrollArea = new QScrollArea(this);
  scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  layout->addWidget(scrollArea);

  QLabel* label = new QLabel(text, scrollArea);
  label->setWordWrap(true);
  label->setTextFormat(Qt::RichText);
  label->setOpenExternalLinks(true);
  label->setTextInteractionFlags(Qt::TextBrowserInteraction);
  scrollArea->setWidget(label);
  scrollArea->setWidgetResizable(true);

  setSizeGripEnabled(true);
}
