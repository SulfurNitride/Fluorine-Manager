#ifndef _ENDERALGAMEPLUGINS_H
#define _ENDERALGAMEPLUGINS_H

#include <gamebryogameplugins.h>
#include <imoinfo.h>
#include <iplugingame.h>
#include <map>

class EnderalGamePlugins : public GamebryoGamePlugins
{
public:
  EnderalGamePlugins(MOBase::IOrganizer* organizer);

  virtual void readPluginLists(MOBase::IPluginList* pluginList) override;

protected:
  virtual QStringList readPluginList(MOBase::IPluginList* pluginList) override;

private:
  std::map<QString, QByteArray> m_LastSaveHash;
};

#endif  // _ENDERALSEGAMEPLUGINS_H
