#ifndef MORROWIND_MODDATACONTENT_H
#define MORROWIND_MODDATACONTENT_H

#include <gamebryomoddatacontent.h>
#include <ifiletree.h>

class MorrowindModDataContent : public GamebryoModDataContent
{
public:
  /**
   *
   */
  MorrowindModDataContent(MOBase::IGameFeatures const* gameFeatures)
      : GamebryoModDataContent(gameFeatures)
  {
    // Just need to disable some contents:
    m_Enabled[CONTENT_MCM]       = false;
    m_Enabled[CONTENT_SKYPROC]   = false;
    m_Enabled[CONTENT_INTERFACE] = false;
    m_Enabled[CONTENT_SCRIPT]    = false;
  }
};

#endif  // MORROWIND_MODDATACONTENT_H
