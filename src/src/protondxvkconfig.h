#ifndef PROTONDXVKCONFIG_H
#define PROTONDXVKCONFIG_H

#include <QString>

namespace ProtonDxvkConfig
{

struct Result
{
  QString path;
  QString error;

  [[nodiscard]] explicit operator bool() const noexcept { return !path.isEmpty(); }
};

// Publishes the Fluorine-owned DXVK configuration without touching dxvk.conf.
// A pre-existing generated-path collision is accepted only when its contents
// are already exactly the generation Fluorine would publish.
[[nodiscard]] Result publish(const QString& prefixPath);

}  // namespace ProtonDxvkConfig

#endif  // PROTONDXVKCONFIG_H
