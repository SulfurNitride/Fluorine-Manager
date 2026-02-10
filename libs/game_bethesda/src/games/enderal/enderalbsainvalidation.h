#ifndef ENDERALBSAINVALIDATION_H
#define ENDERALBSAINVALIDATION_H

#include "enderaldataarchives.h"
#include "gamebryobsainvalidation.h"

#include <memory>

class EnderalBSAInvalidation : public GamebryoBSAInvalidation
{
public:
  EnderalBSAInvalidation(MOBase::DataArchives* dataArchives,
                         MOBase::IPluginGame const* game);

private:
  virtual QString invalidationBSAName() const override;
  virtual unsigned long bsaVersion() const override;
};

#endif  // ENDERALBSAINVALIDATION_H
