#ifndef UIBASE_GAMEFEATURES_LOCALSAVEGAMES_H
#define UIBASE_GAMEFEATURES_LOCALSAVEGAMES_H

#include <QDir>

#include "../filemapping.h"
#include "./game_feature.h"

namespace MOBase
{
class IProfile;

class LocalSavegames : public details::GameFeatureCRTP<LocalSavegames>
{
public:
  virtual MappingType mappings(const QDir& profileSaveDir) const = 0;
  virtual bool prepareProfile(MOBase::IProfile* profile)         = 0;
};
}  // namespace MOBase

#endif  // LOCALSAVEGAMES_H
