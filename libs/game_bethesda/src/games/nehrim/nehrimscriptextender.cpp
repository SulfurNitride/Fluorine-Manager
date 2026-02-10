#include "nehrimscriptextender.h"

#include <QString>
#include <QStringList>

NehrimScriptExtender::NehrimScriptExtender(GameGamebryo const* game)
    : GamebryoScriptExtender(game)
{}

NehrimScriptExtender::~NehrimScriptExtender() {}

QString NehrimScriptExtender::BinaryName() const
{
  return "obse_loader.exe";
}

QString NehrimScriptExtender::PluginPath() const
{
  return "obse/plugins";
}
