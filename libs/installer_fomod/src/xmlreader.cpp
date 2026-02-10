#include "xmlreader.h"

#include <QDebug>

#include <uibase/utility.h>

using MOBase::Exception;

bool XmlReader::getNextElement(QString const& start)
{
  while (!atEnd()) {
    switch (readNext()) {
    case EndElement:
      if (name() != start) {
        qWarning() << "Got end of " << name() << ", expected " << start << " at "
                   << lineNumber();
        continue;
      }
      return false;

    case StartElement:
      return true;

    case Invalid:
      throw Exception("bad xml");

    default:
      qWarning() << "Unexpected token type " << tokenString() << " at " << lineNumber();
    }
  }
  return false;
}

void XmlReader::unexpected()
{
  qWarning() << "Unexpected element " << name() << " near line " << lineNumber();
  // Eat the contents
  QString s = readElementText(IncludeChildElements);
  // Print them out if in debugging mode
  qDebug() << " contains " << s;
}

void XmlReader::finishedElement()
{
  QString const self = name().toString();
  while (!atEnd()) {
    switch (readNext()) {
    case EndElement:
      if (name() != self) {
        qWarning() << "Got end element for " << name() << ", expected " << self
                   << " at " << lineNumber();
        continue;
      }
      return;

    case Invalid:
      throw Exception("bad xml");
      return;

    case StartElement:
      unexpected();
      break;

    default:
      qWarning() << "Unexpected token type " << tokenString() << " at " << lineNumber();
    }
  }
}

QString XmlReader::getText()
{
  // This reads the text in an element, leaving you at the next element.
  QString result;
  while (QXmlStreamReader::readNext() == Comment || tokenType() == Characters) {
    if (tokenType() == Characters) {
      result += text();
    }
  }
  if (tokenType() != EndElement) {
    qWarning() << "Unexpected token type " << tokenString() << " at " << lineNumber();
  }
  return result;
}
