#ifndef _STARFIELDGAMEPLUGINS_H
#define _STARFIELDGAMEPLUGINS_H

#include <creationgameplugins.h>

#include <QObject>
#include <QtGlobal>

class StarfieldGamePlugins : public CreationGamePlugins
{

public:
  StarfieldGamePlugins(MOBase::IOrganizer* organizer);

protected:
  virtual bool mediumPluginsAreSupported() override;
  virtual bool blueprintPluginsAreSupported() override;
  virtual void writePluginList(const MOBase::IPluginList* pluginList,
                               const QString& filePath) override;
  virtual QStringList readPluginList(MOBase::IPluginList* pluginList) override;
};

#endif  // _STARFIELDGAMEPLUGINS_H
