#include "falloutnvscriptextender.h"

#include <QString>
#include <QStringList>

FalloutNVScriptExtender::FalloutNVScriptExtender(GameGamebryo const* game)
    : GamebryoScriptExtender(game)
{}

QString FalloutNVScriptExtender::BinaryName() const
{
  return "nvse_loader.exe";
}

QString FalloutNVScriptExtender::PluginPath() const
{
  return "nvse/plugins";
}
