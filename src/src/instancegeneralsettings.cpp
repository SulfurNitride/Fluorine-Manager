#include "instancegeneralsettings.h"

#include <QString>

namespace InstanceGeneralSettings {
namespace {
struct KeyNames {
  QString canonical;
  QString legacy;
};

KeyNames names(Key key) {
  switch (key) {
  case Key::GameName:
    return {QStringLiteral("gameName"), QStringLiteral("General/gameName")};
  case Key::GamePath:
    return {QStringLiteral("gamePath"), QStringLiteral("General/gamePath")};
  case Key::Portable:
    return {QStringLiteral("portable"), QStringLiteral("General/portable")};
  }

  Q_UNREACHABLE();
}
} // namespace

std::optional<QVariant> read(const QSettings &settings, Key key) {
  const KeyNames keyNames = names(key);
  if (settings.contains(keyNames.canonical)) {
    return settings.value(keyNames.canonical);
  }
  if (settings.contains(keyNames.legacy)) {
    return settings.value(keyNames.legacy);
  }

  return std::nullopt;
}

void write(QSettings &settings, Key key, const QVariant &value) {
  settings.setValue(names(key).canonical, value);
}
} // namespace InstanceGeneralSettings
