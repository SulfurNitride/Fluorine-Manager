#ifndef _SKYRIMVRSCRIPTEXTENDER_H
#define _SKYRIMVRSCRIPTEXTENDER_H

#include "gamebryoscriptextender.h"

class GameGamebryo;

class SkyrimVRScriptExtender : public GamebryoScriptExtender
{
public:
  SkyrimVRScriptExtender(GameGamebryo const* game);

  virtual QString BinaryName() const override;
  virtual QString PluginPath() const override;
};

#endif  // _SKYRIMVRSCRIPTEXTENDER_H
