#ifndef ENDERALSEDATAARCHIVES_H
#define ENDERALSEDATAARCHIVES_H

#include "gamebryodataarchives.h"
#include <QDir>
#include <QStringList>

namespace MOBase
{
class IProfile;
}

class EnderalSEDataArchives : public GamebryoDataArchives
{
public:
  using GamebryoDataArchives::GamebryoDataArchives;

  virtual QStringList vanillaArchives() const override;
  virtual QStringList archives(const MOBase::IProfile* profile) const override;

private:
  virtual void writeArchiveList(MOBase::IProfile* profile,
                                const QStringList& before) override;
};

#endif  // _SKYRIMSEDATAARCHIVES_H
