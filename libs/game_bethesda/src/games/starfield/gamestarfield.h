#ifndef GAMESTARFIELD_H
#define GAMESTARFIELD_H

#include "gamegamebryo.h"
#include "iplugindiagnose.h"

#include <QObject>
#include <QtGlobal>

class GameStarfield : public GameGamebryo, public MOBase::IPluginDiagnose
{
  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginGame MOBase::IPluginDiagnose)
  Q_PLUGIN_METADATA(IID "org.modorganizer.GameStarfield" FILE "gamestarfield.json")

public:
  GameStarfield();

  virtual bool init(MOBase::IOrganizer* moInfo) override;

public:
  QStringList testFilePlugins() const;

public:  // IPluginGame interface
  virtual QString gameName() const override;
  virtual void detectGame() override;
  virtual QDir dataDirectory() const override;
  virtual QMap<QString, QDir> secondaryDataDirectories() const override;
  virtual QList<MOBase::ExecutableInfo> executables() const override;
  virtual QList<MOBase::ExecutableForcedLoadSetting>
  executableForcedLoads() const override;
  virtual void initializeProfile(const QDir& path,
                                 ProfileSettings settings) const override;
  virtual QString steamAPPId() const override;
  virtual QStringList primaryPlugins() const override;
  virtual QStringList enabledPlugins() const override;
  virtual QStringList gameVariants() const override;
  virtual QString gameShortName() const override;
  virtual QString gameNexusName() const override;
  virtual QStringList iniFiles() const override;
  virtual bool prepareIni(const QString& exec) override;
  virtual QStringList DLCPlugins() const override;
  virtual QStringList CCPlugins() const override;
  virtual SortMechanism sortMechanism() const override;
  virtual LoadOrderMechanism loadOrderMechanism() const override;
  virtual int nexusModOrganizerID() const override;
  virtual int nexusGameID() const override;

public:  // IPluginDiagnose interface
  virtual std::vector<unsigned int> activeProblems() const override;
  virtual QString shortDescription(unsigned int key) const override;
  virtual QString fullDescription(unsigned int key) const override;
  virtual bool hasGuidedFix(unsigned int key) const override;
  virtual void startGuidedFix(unsigned int key) const override;

public:  // IPlugin interface
  virtual QString name() const override;
  virtual QString localizedName() const override;
  virtual QString author() const override;
  virtual QString description() const override;
  virtual MOBase::VersionInfo version() const override;
  virtual QList<MOBase::PluginSetting> settings() const override;
  virtual MappingType mappings() const override;

protected:
  virtual QString identifyGamePath() const override;
  std::shared_ptr<const GamebryoSaveGame> makeSaveGame(QString filePath) const override;
  QString savegameExtension() const override;
  QString savegameSEExtension() const override;

private:
  QStringList CCCPlugins() const;
  bool activeESP() const;
  bool testFilePresent() const;

private:
  static const unsigned int PROBLEM_ESP       = 1;
  static const unsigned int PROBLEM_TEST_FILE = 2;

  mutable std::set<QString> m_Active_ESPs;
};

#endif  // GAMEStarfield_H
