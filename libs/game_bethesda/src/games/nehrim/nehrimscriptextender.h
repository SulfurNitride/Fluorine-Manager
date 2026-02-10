#ifndef NEHRIMSCRIPTEXTENDER_H
#define NEHRIMSCRIPTEXTENDER_H

#include "gamebryoscriptextender.h"

class GameGamebryo;

class NehrimScriptExtender : public GamebryoScriptExtender
{
public:
  NehrimScriptExtender(const GameGamebryo* game);
  ~NehrimScriptExtender();

  virtual QString BinaryName() const override;
  virtual QString PluginPath() const override;
};

#endif  // NEHRIMSCRIPTEXTENDER_H
