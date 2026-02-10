#include "starfieldscriptextender.h"

#include <QString>
#include <QStringList>

StarfieldScriptExtender::StarfieldScriptExtender(GameGamebryo const* game)
    : GamebryoScriptExtender(game)
{}

QString StarfieldScriptExtender::BinaryName() const
{
  return "sfse_loader.exe";
}

QString StarfieldScriptExtender::PluginPath() const
{
  return "sfse/plugins";
}
