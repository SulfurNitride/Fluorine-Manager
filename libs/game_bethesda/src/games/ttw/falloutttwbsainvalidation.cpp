#include "falloutttwbsainvalidation.h"

FalloutTTWBSAInvalidation::FalloutTTWBSAInvalidation(MOBase::DataArchives* dataArchives,
                                                     MOBase::IPluginGame const* game)
    : GamebryoBSAInvalidation(dataArchives, "fallout.ini", game)
{}

QString FalloutTTWBSAInvalidation::invalidationBSAName() const
{
  return "Fallout - Invalidation.bsa";
}

unsigned long FalloutTTWBSAInvalidation::bsaVersion() const
{
  return 0x68;
}
