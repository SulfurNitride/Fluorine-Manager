#ifndef SKYRIMSE_MODDATACONTENT_H
#define SKYRIMSE_MODDATACONTENT_H

#include <gamebryomoddatacontent.h>
#include <ifiletree.h>

class SkyrimSEModDataContent : public GamebryoModDataContent
{
public:
  /**
   *
   */
  SkyrimSEModDataContent(MOBase::IGameFeatures const* gameFeatures)
      : GamebryoModDataContent(gameFeatures)
  {
    // Just need to disable some contents:
    m_Enabled[CONTENT_SKYPROC] = false;
  }
};

#endif  // SKYRIMSE_MODDATACONTENT_H
