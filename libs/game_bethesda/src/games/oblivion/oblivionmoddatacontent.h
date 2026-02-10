#ifndef OBLIVION_MODDATACONTENT_H
#define OBLIVION_MODDATACONTENT_H

#include <gamebryomoddatacontent.h>
#include <ifiletree.h>

class OblivionModDataContent : public GamebryoModDataContent
{
public:
  /**
   *
   */
  OblivionModDataContent(const MOBase::IGameFeatures* gameFeatures)
      : GamebryoModDataContent(gameFeatures)
  {
    // Just need to disable some contents:
    m_Enabled[CONTENT_MCM]     = false;
    m_Enabled[CONTENT_SKYPROC] = false;
  }
};

#endif  // OBLIVION_MODDATACONTENT_H
