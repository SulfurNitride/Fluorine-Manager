#ifndef FO4LONDONBSAINVALIDATION_H
#define FO4LONDONBSAINVALIDATION_H

#include "fo4londondataarchives.h"
#include "gamebryobsainvalidation.h"
#include <bsainvalidation.h>
#include <dataarchives.h>

#include <memory>

namespace MOBase
{
class IPluginGame;
}

class Fallout4LondonBSAInvalidation : public GamebryoBSAInvalidation
{
public:
  Fallout4LondonBSAInvalidation(MOBase::DataArchives* dataArchives,
                                MOBase::IPluginGame const* game);
  virtual bool isInvalidationBSA(const QString& bsaName) override;
  virtual bool prepareProfile(MOBase::IProfile* profile) override;

private:
  virtual QString invalidationBSAName() const override;
  virtual unsigned long bsaVersion() const override;

private:
  QString m_IniFileName;
  MOBase::IPluginGame const* m_Game;
};

#endif  // FO4LONDONBSAINVALIDATION_H
