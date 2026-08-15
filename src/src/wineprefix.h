#ifndef WINEPREFIX_H
#define WINEPREFIX_H

#include <QList>
#include <QString>
#include <QStringList>

#include "wineprofileinisync.h"
#include "wineregistryfile.h"

class WinePrefix
{
public:
  // Mirrors MOBase::IPluginGame::LoadOrderMechanism without pulling that
  // header into wineprefix.  Callers translate from the game feature.
  enum class PluginListMechanism
  {
    None,        // no plugin list file in AppData
    FileTime,    // lowercase "plugins.txt" (enabled-only), order via mtime
    PluginsTxt,  // "Plugins.txt" with '*' prefix for enabled
                 // (SSE/AE/FO4/Starfield)
  };

  explicit WinePrefix(const QString& prefixPath,
                      const QString& userProfilePath = {});

  bool isValid() const;  // drive_c/ exists
  QString driveC() const;
  QString documentsPath() const;    // drive_c/users/steamuser/Documents
  QString myGamesPath() const;      // .../Documents/My Games
  QString appdataLocal() const;     // .../AppData/Local
  QString userProfilePath() const;  // drive_c/users/steamuser

  bool deployProfileIni(const QString& sourceIniPath, const QString& targetIniPath,
                        const QString& ownerId,
                        WineProfileIniSync::Deployment& deployment) const;
  bool deployProfileSaves(const QString& profileSaveDir, const QString& absoluteSaveDir,
                          bool clearDestination, const QString& ownerId,
                          bool* cleanupRequired = nullptr) const;
  bool prepareProfileSavesBindTarget(const QString& profileSaveDir,
                                     const QString& absoluteSaveDir,
                                     const QString& ownerId,
                                     bool* cleanupRequired = nullptr) const;

  // Sync saves back from prefix to profile.  Mirrors deletions: profile
  // files absent from the prefix are removed so in-game save deletions
  // propagate.
  bool syncSavesBack(const QString& profileSaveDir, const QString& absoluteSaveDir,
                     const QString& ownerId, bool* topologyComplete = nullptr,
                     bool* cleanupRequired = nullptr) const;
  bool rollbackProfileSaves(const QString& profileSaveDir,
                            const QString& absoluteSaveDir, const QString& ownerId,
                            bool* topologyComplete = nullptr,
                            bool* cleanupRequired  = nullptr) const;

  bool syncProfileInisBack(QList<WineProfileIniSync::Deployment>& deployments,
                           const QString& ownerId, bool publishChanges,
                           WineProfileIniSync::CleanupPhase& phase) const;

  // Restore any stale .mo2linux_backup INI/save files left by a crash.
  // Should be called at startup before any game runs.
  void restoreStaleBackups() const;

  // Wine registry (system.reg / user.reg) access.
  // subKey uses Wine format: "Software\\\\Bethesda Softworks\\\\Skyrim Special
  // Edition" (double-escaped backslashes as stored in .reg files). Convenience
  // overload accepts normal backslash paths and escapes internally.
  QString readRegistryValue(const QString& regFile, const QString& subKey,
                            const QString& valueName) const;
  bool writeRegistryValue(const QString& regFile, const QString& subKey,
                          const QString& valueName, const QString& value) const;

  // High-level: read/write HKLM values via system.reg
  QString readHklmValue(const QString& subKey, const QString& valueName) const;
  bool writeHklmValue(const QString& subKey, const QString& valueName,
                      const QString& value) const;

  bool readHklmValues(QList<WineRegistryFile::Query>& queries,
                      QString* error = nullptr) const;
  bool writeHklmValues(const QList<WineRegistryFile::Update>& updates,
                       QString* error = nullptr,
                       bool prefixLeaseHeld = false) const;
  bool pruneExtraDrives(QStringList& removed, QString* error = nullptr,
                        bool prefixLeaseHeld = false) const;

private:
  QString m_prefixPath;
  QString m_userProfilePath;
};

#endif  // WINEPREFIX_H
