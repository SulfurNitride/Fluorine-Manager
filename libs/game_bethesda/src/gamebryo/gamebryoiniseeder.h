#ifndef GAMEBRYOINISEEDER_H
#define GAMEBRYOINISEEDER_H

#include <QString>

namespace GamebryoIniSeeder
{

struct Result
{
  bool success{false};
  bool seeded{false};
  QString authoritativePath;
  QString error;
};

// Preserves every existing case variant. When the family is completely absent,
// atomically seeds canonicalName from sourcePath and creates checked same-family
// aliases. An absent source is a successful no-op; unsafe or ambiguous leaves
// fail without changing the family.
[[nodiscard]] Result ensure(const QString& basePath, const QString& canonicalName,
                            const QString& sourcePath);

}  // namespace GamebryoIniSeeder

#endif  // GAMEBRYOINISEEDER_H
