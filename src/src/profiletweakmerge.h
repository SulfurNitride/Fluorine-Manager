#pragma once

#include <QString>
#include <QStringList>

namespace ProfileTweakMerge {

// Merges tweak files in list order and atomically publishes one complete
// initweaks.ini generation. Missing inputs are ignored for compatibility;
// present inputs that cannot be read or parsed fail without changing target.
[[nodiscard]] bool publish(const QStringList &tweakFiles,
                           const QString &targetPath, QString *error = nullptr);

} // namespace ProfileTweakMerge
