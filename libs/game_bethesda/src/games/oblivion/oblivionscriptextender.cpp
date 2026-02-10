#include "oblivionscriptextender.h"

#include <QString>
#include <QStringList>

OblivionScriptExtender::OblivionScriptExtender(GameGamebryo const* game)
    : GamebryoScriptExtender(game)
{}

OblivionScriptExtender::~OblivionScriptExtender() {}

QString OblivionScriptExtender::BinaryName() const
{
  return "obse_loader.exe";
}

QString OblivionScriptExtender::PluginPath() const
{
  return "obse/plugins";
}
