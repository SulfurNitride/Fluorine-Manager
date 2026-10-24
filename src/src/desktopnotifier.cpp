#include "desktopnotifier.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QStringList>
#include <QUuid>
#include <QVariantMap>
#include <uibase/log.h>

using namespace MOBase;

namespace notify
{
namespace
{
  constexpr auto Service = "org.freedesktop.Notifications";
  constexpr auto Path    = "/org/freedesktop/Notifications";
  // The default 25 s would stall a launch that is already failing.
  constexpr int CallTimeoutMs = 2000;

  class QtBus : public Bus
  {
  public:
    QtBus(const QString& address)
    {
      if (address.isEmpty()) {
        m_bus = QDBusConnection::sessionBus();
      } else {
        m_name = "fluorine-notify-" + QUuid::createUuid().toString(QUuid::Id128);
        m_bus  = QDBusConnection::connectToBus(address, m_name);
      }
    }

    ~QtBus() override
    {
      m_bus = QDBusConnection(QString());
      if (!m_name.isEmpty()) {
        QDBusConnection::disconnectFromBus(m_name);
      }
    }

    bool hasOwner() override
    {
      if (!m_bus.isConnected() || m_bus.interface() == nullptr) {
        return false;
      }
      // isServiceRegistered() has no timeout parameter, so call NameHasOwner
      // directly to keep the bounded wait.
      auto message = QDBusMessage::createMethodCall(
          "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
          "NameHasOwner");
      message << QString(Service);
      const auto reply = m_bus.call(message, QDBus::Block, CallTimeoutMs);
      return reply.type() == QDBusMessage::ReplyMessage &&
             !reply.arguments().isEmpty() && reply.arguments().first().toBool();
    }

    bool notify(const QString& appName, const QString& summary,
                const QString& body) override
    {
      if (!m_bus.isConnected() || m_bus.interface() == nullptr) {
        log::warn("notification not sent: no D-Bus session bus");
        return false;
      }

      auto message = QDBusMessage::createMethodCall(Service, Path, Service, "Notify");
      message << appName << uint(0) << QString() << summary << body << QStringList()
              << QVariantMap() << int(-1);

      const auto reply = m_bus.call(message, QDBus::Block, CallTimeoutMs);
      if (reply.type() == QDBusMessage::ErrorMessage) {
        log::warn("notification not sent: {}", reply.errorMessage());
        return false;
      }
      return true;
    }

  private:
    QString m_name;
    QDBusConnection m_bus{QString()};
  };
}  // namespace

QString sessionBusAddress(const QProcessEnvironment& env)
{
  const auto address = env.value("DBUS_SESSION_BUS_ADDRESS");
  if (!address.isEmpty()) {
    return address;
  }

  const auto runtime = env.value("XDG_RUNTIME_DIR");
  if (!runtime.isEmpty()) {
    return "unix:path=" + runtime + "/bus";
  }
  return {};
}

std::unique_ptr<Bus> sessionBus(const QProcessEnvironment& env)
{
  return std::make_unique<QtBus>(sessionBusAddress(env));
}

bool send(Bus& bus, const QString& summary, const QString& body)
{
  if (!bus.hasOwner()) {
    return false;
  }
  return bus.notify("Fluorine", summary, body);
}
}  // namespace notify
