#include "obliviondataarchives.h"
#include <utility.h>

QStringList OblivionDataArchives::vanillaArchives() const
{
  return {"Oblivion - Misc.bsa",    "Oblivion - Textures - Compressed.bsa",
          "Oblivion - Meshes.bsa",  "Oblivion - Sounds.bsa",
          "Oblivion - Voices1.bsa", "Oblivion - Voices2.bsa"};
}

QStringList OblivionDataArchives::archives(const MOBase::IProfile* profile) const
{
  QStringList result;

  QString iniFile = profile->localSettingsEnabled()
                        ? QDir(profile->absolutePath()).absoluteFilePath("oblivion.ini")
                        : localGameDirectory().absoluteFilePath("oblivion.ini");
  result.append(getArchivesFromKey(iniFile, "SArchiveList"));

  return result;
}

void OblivionDataArchives::writeArchiveList(MOBase::IProfile* profile,
                                            const QStringList& before)
{
  QString list = before.join(", ");

  QString iniFile = profile->localSettingsEnabled()
                        ? QDir(profile->absolutePath()).absoluteFilePath("oblivion.ini")
                        : localGameDirectory().absoluteFilePath("oblivion.ini");
  setArchivesToKey(iniFile, "SArchiveList", list);
}
