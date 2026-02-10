#include "fallout76dataarchives.h"

#include "iprofile.h"
#include <utility.h>

#include <QRegularExpression>

QStringList Fallout76DataArchives::vanillaArchives() const
{
  return {"SeventySix - Animations.ba2",
          "SeventySix - ATX_Main.ba2",
          "SeventySix - ATX_Textures.ba2",
          "SeventySix - EnlightenExteriors01.ba2",
          "SeventySix - EnlightenExteriors02.ba2",
          "SeventySix - EnlightenInteriors.ba2",
          "SeventySix - GeneratedMeshes.ba2",
          "SeventySix - GeneratedTextures.ba2",
          "SeventySix - Interface.ba2",
          "SeventySix - Localization.ba2",
          "SeventySix - Materials.ba2",
          "SeventySix - Meshes01.ba2",
          "SeventySix - Meshes02.ba2",
          "SeventySix - MeshesExtra.ba2",
          "SeventySix - MiscClient.ba2",
          "SeventySix - Shaders.ba2",
          "SeventySix - Sounds01.ba2",
          "SeventySix - Sounds02.ba2",
          "SeventySix - Startup.ba2",
          "SeventySix - Textures01.ba2",
          "SeventySix - Textures02.ba2",
          "SeventySix - Textures03.ba2",
          "SeventySix - Textures04.ba2",
          "SeventySix - Textures05.ba2",
          "SeventySix - Textures06.ba2",
          "SeventySix - Voices.ba2"};
}

QStringList Fallout76DataArchives::sResourceIndexFileList() const
{
  return {"SeventySix - Textures01.ba2", "SeventySix - Textures02.ba2",
          "SeventySix - Textures03.ba2", "SeventySix - Textures04.ba2",
          "SeventySix - Textures05.ba2", "SeventySix - Textures06.ba2"};
}

QStringList Fallout76DataArchives::sResourceStartUpArchiveList() const
{
  return {"SeventySix - Interface.ba2", "SeventySix - Localization.ba2",
          "SeventySix - Shaders.ba2", "SeventySix - Startup.ba2"};
}

QStringList Fallout76DataArchives::SResourceArchiveMemoryCacheList() const
{
  return {"SeventySix - Interface.ba2", "SeventySix - Materials.ba2",
          "SeventySix - MiscClient.ba2", "SeventySix - Shaders.ba2"};
}

QStringList Fallout76DataArchives::SResourceArchiveList() const
{
  return {"SeventySix - GeneratedMeshes.ba2", "SeventySix - Materials.ba2",
          "SeventySix - Meshes01.ba2",        "SeventySix - Meshes02.ba2",
          "SeventySix - MeshesExtra.ba2",     "SeventySix - MiscClient.ba2",
          "SeventySix - Sounds01.ba2",        "SeventySix - Sounds02.ba2",
          "SeventySix - Startup.ba2",         "SeventySix - Voices.ba2"};
}

QStringList Fallout76DataArchives::SResourceArchiveList2() const
{
  return {"SeventySix - Animations.ba2", "SeventySix - EnlightenInteriors.ba2",
          "SeventySix - GeneratedTextures.ba2", "SeventySix - EnlightenExteriors01.ba2",
          "SeventySix - EnlightenExteriors02.ba2"};
}

QStringList Fallout76DataArchives::sResourceArchive2List() const
{
  return {"SeventySix - ATX_Main.ba2", "SeventySix - ATX_Textures.ba2"};
}

QStringList Fallout76DataArchives::archives(const MOBase::IProfile* profile) const
{
  QStringList result;

  QString iniFile =
      profile->localSettingsEnabled()
          ? QDir(profile->absolutePath()).absoluteFilePath("Fallout76.ini")
          : localGameDirectory().absoluteFilePath("Fallout76.ini");

  result.append(getArchivesFromKey(iniFile, "sResourceIndexFileList"));
  result.append(getArchivesFromKey(iniFile, "sResourceStartUpArchiveList"));
  result.append(getArchivesFromKey(iniFile, "SResourceArchiveMemoryCacheList"));
  result.append(getArchivesFromKey(iniFile, "SResourceArchiveList"));
  result.append(getArchivesFromKey(iniFile, "SResourceArchiveList2"));
  result.append(getArchivesFromKey(iniFile, "sResourceArchive2List"));

  return result;
}

void Fallout76DataArchives::writeArchiveList(MOBase::IProfile* profile,
                                             const QStringList& before)
{
  QString iniFile =
      profile->localSettingsEnabled()
          ? QDir(profile->absolutePath()).absoluteFilePath("Fallout76.ini")
          : localGameDirectory().absoluteFilePath("Fallout76.ini");

  QStringList sResourceIndexFileList          = {};
  QStringList sResourceStartUpArchiveList     = {};
  QStringList SResourceArchiveMemoryCacheList = {};
  QStringList SResourceArchiveList            = {};
  QStringList SResourceArchiveList2           = {};
  QStringList sResourceArchive2List           = {};

  for (int i = 0; i < before.size(); ++i) {
    QString archive = before[i];
    if (archive.contains(QRegularExpression(" - Textures(\\d{2})\\.ba2$"))) {
      sResourceIndexFileList.append(archive);
    } else if (archive.contains(QRegularExpression(
                   " - (Interface|Localization|Shaders|Startup)\\.ba2$"))) {
      sResourceStartUpArchiveList.append(archive);
    } else if (archive.contains(QRegularExpression(
                   " - (Interface|Materials|MiscClient|Shaders)\\.ba2$"))) {
      SResourceArchiveMemoryCacheList.append(archive);
    } else if (archive.contains(QRegularExpression(
                   " - "
                   "(GeneratedMeshes|Materials|Meshes(\\d{2}|\\w+)?|MiscClient|"
                   "Sounds\\d{2}|Startup|Voices)\\.ba2$"))) {
      SResourceArchiveList.append(archive);
    } else if (archive.contains(
                   QRegularExpression(" - "
                                      "(Animations|Enlighten(Interiors|Exteriors\\d{2})"
                                      "|GeneratedTextures)\\.ba2$"))) {
      SResourceArchiveList2.append(archive);
    } else if (archive.contains(QRegularExpression(" - ATX_.*\\.ba2$"))) {
      // if it is named after DLC, it has to go here
      sResourceArchive2List.append(archive);
    } else {
      // if it did not fit any description above, it gets tacked on at the end
      sResourceArchive2List.append(archive);
    }
  }

  setArchivesToKey(iniFile, "sResourceIndexFileList",
                   sResourceIndexFileList.join(", "));
  setArchivesToKey(iniFile, "sResourceStartUpArchiveList",
                   sResourceStartUpArchiveList.join(", "));
  setArchivesToKey(iniFile, "SResourceArchiveMemoryCacheList",
                   SResourceArchiveMemoryCacheList.join(", "));
  setArchivesToKey(iniFile, "SResourceArchiveList", SResourceArchiveList.join(", "));
  setArchivesToKey(iniFile, "SResourceArchiveList2", SResourceArchiveList2.join(", "));
  setArchivesToKey(iniFile, "sResourceArchive2List", sResourceArchive2List.join(", "));
}
