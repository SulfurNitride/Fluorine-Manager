#ifndef FALLOUTNVSCRIPTEXTENDER_H
#define FALLOUTNVSCRIPTEXTENDER_H

#include "gamebryoscriptextender.h"

class GameGamebryo;

class FalloutNVScriptExtender : public GamebryoScriptExtender
{
public:
  FalloutNVScriptExtender(const GameGamebryo* game);

  virtual QString BinaryName() const override;
  virtual QString PluginPath() const override;
};

#endif  // FALLOUTNVSCRIPTEXTENDER_H
