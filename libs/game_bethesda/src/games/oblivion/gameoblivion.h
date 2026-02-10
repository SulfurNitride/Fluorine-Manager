#ifndef GAMEOBLIVION_H
#define GAMEOBLIVION_H

#include "gamegamebryo.h"

#include <QObject>
#include <QtGlobal>

class GameOblivion : public GameGamebryo
{
  Q_OBJECT
  Q_PLUGIN_METADATA(IID "org.tannin.GameOblivion" FILE "gameoblivion.json")

public:
  GameOblivion();

  virtual bool init(MOBase::IOrganizer* moInfo) override;

public:  // IPluginGame interface
  virtual QString gameName() const override;
  virtual QList<MOBase::ExecutableInfo> executables() const override;
  virtual QList<MOBase::ExecutableForcedLoadSetting>
  executableForcedLoads() const override;
  virtual void initializeProfile(const QDir& path,
                                 ProfileSettings settings) const override;
  virtual QString steamAPPId() const override;
  virtual QStringList primaryPlugins() const override;
  virtual QString gameShortName() const override;
  virtual QStringList validShortNames() const override;
  virtual QString gameNexusName() const override;
  virtual QStringList iniFiles() const override;
  virtual QStringList DLCPlugins() const override;
  virtual int nexusModOrganizerID() const override;
  virtual int nexusGameID() const override;

public:  // IPlugin interface
  virtual QString name() const override;
  virtual QString localizedName() const override;
  virtual QString author() const override;
  virtual QString description() const override;
  virtual MOBase::VersionInfo version() const override;
  virtual QList<MOBase::PluginSetting> settings() const override;

protected:
  QString identifyGamePath() const override;
  std::shared_ptr<const GamebryoSaveGame> makeSaveGame(QString filePath) const override;
  QString savegameExtension() const override;
  QString savegameSEExtension() const override;
};

#endif  // GAMEOBLIVION_H
