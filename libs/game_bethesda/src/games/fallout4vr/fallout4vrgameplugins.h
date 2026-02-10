#ifndef _FALLOUT4VRGAMEPLUGINS_H
#define _FALLOUT4VRGAMEPLUGINS_H

#include <creationgameplugins.h>

#include <QObject>
#include <QtGlobal>

class Fallout4VRGamePlugins : public CreationGamePlugins
{

public:
  Fallout4VRGamePlugins(MOBase::IOrganizer* organizer);

protected:
  virtual bool lightPluginsAreSupported() override;
};

#endif  // _FALLOUT4VRGAMEPLUGINS_H
