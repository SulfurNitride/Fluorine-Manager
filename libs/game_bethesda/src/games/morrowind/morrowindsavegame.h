#ifndef MORROWINDSAVEGAME_H
#define MORROWINDSAVEGAME_H

#include "gamebryosavegame.h"
#include "gamemorrowind.h"

namespace MOBase
{
class IPluginGame;
}

class MorrowindSaveGame : public GamebryoSaveGame
{
public:
  MorrowindSaveGame(QString const& fileName, GameMorrowind const* game);

public:  // ISaveGame interface
  // We need to override getName() because we do not read the level at
  // the beginning.
  virtual QString getName() const override;

  // The PC level is not pre-fetch for morrowind.
  unsigned short getPCLevel() const override;

public:
  // Simple getters
  QString getSaveName() const { return m_SaveName; }
  float getPCCurrentHealth() const { return m_PCCurrentHealth; }
  float getPCMaxHealth() const { return m_PCCMaxHealth; }
  float getGameDays() const { return m_GameDays; }

protected:
  QString m_SaveName;
  float m_PCCurrentHealth;
  float m_PCCMaxHealth;
  float m_GameDays;

protected:
  QImage readImageBGRA(GamebryoSaveGame::FileWrapper& file, unsigned long width,
                       unsigned long height, int scale, bool alpha) const;

  // We need to add the PC level here.
  struct MorrowindDataFields : public DataFields
  {
    unsigned short PCLevel = 0;
  };

  // Fetch easy-to-access information.
  void fetchInformationFields(FileWrapper& file, QString& saveName,
                              QStringList& plugins, float& playerCurrentHealth,
                              float& playerMaxHealth, QString& playerLocation,
                              float& gameDays, QString& playerName) const;

  std::unique_ptr<DataFields> fetchDataFields() const override;
};

#endif  // MORROWINDSAVEGAME_H
