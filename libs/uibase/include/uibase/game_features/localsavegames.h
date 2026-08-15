#ifndef UIBASE_GAMEFEATURES_LOCALSAVEGAMES_H
#define UIBASE_GAMEFEATURES_LOCALSAVEGAMES_H

#include <QDir>
#include <QByteArray>
#include <QString>

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

// Optional additive contract for save implementations whose game INI route
// cannot be inferred from IPluginGame::iniFiles() or normalized mappings.
// Kept separate from LocalSavegames so existing plugin vtables remain intact.
class LocalSavegamesRouting
{
public:
  virtual ~LocalSavegamesRouting() = default;
  virtual QString routingIniName() const = 0;
  virtual QByteArray routingPath() const  = 0;
};

// Optional additive contract for save implementations whose authoritative
// destination is a fixed directory under the game installation. This is a
// separate interface so the established LocalSavegames vtable remains intact.
class LocalSavegamesTopology
{
public:
  virtual ~LocalSavegamesTopology() = default;
  virtual bool usesFixedGameDirectory() const = 0;
};
}  // namespace MOBase

#endif  // LOCALSAVEGAMES_H
