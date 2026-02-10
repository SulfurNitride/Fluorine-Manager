#include "enderalsedataarchives.h"

#include "iprofile.h"
#include <utility.h>

QStringList EnderalSEDataArchives::vanillaArchives() const
{
  return {"Skyrim - Textures0.bsa",
          "Skyrim - Textures1.bsa",
          "Skyrim - Textures2.bsa",
          "Skyrim - Textures3.bsa",
          "Skyrim - Textures4.bsa",
          "Skyrim - Textures5.bsa",
          "Skyrim - Textures6.bsa",
          "Skyrim - Textures7.bsa",
          "Skyrim - Textures8.bsa",
          "Skyrim - Meshes0.bsa",
          "Skyrim - Meshes1.bsa",
          "Skyrim - Voices_en0.bsa",
          "Skyrim - Sounds.bsa",
          "Skyrim - Interface.bsa",
          "Skyrim - Animations.bsa",
          "Skyrim - Shaders.bsa",
          "Skyrim - Misc.bsa",
          "E - Meshes.bsa",
          "E - SE.bsa",
          "E - Scripts.bsa",
          "E - Sounds.bsa",
          "E - Textures1.bsa",
          "E - Textures2.bsa",
          "E - Textures3.bsa",
          "L - Textures.bsa",
          "L - Voices.bsa"};
}

QStringList EnderalSEDataArchives::archives(const MOBase::IProfile* profile) const
{
  QStringList result;

  QString iniFile = profile->localSettingsEnabled()
                        ? QDir(profile->absolutePath()).absoluteFilePath("enderal.ini")
                        : localGameDirectory().absoluteFilePath("enderal.ini");
  result.append(getArchivesFromKey(iniFile, "SResourceArchiveList", 512));
  result.append(getArchivesFromKey(iniFile, "SResourceArchiveList2", 512));

  return result;
}

void EnderalSEDataArchives::writeArchiveList(MOBase::IProfile* profile,
                                             const QStringList& before)
{
  QString list = before.join(",");

  QString iniFile = profile->localSettingsEnabled()
                        ? QDir(profile->absolutePath()).absoluteFilePath("enderal.ini")
                        : localGameDirectory().absoluteFilePath("enderal.ini");
  if (list.length() > 511) {
    int splitIdx = list.lastIndexOf(",", 512);
    setArchivesToKey(iniFile, "SResourceArchiveList", list.mid(0, splitIdx));
    setArchivesToKey(iniFile, "SResourceArchiveList2", list.mid(splitIdx + 2));
  } else {
    setArchivesToKey(iniFile, "SResourceArchiveList", list);
  }
}
