#ifndef FALLOUT3_MODDATACONTENT_H
#define FALLOUT3_MODDATACONTENT_H

#include <gamebryomoddatacontent.h>
#include <ifiletree.h>

class Fallout3ModDataContent : public GamebryoModDataContent
{
public:
  /**
   *
   */
  Fallout3ModDataContent(MOBase::IGameFeatures const* gameFeatures)
      : GamebryoModDataContent(gameFeatures)
  {
    // Just need to disable some contents:
    m_Enabled[CONTENT_MCM]     = false;
    m_Enabled[CONTENT_SKYPROC] = false;
  }
};

#endif  // FALLOUT3_MODDATACONTENT_H
