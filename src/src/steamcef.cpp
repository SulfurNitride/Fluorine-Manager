#include "steamcef.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>

namespace SteamCef
{

Error::Error(Kind kind, const QString& message)
    : std::runtime_error(message.toStdString()), m_kind(kind)
{}

Client::Client(QString host, quint16 port) : m_host(std::move(host)), m_port(port) {}

QJsonValue Client::evaluate(const QString& expression, int timeoutMs) const
{
  QNetworkAccessManager network;
  network.setProxy(QNetworkProxy::NoProxy);
  QNetworkRequest request(
      QUrl(QStringLiteral("http://%1:%2/json/list").arg(m_host).arg(m_port)));
  request.setTransferTimeout(3000);
  auto* reply = network.get(request);
  QEventLoop loop;
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  loop.exec(QEventLoop::ExcludeUserInputEvents);
  const auto replyError = reply->error();
  const auto body       = reply->readAll();
  reply->deleteLater();
  if (replyError != QNetworkReply::NoError)
    throw Error(Error::Kind::Unavailable,
                QStringLiteral("Steam's local debugging connection is unavailable."));

  QUrl endpoint;
  for (const auto& target : QJsonDocument::fromJson(body).array()) {
    const auto object = target.toObject();
    if (object.value("title").toString() == "SharedJSContext")
      endpoint = QUrl(object.value("webSocketDebuggerUrl").toString());
  }
  if (endpoint.scheme() != "ws" || endpoint.host() != m_host ||
      endpoint.port() != m_port)
    throw Error(Error::Kind::NoContext,
                QStringLiteral("Steam did not expose a trusted local UI context."));

  QWebSocket socket;
  socket.setProxy(QNetworkProxy::NoProxy);
  socket.setMaxAllowedIncomingMessageSize(2 * 1024 * 1024);
  QTimer timeout;
  timeout.setSingleShot(true);
  QJsonObject response;
  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  QObject::connect(&socket, &QWebSocket::disconnected, &loop, &QEventLoop::quit);
  QObject::connect(&socket, &QWebSocket::connected, &loop, [&] {
    socket.sendTextMessage(QString::fromUtf8(
        QJsonDocument(QJsonObject{{"id", 1},
                                  {"method", "Runtime.evaluate"},
                                  {"params", QJsonObject{{"expression", expression},
                                                         {"returnByValue", true},
                                                         {"awaitPromise", true}}}})
            .toJson()));
  });
  QObject::connect(
      &socket, &QWebSocket::textMessageReceived, &loop, [&](const QString& text) {
        const auto message = QJsonDocument::fromJson(text.toUtf8()).object();
        if (message.value("id").toInt() == 1) {
          response = message;
          loop.quit();
        }
      });
  timeout.start(timeoutMs);
  socket.open(endpoint);
  loop.exec(QEventLoop::ExcludeUserInputEvents);
  socket.abort();

  if (response.isEmpty())
    throw Error(Error::Kind::NoResponse,
                QStringLiteral("Steam's internal interface did not respond."));
  // A frame with neither payload is as unusable as no frame; a script that
  // returns undefined still carries a `result` object.
  if (!response.contains("result") && !response.contains("error"))
    throw Error(Error::Kind::NoResponse,
                QStringLiteral("Steam's internal interface did not respond."));
  const auto result = response.value("result").toObject();
  if (response.contains("error") || result.contains("exceptionDetails"))
    throw Error(Error::Kind::ScriptFailed,
                QStringLiteral("Steam's internal interface reported an error."));
  return result.value("result").toObject().value("value");
}

QString jsString(const QString& text)
{
  // A bare string isn't a JSON document, so serialize it as a one-element array.
  auto json =
      QString::fromUtf8(QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact));
  json = json.mid(1, json.size() - 2);
  json.replace(QChar(0x2028), QStringLiteral("\\u2028"));
  json.replace(QChar(0x2029), QStringLiteral("\\u2029"));
  return json;
}

}  // namespace SteamCef
