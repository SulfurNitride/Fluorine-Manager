#include "fallout3bsainvalidation.h"

Fallout3BSAInvalidation::Fallout3BSAInvalidation(MOBase::DataArchives* dataArchives,
                                                 MOBase::IPluginGame const* game)
    : GamebryoBSAInvalidation(dataArchives, "fallout.ini", game)
{}

QString Fallout3BSAInvalidation::invalidationBSAName() const
{
  return "Fallout - Invalidation.bsa";
}

unsigned long Fallout3BSAInvalidation::bsaVersion() const
{
  return 0x68;
}
