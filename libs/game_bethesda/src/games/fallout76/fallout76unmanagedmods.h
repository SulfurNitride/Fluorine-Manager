#ifndef FALLOUT76UNMANAGEDMODS_H
#define FALLOUT76UNMANAGEDMODS_H

#include "gamebryounmanagedmods.h"
#include <gamegamebryo.h>

class Fallout76UnmangedMods : public GamebryoUnmangedMods
{
public:
  Fallout76UnmangedMods(const GameGamebryo* game);
  ~Fallout76UnmangedMods();

  virtual QStringList mods(bool onlyOfficial) const override;
  virtual QStringList secondaryFiles(const QString& modName) const override;
  virtual QString displayName(const QString& modName) const override;
};

#endif  // FALLOUT76UNMANAGEDMODS_H
