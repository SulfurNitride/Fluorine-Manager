#ifndef XMLREADER_H
#define XMLREADER_H

#include <QXmlStreamReader>

class XmlReader : public QXmlStreamReader
{
public:
  XmlReader(QIODevice* device) : QXmlStreamReader(device) {}

  XmlReader(QByteArray array) : QXmlStreamReader(array) {}

  /** Get the next token, ignoring comments and white space text */
  TokenType readNext()
  {
    while (QXmlStreamReader::readNext() == Comment || isWhitespace()) {
      continue;
    }
    return tokenType();
  }

  /** get the next element.
   *
   * \param start - the name of the current start element
   *
   * \returns false if no more elements
   */
  bool getNextElement(QString const& start);

  /* Get the text associated with this token. */
  QString getText();

  /** Print a message if we get an unexpected tag */
  void unexpected();

  /** Read till the end of an element. Used for leaf nodes */
  void finishedElement();
};

#endif  // XMLREADER_H
