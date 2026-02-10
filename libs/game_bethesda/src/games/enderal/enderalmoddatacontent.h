#ifndef ENDERAL_MODDATACONTENT_H
#define ENDERAL_MODDATACONTENT_H

#include <gamebryomoddatacontent.h>
#include <ifiletree.h>

// Like Skyrim, Enderal does not need any change from the default feature:
class EnderalModDataContent : public GamebryoModDataContent
{
public:
  using GamebryoModDataContent::GamebryoModDataContent;
};

#endif  // ENDERAL_MODDATACONTENT_H
