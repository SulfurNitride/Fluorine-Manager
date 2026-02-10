#ifndef GAMEFALLOUT3_H
#define GAMEFALLOUT3_H

#include "gamegamebryo.h"

#include <QObject>
#include <QtGlobal>

class GameFallout3 : public GameGamebryo
{
  Q_OBJECT
  Q_PLUGIN_METADATA(IID "org.tannin.GameFallout3" FILE "gamefallout3.json")

public:
  GameFallout3();

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
  virtual QStringList gameVariants() const;
  QString binaryName() const override;
  virtual QString gameShortName() const override;
  virtual QStringList validShortNames() const override;
  virtual QString gameNexusName() const override;
  virtual QString getLauncherName() const override;
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
  virtual QString identifyGamePath() const override;
  virtual QString savegameExtension() const override;
  virtual QString savegameSEExtension() const override;
  std::shared_ptr<const GamebryoSaveGame> makeSaveGame(QString filePath) const override;

private:
  QString identifyVariant() const;
};

#endif  // GAMEFALLOUT3_H
