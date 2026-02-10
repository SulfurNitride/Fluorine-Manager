#ifndef MORROWINDGAMEPLUGINS_H
#define MORROWINDGAMEPLUGINS_H

#include <gamebryogameplugins.h>

class MorrowindGamePlugins : public GamebryoGamePlugins
{

public:
  MorrowindGamePlugins(MOBase::IOrganizer* organizer);

  virtual void writePluginLists(const MOBase::IPluginList* pluginList) override;
  virtual void readPluginLists(MOBase::IPluginList* pluginList) override;

protected:
  virtual void writePluginList(const MOBase::IPluginList* pluginList,
                               const QString& filePath) override;
  virtual QStringList readPluginList(MOBase::IPluginList* pluginList) override;

private:
  virtual void writeList(const MOBase::IPluginList* pluginList, const QString& filePath,
                         bool loadOrder);

private:
  QDateTime m_LastRead;
};

#endif  // MORROWINDGAMEPLUGINS_H
