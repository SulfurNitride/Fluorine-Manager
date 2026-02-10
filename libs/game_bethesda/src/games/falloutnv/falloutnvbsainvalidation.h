#ifndef FALLOUTNVBSAINVALIDATION_H
#define FALLOUTNVBSAINVALIDATION_H

#include "falloutnvdataarchives.h"
#include "gamebryobsainvalidation.h"

#include <memory>

class FalloutNVBSAInvalidation : public GamebryoBSAInvalidation
{
public:
  FalloutNVBSAInvalidation(MOBase::DataArchives* dataArchives,
                           MOBase::IPluginGame const* game);

private:
  virtual QString invalidationBSAName() const override;
  virtual unsigned long bsaVersion() const override;
};

#endif  // FALLOUTNVBSAINVALIDATION_H
