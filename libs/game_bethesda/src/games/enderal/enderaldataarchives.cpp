#include "enderaldataarchives.h"
#include <utility.h>

QStringList EnderalDataArchives::vanillaArchives() const
{
  return {"Skyrim - Misc.bsa",      "Skyrim - Shaders.bsa",    "Skyrim - Textures.bsa",
          "Skyrim - Interface.bsa", "Skyrim - Animations.bsa", "Skyrim - Meshes.bsa",
          "Skyrim - Sounds.bsa",    "E - Meshes.bsa",          "E - Music.bsa",
          "E - Scripts.bsa",        "E - Sounds.bsa",          "E - Textures1.bsa",
          "E - Textures2.bsa",      "E - Textures3.bsa",       "L - Textures.bsa",
          "L - Voices.bsa"};
}

QStringList EnderalDataArchives::archives(const MOBase::IProfile* profile) const
{
  QStringList result;

  QString iniFile = profile->localSettingsEnabled()
                        ? QDir(profile->absolutePath()).absoluteFilePath("enderal.ini")
                        : localGameDirectory().absoluteFilePath("enderal.ini");
  result.append(getArchivesFromKey(iniFile, "SResourceArchiveList"));
  result.append(getArchivesFromKey(iniFile, "SResourceArchiveList2"));

  return result;
}

void EnderalDataArchives::writeArchiveList(MOBase::IProfile* profile,
                                           const QStringList& before)
{
  QString list = before.join(", ");

  QString iniFile = profile->localSettingsEnabled()
                        ? QDir(profile->absolutePath()).absoluteFilePath("enderal.ini")
                        : localGameDirectory().absoluteFilePath("enderal.ini");
  if (list.length() > 255) {
    int splitIdx = list.lastIndexOf(",", 256);
    setArchivesToKey(iniFile, "SResourceArchiveList", list.mid(0, splitIdx));
    setArchivesToKey(iniFile, "SResourceArchiveList2", list.mid(splitIdx + 2));
  } else {
    setArchivesToKey(iniFile, "SResourceArchiveList", list);
  }
}
