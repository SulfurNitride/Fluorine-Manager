#ifndef _SKYRIMVRUNMANAGEDMODS_H
#define _SKYRIMVRUNMANAGEDMODS_H

#include "gamebryounmanagedmods.h"
#include <gamegamebryo.h>

class SkyrimVRUnmangedMods : public GamebryoUnmangedMods
{
public:
  SkyrimVRUnmangedMods(const GameGamebryo* game);
  ~SkyrimVRUnmangedMods();

  virtual QStringList mods(bool onlyOfficial) const override;
};

#endif  // _SKYRIMVRUNMANAGEDMODS_H
