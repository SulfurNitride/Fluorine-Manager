#ifndef _SKYRIMSESCRIPTEXTENDER_H
#define _SKYRIMSESCRIPTEXTENDER_H

#include "gamebryoscriptextender.h"

class GameGamebryo;

class SkyrimSEScriptExtender : public GamebryoScriptExtender
{
public:
  SkyrimSEScriptExtender(GameGamebryo const* game);

  virtual QString BinaryName() const override;
  virtual QString PluginPath() const override;
};

#endif  // _SKYRIMSESCRIPTEXTENDER_H
