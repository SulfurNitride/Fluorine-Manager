#include "fo4londonbsainvalidation.h"

#include "dummybsa.h"
#include "iplugingame.h"
#include "iprofile.h"
#include "registry.h"
#include <imoinfo.h>
#include <utility.h>

#include "gamegamebryo.h"

Fallout4LondonBSAInvalidation::Fallout4LondonBSAInvalidation(
    MOBase::DataArchives* dataArchives, MOBase::IPluginGame const* game)
    : GamebryoBSAInvalidation(dataArchives, "Fallout4Custom.ini", game)
{
  m_IniFileName = "Fallout4Custom.ini";
  m_Game        = game;
}

bool Fallout4LondonBSAInvalidation::isInvalidationBSA(const QString& bsaName)
{
  return false;
}

QString Fallout4LondonBSAInvalidation::invalidationBSAName() const
{
  return "";
}

unsigned long Fallout4LondonBSAInvalidation::bsaVersion() const
{
  return 0x68;
}

bool Fallout4LondonBSAInvalidation::prepareProfile(MOBase::IProfile* profile)
{
  bool dirty          = false;
  QString basePath    = profile->localSettingsEnabled()
                            ? profile->absolutePath()
                            : m_Game->documentsDirectory().absolutePath();
  QString iniFilePath = basePath + "/" + m_IniFileName;

  if (profile->invalidationActive(nullptr)) {
    // write bInvalidateOlderFiles = 1, if needed
    QString bInvalidateOlderFiles = GameGamebryo::readIniValue(
        iniFilePath, "Archive", "bInvalidateOlderFiles", "0");
    if (bInvalidateOlderFiles.toLong() != 1) {
      dirty = true;
      if (!MOBase::WriteRegistryValue("Archive", "bInvalidateOlderFiles", "1",
                                      iniFilePath)) {
        qWarning("failed to override data directory in \"%s\"",
                 qUtf8Printable(m_IniFileName));
      }
    }
    QString sResourceDataDirsFinal = GameGamebryo::readIniValue(
        iniFilePath, "Archive", "sResourceDataDirsFinal", "STRINGS\\");
    if (sResourceDataDirsFinal != "") {
      dirty = true;
      if (!MOBase::WriteRegistryValue("Archive", "sResourceDataDirsFinal", "",
                                      iniFilePath)) {
        qWarning("failed to override data directory in \"%s\"",
                 qUtf8Printable(m_IniFileName));
      }
    }
  }

  return dirty;
}
