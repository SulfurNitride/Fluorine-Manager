#include <uibase/imoinfo.h>

namespace MOBase
{

static QString g_pluginDataPath;

QString IOrganizer::getPluginDataPath()
{
  return g_pluginDataPath;
}

}  // namespace MOBase

namespace MOBase::details
{

void setPluginDataPath(const QString& s)
{
  g_pluginDataPath = s;
}

}  // namespace MOBase::details
