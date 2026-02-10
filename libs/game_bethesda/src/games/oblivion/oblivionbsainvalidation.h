#ifndef OBLIVIONBSAINVALIDATION_H
#define OBLIVIONBSAINVALIDATION_H

#include "gamebryobsainvalidation.h"
#include "obliviondataarchives.h"

#include <memory>

class OblivionBSAInvalidation : public GamebryoBSAInvalidation
{
public:
  OblivionBSAInvalidation(MOBase::DataArchives* dataArchives,
                          MOBase::IPluginGame const* game);

private:
  virtual QString invalidationBSAName() const override;
  virtual unsigned long bsaVersion() const override;
};

#endif  // OBLIVIONBSAINVALIDATION_H
