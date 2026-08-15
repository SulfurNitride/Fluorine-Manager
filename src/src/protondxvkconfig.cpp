#include "protondxvkconfig.h"

#include <uibase/transactionalwritefile.h>

#include <QDir>

namespace ProtonDxvkConfig
{
namespace
{
constexpr auto FileName = ".fluorine-dxvk.conf";
constexpr auto Contents = "dxvk.enableGraphicsPipelineLibrary = False\n";
}  // namespace

Result publish(const QString& prefixPath)
{
  if (prefixPath.isEmpty())
  {
    return {{}, QStringLiteral("Wine prefix path is empty")};
  }

  const QString path = QDir(prefixPath).filePath(QString::fromLatin1(FileName));
  MOBase::TransactionalWriteFile transaction(path);

  QByteArray original;
  bool present = false;
  if (!transaction.readOriginal(original, present))
  {
    return {{}, transaction.errorString()};
  }

  const QByteArray desired(Contents);
  if (present)
  {
    if (original == desired)
    {
      return {path, {}};
    }

    return {
        {},
        QStringLiteral("Refusing to replace existing non-Fluorine file '%1'").arg(path)};
  }

  if (!transaction.replaceWith(desired))
  {
    return {{}, transaction.errorString()};
  }

  return {path, {}};
}

}  // namespace ProtonDxvkConfig
