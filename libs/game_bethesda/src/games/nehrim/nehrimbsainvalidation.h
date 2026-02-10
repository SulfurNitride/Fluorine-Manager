#ifndef NEHRIMBSAINVALIDATION_H
#define NEHRIMBSAINVALIDATION_H

#include "gamebryobsainvalidation.h"
#include "nehrimdataarchives.h"

#include <memory>

class NehrimBSAInvalidation : public GamebryoBSAInvalidation
{
public:
  NehrimBSAInvalidation(MOBase::DataArchives* dataArchives,
                        MOBase::IPluginGame const* game);

private:
  virtual QString invalidationBSAName() const override;
  virtual unsigned long bsaVersion() const override;
};

#endif  // NEHRIMBSAINVALIDATION_H
