#ifndef FALLOUTTTW_MODDATACONTENT_H
#define FALLOUTTTW_MODDATACONTENT_H

#include <gamebryomoddatacontent.h>
#include <ifiletree.h>

class FalloutTTWModDataContent : public GamebryoModDataContent
{
public:
  /**
   *
   */
  FalloutTTWModDataContent(MOBase::IGameFeatures const* gameFeatures)
      : GamebryoModDataContent(gameFeatures)
  {
    // Just need to disable some contents:
    m_Enabled[CONTENT_MCM]     = false;
    m_Enabled[CONTENT_SKYPROC] = false;
  }
};

#endif  // FALLOUTTTW_MODDATACONTENT_H
