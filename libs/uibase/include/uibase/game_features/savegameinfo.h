#ifndef UIBASE_GAMEFEATURES_SAVEGAMEINFO_H
#define UIBASE_GAMEFEATURES_SAVEGAMEINFO_H

#include <QMap>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "./game_feature.h"

namespace MOBase
{
class ISaveGame;
class ISaveGameInfoWidget;

/** Feature to get hold of stuff to do with save games */
class SaveGameInfo : public details::GameFeatureCRTP<SaveGameInfo>
{
public:
  virtual ~SaveGameInfo() {}

  typedef QStringList ProvidingModules;
  typedef QMap<QString, ProvidingModules> MissingAssets;

  /**
   * @brief Get missing items from a save.
   *
   * @param save The save to retrieve missing assets from.
   *
   * @returns a collection of missing assets and the modules that can supply those
   * assets.
   *
   * Note that in the situation where 'module' and 'asset' are indistinguishable,
   * both still have to be supplied.
   */
  virtual MissingAssets getMissingAssets(MOBase::ISaveGame const& save) const = 0;

  /**
   * @brief Get a widget to display over the save game list.
   *
   * @param parent The parent widget.
   *
   * @returns a Qt widget to display saves Widget.
   *
   * It is permitted to return a null pointer to indicate the plugin does not have a
   * nice visual way of displaying save game contents.
   */
  virtual MOBase::ISaveGameInfoWidget* getSaveGameWidget(QWidget* parent = 0) const = 0;
};
}  // namespace MOBase

#endif  // SAVEGAMEINFO_H
