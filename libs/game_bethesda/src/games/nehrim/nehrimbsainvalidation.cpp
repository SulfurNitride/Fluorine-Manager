#include "nehrimbsainvalidation.h"

NehrimBSAInvalidation::NehrimBSAInvalidation(MOBase::DataArchives* dataArchives,
                                             MOBase::IPluginGame const* game)
    : GamebryoBSAInvalidation(dataArchives, "oblivion.ini", game)
{}

QString NehrimBSAInvalidation::invalidationBSAName() const
{
  return "Nehrim - Invalidation.bsa";
}

unsigned long NehrimBSAInvalidation::bsaVersion() const
{
  return 0x67;
}
