#ifndef STARFIELDDATAARCHIVES_H
#define STARFIELDDATAARCHIVES_H

#include <QDir>
#include <QStringList>

#include "gamebryodataarchives.h"

class GameGamebryo;

class StarfieldDataArchives : public GamebryoDataArchives
{
public:
  using GamebryoDataArchives::GamebryoDataArchives;

  virtual QStringList vanillaArchives() const override;
  virtual QStringList archives(const MOBase::IProfile* profile) const override;
  virtual void addArchive(MOBase::IProfile* profile, int index,
                          const QString& archiveName) override;
  virtual void removeArchive(MOBase::IProfile* profile,
                             const QString& archiveName) override;

private:
  virtual void writeArchiveList(MOBase::IProfile* profile,
                                const QStringList& before) override;
};

#endif  // STARFIELDDATAARCHIVES_H
