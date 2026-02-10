#include "fallout76scriptextender.h"

#include <QString>
#include <QStringList>

Fallout76ScriptExtender::Fallout76ScriptExtender(GameGamebryo const* game)
    : GamebryoScriptExtender(game)
{}

QString Fallout76ScriptExtender::BinaryName() const
{
  return "f76se_loader.exe";
}

QString Fallout76ScriptExtender::PluginPath() const
{
  return "f76se/plugins";
}
