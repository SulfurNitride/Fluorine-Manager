#ifndef FALLOUT4DATAARCHIVES_H
#define FALLOUT4DATAARCHIVES_H

#include "gamebryodataarchives.h"

namespace MOBase
{
class IProfile;
}

#include <QDir>
#include <QStringList>

class Fallout4DataArchives : public GamebryoDataArchives
{
public:
  using GamebryoDataArchives::GamebryoDataArchives;

  virtual QStringList vanillaArchives() const override;
  virtual QStringList archives(const MOBase::IProfile* profile) const override;

private:
  virtual void writeArchiveList(MOBase::IProfile* profile,
                                const QStringList& before) override;
};

#endif  // FALLOUT4DATAARCHIVES_H
