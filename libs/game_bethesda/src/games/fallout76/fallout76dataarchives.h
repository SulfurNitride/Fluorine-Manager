#ifndef FALLOUT76DATAARCHIVES_H
#define FALLOUT76DATAARCHIVES_H

#include "gamebryodataarchives.h"

namespace MOBase
{
class IProfile;
}

#include <QDir>
#include <QStringList>

class Fallout76DataArchives : public GamebryoDataArchives
{
public:
  using GamebryoDataArchives::GamebryoDataArchives;

  virtual QStringList vanillaArchives() const override;
  virtual QStringList sResourceIndexFileList() const;
  virtual QStringList sResourceStartUpArchiveList() const;
  virtual QStringList SResourceArchiveMemoryCacheList() const;
  virtual QStringList SResourceArchiveList() const;
  virtual QStringList SResourceArchiveList2() const;
  virtual QStringList sResourceArchive2List() const;
  virtual QStringList archives(const MOBase::IProfile* profile) const override;

private:
  virtual void writeArchiveList(MOBase::IProfile* profile,
                                const QStringList& before) override;
};

#endif  // FALLOUT76DATAARCHIVES_H
