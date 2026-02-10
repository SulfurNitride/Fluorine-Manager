#include "fallout4vrgameplugins.h"

using namespace MOBase;

Fallout4VRGamePlugins::Fallout4VRGamePlugins(MOBase::IOrganizer* organizer)
    : CreationGamePlugins(organizer)
{}

bool Fallout4VRGamePlugins::lightPluginsAreSupported()
{
  auto files = m_Organizer->findFiles("f4se\\plugins", {"falloutvresl.dll"});
  if (files.isEmpty())
    return false;
  return true;
}
