#include "nehrimdataarchives.h"
#include <utility.h>

QStringList NehrimDataArchives::vanillaArchives() const
{
  return {"N - Meshes.bsa", "N - Textures1.bsa", "N - Textures2.bsa", "N - Misc.bsa",
          "N - Sounds.bsa", "L - Voices.bsa",    "L - Misc.bsa"};
}

QStringList NehrimDataArchives::archives(const MOBase::IProfile* profile) const
{
  QStringList result;

  QString iniFile = profile->localSettingsEnabled()
                        ? QDir(profile->absolutePath()).absoluteFilePath("oblivion.ini")
                        : localGameDirectory().absoluteFilePath("oblivion.ini");
  result.append(getArchivesFromKey(iniFile, "SArchiveList"));

  return result;
}

void NehrimDataArchives::writeArchiveList(MOBase::IProfile* profile,
                                          const QStringList& before)
{
  QString list = before.join(", ");

  QString iniFile = profile->localSettingsEnabled()
                        ? QDir(profile->absolutePath()).absoluteFilePath("oblivion.ini")
                        : localGameDirectory().absoluteFilePath("oblivion.ini");
  setArchivesToKey(iniFile, "SArchiveList", list);
}
