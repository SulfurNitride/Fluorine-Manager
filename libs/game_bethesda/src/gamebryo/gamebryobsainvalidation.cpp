#include "gamebryobsainvalidation.h"

#include "dummybsa.h"
#include "iplugingame.h"
#include "iprofile.h"
#include "registry.h"
#include <imoinfo.h>
#include <utility.h>

#include <QDir>
#include <QStringList>

#include "gamegamebryo.h"

GamebryoBSAInvalidation::GamebryoBSAInvalidation(MOBase::DataArchives* dataArchives,
                                                 const QString& iniFilename,
                                                 MOBase::IPluginGame const* game)
    : m_DataArchives(dataArchives), m_IniFileName(iniFilename), m_Game(game)
{}

bool GamebryoBSAInvalidation::isInvalidationBSA(const QString& bsaName)
{
  static QStringList invalidation{invalidationBSAName()};

  for (const QString& file : invalidation) {
    if (file.compare(bsaName, Qt::CaseInsensitive) == 0) {
      return true;
    }
  }
  return false;
}

void GamebryoBSAInvalidation::deactivate(MOBase::IProfile* profile)
{
  prepareProfile(profile);
}

void GamebryoBSAInvalidation::activate(MOBase::IProfile* profile)
{
  prepareProfile(profile);
}

bool GamebryoBSAInvalidation::prepareProfile(MOBase::IProfile* profile)
{
  bool dirty          = false;
  QString basePath    = profile->localSettingsEnabled()
                            ? profile->absolutePath()
                            : m_Game->documentsDirectory().absolutePath();
  QString iniFilePath = basePath + "/" + m_IniFileName;

  // write bInvalidateOlderFiles = 1, if needed
  QString setting =
      GameGamebryo::readIniValue(iniFilePath, "Archive", "bInvalidateOlderFiles", "0");
  if (setting.toLong() != 1) {
    dirty = true;
    if (!MOBase::WriteRegistryValue("Archive", "bInvalidateOlderFiles", "1",
                                    iniFilePath)) {
      qWarning("failed to activate BSA invalidation in \"%s\"",
               qUtf8Printable(m_IniFileName));
    }
  }

  if (profile->invalidationActive(nullptr)) {

    // add the dummy bsa to the archive string, if needed
    QStringList archives = m_DataArchives->archives(profile);
    bool bsaInstalled    = false;
    for (const QString& archive : archives) {
      if (isInvalidationBSA(archive)) {
        bsaInstalled = true;
        break;
      }
    }
    if (!bsaInstalled) {
      m_DataArchives->addArchive(profile, 0, invalidationBSAName());
      dirty = true;
    }

    // create the dummy bsa if necessary
    QString bsaFile = m_Game->dataDirectory().absoluteFilePath(invalidationBSAName());
    if (!QFile::exists(bsaFile)) {
      DummyBSA bsa(bsaVersion());
      bsa.write(bsaFile);
      dirty = true;
    }

    // write SInvalidationFile = "", if needed
    QString sInvalidation = GameGamebryo::readIniValue(
        iniFilePath, "Archive", "SInvalidationFile", "ArchiveInvalidation.txt");
    if (sInvalidation != "") {
      dirty = true;
      if (!MOBase::WriteRegistryValue("Archive", "SInvalidationFile", "", iniFilePath)) {
        qWarning("failed to activate BSA invalidation in \"%s\"",
                 qUtf8Printable(m_IniFileName));
      }
    }
  } else {

    // remove the dummy bsa from the archive string, if needed
    QStringList archivesBefore = m_DataArchives->archives(profile);
    for (const QString& archive : archivesBefore) {
      if (isInvalidationBSA(archive)) {
        m_DataArchives->removeArchive(profile, archive);
        dirty = true;
      }
    }

    // delete the dummy bsa, if needed
    QString bsaFile = m_Game->dataDirectory().absoluteFilePath(invalidationBSAName());
    if (QFile::exists(bsaFile)) {
      MOBase::shellDeleteQuiet(bsaFile);
      dirty = true;
    }

    // write SInvalidationFile = "ArchiveInvalidation.txt", if needed
    QString sInvalidation2 =
        GameGamebryo::readIniValue(iniFilePath, "Archive", "SInvalidationFile", "");
    if (sInvalidation2 != "ArchiveInvalidation.txt") {
      dirty = true;
      if (!MOBase::WriteRegistryValue("Archive", "SInvalidationFile",
                                      "ArchiveInvalidation.txt", iniFilePath)) {
        qWarning("failed to activate BSA invalidation in \"%s\"",
                 qUtf8Printable(m_IniFileName));
      }
    }
  }

  return dirty;
}
