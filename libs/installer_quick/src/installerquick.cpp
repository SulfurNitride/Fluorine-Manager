#include "installerquick.h"

#include <QDialog>
#include <QtPlugin>

#include <uibase/game_features/igamefeatures.h>
#include <uibase/iplugingame.h>
#include <uibase/log.h>

#include "simpleinstalldialog.h"

using namespace MOBase;

InstallerQuick::InstallerQuick() : m_MOInfo(nullptr) {}

bool InstallerQuick::init(IOrganizer* moInfo)
{
  m_MOInfo = moInfo;
  // Note: Cannot retrieve the checker here because the game might
  // not be initialized yet.
  return true;
}

QString InstallerQuick::name() const
{
  return "Simple Installer";
}

QString InstallerQuick::localizedName() const
{
  return tr("Simple Installer");
}

QString InstallerQuick::author() const
{
  return "Tannin";
}

QString InstallerQuick::description() const
{
  return tr("Installer for very simple archives");
}

VersionInfo InstallerQuick::version() const
{
  return VersionInfo(1, 3, 0, VersionInfo::RELEASE_FINAL);
}

QList<PluginSetting> InstallerQuick::settings() const
{
  return {PluginSetting("silent",
                        "simple plugins will be installed without any user interaction",
                        QVariant(false))};
}

unsigned int InstallerQuick::priority() const
{
  return 50;
}

bool InstallerQuick::isManualInstaller() const
{
  return false;
}

bool InstallerQuick::isDataTextArchiveTopLayer(std::shared_ptr<const IFileTree> tree,
                                               QString const& dataFolderName,
                                               ModDataChecker*) const
{
  // A "DataText" archive is defined as having exactly one folder named like
  // `dataFolderName` and one or more "useless" files (text files, pdf, or images).
  static const std::set<QString, FileNameComparator> txtExtensions{
      "txt", "pdf", "md", "jpg", "jpeg", "png", "bmp"};
  bool dataFound = false;
  bool txtFound  = false;
  for (auto entry : *tree) {
    if (entry->isDir()) {
      // If data was already found, or this is a directory not named "data", fail:
      if (dataFound || entry->compare(dataFolderName) != 0) {
        return false;
      }
      dataFound = true;
    } else {
      if (txtExtensions.count(entry->suffix()) == 0) {
        return false;
      }
      txtFound = true;
    }
  }
  return dataFound && txtFound;
}

std::shared_ptr<const IFileTree>
InstallerQuick::getSimpleArchiveBase(std::shared_ptr<const IFileTree> dataTree,
                                     QString const& dataFolderName,
                                     ModDataChecker* checker) const
{
  if (!checker) {
    return nullptr;
  }
  while (true) {
    if (checker->dataLooksValid(dataTree) == ModDataChecker::CheckReturn::VALID ||
        isDataTextArchiveTopLayer(dataTree, dataFolderName, checker)) {
      return dataTree;
    } else if (dataTree->size() == 1 && dataTree->at(0)->isDir()) {
      dataTree = dataTree->at(0)->astree();
    } else {
      log::debug("Archive is not a simple archive.");
      return nullptr;
    }
  }
}

bool InstallerQuick::isArchiveSupported(std::shared_ptr<const IFileTree> tree) const
{
  auto checker = m_MOInfo->gameFeatures()->gameFeature<ModDataChecker>();
  if (!checker) {
    return false;
  }
  if (getSimpleArchiveBase(tree, m_MOInfo->managedGame()->dataDirectory().dirName(),
                           checker.get()) != nullptr) {
    return true;
  }
  return checker->dataLooksValid(tree) == ModDataChecker::CheckReturn::FIXABLE;
}

IPluginInstaller::EInstallResult
InstallerQuick::install(GuessedValue<QString>& modName,
                        std::shared_ptr<IFileTree>& tree, QString&, int&)
{
  const QString dataFolderName = m_MOInfo->managedGame()->dataDirectory().dirName();
  auto checker = m_MOInfo->gameFeatures()->gameFeature<ModDataChecker>();

  auto base = std::const_pointer_cast<IFileTree>(
      getSimpleArchiveBase(tree, dataFolderName, checker.get()));
  if (base == nullptr &&
      checker->dataLooksValid(tree) == ModDataChecker::CheckReturn::FIXABLE) {
    tree = checker->fix(tree);
  } else {
    tree = base;
  }
  if (tree != nullptr) {
    SimpleInstallDialog dialog(modName, parentWidget());
    if (m_MOInfo->pluginSetting(name(), "silent").toBool() ||
        dialog.exec() == QDialog::Accepted) {
      modName.update(dialog.getName(), GUESS_USER);

      // If we have a data+txt archive, we move files to the data folder and
      // switch to the data folder. We need to check that we actually have a
      // checker here, otherwise it is anyway impossible that
      // isDataTextArchiveTopLayer() returned true.
      if (checker && isDataTextArchiveTopLayer(tree, dataFolderName, checker.get())) {
        auto dataTree = tree->findDirectory(dataFolderName);
        dataTree->detach();
        dataTree->merge(tree);
        tree = dataTree;
      }
      return RESULT_SUCCESS;
    } else {
      if (dialog.manualRequested()) {
        modName.update(dialog.getName(), GUESS_USER);
        return RESULT_MANUALREQUESTED;
      } else {
        return RESULT_CANCELED;
      }
    }
  } else {
    // install shouldn't even have even have been called
    qCritical("unsupported archive for quick installer");
    return RESULT_FAILED;
  }
}

#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
Q_EXPORT_PLUGIN2(installerQuick, InstallerQuick)
#endif
