#ifndef UIBASE_GAMEFEATURES_BSAINVALIDATION_H
#define UIBASE_GAMEFEATURES_BSAINVALIDATION_H

#include <QString>

#include "./game_feature.h"

namespace MOBase
{
class IProfile;

class BSAInvalidation : public details::GameFeatureCRTP<BSAInvalidation>
{
public:
  virtual bool isInvalidationBSA(const QString& bsaName) = 0;

  virtual void deactivate(MOBase::IProfile* profile) = 0;

  virtual void activate(MOBase::IProfile* profile) = 0;

  virtual bool prepareProfile(MOBase::IProfile* profile) = 0;
};

}  // namespace MOBase

#endif  // BSAINVALIDATION_H
