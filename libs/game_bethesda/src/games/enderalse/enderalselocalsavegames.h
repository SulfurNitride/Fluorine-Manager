#ifndef ENDERALSELOCALSAVEGAMES_H
#define ENDERALSELOCALSAVEGAMES_H

#include <gamebryolocalsavegames.h>

#include <QString>

class EnderalSELocalSavegames : public GamebryoLocalSavegames
{
public:
  using GamebryoLocalSavegames::GamebryoLocalSavegames;

protected:
  QString localSavesDummy() const override;
};

#endif  // ENDERALSELOCALSAVEGAMES_H
