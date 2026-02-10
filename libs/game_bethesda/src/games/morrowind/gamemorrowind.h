#ifndef GAMEMORROWIND_H
#define GAMEMORROWIND_H

#include "gamegamebryo.h"

#include <QObject>
#include <QtGlobal>

class GameMorrowind : public GameGamebryo
{
  Q_OBJECT
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
  Q_PLUGIN_METADATA(IID "com.schilduin.GameMorrowind" FILE "gamemorrowind.json")
#endif

  friend class MorrowindSaveGameInfo;
  friend class MorrowindSaveGameInfoWidget;

public:
  GameMorrowind();

  virtual bool init(MOBase::IOrganizer* moInfo) override;

public:  // IPluginGame interface
  virtual QString gameName() const override;
  virtual QString getLauncherName() const override;
  virtual QDir dataDirectory() const override;
  virtual QDir savesDirectory() const override;
  virtual QDir documentsDirectory() const override;
  virtual QList<MOBase::ExecutableInfo> executables() const override;
  virtual QList<MOBase::ExecutableForcedLoadSetting>
  executableForcedLoads() const override;
  virtual void initializeProfile(const QDir& path,
                                 ProfileSettings settings) const override;
  virtual QString steamAPPId() const override;
  virtual QStringList primaryPlugins() const override;
  virtual QString binaryName() const override;
  virtual QString gameShortName() const override;
  virtual QString gameNexusName() const override;
  virtual QStringList iniFiles() const override;
  virtual QStringList DLCPlugins() const override;
  virtual SortMechanism sortMechanism() const override;
  virtual int nexusModOrganizerID() const override;
  virtual int nexusGameID() const override;
  virtual QString identifyGamePath() const;

public:  // IPlugin interface
  virtual QString name() const override;
  virtual QString localizedName() const override;
  virtual QString author() const override;
  virtual QString description() const override;
  virtual MOBase::VersionInfo version() const override;
  virtual QList<MOBase::PluginSetting> settings() const override;

protected:
  virtual QString savegameExtension() const override;
  virtual QString savegameSEExtension() const override;
  virtual std::shared_ptr<const GamebryoSaveGame>
  makeSaveGame(QString filepath) const override;
};

#endif  // GAMEMORROWIND_H
