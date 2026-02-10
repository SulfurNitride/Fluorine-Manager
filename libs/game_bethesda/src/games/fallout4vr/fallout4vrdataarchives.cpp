#include "fallout4vrdataarchives.h"

#include "iprofile.h"
#include <utility.h>

QStringList Fallout4VRDataArchives::vanillaArchives() const
{
  return {"Fallout4 - Textures1.ba2",   "Fallout4 - Textures2.ba2",
          "Fallout4 - Textures3.ba2",   "Fallout4 - Textures4.ba2",
          "Fallout4 - Textures5.ba2",   "Fallout4 - Textures6.ba2",
          "Fallout4 - Textures7.ba2",   "Fallout4 - Textures8.ba2",
          "Fallout4 - Textures9.ba2",   "Fallout4 - Meshes.ba2",
          "Fallout4 - MeshesExtra.ba2", "Fallout4 - Voices.ba2",
          "Fallout4 - Sounds.ba2",      "Fallout4 - Interface.ba2",
          "Fallout4 - Animations.ba2",  "Fallout4 - Materials.ba2",
          "Fallout4 - Shaders.ba2",     "Fallout4 - Startup.ba2",
          "Fallout4 - Misc.ba2",        "Fallout4_VR - Main.ba2",
          "Fallout4_VR - Shaders.ba2",  "Fallout4_VR - Textures.ba2"};
}

QStringList Fallout4VRDataArchives::archives(const MOBase::IProfile* profile) const
{
  QStringList result;

  QString iniFile = profile->localSettingsEnabled()
                        ? QDir(profile->absolutePath()).absoluteFilePath("fallout4.ini")
                        : localGameDirectory().absoluteFilePath("fallout4.ini");
  result.append(getArchivesFromKey(iniFile, "SResourceArchiveList"));
  result.append(getArchivesFromKey(iniFile, "SResourceArchiveList2"));

  return result;
}

void Fallout4VRDataArchives::writeArchiveList(MOBase::IProfile* profile,
                                              const QStringList& before)
{
  QString list = before.join(", ");

  QString iniFile = profile->localSettingsEnabled()
                        ? QDir(profile->absolutePath()).absoluteFilePath("fallout4.ini")
                        : localGameDirectory().absoluteFilePath("fallout4.ini");
  if (list.length() > 255) {
    int splitIdx = list.lastIndexOf(",", 256);
    setArchivesToKey(iniFile, "SResourceArchiveList", list.mid(0, splitIdx));
    setArchivesToKey(iniFile, "SResourceArchiveList2", list.mid(splitIdx + 2));
  } else {
    setArchivesToKey(iniFile, "SResourceArchiveList", list);
  }
}
