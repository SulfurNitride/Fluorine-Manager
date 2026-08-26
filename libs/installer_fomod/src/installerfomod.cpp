#include "installerfomod.h"

#include <algorithm>

#include <QImageReader>
#include <QJsonDocument>
#include <QDir>
#include <QStringList>
#include <QTimer>
#include <QtPlugin>

#include <uibase/iinstallationmanager.h>
#include <uibase/imodinterface.h>
#include <uibase/imodlist.h>
#include <uibase/log.h>
#include <uibase/report.h>
#include <uibase/utility.h>

#include "fomodinstallerdialog.h"
#include "fomoddependency.h"

using namespace MOBase;

const unsigned int InstallerFomod::PROBLEM_IMAGETYPE_UNSUPPORTED;

InstallerFomod::InstallerFomod() : m_MOInfo(nullptr) {}

bool InstallerFomod::init(IOrganizer* moInfo)
{
  m_MOInfo = moInfo;
  m_MOInfo->pluginList()->onRefreshed(
      [this] { scheduleDependencyEvaluation(); });
  m_MOInfo->pluginList()->onPluginStateChanged(
      [this](const auto&) { scheduleDependencyEvaluation(); });
  m_MOInfo->modList()->onModInstalled(
      [this](auto*) { scheduleDependencyEvaluation(); });
  m_MOInfo->modList()->onModRemoved(
      [this](const auto&) { scheduleDependencyEvaluation(); });
  m_MOInfo->modList()->onModStateChanged(
      [this](const auto&) { scheduleDependencyEvaluation(); });
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
  return VersionInfo(1, 8, 0, VersionInfo::RELEASE_FINAL);
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

void InstallerFomod::onInstallationStart(QString const&, bool,
                                         IModInterface* currentMod)
{
  m_InstallerUsed = false;
  m_DependencySnapshot.clear();
  m_PreviousDependencySnapshot.clear();
  if (currentMod != nullptr) {
    m_PreviousDependencySnapshot =
        currentMod->pluginSetting(name(), FomodDependency::SnapshotKey)
            .toString()
            .toUtf8();
  }
}

bool InstallerFomod::supportsDependencyTracking(const QString& fileName) const
{
  const QString suffix = QFileInfo(fileName).suffix().toLower();
  return suffix == QLatin1String("esp") || suffix == QLatin1String("esm") ||
         suffix == QLatin1String("esl") || allowAnyFile();
}

void InstallerFomod::onInstallationEnd(EInstallResult result, IModInterface* newMod)
{
  if (result == EInstallResult::RESULT_SUCCESS && m_InstallerUsed && newMod != nullptr) {
    if (newMod->url().isEmpty()) {
      newMod->setUrl(m_Url);
    }
    if (!m_DependencySnapshot.isEmpty()) {
      newMod->setPluginSetting(name(), FomodDependency::SnapshotKey,
                               QString::fromUtf8(m_DependencySnapshot));
      newMod->setPluginSetting(name(), FomodDependency::ReviewReasonKey, QString());
    }
  }
  scheduleDependencyEvaluation();
}

void InstallerFomod::scheduleDependencyEvaluation()
{
  if (m_DependencyEvaluationQueued || m_MOInfo == nullptr) {
    return;
  }
  m_DependencyEvaluationQueued = true;
  QTimer::singleShot(0, this, [this] {
    m_DependencyEvaluationQueued = false;
    evaluateDependencies();
  });
}

void InstallerFomod::evaluateDependencies()
{
  if (m_MOInfo == nullptr) {
    return;
  }

  const auto resolve = [this](const QString& file) {
    const IPluginList::PluginStates state = fileState(file);
    if (state == IPluginList::STATE_ACTIVE) {
      return FomodDependency::FileState::Active;
    }
    if (state == IPluginList::STATE_INACTIVE) {
      return FomodDependency::FileState::Inactive;
    }
    return FomodDependency::FileState::Missing;
  };

  bool reviewStateChanged = false;
  IModList* modList        = m_MOInfo->modList();
  for (const QString& modName : modList->allMods()) {
    IModInterface* mod = modList->getMod(modName);
    if (mod == nullptr) {
      continue;
    }
    QByteArray snapshot =
        mod->pluginSetting(name(), FomodDependency::SnapshotKey).toString().toUtf8();
    if (snapshot.isEmpty()) {
      continue;
    }

    QString reason;
    if (!FomodDependency::isBaselined(snapshot)) {
      snapshot = FomodDependency::rebaseline(snapshot, resolve);
      if (!snapshot.isEmpty()) {
        mod->setPluginSetting(name(), FomodDependency::SnapshotKey,
                              QString::fromUtf8(snapshot));
      }
    } else if ((modList->state(modName) & IModList::STATE_ACTIVE) != 0) {
      QStringList reasons = FomodDependency::reviewReasons(snapshot, resolve);
      constexpr qsizetype MaxDisplayedReasons = 3;
      if (reasons.size() > MaxDisplayedReasons) {
        const qsizetype additional = reasons.size() - MaxDisplayedReasons;
        reasons = reasons.mid(0, MaxDisplayedReasons);
        reasons.append(tr("%1 more FOMOD changes").arg(additional));
      }
      reason = reasons.join(QLatin1Char('\n'));
    }

    const QString previous =
        mod->pluginSetting(name(), FomodDependency::ReviewReasonKey).toString();
    if (reason != previous) {
      mod->setPluginSetting(name(), FomodDependency::ReviewReasonKey, reason);
      reviewStateChanged = true;
    }
  }

  if (reviewStateChanged) {
    requestModListRefresh();
  }
}

void InstallerFomod::requestModListRefresh()
{
  if (m_RefreshQueued) {
    return;
  }
  m_RefreshQueued = true;
  QTimer::singleShot(0, this, [this] {
    m_RefreshQueued = false;
    if (m_MOInfo != nullptr) {
      m_MOInfo->refresh(false);
    }
  });
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
  if (tree == nullptr) {
    return false;
  }

  return std::any_of(tree->begin(), tree->end(), [](const auto& entry) {
    return entry->isFile() &&
           entry->name().compare("ModuleConfig.xml", Qt::CaseInsensitive) == 0;
  });
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
        (entry->name().compare("info.xml", Qt::CaseInsensitive) == 0 ||
         entry->name().compare("ModuleConfig.xml", Qt::CaseInsensitive) == 0)) {
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
  const QStringList extractedFiles = manager()->extractFiles(installerFiles);
  if (extractedFiles.size() == installerFiles.size()) {
    try {
      std::shared_ptr<const IFileTree> fomodTree = findFomodDirectory(tree);

      QString extractionRoot;
      for (qsizetype i = 0; i < extractedFiles.size(); ++i) {
        QString relativePath =
            QDir::fromNativeSeparators(installerFiles[i]->path());
        relativePath.replace(QLatin1Char('\\'), QLatin1Char('/'));
        relativePath = QDir::cleanPath(relativePath);
        const QString absolutePath =
            QDir::cleanPath(QDir::fromNativeSeparators(extractedFiles[i]));
        const QString suffix = QStringLiteral("/") + relativePath;
        if (absolutePath.endsWith(suffix)) {
          extractionRoot =
              absolutePath.left(absolutePath.size() - suffix.size());
          break;
        }
      }
      if (extractionRoot.isEmpty()) {
        throw Exception(tr("Unable to locate extracted FOMOD files."));
      }

      QString fomodPath = fomodTree->parent()->path("/");
      QString fomodDirName = fomodTree->name();
      FomodInstallerDialog dialog(
          this, modName, extractionRoot, fomodPath, fomodDirName,
          std::bind(&InstallerFomod::fileState, this, std::placeholders::_1));
      dialog.initData(m_MOInfo);
      if (!m_PreviousDependencySnapshot.isEmpty()) {
        dialog.restoreTrackedSelections(m_PreviousDependencySnapshot);
      }
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
        const auto installResult = dialog.updateTree(tree);
        if (installResult == IPluginInstaller::RESULT_SUCCESS) {
          m_DependencySnapshot =
              QJsonDocument(dialog.dependencySnapshot()).toJson(QJsonDocument::Compact);
        }
        return installResult;
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
    return tr("This indicates that Qt image format plugins are missing from your "
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
