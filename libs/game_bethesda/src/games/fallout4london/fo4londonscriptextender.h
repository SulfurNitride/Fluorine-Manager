#ifndef FO4LONDONSCRIPTEXTENDER_H
#define FO4LONDONSCRIPTEXTENDER_H

#include "gamebryoscriptextender.h"

class GameGamebryo;

class Fallout4LondonScriptExtender : public GamebryoScriptExtender
{
public:
  Fallout4LondonScriptExtender(GameGamebryo const* game);

  virtual QString BinaryName() const override;
  virtual QString PluginPath() const override;
};

#endif  // FO4LONDONSCRIPTEXTENDER_H
