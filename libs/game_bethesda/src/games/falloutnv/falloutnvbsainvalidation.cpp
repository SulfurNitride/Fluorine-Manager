#include "falloutnvbsainvalidation.h"

FalloutNVBSAInvalidation::FalloutNVBSAInvalidation(MOBase::DataArchives* dataArchives,
                                                   MOBase::IPluginGame const* game)
    : GamebryoBSAInvalidation(dataArchives, "fallout.ini", game)
{}

QString FalloutNVBSAInvalidation::invalidationBSAName() const
{
  return "Fallout - Invalidation.bsa";
}

unsigned long FalloutNVBSAInvalidation::bsaVersion() const
{
  return 0x68;
}
