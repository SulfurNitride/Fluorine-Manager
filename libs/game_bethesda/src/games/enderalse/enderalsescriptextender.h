#ifndef ENDERALSESCRIPTEXTENDER_H
#define ENDERALESCRIPTEXTENDER_H

#include "gamebryoscriptextender.h"

class GameGamebryo;

class EnderalSEScriptExtender : public GamebryoScriptExtender
{
public:
  EnderalSEScriptExtender(GameGamebryo const* game);

  virtual QString BinaryName() const override;
  virtual QString PluginPath() const override;
};

#endif  // _SKYRIMSESCRIPTEXTENDER_H
