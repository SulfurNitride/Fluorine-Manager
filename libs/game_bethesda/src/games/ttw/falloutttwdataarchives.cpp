#include "falloutttwdataarchives.h"
#include <utility.h>

QStringList FalloutTTWDataArchives::vanillaArchives() const
{
  return {"Fallout - Textures.bsa", "Fallout - Textures2.bsa", "Fallout - Meshes.bsa",
          "Fallout - Voices1.bsa",  "Fallout - Sound.bsa",     "Fallout - Misc.bsa"};
}

QStringList FalloutTTWDataArchives::archives(const MOBase::IProfile* profile) const
{
  QStringList result;

  QString iniFile = profile->localSettingsEnabled()
                        ? QDir(profile->absolutePath()).absoluteFilePath("fallout.ini")
                        : localGameDirectory().absoluteFilePath("fallout.ini");
  result.append(getArchivesFromKey(iniFile, "SArchiveList"));

  return result;
}

void FalloutTTWDataArchives::writeArchiveList(MOBase::IProfile* profile,
                                              const QStringList& before)
{
  QString list = before.join(", ");

  QString iniFile = profile->localSettingsEnabled()
                        ? QDir(profile->absolutePath()).absoluteFilePath("fallout.ini")
                        : localGameDirectory().absoluteFilePath("fallout.ini");
  setArchivesToKey(iniFile, "SArchiveList", list);
}
