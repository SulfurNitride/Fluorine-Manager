#ifndef FALLOUT4VRUNMANAGEDMODS_H
#define FALLOUT4VRUNMANAGEDMODS_H

#include "gamebryounmanagedmods.h"
#include <gamegamebryo.h>

class Fallout4VRUnmangedMods : public GamebryoUnmangedMods
{
public:
  Fallout4VRUnmangedMods(const GameGamebryo* game);
  ~Fallout4VRUnmangedMods();

  virtual QStringList mods(bool onlyOfficial) const override;
  virtual QStringList secondaryFiles(const QString& modName) const override;
  virtual QString displayName(const QString& modName) const override;
};

#endif  // FALLOUT4UNMANAGEDMODS_H
