#ifndef MORROWINDDATAARCHIVES_H
#define MORROWINDDATAARCHIVES_H

#include <QDir>
#include <QString>
#include <QStringList>
#include <gamebryodataarchives.h>
#include <iplugingame.h>
#include <iprofile.h>

class MorrowindDataArchives : public GamebryoDataArchives
{
public:
  using GamebryoDataArchives::GamebryoDataArchives;

  virtual QStringList vanillaArchives() const override;
  virtual QStringList archives(const MOBase::IProfile* profile) const override;

protected:
  QStringList getArchives(const QString& iniFile) const;
  void setArchives(const QString& iniFile, const QStringList& list);

private:
  virtual void writeArchiveList(MOBase::IProfile* profile,
                                const QStringList& before) override;
};

#endif  // MORROWINDDATAARCHIVES_H
