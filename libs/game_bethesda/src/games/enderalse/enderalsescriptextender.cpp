#include "enderalsescriptextender.h"

#include <QString>
#include <QStringList>

EnderalSEScriptExtender::EnderalSEScriptExtender(GameGamebryo const* game)
    : GamebryoScriptExtender(game)
{}

QString EnderalSEScriptExtender::BinaryName() const
{
  return "skse64_loader.exe";
}

QString EnderalSEScriptExtender::PluginPath() const
{
  return "skse/plugins";
}
