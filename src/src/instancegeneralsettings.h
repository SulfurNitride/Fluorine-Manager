#ifndef INSTANCEGENERALSETTINGS_H
#define INSTANCEGENERALSETTINGS_H

#include <QSettings>
#include <QVariant>

#include <optional>

namespace InstanceGeneralSettings {
// QSettings represents physical [General] entries as root keys. Affected
// Fluorine writers instead used General/... and created a separate [%General]
// group. Keep the compatibility mapping finite and shared by every consumer.
enum class Key {
  GameName,
  GamePath,
  Portable,
};

// A present canonical value always wins, including an empty string or false.
// The exact legacy key is consulted only when the canonical key is absent.
std::optional<QVariant> read(const QSettings &settings, Key key);

// New writes always use the canonical root key.
void write(QSettings &settings, Key key, const QVariant &value);
} // namespace InstanceGeneralSettings

#endif // INSTANCEGENERALSETTINGS_H
