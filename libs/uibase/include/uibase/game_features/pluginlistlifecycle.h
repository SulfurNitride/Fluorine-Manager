#ifndef UIBASE_GAMEFEATURES_PLUGINLISTLIFECYCLE_H
#define UIBASE_GAMEFEATURES_PLUGINLISTLIFECYCLE_H

#include "./game_feature.h"

namespace MOBase
{

class IPluginList;

/**
 * Optional observer for the complete PluginList refresh transaction.
 *
 * This is a separate game feature instead of an extension to GamePlugins so
 * adding the lifecycle does not change the GamePlugins vtable used by native
 * plugins built against an earlier uibase. flushPendingWrites() provides the
 * synchronous persistence boundary needed when core coalesces refresh work.
 */
class PluginListLifecycle : public details::GameFeatureCRTP<PluginListLifecycle>
{
public:
  virtual void refreshStarted()   = 0;
  virtual void refreshCompleted() = 0;
  virtual void refreshFailed()    = 0;
  virtual void flushPendingWrites(IPluginList* pluginList) = 0;
};

}  // namespace MOBase

#endif  // UIBASE_GAMEFEATURES_PLUGINLISTLIFECYCLE_H
