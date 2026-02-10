#include "enderalscriptextender.h"

#include <QString>
#include <QStringList>

EnderalScriptExtender::EnderalScriptExtender(GameGamebryo const* game)
    : GamebryoScriptExtender(game)
{}

QString EnderalScriptExtender::BinaryName() const
{
  return "skse_loader.exe";
}

QString EnderalScriptExtender::PluginPath() const
{
  return "skse/plugins";
}
