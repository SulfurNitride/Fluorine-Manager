#ifndef GAMEFALLOUT4VR_H
#define GAMEFALLOUT4VR_H

#include "gamegamebryo.h"

#include <QObject>
#include <QtGlobal>

class GameFallout4VR : public GameGamebryo
{
  Q_OBJECT

  Q_PLUGIN_METADATA(IID "org.tannin.GameFallout4VR" FILE "gamefallout4vr.json")

public:
  GameFallout4VR();

  virtual bool init(MOBase::IOrganizer* moInfo) override;

public:  // IPluginGame interface
  virtual QString gameName() const override;
  virtual void detectGame() override;
  virtual QList<MOBase::ExecutableInfo> executables() const override;
  virtual QList<MOBase::ExecutableForcedLoadSetting>
  executableForcedLoads() const override;
  virtual void initializeProfile(const QDir& path,
                                 ProfileSettings settings) const override;
  virtual QString steamAPPId() const override;
  virtual QStringList primaryPlugins() const override;
  virtual QStringList gameVariants() const override;
  virtual QString gameShortName() const override;
  virtual QStringList primarySources() const override { return validShortNames(); };
  virtual QStringList validShortNames() const override;
  virtual QString gameNexusName() const override;
  virtual QStringList iniFiles() const override;
  virtual QStringList DLCPlugins() const override;
  virtual QStringList CCPlugins() const override;
  virtual LoadOrderMechanism loadOrderMechanism() const override;
  virtual int nexusModOrganizerID() const override;
  virtual int nexusGameID() const override;
  virtual QString getLauncherName() const override;

public:  // IPlugin interface
  virtual QString name() const override;
  virtual QString localizedName() const override;
  virtual QString author() const override;
  virtual QString description() const override;
  virtual MOBase::VersionInfo version() const override;
  virtual QList<MOBase::PluginSetting> settings() const override;

protected:
  std::shared_ptr<const GamebryoSaveGame> makeSaveGame(QString filePath) const override;
  QString savegameExtension() const override;
  QString savegameSEExtension() const override;

  virtual QString identifyGamePath() const override;
};

#endif  // GAMEFallout4VR_H
