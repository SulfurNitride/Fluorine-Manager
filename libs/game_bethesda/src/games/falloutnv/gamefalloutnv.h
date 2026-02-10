#ifndef GAMEFALLOUTNV_H
#define GAMEFALLOUTNV_H

#include "gamegamebryo.h"

#include <QObject>
#include <QtGlobal>

class GameFalloutNV : public GameGamebryo
{
  Q_OBJECT
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
  Q_PLUGIN_METADATA(IID "org.tannin.GameFalloutNV" FILE "gamefalloutnv.json")
#endif

public:
  GameFalloutNV();

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
  virtual QStringList validShortNames() const override;
  virtual QString gameNexusName() const override;
  virtual QStringList iniFiles() const override;
  virtual QStringList DLCPlugins() const override;
  virtual SortMechanism sortMechanism() const override;
  virtual int nexusModOrganizerID() const override;
  virtual int nexusGameID() const override;

  virtual bool isInstalled() const override;
  virtual void setGamePath(const QString& path) override;
  virtual QDir gameDirectory() const override;

public:  // IPlugin interface
  virtual QString name() const override;
  virtual QString localizedName() const override;
  virtual QString author() const override;
  virtual QString description() const override;
  virtual MOBase::VersionInfo version() const override;
  virtual QList<MOBase::PluginSetting> settings() const override;
  virtual MappingType mappings() const override;

protected:
  QString gameDirectoryName() const;
  QDir documentsDirectory() const;
  QDir savesDirectory() const;
  QString myGamesPath() const;

  void setVariant(QString variant);
  void checkVariants();

protected:
  virtual QString identifyGamePath() const override;
  virtual QString savegameExtension() const override;
  virtual QString savegameSEExtension() const override;
  std::shared_ptr<const GamebryoSaveGame> makeSaveGame(QString filePath) const override;
};

#endif  // GAMEFALLOUTNV_H
