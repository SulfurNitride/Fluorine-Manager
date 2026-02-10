#ifndef UIBASE_EXCEPTIONS_H
#define UIBASE_EXCEPTIONS_H

#include <stdexcept>

#include <QObject>
#include <QString>

#include "dllimport.h"

namespace MOBase
{

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4275)  // non-dll interface
#endif

/**
 * @brief exception class that takes a QString as the parameter
 **/
class QDLLEXPORT Exception : public std::exception
{
public:
  Exception(const QString& text) : m_Message(text.toUtf8()) {}

  virtual const char* what() const noexcept override { return m_Message.constData(); }

private:
  QByteArray m_Message;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

// Exception thrown in case of incompatibilities, i.e. between plugins.
class QDLLEXPORT IncompatibilityException : public Exception
{
public:
  using Exception::Exception;
};

// Exception thrown for invalid NXM links.
class QDLLEXPORT InvalidNXMLinkException : public Exception
{
public:
  InvalidNXMLinkException(const QString& link)
      : Exception(QObject::tr("invalid nxm-link: %1").arg(link))
  {}
};

// alias for backward-compatibility, should be removed when possible
using MyException = Exception;

}  // namespace MOBase

#endif
