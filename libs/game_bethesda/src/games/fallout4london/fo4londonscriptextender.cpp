#include "fo4londonscriptextender.h"

#include <QString>
#include <QStringList>

Fallout4LondonScriptExtender::Fallout4LondonScriptExtender(GameGamebryo const* game)
    : GamebryoScriptExtender(game)
{}

QString Fallout4LondonScriptExtender::BinaryName() const
{
  return "f4se_loader.exe";
}

QString Fallout4LondonScriptExtender::PluginPath() const
{
  return "f4se/plugins";
}
