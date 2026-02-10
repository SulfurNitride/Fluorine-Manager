#include "bsaextractor.h"

#include <uibase/iplugingame.h>
#include <uibase/imodlist.h>
#include <uibase/versioninfo.h>
#include <uibase/imodinterface.h>
#include <uibase/questionboxmemory.h>
#include <uibase/report.h>

#include <bsatk/bsaarchive.h>

#include <QDir>
#include <QFileInfoList>
#include <QCoreApplication>
#include <QMessageBox>
#include <QtPlugin>

#include <functional>

using namespace MOBase;


BsaExtractor::BsaExtractor()
  : m_Organizer(nullptr)
{
}

bool BsaExtractor::init(MOBase::IOrganizer *moInfo)
{
  m_Organizer = moInfo;
  moInfo->modList()->onModInstalled([this](auto* mod) { modInstalledHandler(mod); });
  return true;
}

QString BsaExtractor::name() const
{
  return "BSA Extractor";
}

QString BsaExtractor::localizedName() const
{
  return tr("BSA Extractor");
}


QString BsaExtractor::author() const
{
  return "Tannin & MO2 Team";
}

QString BsaExtractor::description() const
{
  return tr("Offers a dialog during installation of a mod to unpack all its BSAs");
}

VersionInfo BsaExtractor::version() const
{
  return VersionInfo(1, 5, 0, VersionInfo::RELEASE_FINAL);
}

QList<PluginSetting> BsaExtractor::settings() const
{
  return {
    PluginSetting("only_alternate_source", "only trigger bsa extraction for alternate game sources", true)
  };
}


bool BsaExtractor::extractProgress(QProgressDialog &progress, int percentage, std::string fileName)
{
  progress.setLabelText(fileName.c_str());
  progress.setValue(percentage);
  qApp->processEvents();
  return !progress.wasCanceled();
}


void BsaExtractor::modInstalledHandler(IModInterface *mod)
{

  if (m_Organizer->pluginSetting(name(), "only_alternate_source").toBool() &&
      !(m_Organizer->modList()->state(mod->name()) & IModList::STATE_ALTERNATE)) {
    return;
  }

  if (QFileInfo(mod->absolutePath()) == QFileInfo(m_Organizer->managedGame()->dataDirectory().absolutePath())) {
    QMessageBox::information(nullptr, tr("invalid mod name"),
                             tr("BSA extraction doesn't work on mods that have the same name as a non-MO mod."
                                "Please remove the mod then reinstall with a different name."));
    return;
  }
  QDir dir(mod->absolutePath());

  QFileInfoList archives = dir.entryInfoList(QStringList({ "*.bsa", "*.ba2" }));
  if (archives.length() != 0 &&
      (QuestionBoxMemory::query(nullptr, "unpackBSA", tr("Extract BSA"),
                             tr("This mod contains at least one BSA. Do you want to unpack it?\n"
                                "(If you don't know about BSAs, just select no)"),
                             QDialogButtonBox::Yes | QDialogButtonBox::No, QDialogButtonBox::No) == QDialogButtonBox::Yes)) {

    bool removeBSAs = (QuestionBoxMemory::query(nullptr, "removeUnpackedBSA", tr("Remove extracted archives"),
                       tr("Do you wish to remove BSAs after extraction completed?\n"),
                       QDialogButtonBox::Yes | QDialogButtonBox::No, QDialogButtonBox::No) == QDialogButtonBox::Yes);
    foreach (QFileInfo archiveInfo, archives) {
      BSA::Archive archive;
      BSA::EErrorCode result = archive.read(archiveInfo.absoluteFilePath().toLocal8Bit().constData(), true);
      if ((result != BSA::ERROR_NONE) && (result != BSA::ERROR_INVALIDHASHES)) {
        reportError(tr("failed to read %1: %2").arg(archiveInfo.fileName()).arg(result));
        return;
      }

      QProgressDialog progress(nullptr);
      progress.setMaximum(100);
      progress.setValue(0);
      progress.show();

      archive.extractAll(mod->absolutePath().toLocal8Bit().constData(),
                         [this, &progress](int value, std::string filename) {
                          return extractProgress(progress, value, filename);
                         },
                         false);

      if (result == BSA::ERROR_INVALIDHASHES) {
        reportError(tr("This archive contains invalid hashes. Some files may be broken."));
      }

      archive.close();

      if (removeBSAs) {
        if (!QFile::remove(archiveInfo.absoluteFilePath())) {
          qCritical("failed to remove archive %s", archiveInfo.absoluteFilePath().toUtf8().constData());
        }
      }
    }
  }
}

#if QT_VERSION < QT_VERSION_CHECK(5,0,0)
Q_EXPORT_PLUGIN2(bsaextractor, BsaExtractor)
#endif
