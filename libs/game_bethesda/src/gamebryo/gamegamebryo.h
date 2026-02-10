#ifndef GAMEGAMEBRYO_H
#define GAMEGAMEBRYO_H

#include "iplugingame.h"

class BSAInvalidation;
class DataArchives;
class LocalSavegames;
class SaveGameInfo;
class BSAInvalidation;
class LocalSavegames;
class ScriptExtender;
class GamePlugins;
class UnmanagedMods;

#include <QObject>
#include <QString>
#ifdef _WIN32
#include <ShlObj.h>
#include <dbghelp.h>
#endif
#include <ipluginfilemapper.h>
#include <iplugingame.h>
#include <memory>

#include "gamebryosavegame.h"
#include "igamefeatures.h"

class GameGamebryo : public MOBase::IPluginGame, public MOBase::IPluginFileMapper
{
  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginGame MOBase::IPluginFileMapper)

  friend class GamebryoScriptExtender;
  friend class GamebryoSaveGameInfo;
  friend class GamebryoSaveGameInfoWidget;
  friend class GamebryoSaveGame;

  /**
   * Some Bethesda games do not have a valid file version but a valid product
   * version. If the file version starts with FALLBACK_GAME_VERSION, the product
   * version will be tried.
   */
  static constexpr const char* FALLBACK_GAME_VERSION = "1.0.0";

public:
  GameGamebryo();

  void detectGame() override;
  bool init(MOBase::IOrganizer* moInfo) override;

public:  // IPluginGame interface
  // getName
  // initializeProfile
  virtual std::vector<std::shared_ptr<const MOBase::ISaveGame>>
  listSaves(QDir folder) const override;

  virtual bool isInstalled() const override;
  virtual QIcon gameIcon() const override;
  virtual QDir gameDirectory() const override;
  virtual QDir dataDirectory() const override;
  // secondaryDataDirectories
  virtual void setGamePath(const QString& path) override;
  virtual QDir documentsDirectory() const override;
  virtual QDir savesDirectory() const override;
  // executables
  // steamAPPId
  // primaryPlugins
  // enabledPlugins
  // gameVariants
  virtual void setGameVariant(const QString& variant) override;
  virtual QString binaryName() const override;
  // gameShortName
  // primarySources
  // validShortNames
  // iniFiles
  // DLCPlugins
  // CCPlugins
  virtual LoadOrderMechanism loadOrderMechanism() const override;
  virtual SortMechanism sortMechanism() const override;
  // nexusModOrganizerID
  // nexusGameID
  virtual bool looksValid(QDir const&) const override;
  virtual QString gameVersion() const override;
  virtual QString getLauncherName() const override;

public:  // IPluginFileMapper interface
  virtual MappingType mappings() const;

public:  // Other (e.g. for game features)
  QString myGamesPath() const;

protected:
  // Retrieve the saves extension for the game.
  virtual QString savegameExtension() const   = 0;
  virtual QString savegameSEExtension() const = 0;

  // Create a save game.
  virtual std::shared_ptr<const GamebryoSaveGame>
  makeSaveGame(QString filepath) const = 0;

  QFileInfo findInGameFolder(const QString& relativePath) const;
  QString selectedVariant() const;

  static QString localAppFolder();
  // Arguably this shouldn't really be here but every gamebryo program seems to
  // use it
  static QString getLootPath();

  // This function is not terribly well named as it copies exactly where it's told
  // to, irrespective of whether it's in the profile...
  static void copyToProfile(const QString& sourcePath, const QDir& destinationDirectory,
                            const QString& sourceFileName);

  static void copyToProfile(const QString& sourcePath, const QDir& destinationDirectory,
                            const QString& sourceFileName,
                            const QString& destinationFileName);

  virtual QString identifyGamePath() const;

  virtual bool prepareIni(const QString& exec);

#ifdef _WIN32
  static std::unique_ptr<BYTE[]> getRegValue(HKEY key, LPCWSTR path, LPCWSTR value,
                                             DWORD flags, LPDWORD type);

  static QString findInRegistry(HKEY baseKey, LPCWSTR path, LPCWSTR value);

  static QString getKnownFolderPath(REFKNOWNFOLDERID folderId, bool useDefault);

  static QString getSpecialPath(const QString& name);
#endif

  WORD getArch(QString const& program) const;

  static QString determineMyGamesPath(const QString& gameName);

  static QString parseEpicGamesLocation(const QStringList& manifests);

  static QString parseSteamLocation(const QString& appid, const QString& directoryName);

public:  // Cross-platform INI file utilities (used by game features)
  // Read a value from a Bethesda-style INI file using QSettings
  static QString readIniValue(const QString& iniFile, const QString& section,
                              const QString& key, const QString& defaultValue = QString());

protected:
  void registerFeature(std::shared_ptr<MOBase::GameFeature> feature);

protected:
  // to access organizer for game features, avoid having to pass it to all saves since
  // we already pass the game
  friend class GamebryoSaveGame;

  QString m_GamePath;
  QString m_MyGamesPath;
  QString m_GameVariant;
  MOBase::IOrganizer* m_Organizer;
};

#endif  // GAMEGAMEBRYO_H
