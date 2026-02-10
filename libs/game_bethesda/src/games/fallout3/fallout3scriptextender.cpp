#include "fallout3scriptextender.h"

#include <QString>
#include <QStringList>

Fallout3ScriptExtender::Fallout3ScriptExtender(GameGamebryo const* game)
    : GamebryoScriptExtender(game)
{}

QString Fallout3ScriptExtender::BinaryName() const
{
  return "fose_loader.exe";
}

QString Fallout3ScriptExtender::PluginPath() const
{
  return "fose/plugins";
}
