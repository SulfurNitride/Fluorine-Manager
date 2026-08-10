#include "datatab.h"
#include "filetree.h"
#include "filetreemodel.h"
#include "messagedialog.h"
#include "modelutils.h"
#include "organizercore.h"
#include "profile.h"
#include "settings.h"
#include "ui_mainwindow.h"
#include <log.h>
#include <report.h>
#include <uibase/scopeguard.h>

#include <QMessageBox>
#include <QSettings>
#include <QUuid>
#include <utility.h>

using namespace MOShared;
using namespace MOBase;

// in mainwindow.cpp
QString UnmanagedModName();

DataTab::DataTab(OrganizerCore& core, PluginContainer& pc, QWidget* parent,
                 Ui::MainWindow* mwui)
    : m_core(core), m_pluginContainer(pc), m_parent(parent),
      ui{.tabs=mwui->tabWidget,
         .tab=mwui->dataTab,
         .refresh=mwui->dataTabRefresh,
         .browseVFS=mwui->dataTabBrowseVFS,
         .browseRootBuilder=mwui->dataTabBrowseRootBuilder,
         .tree=mwui->dataTree,
         .conflicts=mwui->dataTabShowOnlyConflicts,
         .archives=mwui->dataTabShowFromArchives,
         .hiddenFiles=mwui->dataTabShowHiddenFiles}

{
  m_filetree.reset(new FileTree(core, m_pluginContainer, ui.tree));
  m_filter.setUseSourceSort(true);
  m_filter.setFilterColumn(FileTreeModel::FileName);
  m_filter.setEdit(mwui->dataTabFilter);
  m_filter.setList(mwui->dataTree);
  m_filter.setUpdateDelay(true);

  if (auto* m = m_filter.proxyModel()) {
    m->setDynamicSortFilter(false);
  }

  connect(&m_filter, &FilterWidget::aboutToChange, [&] {
    ensureFullyLoaded();
  });

  connect(ui.browseVFS, &QPushButton::clicked, [&] {
    onBrowseVFS();
  });
  connect(ui.browseRootBuilder, &QPushButton::clicked, [&] {
    onBrowseRootBuilder();
  });

  // Hide Root Builder button if the feature is disabled for this instance.
  {
    bool rbEnabled = true;
    if (const auto* s = Settings::maybeInstance()) {
      const QSettings instanceIni(s->filename(), QSettings::IniFormat);
      rbEnabled = instanceIni.value("fluorine/vfs_root_builder", true).toBool();
    }
    ui.browseRootBuilder->setVisible(rbEnabled);
  }

  connect(ui.refresh, &QPushButton::clicked, [&] {
    onRefresh();
  });

  connect(ui.conflicts, &QCheckBox::toggled, [&] {
    onConflicts();
  });

  connect(ui.archives, &QCheckBox::toggled, [&] {
    onArchives();
  });

  connect(ui.hiddenFiles, &QCheckBox::toggled, [&] {
    onHiddenFiles();
  });

  connect(ui.tree->selectionModel(), &QItemSelectionModel::selectionChanged, [=, this] {
    const auto* fileTreeModel     = m_filetree->model();
    const auto& selectedIndexList = MOShared::indexViewToModel(
        ui.tree->selectionModel()->selectedRows(), fileTreeModel);
    std::set<QString> mods;
    for (const auto& idx : selectedIndexList) {
      mods.insert(fileTreeModel->itemFromIndex(idx)->mod());
    }
    mwui->modList->setHighlightedMods(mods);
  });

  connect(m_filetree.get(), &FileTree::executablesChanged, this,
          &DataTab::executablesChanged);

  connect(m_filetree.get(), &FileTree::originModified, this, &DataTab::originModified);

  connect(m_filetree.get(), &FileTree::displayModInformation, this,
          &DataTab::displayModInformation);
}

void DataTab::saveState(Settings& s) const
{
  s.geometry().saveState(ui.tree->header());
  s.widgets().saveChecked(ui.conflicts);
  s.widgets().saveChecked(ui.archives);
  s.widgets().saveChecked(ui.hiddenFiles);
}

void DataTab::restoreState(const Settings& s) const
{
  s.geometry().restoreState(ui.tree->header());

  // prior to 2.3, the list was not sortable, and this remembered in the
  // widget state, for whatever reason
  ui.tree->setSortingEnabled(true);

  s.widgets().restoreChecked(ui.conflicts);
  s.widgets().restoreChecked(ui.archives);
  s.widgets().restoreChecked(ui.hiddenFiles);
}

void DataTab::activated()
{
  if (m_needUpdate) {
    updateTree();
  }
  // update highlighted mods
  ui.tree->selectionModel()->selectionChanged({}, {});
}

bool DataTab::isActive() const
{
  return ui.tabs->currentWidget() == ui.tab;
}

void DataTab::onRefresh()
{
  if (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier) {
    m_filetree->model()->setEnabled(false);
    m_filetree->clear();
  }

  m_core.refreshDirectoryStructure();
}

void DataTab::onBrowseVFS()
{
  QString const dataPath = m_core.managedGame()->dataDirectory().absolutePath();
  browseVfsSession(
      dataPath, QObject::tr("Browse VFS"),
      QObject::tr("The virtual filesystem is mounted.\n\n"
                  "The game Data folder has been opened in your file manager. "
                  "You can browse the merged mod files as the game would see "
                  "them.\n\nClose this dialog to unmount the VFS."));
}

void DataTab::onBrowseRootBuilder()
{
  QString const gameRoot = m_core.managedGame()->gameDirectory().absolutePath();
  browseVfsSession(
      gameRoot, QObject::tr("Browse Root Builder"),
      QObject::tr("The virtual filesystem is mounted and Root Builder files "
                  "have been deployed to the game root.\n\nThe game folder has "
                  "been opened in your file manager. You can see files "
                  "deployed by Root Builder (e.g. SKSE, ENB).\n\nClose this "
                  "dialog to unmount and clean up."));
}

void DataTab::browseVfsSession(const QString& path, const QString& title,
                               const QString& message)
{
  const auto profile = m_core.currentProfile();
  if (profile == nullptr) {
    QMessageBox::warning(m_parent, QObject::tr("VFS unavailable"),
                         QObject::tr("No active profile is available."));
    return;
  }

  const QString profileName = profile->name();
  const QString launchToken =
      QUuid::createUuid().toString(QUuid::WithoutBraces);

  OrganizerCore::VfsPreviewSessionResult result;
  try {
    result = m_core.beginVfsPreviewSession(launchToken, profileName);
  } catch (const std::exception& e) {
    log::error("Unable to start VFS preview: {}", e.what());
    QMessageBox::warning(
        m_parent, QObject::tr("VFS unavailable"),
        QObject::tr("The virtual filesystem could not be mounted:\n\n%1")
            .arg(QString::fromUtf8(e.what())));
    return;
  } catch (...) {
    log::error("Unable to start VFS preview: unknown exception");
    QMessageBox::warning(
        m_parent, QObject::tr("VFS unavailable"),
        QObject::tr("The virtual filesystem could not be mounted."));
    return;
  }

  if (result == OrganizerCore::VfsPreviewSessionResult::Busy) {
    QMessageBox::information(
        m_parent, QObject::tr("VFS in use"),
        QObject::tr("Another application or browse session is using the "
                    "virtual filesystem. Close it before browsing the VFS."));
    return;
  }
  if (result == OrganizerCore::VfsPreviewSessionResult::Unavailable) {
    QMessageBox::information(
        m_parent, QObject::tr("VFS unavailable"),
        QObject::tr("The managed game does not use Fluorine Manager's virtual "
                    "filesystem."));
    return;
  }

  const auto closeSession = MakeGuard([this, launchToken, profileName]() {
    try {
      if (!m_core.endVfsPreviewSession(launchToken, profileName)) {
        log::error("VFS preview session '{}' could not close its exact tracked "
                   "session",
                   launchToken.toStdString());
      }
    } catch (const std::exception& e) {
      log::error("Unable to close VFS preview session '{}': {}",
                 launchToken.toStdString(), e.what());
    } catch (...) {
      log::error("Unable to close VFS preview session '{}': unknown exception",
                 launchToken.toStdString());
    }
  });

  shell::Explore(path);

  QMessageBox box(QMessageBox::Information, title, message,
                  QMessageBox::Close, m_parent);
  box.setWindowModality(Qt::WindowModal);
  box.exec();
}

void DataTab::updateTree()
{
  if (isActive()) {
    doUpdateTree();
  } else {
    m_needUpdate = true;
  }
}

void DataTab::doUpdateTree()
{
  m_filetree->model()->setEnabled(true);
  m_filetree->refresh();

  if (!m_filter.empty()) {
    ensureFullyLoaded();

    if (auto* m = m_filter.proxyModel()) {
      m->invalidate();
    }
  }

  m_needUpdate = false;
}

void DataTab::ensureFullyLoaded()
{
  if (!m_filetree->fullyLoaded()) {
    m_filter.setFilteringEnabled(false);
    m_filetree->ensureFullyLoaded();
    m_filter.setFilteringEnabled(true);
  }
}

void DataTab::onConflicts()
{
  updateOptions();
}

void DataTab::onArchives()
{
  updateOptions();
}

void DataTab::onHiddenFiles()
{
  updateOptions();
}

void DataTab::updateOptions()
{
  using M = FileTreeModel;

  M::Flags flags = M::NoFlags;

  if (ui.conflicts->isChecked()) {
    flags |= M::ConflictsOnly | M::PruneDirectories;
  }

  if (ui.archives->isChecked()) {
    flags |= M::Archives;
  }

  if (ui.hiddenFiles->isChecked()) {
    flags |= M::HiddenFiles;
  }

  m_filetree->model()->setFlags(flags);
  updateTree();
}
