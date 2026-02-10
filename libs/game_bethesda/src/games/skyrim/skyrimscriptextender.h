#ifndef SKYRIMSCRIPTEXTENDER_H
#define SKYRIMSCRIPTEXTENDER_H

#include "gamebryoscriptextender.h"

class GameGamebryo;

class SkyrimScriptExtender : public GamebryoScriptExtender
{
public:
  SkyrimScriptExtender(const GameGamebryo* game);

  virtual QString BinaryName() const override;
  virtual QString PluginPath() const override;
};

#endif  // SKYRIMSCRIPTEXTENDER_H
