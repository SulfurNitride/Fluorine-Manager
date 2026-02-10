#ifndef FALLOUT76SCRIPTEXTENDER_H
#define FALLOUT76SCRIPTEXTENDER_H

#include "gamebryoscriptextender.h"

class GameGamebryo;

class Fallout76ScriptExtender : public GamebryoScriptExtender
{
public:
  Fallout76ScriptExtender(GameGamebryo const* game);

  virtual QString BinaryName() const override;
  virtual QString PluginPath() const override;
};

#endif  // FALLOUT76SCRIPTEXTENDER_H
