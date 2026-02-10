#ifndef FALLOUTTTWBSAINVALIDATION_H
#define FALLOUTTTWBSAINVALIDATION_H

#include "falloutttwdataarchives.h"
#include "gamebryobsainvalidation.h"

#include <memory>

class FalloutTTWBSAInvalidation : public GamebryoBSAInvalidation
{
public:
  FalloutTTWBSAInvalidation(MOBase::DataArchives* dataArchives,
                            MOBase::IPluginGame const* game);

private:
  virtual QString invalidationBSAName() const override;
  virtual unsigned long bsaVersion() const override;
};

#endif  // FALLOUTTTWBSAINVALIDATION_H
