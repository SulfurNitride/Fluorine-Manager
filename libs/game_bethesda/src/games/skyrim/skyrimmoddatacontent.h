#ifndef SKYRIM_MODDATACONTENT_H
#define SKYRIM_MODDATACONTENT_H

#include <gamebryomoddatacontent.h>
#include <ifiletree.h>

// Skyrim does not need any change from the default feature:
class SkyrimModDataContent : public GamebryoModDataContent
{
public:
  using GamebryoModDataContent::GamebryoModDataContent;
};

#endif  // SKYRIM_MODDATACONTENT_H
