#ifndef STARFIELDSCRIPTEXTENDER_H
#define STARFIELDSCRIPTEXTENDER_H

#include "gamebryoscriptextender.h"

class GameGamebryo;

class StarfieldScriptExtender : public GamebryoScriptExtender
{
public:
  StarfieldScriptExtender(GameGamebryo const* game);

  virtual QString BinaryName() const override;
  virtual QString PluginPath() const override;
};

#endif  // STARFIELDSCRIPTEXTENDER_H
