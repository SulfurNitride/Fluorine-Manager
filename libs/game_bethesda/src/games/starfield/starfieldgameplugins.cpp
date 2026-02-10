#include "starfieldgameplugins.h"

using namespace MOBase;

StarfieldGamePlugins::StarfieldGamePlugins(MOBase::IOrganizer* organizer)
    : CreationGamePlugins(organizer)
{}

bool StarfieldGamePlugins::mediumPluginsAreSupported()
{
  return true;
}

bool StarfieldGamePlugins::blueprintPluginsAreSupported()
{
  return true;
}

void StarfieldGamePlugins::writePluginList(const IPluginList* pluginList,
                                           const QString& filePath)
{
  if (m_Organizer->managedGame()->loadOrderMechanism() ==
      MOBase::IPluginGame::LoadOrderMechanism::PluginsTxt) {
    CreationGamePlugins::writePluginList(pluginList, filePath);
  }
}

QStringList StarfieldGamePlugins::readPluginList(MOBase::IPluginList* pluginList)
{
  if (m_Organizer->managedGame()->loadOrderMechanism() ==
      MOBase::IPluginGame::LoadOrderMechanism::PluginsTxt) {
    return CreationGamePlugins::readPluginList(pluginList);
  }
  return {};
}
