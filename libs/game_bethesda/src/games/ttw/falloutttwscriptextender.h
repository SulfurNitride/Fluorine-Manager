#ifndef FALLOUTTTWSCRIPTEXTENDER_H
#define FALLOUTTTWSCRIPTEXTENDER_H

#include "gamebryoscriptextender.h"

class GameGamebryo;

class FalloutTTWScriptExtender : public GamebryoScriptExtender
{
public:
  FalloutTTWScriptExtender(const GameGamebryo* game);

  virtual QString BinaryName() const override;
  virtual QString PluginPath() const override;
};

#endif  // FALLOUTTTWSCRIPTEXTENDER_H
