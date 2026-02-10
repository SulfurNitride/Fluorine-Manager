#include "skyrimsescriptextender.h"

#include <QString>
#include <QStringList>

SkyrimSEScriptExtender::SkyrimSEScriptExtender(GameGamebryo const* game)
    : GamebryoScriptExtender(game)
{}

QString SkyrimSEScriptExtender::BinaryName() const
{
  return "skse64_loader.exe";
}

QString SkyrimSEScriptExtender::PluginPath() const
{
  return "skse/plugins";
}
