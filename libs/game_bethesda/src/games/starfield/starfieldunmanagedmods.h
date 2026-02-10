#ifndef STARFIELDUNMANAGEDMODS_H
#define STARFIELDUNMANAGEDMODS_H

#include "gamebryounmanagedmods.h"
#include "gamestarfield.h"
#include <gamegamebryo.h>

#include <QJsonObject>

class StarfieldUnmanagedMods : public GamebryoUnmangedMods
{
  friend class GameStarfield;

  struct ContentCatalog
  {
    QString name;
    QStringList files;
  };

public:
  StarfieldUnmanagedMods(const GameStarfield* game, const QString& appDataFolder);
  ~StarfieldUnmanagedMods();

  virtual QStringList mods(bool onlyOfficial) const override;
  virtual QFileInfo referenceFile(const QString& modName) const override;
  virtual QStringList secondaryFiles(const QString& modName) const override;
  virtual QString displayName(const QString& modName) const override;

private:
  std::map<QString, ContentCatalog> parseContentCatalog() const;

private:
  QString m_AppDataFolder;
};

#endif  // STARFIELDUNMANAGEDMODS_H
