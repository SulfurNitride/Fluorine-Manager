#ifndef STEAMCEF_H
#define STEAMCEF_H

#include <QJsonValue>
#include <QString>
#include <stdexcept>

// Client for Steam's local Chromium debugging endpoint. Needs no widgets and
// no uibase logging, so headless code can use it.
namespace SteamCef
{

class Error : public std::runtime_error
{
public:
  enum class Kind
  {
    Unavailable,  // nothing answered /json/list
    NoContext,    // no trusted SharedJSContext target
    NoResponse,   // no usable reply before the timeout, or the socket dropped
    ScriptFailed  // Steam answered with an error or a thrown exception
  };

  Error(Kind kind, const QString& message);
  Kind kind() const { return m_kind; }

private:
  Kind m_kind;
};

class Client
{
public:
  explicit Client(QString host = QStringLiteral("127.0.0.1"), quint16 port = 8080);

  // Evaluates in SharedJSContext and returns result.result.value. Spins a
  // nested event loop, so call it from the thread that owns the app.
  QJsonValue evaluate(const QString& expression, int timeoutMs = 10000) const;

private:
  QString m_host;
  quint16 m_port;
};

// JSON string literal, safe to splice into a JS expression. JSON allows raw
// U+2028/U+2029 inside strings but JS sources historically did not, so they
// are escaped as well.
QString jsString(const QString& text);

}  // namespace SteamCef

#endif
