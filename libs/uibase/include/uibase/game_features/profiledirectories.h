#ifndef UIBASE_GAMEFEATURES_PROFILEDIRECTORIES_H
#define UIBASE_GAMEFEATURES_PROFILEDIRECTORIES_H

#include <QDir>
#include <QList>

#include "./game_feature.h"

namespace MOBase
{

/**
 * @brief Game feature describing host directories that should be captured per profile.
 *
 * Games that register this feature provide a list of absolute host directories (e.g.
 * a Wine prefix's `%LOCALAPPDATA%`). The core relinks each directory's children into
 * `<profile>/Local/<leaf>`, seeding pre-existing contents on first use, so that any file
 * created under those roots — including save games — is automatically isolated per
 * profile. No carve-out or exemption logic is applied.
 */
class ProfileDirectories : public details::GameFeatureCRTP<ProfileDirectories>
{
public:
  virtual QList<QDir> directories() const = 0;
};

}  // namespace MOBase

#endif  // UIBASE_GAMEFEATURES_PROFILEDIRECTORIES_H
