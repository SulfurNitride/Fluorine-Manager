#include "fallout3dataarchives.h"

#include "iprofile.h"
#include <utility.h>

QStringList Fallout3DataArchives::vanillaArchives() const
{
  return {"Fallout - Textures.bsa", "Fallout - Meshes.bsa",     "Fallout - Voices.bsa",
          "Fallout - Sound.bsa",    "Fallout - MenuVoices.bsa", "Fallout - Misc.bsa"};
}

QStringList Fallout3DataArchives::archives(const MOBase::IProfile* profile) const
{
  QStringList result;

  QString iniFile = profile->localSettingsEnabled()
                        ? QDir(profile->absolutePath()).absoluteFilePath("fallout.ini")
                        : localGameDirectory().absoluteFilePath("fallout.ini");
  result.append(getArchivesFromKey(iniFile, "SArchiveList"));

  return result;
}

void Fallout3DataArchives::writeArchiveList(MOBase::IProfile* profile,
                                            const QStringList& before)
{
  QString list = before.join(", ");

  QString iniFile = profile->localSettingsEnabled()
                        ? QDir(profile->absolutePath()).absoluteFilePath("fallout.ini")
                        : localGameDirectory().absoluteFilePath("fallout.ini");
  setArchivesToKey(iniFile, "SArchiveList", list);
}
