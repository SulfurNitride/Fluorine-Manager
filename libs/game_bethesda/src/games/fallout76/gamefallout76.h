#ifndef GAMEFALLOUT76_H
#define GAMEFALLOUT76_H

#include "gamegamebryo.h"

#include <QObject>
#include <QtGlobal>

class GameFallout76 : public GameGamebryo
{
  Q_OBJECT

  Q_PLUGIN_METADATA(IID "in.ejew.GameFallout76" FILE "gamefallout76.json")

public:
  GameFallout76();

  virtual bool init(MOBase::IOrganizer* moInfo) override;

public:  // IPluginGame interface
  QString gameName() const override;
  void detectGame() override;
  QList<MOBase::ExecutableInfo> executables() const override;
  QList<MOBase::ExecutableForcedLoadSetting> executableForcedLoads() const override;
  void initializeProfile(const QDir& path, ProfileSettings settings) const override;
  QString steamAPPId() const override;
  QStringList primaryPlugins() const override;
  QStringList gameVariants() const override;
  QString gameShortName() const override;
  QString gameNexusName() const override;
  QStringList iniFiles() const override;
  QStringList DLCPlugins() const override;
  QStringList CCPlugins() const override;
  SortMechanism sortMechanism() const override;
  LoadOrderMechanism loadOrderMechanism() const override;
  int nexusModOrganizerID() const override;
  int nexusGameID() const override;
  std::vector<std::shared_ptr<const MOBase::ISaveGame>>
  listSaves(QDir folder) const override;

public:  // IPlugin interface
  QString name() const override;
  QString author() const override;
  QString description() const override;
  MOBase::VersionInfo version() const override;
  QList<MOBase::PluginSetting> settings() const override;

protected:
  QString identifyGamePath() const override;
  std::shared_ptr<const GamebryoSaveGame> makeSaveGame(QString) const override;
  QString savegameExtension() const override;
  QString savegameSEExtension() const override;
};

#endif  // GAMEFallout76_H
