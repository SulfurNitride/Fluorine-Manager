#include "installerfomod.h"

#include <QImageReader>
#include <QStringList>
#include <QtPlugin>

#include <uibase/iinstallationmanager.h>
#include <uibase/imodinterface.h>
#include <uibase/imodlist.h>
#include <uibase/log.h>
#include <uibase/report.h>
#include <uibase/utility.h>

#include "fomodinstallerdialog.h"

using namespace MOBase;

const unsigned int InstallerFomod::PROBLEM_IMAGETYPE_UNSUPPORTED;

InstallerFomod::InstallerFomod() : m_MOInfo(nullptr) {}

bool InstallerFomod::init(IOrganizer* moInfo)
{
  m_MOInfo = moInfo;
  return true;
}

QString InstallerFomod::name() const
{
  return "Fomod Installer";
}

QString InstallerFomod::author() const
{
  return "Tannin & thosrtanner";
}

QString InstallerFomod::description() const
{
  return tr("Installer for xml based fomod archives.");
}

VersionInfo InstallerFomod::version() const
{
  return VersionInfo(1, 7, 0, VersionInfo::RELEASE_FINAL);
}

QString InstallerFomod::localizedName() const
{
  return tr("Fomod Installer");
}

bool InstallerFomod::allowAnyFile() const
{
  return m_MOInfo->pluginSetting(name(), "use_any_file").toBool();
}

bool InstallerFomod::checkDisabledMods() const
{
  return m_MOInfo->pluginSetting(name(), "see_disabled_mods").toBool();
}

QList<PluginSetting> InstallerFomod::settings() const
{
  QList<PluginSetting> result;
  result.push_back(
      PluginSetting("prefer", "prefer this over the NCC based plugin", QVariant(true)));
  result.push_back(PluginSetting("use_any_file",
                                 "allow dependencies on any file, not just esp/esm",
                                 QVariant(false)));
  result.push_back(PluginSetting("see_disabled_mods",
                                 "treat disabled mods as inactive rather than missing",
                                 QVariant(false)));
  return result;
}

unsigned int InstallerFomod::priority() const
{
  return m_MOInfo->pluginSetting(name(), "prefer").toBool() ? 110 : 90;
}

bool InstallerFomod::isManualInstaller() const
{
  return false;
}

void InstallerFomod::onInstallationStart(QString const&, bool, IModInterface*)
{
  m_InstallerUsed = false;
}

void InstallerFomod::onInstallationEnd(EInstallResult result, IModInterface* newMod)
{
  if (result == EInstallResult::RESULT_SUCCESS && m_InstallerUsed &&
      newMod->url().isEmpty()) {
    newMod->setUrl(m_Url);
  }
}

std::shared_ptr<const IFileTree>
InstallerFomod::findFomodDirectory(std::shared_ptr<const IFileTree> tree) const
{
  auto entry = tree->find("fomod", FileTreeEntry::DIRECTORY);

  if (entry != nullptr) {
    return entry->astree();
  }

  if (tree->size() == 1 && tree->at(0)->isDir()) {
    return findFomodDirectory(tree->at(0)->astree());
  }
  return nullptr;
}

bool InstallerFomod::isArchiveSupported(std::shared_ptr<const IFileTree> tree) const
{
  tree = findFomodDirectory(tree);
  if (tree != nullptr) {
    return tree->exists("ModuleConfig.xml", FileTreeEntry::FILE);
  }
  return false;
}

void InstallerFomod::appendImageFiles(
    std::vector<std::shared_ptr<const FileTreeEntry>>& entries,
    std::shared_ptr<const IFileTree> tree) const
{
  static std::set<QString, FileNameComparator> imageSuffixes{"png", "jpg", "jpeg",
                                                             "gif", "bmp"};
  for (auto entry : *tree) {
    if (entry->isDir()) {
      appendImageFiles(entries, entry->astree());
    } else if (imageSuffixes.count(entry->suffix()) > 0) {
      entries.push_back(entry);
    }
  }
}

std::vector<std::shared_ptr<const FileTreeEntry>>
InstallerFomod::buildFomodTree(std::shared_ptr<const IFileTree> tree) const
{
  std::vector<std::shared_ptr<const FileTreeEntry>> entries;

  auto fomodTree = findFomodDirectory(tree);

  for (auto entry : *fomodTree) {
    if (entry->isFile() &&
        (entry->compare("info.xml") == 0 || entry->compare("ModuleConfig.xml") == 0)) {
      entries.push_back(entry);
    }
  }

  appendImageFiles(entries, tree);

  return entries;
}

IPluginList::PluginStates InstallerFomod::fileState(const QString& fileName) const
{
  QString ext = QFileInfo(fileName).suffix().toLower();
  if ((ext == "esp") || (ext == "esm") || (ext == "esl")) {
    IPluginList::PluginStates state = m_MOInfo->pluginList()->state(fileName);
    if (state != IPluginList::STATE_MISSING) {
      return state;
    }
  } else if (allowAnyFile()) {
    QFileInfo info(fileName);
    QString name = info.fileName();
    QStringList files =
        m_MOInfo->findFiles(info.dir().path(), [&, name](const QString& f) -> bool {
          return name.compare(QFileInfo(f).fileName(),
                              FileNameComparator::CaseSensitivity) == 0;
        });
    // A note: The list of files produced is somewhat odd as it's the full path
    // to the originating mod (or mods). However, all we care about is if it's
    // there or not.
    if (files.size() != 0) {
      return IPluginList::STATE_ACTIVE;
    }
  } else {
    log::warn("A dependency on non esp/esm/esl {} will always find it as missing.",
              fileName);
    return IPluginList::STATE_MISSING;
  }

  // If they are really desparate we look in the full mod list and try that
  if (checkDisabledMods()) {
    IModList* modList = m_MOInfo->modList();
    QStringList list  = modList->allMods();
    for (QString mod : list) {
      // Get mod state. if it's active we've already looked. If it's not valid,
      // no point in looking.
      IModList::ModStates state = modList->state(mod);
      if ((state & IModList::STATE_ACTIVE) != 0 ||
          (state & IModList::STATE_VALID) == 0) {
        continue;
      }
      MOBase::IModInterface* modInfo = m_MOInfo->modList()->getMod(mod);
      // Go see if the file is in the mod
      QDir modpath(modInfo->absolutePath());
      QFile file(modpath.absoluteFilePath(fileName));
      if (file.exists()) {
        return IPluginList::STATE_INACTIVE;
      }
    }
  }
  return IPluginList::STATE_MISSING;
}

IPluginInstaller::EInstallResult
InstallerFomod::install(GuessedValue<QString>& modName,
                        std::shared_ptr<IFileTree>& tree, QString& version, int& modID)
{
  auto installerFiles = buildFomodTree(tree);
  if (manager()->extractFiles(installerFiles).size() == installerFiles.size()) {
    try {
      std::shared_ptr<const IFileTree> fomodTree = findFomodDirectory(tree);

      QString fomodPath = fomodTree->parent()->path();
      FomodInstallerDialog dialog(
          this, modName, fomodPath,
          std::bind(&InstallerFomod::fileState, this, std::placeholders::_1));
      dialog.initData(m_MOInfo);
      if (!dialog.getVersion().isEmpty()) {
        version = dialog.getVersion();
      }
      if (dialog.getModID() != -1) {
        modID = dialog.getModID();
      }

      m_InstallerUsed = true;
      m_Url           = dialog.getURL();

      if (!dialog.hasOptions()) {
        dialog.transformToSmallInstall();
      }

      auto result = dialog.exec();
      if (result == QDialog::Accepted) {
        modName.update(dialog.getName(), GUESS_USER);
        return dialog.updateTree(tree);
      } else {
        if (dialog.manualRequested()) {
          modName.update(dialog.getName(), GUESS_USER);
          return IPluginInstaller::RESULT_MANUALREQUESTED;
        } else if (result == QDialog::Rejected) {
          return IPluginInstaller::RESULT_CANCELED;
        } else {
          return IPluginInstaller::RESULT_FAILED;
        }
      }
    } catch (const std::exception& e) {
      reportError(tr("Installation as fomod failed: %1").arg(e.what()));
      return IPluginInstaller::RESULT_FAILED;
    }
  }
  return IPluginInstaller::RESULT_CANCELED;
}

#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
Q_EXPORT_PLUGIN2(installerFomod, InstallerFomod)
#endif

std::vector<unsigned int> InstallerFomod::activeProblems() const
{
  std::vector<unsigned int> result;
  QList<QByteArray> formats = QImageReader::supportedImageFormats();
  if (!formats.contains("jpg")) {
    result.push_back(PROBLEM_IMAGETYPE_UNSUPPORTED);
  }
  return result;
}

QString InstallerFomod::shortDescription(unsigned int key) const
{
  switch (key) {
  case PROBLEM_IMAGETYPE_UNSUPPORTED:
    return tr("image formats not supported.");
  default:
    throw Exception(tr("invalid problem key %1").arg(key));
  }
}

QString InstallerFomod::fullDescription(unsigned int key) const
{
  switch (key) {
  case PROBLEM_IMAGETYPE_UNSUPPORTED:
    return tr("This indicates that files from dlls/imageformats are missing from your "
              "MO installation or outdated. "
              "Images in installers may not be displayed. Please re-install MO");
  default:
    throw Exception(tr("invalid problem key %1").arg(key));
  }
}

bool InstallerFomod::hasGuidedFix(unsigned int) const
{
  return false;
}

void InstallerFomod::startGuidedFix(unsigned int) const {}
