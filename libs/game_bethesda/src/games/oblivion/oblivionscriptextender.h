#ifndef OBLIVIONSCRIPTEXTENDER_H
#define OBLIVIONSCRIPTEXTENDER_H

#include "gamebryoscriptextender.h"

class GameGamebryo;

class OblivionScriptExtender : public GamebryoScriptExtender
{
public:
  OblivionScriptExtender(const GameGamebryo* game);
  ~OblivionScriptExtender();

  virtual QString BinaryName() const override;
  virtual QString PluginPath() const override;
};

#endif  // OBLIVIONSCRIPTEXTENDER_H
