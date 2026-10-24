#pragma once

#include <QProcessEnvironment>
#include <QString>
#include <memory>

namespace notify
{
// Seam over org.freedesktop.Notifications so callers can be tested without a
// bus.
class Bus
{
public:
  virtual ~Bus()                           = default;
  virtual bool hasOwner()                  = 0;
  virtual bool notify(const QString& appName, const QString& summary,
                      const QString& body) = 0;
};

// DBUS_SESSION_BUS_ADDRESS, else the standard socket under XDG_RUNTIME_DIR,
// else empty.
QString sessionBusAddress(const QProcessEnvironment& env);

// QtDBus-backed bus. An empty address means the default session bus.
std::unique_ptr<Bus> sessionBus(const QProcessEnvironment& env);

// Shows a notification as "Fluorine". False when nobody is listening or the
// call fails; never throws.
bool send(Bus& bus, const QString& summary, const QString& body);
}  // namespace notify
