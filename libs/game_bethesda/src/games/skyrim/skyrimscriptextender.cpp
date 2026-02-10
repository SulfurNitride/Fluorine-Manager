#include "skyrimscriptextender.h"

#include <QString>
#include <QStringList>

SkyrimScriptExtender::SkyrimScriptExtender(GameGamebryo const* game)
    : GamebryoScriptExtender(game)
{}

QString SkyrimScriptExtender::BinaryName() const
{
  return "skse_loader.exe";
}

QString SkyrimScriptExtender::PluginPath() const
{
  return "skse/plugins";
}
