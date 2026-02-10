#include "falloutttwscriptextender.h"

#include <QString>
#include <QStringList>

FalloutTTWScriptExtender::FalloutTTWScriptExtender(GameGamebryo const* game)
    : GamebryoScriptExtender(game)
{}

QString FalloutTTWScriptExtender::BinaryName() const
{
  return "nvse_loader.exe";
}

QString FalloutTTWScriptExtender::PluginPath() const
{
  return "nvse/plugins";
}
