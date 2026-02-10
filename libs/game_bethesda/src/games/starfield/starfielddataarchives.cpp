#include "starfielddataarchives.h"

#include "iprofile.h"
#include <utility.h>

QStringList StarfieldDataArchives::vanillaArchives() const
{
  return {"Starfield - Animations.ba2",
          "Starfield - DensityMaps.ba2",
          "Starfield - FaceAnimation01.ba2",
          "Starfield - FaceAnimation02.ba2",
          "Starfield - FaceAnimation03.ba2",
          "Starfield - FaceAnimation04.ba2",
          "Starfield - FaceAnimationPatch.ba2",
          "Starfield - FaceMeshes.ba2",
          "Starfield - GeneratedTextures.ba2",
          "Starfield - Interface.ba2",
          "Starfield - Localization.ba2",
          "Starfield - LODMeshes.ba2",
          "Starfield - LODMeshesPatch.ba2",
          "Starfield - LODTextures.ba2",
          "Starfield - Materials.ba2",
          "Starfield - Meshes01.ba2",
          "Starfield - Meshes02.ba2",
          "Starfield - MeshesPatch.ba2",
          "Starfield - Misc.ba2",
          "Starfield - Particles.ba2",
          "Starfield - ParticlesTestData.ba2",
          "Starfield - PlanetData.ba2",
          "Starfield - Shaders.ba2",
          "Starfield - ShadersBeta.ba2",
          "Starfield - Terrain01.ba2",
          "Starfield - Terrain02.ba2",
          "Starfield - Terrain03.ba2",
          "Starfield - Terrain04.ba2",
          "Starfield - TerrainPatch.ba2",
          "Starfield - Textures01.ba2",
          "Starfield - Textures02.ba2",
          "Starfield - Textures03.ba2",
          "Starfield - Textures04.ba2",
          "Starfield - Textures05.ba2",
          "Starfield - Textures06.ba2",
          "Starfield - Textures07.ba2",
          "Starfield - Textures08.ba2",
          "Starfield - Textures09.ba2",
          "Starfield - Textures10.ba2",
          "Starfield - Textures11.ba2",
          "Starfield - TexturesPatch.ba2",
          "Starfield - Voices01.ba2",
          "Starfield - Voices02.ba2",
          "Starfield - VoicesPatch.ba2",
          "Starfield - WwiseSounds01.ba2",
          "Starfield - WwiseSounds02.ba2",
          "Starfield - WwiseSounds03.ba2",
          "Starfield - WwiseSounds04.ba2",
          "Starfield - WwiseSounds05.ba2",
          "Starfield - WwiseSoundsPatch.ba2",
          "Constellation - Localization.ba2",
          "Constellation - Textures.ba2",
          "OldMars - Localization.ba2",
          "OldMars - Textures.ba2",
          "BlueprintShips-Starfield - Localization.ba2"};
}

QStringList StarfieldDataArchives::archives(const MOBase::IProfile* profile) const
{
  QStringList result;

  QString defaultIniFile = gameDirectory().absoluteFilePath("Starfield.ini");
  QString customIniFile =
      profile->localSettingsEnabled()
          ? QDir(profile->absolutePath()).absoluteFilePath("StarfieldCustom.ini")
          : localGameDirectory().absoluteFilePath("StarfieldCustom.ini");
  QStringList archiveSettings = {"SResourceArchiveList", "sResourceIndexFileList",
                                 "SResourceArchiveMemoryCacheList",
                                 "sResourceStartUpArchiveList",
                                 "sResourceEnglishVoiceList"};
  for (auto setting : archiveSettings) {
    auto archives = getArchivesFromKey(customIniFile, setting, 4096);
    if (archives.isEmpty())
      archives = getArchivesFromKey(defaultIniFile, setting, 4096);
    result.append(archives);
  }

  return result;
}

void StarfieldDataArchives::writeArchiveList(MOBase::IProfile* profile,
                                             const QStringList& before)
{}

void StarfieldDataArchives::addArchive(MOBase::IProfile* profile, int index,
                                       const QString& archiveName)
{}

void StarfieldDataArchives::removeArchive(MOBase::IProfile* profile,
                                          const QString& archiveName)
{}
