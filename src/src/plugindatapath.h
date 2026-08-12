#ifndef PLUGINDATAPATH_H
#define PLUGINDATAPATH_H

#include <QString>

namespace PluginDataPath
{

// Preserve the writable default for bundled and external plugins. A plugin
// loaded directly from the active instance's plugins directory may use the
// legacy shared plugins/data directory when that directory is usable.
QString select(const QString& writableDefault,
               const QString& instancePluginDirectory,
               const QString& pluginIdentifier);

}  // namespace PluginDataPath

#endif  // PLUGINDATAPATH_H
