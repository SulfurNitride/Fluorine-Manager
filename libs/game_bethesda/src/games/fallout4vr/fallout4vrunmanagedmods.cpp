#include "fallout4vrunmanagedmods.h"

Fallout4VRUnmangedMods::Fallout4VRUnmangedMods(const GameGamebryo* game)
    : GamebryoUnmangedMods(game)
{}

Fallout4VRUnmangedMods::~Fallout4VRUnmangedMods() {}

QStringList Fallout4VRUnmangedMods::mods(bool onlyOfficial) const
{
  QStringList result;

  QStringList pluginList   = game()->primaryPlugins();
  QStringList otherPlugins = game()->DLCPlugins();
  otherPlugins.append(game()->CCPlugins());
  for (QString plugin : otherPlugins) {
    pluginList.removeAll(plugin);
  }
  QDir dataDir(game()->dataDirectory());
  for (const QString& fileName : dataDir.entryList({"*.esp", "*.esl", "*.esm"})) {
    if (!pluginList.contains(fileName, Qt::CaseInsensitive)) {
      if (!onlyOfficial || pluginList.contains(fileName, Qt::CaseInsensitive)) {
        result.append(fileName.chopped(4));  // trims the extension off
      }
    }
  }

  return result;
}

QStringList Fallout4VRUnmangedMods::secondaryFiles(const QString& modName) const
{
  // file extension in FO4 is .ba2 instead of bsa
  QStringList archives;
  QDir dataDir = game()->dataDirectory();
  for (const QString& archiveName : dataDir.entryList({modName + "*.ba2"})) {
    archives.append(dataDir.absoluteFilePath(archiveName));
  }
  return archives;
}

QString Fallout4VRUnmangedMods::displayName(const QString& modName) const
{
  // unlike in earlier games, in fallout 4 the file name doesn't correspond to
  // the public name
  if (modName.compare("dlcrobot", Qt::CaseInsensitive) == 0) {
    return "Automatron";
  } else if (modName.compare("dlcworkshop01", Qt::CaseInsensitive) == 0) {
    return "Wasteland Workshop";
  } else if (modName.compare("dlccoast", Qt::CaseInsensitive) == 0) {
    return "Far Harbor";
  } else if (modName.compare("dlcworkshop02", Qt::CaseInsensitive) == 0) {
    return "Contraptions Workshop";
  } else if (modName.compare("dlcworkshop03", Qt::CaseInsensitive) == 0) {
    return "Vault-Tec Workshop";
  } else if (modName.compare("dlcnukaworld", Qt::CaseInsensitive) == 0) {
    return "Nuka-World";
  } else if (modName.compare("dlcultrahighresolution", Qt::CaseInsensitive) == 0) {
    return "Ultra High Resolution Texture Pack";
  } else {
    return modName;
  }
}
