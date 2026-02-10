#ifndef GAMENEHRIM_H
#define GAMENEHRIM_H

#include "gamegamebryo.h"

#include <QObject>
#include <QtGlobal>

class GameNehrim : public GameGamebryo
{
  Q_OBJECT
  Q_PLUGIN_METADATA(IID "org.tannin.GameNehrim" FILE "gamenehrim.json")

public:
  GameNehrim();

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
  virtual QString gameNexusName() const override;
  virtual QStringList iniFiles() const override;
  virtual QStringList DLCPlugins() const override;
  virtual int nexusModOrganizerID() const override;
  virtual int nexusGameID() const override;
  virtual QStringList primarySources() const override;
  virtual QStringList validShortNames() const override;

  // Weird stuff happens in these functions due to Nehrim
  // technically being in the Oblivion folder
  virtual QString identifyGamePath() const override;
  virtual QString binaryName() const override;

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
};

#endif  // GAMENEHRIM_H
