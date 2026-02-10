#ifndef OBLIVIONDATAARCHIVES_H
#define OBLIVIONDATAARCHIVES_H

#include <QDir>
#include <QString>
#include <QStringList>
#include <gamebryodataarchives.h>
#include <iprofile.h>

class OblivionDataArchives : public GamebryoDataArchives
{
public:
  using GamebryoDataArchives::GamebryoDataArchives;

  virtual QStringList vanillaArchives() const override;
  virtual QStringList archives(const MOBase::IProfile* profile) const override;

private:
  virtual void writeArchiveList(MOBase::IProfile* profile,
                                const QStringList& before) override;
};

#endif  // OBLIVIONDATAARCHIVES_H
