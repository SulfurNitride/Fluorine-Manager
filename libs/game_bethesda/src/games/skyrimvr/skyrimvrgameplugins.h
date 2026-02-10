#ifndef _SKYRIMVRGAMEPLUGINS_H
#define _SKYRIMVRGAMEPLUGINS_H

#include <creationgameplugins.h>

#include <QObject>
#include <QtGlobal>

class SkyrimVRGamePlugins : public CreationGamePlugins
{

public:
  SkyrimVRGamePlugins(MOBase::IOrganizer* organizer);

protected:
  virtual bool lightPluginsAreSupported() override;
};

#endif  // _SKYRIMVRGAMEPLUGINS_H
