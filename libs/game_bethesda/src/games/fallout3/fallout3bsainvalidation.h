#ifndef FALLOUT3BSAINVALIDATION_H
#define FALLOUT3BSAINVALIDATION_H

#include "fallout3dataarchives.h"
#include "gamebryobsainvalidation.h"

#include <memory>

class Fallout3BSAInvalidation : public GamebryoBSAInvalidation
{
public:
  Fallout3BSAInvalidation(MOBase::DataArchives* dataArchives,
                          MOBase::IPluginGame const* game);

private:
  virtual QString invalidationBSAName() const override;
  virtual unsigned long bsaVersion() const override;
};

#endif  // FALLOUT3BSAINVALIDATION_H
