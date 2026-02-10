#ifndef ENDERALSEUNMANAGEDMODS_H
#define ENDERALSEUNMANAGEDMODS_H

#include "gamebryounmanagedmods.h"
#include <gamegamebryo.h>

class EnderalSEUnmangedMods : public GamebryoUnmangedMods
{
public:
  EnderalSEUnmangedMods(const GameGamebryo* game);
  ~EnderalSEUnmangedMods();

  virtual QStringList mods(bool onlyOfficial) const override;
};

#endif  // _SKYRIMSEUNMANAGEDMODS_H
