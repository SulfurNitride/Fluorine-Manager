#ifndef WINEPREFIX_H
#define WINEPREFIX_H

#include <QString>
#include <QStringList>
#include <QList>
#include <QPair>

class WinePrefix
{
public:
  explicit WinePrefix(const QString& prefixPath);

  bool isValid() const;  // drive_c/ exists
  QString driveC() const;
  QString documentsPath() const;  // drive_c/users/steamuser/Documents
  QString myGamesPath() const;    // .../Documents/My Games
  QString appdataLocal() const;   // .../AppData/Local

  // Deploy profile files into prefix
  bool deployPlugins(const QStringList& plugins, const QString& dataDir) const;
  bool deployProfileIni(const QString& sourceIniPath,
                        const QString& targetIniPath) const;
  bool deployProfileSaves(const QString& profileSaveDir, const QString& gameName,
                          const QString& saveRelativePath,
                          bool clearDestination) const;

  // Sync saves back from prefix to profile
  bool syncSavesBack(const QString& profileSaveDir, const QString& gameName,
                     const QString& saveRelativePath) const;
  bool syncProfileInisBack(
      const QList<QPair<QString, QString>>& iniMappings) const;

private:
  QString m_prefixPath;
};

#endif  // WINEPREFIX_H
