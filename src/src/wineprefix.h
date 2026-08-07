#ifndef WINEPREFIX_H
#define WINEPREFIX_H

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QList>
#include <QPair>

class WinePrefix
{
public:
  // Mirrors MOBase::IPluginGame::LoadOrderMechanism without pulling that
  // header into wineprefix.  Callers translate from the game feature.
  enum class PluginListMechanism {
    None,        // no plugin list file in AppData
    FileTime,    // lowercase "plugins.txt" (enabled-only), order via mtime
    PluginsTxt,  // "Plugins.txt" with '*' prefix for enabled (SSE/AE/FO4/Starfield)
  };

  explicit WinePrefix(const QString& prefixPath);

  bool isValid() const;  // drive_c/ exists
  QString driveC() const;
  QString documentsPath() const;  // drive_c/users/steamuser/Documents
  QString myGamesPath() const;    // .../Documents/My Games
  QString appdataLocal() const;   // .../AppData/Local
  QString userProfilePath() const; // drive_c/users/steamuser

  // Deploy profile files into prefix.  Only the plugin list file the game
  // actually reads is written — loadorder.txt is MO2-internal and never
  // belongs in AppData/Local/<Game>/.
  bool deployPlugins(const QStringList& plugins, const QString& dataDir,
                     PluginListMechanism mechanism) const;
  bool deployProfileIni(const QString& sourceIniPath,
                        const QString& targetIniPath) const;
  bool deployProfileSaves(const QString& profileSaveDir,
                          const QString& absoluteSaveDir,
                          bool clearDestination) const;

  // Sync saves back from prefix to profile.  Mirrors deletions: profile
  // files absent from the prefix are removed so in-game save deletions
  // propagate.
  bool syncSavesBack(const QString& profileSaveDir,
                     const QString& absoluteSaveDir) const;

   /// Remove the __MO_Saves symlink(s) deployed by deployProfileSaves so a
   /// vanilla game launch outside MO2 uses the prefix's default Saves dir.
   /// Only removes entries that are actually symlinks — real directories
   /// from a pre-symlink install are left alone.
   void undeployProfileSaves(const QString& absoluteSaveDir) const;

   /// Relink a host directory into the profile tree so that writes to the host
   /// path are captured per profile.
   ///
   /// - On first use (real directory at hostPath), seeds the profile path by
   ///   copying existing contents, then relinks.
   /// - Symlinks hostPath (and its lowercase alias) to profilePath.
   /// - Idempotent: a prior symlink is just re-pointed.
   /// Returns true on success.
   bool captureNode(const QString& hostPath, const QString& profilePath) const;

    /// Remove symlinks deployed by captureNode for the given host path.  Only
    /// symlinks are removed; real directories from a pre-symlink install are
    /// left alone.  Recursively descends through symlinked subtrees (resolved
    /// via the profile target) so per-child links are cleaned up before the
    /// parent is removed.
   void uncaptureNode(const QString& hostPath) const;

  bool syncProfileInisBack(
      const QList<QPair<QString, QString>>& iniMappings) const;

  // Sync the game's plugin-list file from the prefix AppData back to the
  // profile after a tool like LOOT may have edited it.  Picks the newest
  // case-insensitive variant.  Only the Plugins.txt/plugins.txt file the
  // game reads is synced — loadorder.txt is never written to the prefix
  // and is not read back.
  bool syncPluginsBack(const QString& profilePluginsPath,
                       const QString& dataDir,
                       PluginListMechanism mechanism) const;

  // Last-modified time of the newest plugin-list variant in the prefix.
  // Invalid QDateTime if no variant exists.  Used to decide whether to
  // sync-back (prefix newer, e.g. external LOOT run) before deploying.
  QDateTime prefixPluginsMTime(const QString& dataDir) const;

  // Restore any stale .mo2linux_backup INI/save files left by a crash.
  // Should be called at startup before any game runs.
  void restoreStaleBackups() const;

  // Wine registry (system.reg / user.reg) access.
  // subKey uses Wine format: "Software\\\\Bethesda Softworks\\\\Skyrim Special Edition"
  // (double-escaped backslashes as stored in .reg files).
  // Convenience overload accepts normal backslash paths and escapes internally.
  QString readRegistryValue(const QString& regFile, const QString& subKey,
                            const QString& valueName) const;
  bool writeRegistryValue(const QString& regFile, const QString& subKey,
                          const QString& valueName, const QString& value) const;

  // High-level: read/write HKLM values via system.reg
  QString readHklmValue(const QString& subKey, const QString& valueName) const;
  bool writeHklmValue(const QString& subKey, const QString& valueName,
                      const QString& value) const;

private:
  QString m_prefixPath;
};

#endif  // WINEPREFIX_H
