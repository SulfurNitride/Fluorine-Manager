#include "gamebryobsainvalidation.h"

#include "dummybsa.h"
#include "iplugingame.h"
#include "iprofile.h"
#include "registry.h"
#include <imoinfo.h>
#include <utility.h>

#include <QDir>
#include <QStringList>

#include <Windows.h>

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
  WCHAR setting[MAX_PATH];

  // write bInvalidateOlderFiles = 1, if needed
  if (!::GetPrivateProfileStringW(L"Archive", L"bInvalidateOlderFiles", L"0", setting,
                                  MAX_PATH, iniFilePath.toStdWString().c_str()) ||
      wcstol(setting, nullptr, 10) != 1) {
    dirty = true;
    if (!MOBase::WriteRegistryValue(L"Archive", L"bInvalidateOlderFiles", L"1",
                                    iniFilePath.toStdWString().c_str())) {
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
    if (::GetPrivateProfileStringW(L"Archive", L"SInvalidationFile",
                                   L"ArchiveInvalidation.txt", setting, MAX_PATH,
                                   iniFilePath.toStdWString().c_str()) ||
        wcscmp(setting, L"") != 0) {
      dirty = true;
      if (!MOBase::WriteRegistryValue(L"Archive", L"SInvalidationFile", L"",
                                      iniFilePath.toStdWString().c_str())) {
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
    if (!::GetPrivateProfileStringW(L"Archive", L"SInvalidationFile", L"", setting,
                                    MAX_PATH, iniFilePath.toStdWString().c_str()) ||
        wcscmp(setting, L"ArchiveInvalidation.txt") != 0) {
      dirty = true;
      if (!MOBase::WriteRegistryValue(L"Archive", L"SInvalidationFile",
                                      L"ArchiveInvalidation.txt",
                                      iniFilePath.toStdWString().c_str())) {
        qWarning("failed to activate BSA invalidation in \"%s\"",
                 qUtf8Printable(m_IniFileName));
      }
    }
  }

  return dirty;
}
