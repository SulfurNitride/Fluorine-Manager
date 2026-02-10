#include "fallout4vrscriptextender.h"

#include <QString>
#include <QStringList>

Fallout4VRScriptExtender::Fallout4VRScriptExtender(GameGamebryo const* game)
    : GamebryoScriptExtender(game)
{}

QString Fallout4VRScriptExtender::BinaryName() const
{
  return "f4se_loader.exe";
}

QString Fallout4VRScriptExtender::PluginPath() const
{
  return "f4se/plugins";
}
