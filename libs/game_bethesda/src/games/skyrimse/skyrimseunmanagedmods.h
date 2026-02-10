#ifndef _SKYRIMSEUNMANAGEDMODS_H
#define _SKYRIMSEUNMANAGEDMODS_H

#include "gamebryounmanagedmods.h"
#include <gamegamebryo.h>

class SkyrimSEUnmangedMods : public GamebryoUnmangedMods
{
public:
  SkyrimSEUnmangedMods(const GameGamebryo* game);
  ~SkyrimSEUnmangedMods();

  virtual QStringList mods(bool onlyOfficial) const override;
};

#endif  // _SKYRIMSEUNMANAGEDMODS_H
