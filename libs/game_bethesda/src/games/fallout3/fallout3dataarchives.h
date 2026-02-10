#ifndef FALLOUT3DATAARCHIVES_H
#define FALLOUT3DATAARCHIVES_H

#include "gamebryodataarchives.h"
#include <QDir>

class Fallout3DataArchives : public GamebryoDataArchives
{
public:
  using GamebryoDataArchives::GamebryoDataArchives;

  virtual QStringList vanillaArchives() const override;
  virtual QStringList archives(const MOBase::IProfile* profile) const override;

private:
  virtual void writeArchiveList(MOBase::IProfile* profile,
                                const QStringList& before) override;
};

#endif  // FALLOUT3DATAARCHIVES_H
