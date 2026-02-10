#ifndef FALLOUT4VRSCRIPTEXTENDER_H
#define FALLOUT4VRSCRIPTEXTENDER_H

#include "gamebryoscriptextender.h"

class GameGamebryo;

class Fallout4VRScriptExtender : public GamebryoScriptExtender
{
public:
  Fallout4VRScriptExtender(GameGamebryo const* game);

  virtual QString BinaryName() const override;
  virtual QString PluginPath() const override;
};

#endif  // FALLOUT4SCRIPTEXTENDER_H
