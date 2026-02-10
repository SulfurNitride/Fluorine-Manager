#ifndef ENDERALSCRIPTEXTENDER_H
#define ENDERALSCRIPTEXTENDER_H

#include "gamebryoscriptextender.h"

class GameGamebryo;

class EnderalScriptExtender : public GamebryoScriptExtender
{
public:
  EnderalScriptExtender(const GameGamebryo* game);

  virtual QString BinaryName() const override;
  virtual QString PluginPath() const override;
};

#endif  // ENDERALSCRIPTEXTENDER_H
