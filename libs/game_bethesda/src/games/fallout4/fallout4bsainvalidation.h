#ifndef FALLOUT4BSAINVALIDATION_H
#define FALLOUT4BSAINVALIDATION_H

#include "fallout4dataarchives.h"
#include "gamebryobsainvalidation.h"
#include <bsainvalidation.h>
#include <dataarchives.h>

#include <memory>

namespace MOBase
{
class IPluginGame;
}

class Fallout4BSAInvalidation : public GamebryoBSAInvalidation
{
public:
  Fallout4BSAInvalidation(MOBase::DataArchives* dataArchives,
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

#endif  // FALLOUT4BSAINVALIDATION_H
