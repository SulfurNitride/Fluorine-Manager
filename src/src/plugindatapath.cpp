#include "plugindatapath.h"

#include <QDir>
#include <QFileDevice>
#include <QFileInfo>

namespace PluginDataPath
{

QString select(const QString& writableDefault,
               const QString& instancePluginDirectory,
               const QString& pluginIdentifier)
{
  if (instancePluginDirectory.isEmpty() || pluginIdentifier.isEmpty()) {
    return writableDefault;
  }

  const QString canonicalPlugins =
      QDir(instancePluginDirectory).canonicalPath();
  const QString canonicalPlugin =
      QFileInfo(pluginIdentifier).canonicalFilePath();
  if (canonicalPlugins.isEmpty() || canonicalPlugin.isEmpty()) {
    return writableDefault;
  }

  const QString canonicalParent = QFileInfo(canonicalPlugin).dir().canonicalPath();
  if (canonicalParent != canonicalPlugins) {
    return writableDefault;
  }

  const QString legacyData =
      QDir(instancePluginDirectory).filePath(QStringLiteral("data"));
  const QFileInfo dataInfo(legacyData);
  const auto permissions = dataInfo.permissions();
  // Owner/group/other represent the stored mode bits. The separate User flags
  // describe effective access and report root as writable even for a 0500
  // directory, which would make tests and privileged launches misclassify the
  // directory for the eventual desktop user.
  const auto readable = QFileDevice::ReadOwner | QFileDevice::ReadGroup |
                        QFileDevice::ReadOther;
  const auto writable = QFileDevice::WriteOwner | QFileDevice::WriteGroup |
                        QFileDevice::WriteOther;
  if (!dataInfo.isDir() || !dataInfo.isReadable() || !dataInfo.isWritable() ||
      !(permissions & readable) || !(permissions & writable)) {
    return writableDefault;
  }

  return QDir::cleanPath(legacyData);
}

}  // namespace PluginDataPath
