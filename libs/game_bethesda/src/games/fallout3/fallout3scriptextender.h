#ifndef FALLOUT3SCRIPTEXTENDER_H
#define FALLOUT3SCRIPTEXTENDER_H

#include "gamebryoscriptextender.h"

class GameGamebryo;

class Fallout3ScriptExtender : public GamebryoScriptExtender
{
public:
  Fallout3ScriptExtender(GameGamebryo const* game);

  virtual QString BinaryName() const override;
  virtual QString PluginPath() const override;
};

#endif  // FALLOUT3SCRIPTEXTENDER_H
