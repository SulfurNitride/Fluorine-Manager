#include "skyrimdataarchives.h"
#include <utility.h>

QStringList SkyrimDataArchives::vanillaArchives() const
{
  return {"Skyrim - Misc.bsa",        "Skyrim - Shaders.bsa",
          "Skyrim - Textures.bsa",    "HighResTexturePack01.bsa",
          "HighResTexturePack02.bsa", "HighResTexturePack03.bsa",
          "Skyrim - Interface.bsa",   "Skyrim - Animations.bsa",
          "Skyrim - Meshes.bsa",      "Skyrim - Sounds.bsa",
          "Skyrim - Voices.bsa",      "Skyrim - VoicesExtra.bsa"};
}

QStringList SkyrimDataArchives::archives(const MOBase::IProfile* profile) const
{
  QStringList result;

  QString iniFile = profile->localSettingsEnabled()
                        ? QDir(profile->absolutePath()).absoluteFilePath("skyrim.ini")
                        : localGameDirectory().absoluteFilePath("skyrim.ini");
  result.append(getArchivesFromKey(iniFile, "SResourceArchiveList"));
  result.append(getArchivesFromKey(iniFile, "SResourceArchiveList2"));

  return result;
}

void SkyrimDataArchives::writeArchiveList(MOBase::IProfile* profile,
                                          const QStringList& before)
{
  QString list = before.join(", ");

  QString iniFile = profile->localSettingsEnabled()
                        ? QDir(profile->absolutePath()).absoluteFilePath("skyrim.ini")
                        : localGameDirectory().absoluteFilePath("skyrim.ini");
  if (list.length() > 255) {
    int splitIdx = list.lastIndexOf(",", 256);
    setArchivesToKey(iniFile, "SResourceArchiveList", list.mid(0, splitIdx));
    setArchivesToKey(iniFile, "SResourceArchiveList2", list.mid(splitIdx + 2));
  } else {
    setArchivesToKey(iniFile, "SResourceArchiveList", list);
  }
}
