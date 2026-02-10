#include "fallout4scriptextender.h"

#include <QString>
#include <QStringList>

Fallout4ScriptExtender::Fallout4ScriptExtender(GameGamebryo const* game)
    : GamebryoScriptExtender(game)
{}

QString Fallout4ScriptExtender::BinaryName() const
{
  return "f4se_loader.exe";
}

QString Fallout4ScriptExtender::PluginPath() const
{
  return "f4se/plugins";
}
