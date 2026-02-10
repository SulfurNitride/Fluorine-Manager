#include "morrowinddataarchives.h"
#include "registry.h"
#include <utility.h>

#include <QSettings>

#include "gamegamebryo.h"

QStringList MorrowindDataArchives::vanillaArchives() const
{
  return {"Morrowind.bsa"};
}

QStringList MorrowindDataArchives::getArchives(const QString& iniFile) const
{
  QStringList result;
  QSettings settings(iniFile, QSettings::IniFormat);

  QString key = "Archive ";
  int i       = 0;
  while (true) {
    QString value =
        settings.value("Archives/" + key + QString::number(i), "").toString().trimmed();
    if (value.isEmpty()) {
      break;
    }
    result.append(value);
    i++;
  }

  return result;
}

void MorrowindDataArchives::setArchives(const QString& iniFile, const QStringList& list)
{
  QSettings settings(iniFile, QSettings::IniFormat);
  settings.remove("Archives");

  QString key      = "Archive ";
  int writtenCount = 0;
  foreach (const QString& value, list) {
    if (!MOBase::WriteRegistryValue(
            "Archives", key + QString::number(writtenCount), value, iniFile)) {
      qWarning("failed to set archives in \"%s\"", qUtf8Printable(iniFile));
    }
    ++writtenCount;
  }
}

QStringList MorrowindDataArchives::archives(const MOBase::IProfile* profile) const
{
  QStringList result;

  QString iniFile =
      profile->localSettingsEnabled()
          ? QDir(profile->absolutePath()).absoluteFilePath("morrowind.ini")
          : gameDirectory().absoluteFilePath("morrowind.ini");
  result.append(getArchives(iniFile));

  return result;
}

void MorrowindDataArchives::writeArchiveList(MOBase::IProfile* profile,
                                             const QStringList& before)
{
  QString iniFile =
      profile->localSettingsEnabled()
          ? QDir(profile->absolutePath()).absoluteFilePath("morrowind.ini")
          : gameDirectory().absoluteFilePath("morrowind.ini");
  setArchives(iniFile, before);
}
