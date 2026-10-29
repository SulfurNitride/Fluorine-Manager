#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "fluorinetheme.h"
#include "instancemanager.h"
#include "libraryview.h"
#include "modlist.h"
#include "modlistviewactions.h"
#include "openmwsettingstab.h"
#include "organizercore.h"
#include "profile.h"
#include "settings.h"
#include "selectedmodpanel.h"
#include "workspaceframe.h"
#include "workspacelayout.h"

#include <QActionGroup>
#include <QMenu>
#include <QSettings>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTabBar>
#include <QVBoxLayout>

namespace
{
std::optional<unsigned> singleModIndex(ModListView* view)
{
  const auto rows = view->selectionModel()->selectedRows();
  if (rows.size() != 1) return {};
  bool valid = false;
  const auto index = rows.front().data(ModList::IndexRole).toUInt(&valid);
  if (!valid || index >= ModInfo::getNumMods()) return {};
  return index;
}
}

void MainWindow::setupWorkspace()
{
  m_WorkspacePages = new QStackedWidget(ui->centralWidget);
  m_WorkspacePages->setObjectName("workspacePages");
  m_WorkspaceFrame = new WorkspaceFrame(m_WorkspacePages);
  m_WorkspacePages->addWidget(m_WorkspaceFrame);
  ui->verticalLayout_8->removeWidget(ui->categoriesSplitter);
  ui->verticalLayout_8->setContentsMargins(0, 0, 0, 0);
  ui->verticalLayout_8->setSpacing(0);
  ui->verticalLayout_8->addWidget(m_WorkspacePages);

  auto* profile = new QWidget;
  profile->setObjectName("workspaceProfileControls");
  ui->verticalLayout->removeItem(ui->profileControls);
  profile->setLayout(ui->profileControls);
  ui->profileControls->setContentsMargins(0, 0, 0, 0);
  ui->profileControls->setSpacing(6);
  ui->profileControls->removeWidget(ui->gameButton);
  ui->gameButton->hide();
  ui->profileControls->removeWidget(ui->openFolderMenu);
  // The Profiles window already handles choosing and managing profiles.
  // Keep the combo as the existing activation/state bridge, without a second
  // visible control for the same task.
  ui->profileControls->removeWidget(ui->label_3);
  ui->profileControls->removeWidget(ui->profileBox);
  ui->label_3->hide();
  ui->profileBox->hide();
  ui->profileActionsButton->setMinimumHeight(0);
  ui->profileActionsButton->setPopupMode(QToolButton::DelayedPopup);
  connect(ui->profileActionsButton, &QToolButton::clicked,
          ui->actionAdd_Profile, &QAction::trigger);
  connect(ui->profileBox, &QComboBox::currentTextChanged, this,
          &MainWindow::updateProfileControl);

  ui->verticalLayout_2->removeWidget(ui->startGroup);
  ui->horizontalLayout_5->setContentsMargins(0, 0, 0, 0);
  ui->horizontalLayout_5->setSpacing(6);
  ui->programLabel->hide();
  ui->executablesListBox->setMinimumHeight(0);
  ui->executablesListBox->setMinimumContentsLength(14);
  ui->executablesListBox->setSizeAdjustPolicy(
      QComboBox::AdjustToMinimumContentsLengthWithIcon);
  ui->executablesListBox->setIconSize(QSize(20, 20));
  ui->executablesListBox->setFont(font());
  ui->executablesListBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  ui->startButton->setMinimumSize(78, 0);
  ui->startButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
  ui->startButton->setFont(font());
  ui->startButton->setIconSize(QSize(18, 18));
  ui->setupChecksButton->setText(tr("Checks"));
  ui->setupChecksButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
  m_WorkspaceFrame->setHeaderControls(profile, ui->startGroup);
  m_WorkspaceFrame->setLibraryAction(ui->actionChange_Game);

  auto* content = new QWidget;
  content->setObjectName("workspaceContent");
  auto* contentLayout = new QVBoxLayout(content);
  contentLayout->setContentsMargins(12, 8, 12, 10);
  contentLayout->setSpacing(0);
  auto* tools = new QWidget(content);
  tools->setObjectName("modWorkspaceTools");
  ui->verticalLayout->removeItem(ui->modSearchControls);
  tools->setLayout(ui->modSearchControls);
  ui->modSearchControls->setContentsMargins(0, 6, 0, 2);
  ui->modSearchControls->setSpacing(7);
  // Keep the existing grouping model and saved preference, with the controls
  // in View instead of occupying the everyday mod toolbar.
  ui->groupCombo->hide();
  ui->verticalLayout->insertWidget(0, tools);
  auto* modsHeading = new QLabel(tr("Mods"), ui->layoutWidget);
  modsHeading->setObjectName("workspaceModsHeading");
  modsHeading->setContentsMargins(8, 6, 0, 6);
  ui->verticalLayout->insertWidget(0, modsHeading);
  contentLayout->addWidget(ui->categoriesSplitter, 1);
  ui->categoriesSplitter->setHandleWidth(10);
  ui->splitter->setHandleWidth(10);
  ui->categoriesSplitter->handle(1)->setToolTip(tr("Drag to resize the filters and workspace."));
  ui->splitter->handle(1)->setToolTip(tr("Drag to resize the mod list and task pane."));
  ui->verticalLayout->setContentsMargins(0, 0, 0, 0);
  ui->verticalLayout->setSpacing(0);
  ui->modListStatus->setContentsMargins(0, 5, 0, 0);
  ui->verticalLayout_2->setSpacing(0);
  ui->verticalLayout_2->setContentsMargins(0, 0, 0, 0);
  ui->tabWidget->setDocumentMode(true);
  ui->tabWidget->tabBar()->setObjectName("workspaceTaskTabs");
  ui->tabWidget->tabBar()->setDrawBase(false);
  ui->tabWidget->tabBar()->setUsesScrollButtons(true);
  ui->tabWidget->tabBar()->setExpanding(false);
  ui->tabWidget->tabBar()->setElideMode(Qt::ElideRight);
  m_WorkspaceFrame->setContent(content);
  m_WorkspaceFrame->setNavigationVisible(false);

  m_SelectedModPanel = new SelectedModPanel(ui->layoutWidget_2);
  ui->verticalLayout_2->addWidget(m_SelectedModPanel);
  m_SelectedModPanel->hide();
  m_WorkspaceLayout = new WorkspaceLayout(ui->categoriesSplitter, ui->splitter,
      ui->layoutWidget, ui->categoriesGroup, ui->layoutWidget_2, ui->tabWidget,
      ui->tabWidget->indexOf(ui->espTab) >= 0 ? ui->espTab : nullptr,
      m_SelectedModPanel, tools, this);

  // Workspace presentation belongs in the main View menu, leaving the
  // everyday header focused on the active profile and launch controls.
  auto* menu = ui->menuView;
  auto* before = menu->actions().isEmpty() ? nullptr : menu->actions().front();
  auto addViewAction = [menu, before](const QString& text) {
    auto* action = new QAction(text, menu);
    menu->insertAction(before, action);
    return action;
  };
  auto* modes = new QActionGroup(menu);
  modes->setExclusive(true);
  for (const auto mode : {WorkspaceLayout::Mode::Split,
                          WorkspaceLayout::Mode::Full, WorkspaceLayout::Mode::Details}) {
    auto* action = addViewAction(QString());
    action->setCheckable(true);
    action->setData(static_cast<int>(mode));
    action->setProperty("workspaceMode", true);
    modes->addAction(action);
    if (mode == WorkspaceLayout::Mode::Split)
      action->setEnabled(ui->tabWidget->count() > 0);
    connect(action, &QAction::triggered, this, [this, mode] {
      m_WorkspaceLayout->setMode(mode);
      showWorkspaceDestination("mods");
    });
  }
  menu->insertSeparator(before);
  auto* compact = addViewAction(tr("Compact rows"));
  compact->setObjectName("workspaceCompactAction");
  compact->setCheckable(true);
  connect(compact, &QAction::toggled, this, [this](bool checked) {
    m_CompactWorkspace = checked;
    applyWorkspaceTheme();
  });
  auto* grouping = new QMenu(tr("Group mods"), menu);
  menu->insertMenu(before, grouping);
  grouping->setObjectName("workspaceGroupingMenu");
  auto* groups = new QActionGroup(grouping);
  groups->setExclusive(true);
  for (int index = 0; index < ui->groupCombo->count(); ++index) {
    auto* action = grouping->addAction(ui->groupCombo->itemText(index));
    action->setCheckable(true);
    action->setData(index);
    groups->addAction(action);
    connect(action, &QAction::triggered, this, [this, index] {
      ui->groupCombo->setCurrentIndex(index);
    });
  }
  connect(grouping, &QMenu::aboutToShow, this, [this, grouping] {
    for (auto* action : grouping->actions()) {
      const int index = action->data().toInt();
      action->setText(ui->groupCombo->itemText(index));
      action->setChecked(ui->groupCombo->currentIndex() == index);
    }
  });
  menu->insertSeparator(before);
  menu->insertAction(before, ui->toolBar->toggleViewAction());
  menu->insertAction(before, ui->actionShowFilters);
  connect(menu, &QMenu::aboutToShow, this, [this, modes, compact] {
    for (auto* action : modes->actions())
      action->setChecked(action->data().toInt() == static_cast<int>(m_WorkspaceLayout->mode()));
    const QSignalBlocker blocker(compact);
    compact->setChecked(m_CompactWorkspace);
  });
  ui->openFolderMenu->setMinimumHeight(0);
  ui->profileControls->addWidget(ui->openFolderMenu);
  translateWorkspace();

  connect(m_SelectedModPanel, &SelectedModPanel::detailsRequested, this,
          [this] { openSelectedMod(false); });
  connect(m_SelectedModPanel, &SelectedModPanel::conflictsRequested, this,
          [this] { openSelectedMod(true); });
  connect(m_SelectedModPanel, &SelectedModPanel::closeRequested, this, [this] {
    m_WorkspaceLayout->setMode(WorkspaceLayout::Mode::Full);
    showWorkspaceDestination("mods");
  });

  m_SelectedModTimer.setSingleShot(true);
  m_SelectedModTimer.setInterval(40);
  connect(&m_SelectedModTimer, &QTimer::timeout, this, &MainWindow::updateSelectedMod);
  auto queue = [this] { queueSelectedModUpdate(); };
  connect(ui->modList->selectionModel(), &QItemSelectionModel::selectionChanged,
          this, queue);
  connect(ui->modList->model(), &QAbstractItemModel::dataChanged, this, queue);
  connect(ui->modList->model(), &QAbstractItemModel::modelReset, this, queue);
  connect(ui->modList->model(), &QAbstractItemModel::layoutChanged, this, queue);
  connect(ui->modList->model(), &QAbstractItemModel::rowsRemoved, this, queue);
  connect(ui->profileBox, &QComboBox::currentTextChanged, this, queue);
  applyWorkspaceTheme();
}

void MainWindow::translateWorkspace()
{
  if (!m_WorkspaceFrame) return;
  const auto labelTab = [this](QWidget* page, const QString& id, const QString& label) {
    const int index = ui->tabWidget->indexOf(page);
    if (index < 0) return;
    ui->tabWidget->setTabText(index, label);
    ui->tabWidget->setTabToolTip(index, label);
    ui->tabWidget->tabBar()->setTabData(index, id);
  };
  labelTab(ui->espTab, "plugins", tr("Plugins"));
  labelTab(ui->downloadTab, "downloads", tr("Downloads"));
  labelTab(ui->dataTab, "files", tr("Game files"));
  labelTab(ui->savesTab, "saves", tr("Saves"));
  labelTab(ui->bsaTab, "archives", tr("Archives"));
  if (auto* heading = m_WorkspaceFrame->findChild<QLabel*>("workspaceModsHeading"))
    heading->setText(tr("Mods"));
  updateProfileControl();
  if (auto* grouping = ui->menuView->findChild<QMenu*>("workspaceGroupingMenu"))
    grouping->setTitle(tr("Group mods"));
  for (auto* action : ui->menuView->actions()) {
    if (action->property("workspaceMode").toBool()) {
      const auto mode = static_cast<WorkspaceLayout::Mode>(action->data().toInt());
      action->setText(mode == WorkspaceLayout::Mode::Details ? tr("Mods with details") :
          mode == WorkspaceLayout::Mode::Split ? tr("Mods with tool tabs") : tr("Mods only"));
    } else if (action->objectName() == "workspaceCompactAction") {
      action->setText(tr("Compact rows"));
    }
  }
}

void MainWindow::updateProfileControl()
{
  const auto name = ui->profileBox->currentText();
  const auto metrics = ui->profileActionsButton->fontMetrics();
  const auto visibleName = metrics.elidedText(name, Qt::ElideRight,
                                             metrics.averageCharWidth() * 24);
  auto label = name.isEmpty() ? tr("Profiles") : tr("Profiles: %1").arg(visibleName);
  ui->profileActionsButton->setText(label.replace('&', QStringLiteral("&&")));
  ui->profileActionsButton->setAccessibleName(
      name.isEmpty() ? tr("Profiles") : tr("Profiles: %1").arg(name));
  const QString description = name.isEmpty()
      ? tr("Choose, create, copy, rename, or remove profiles.")
      : tr("Current profile: %1. Choose or manage profiles.").arg(name);
  ui->profileActionsButton->setToolTip(description);
  ui->profileActionsButton->setAccessibleDescription(description);
}

void MainWindow::showWorkspaceDestination(const QString& destination)
{
  if (!m_WorkspaceReady) return;
  if (destination == "mods") {
    m_WorkspaceLayout->showMods();
  } else {
    QWidget* page = destination == "plugins" ? ui->espTab :
        destination == "files" ? ui->dataTab :
        destination == "downloads" ? ui->downloadTab :
        destination == "saves" ? ui->savesTab :
        destination == "archives" ? ui->bsaTab : nullptr;
    if (!m_WorkspaceLayout->showPage(page)) return;
  }
  m_WorkspacePages->setCurrentWidget(m_WorkspaceFrame);
  queueSelectedModUpdate();
}

void MainWindow::restoreWorkspaceSettings()
{
  if (!m_WorkspaceLayout) return;
  QSettings settings(m_OrganizerCore.settings().filename(), QSettings::IniFormat);
  const int value = settings.value("Fluorine/workspaceView", 1).toInt();
  auto mode = value >= 0 && value <= 2 ? static_cast<WorkspaceLayout::Mode>(value) :
                                       WorkspaceLayout::Mode::Split;
  // Adopt the tabbed workspace once. Later explicit layout choices persist.
  if (settings.value("Fluorine/workspaceLayoutVersion", 0).toInt() < 2)
    mode = WorkspaceLayout::Mode::Split;
  const QString tab = settings.value("Fluorine/workspaceTab",
      settings.value("Fluorine/workspaceDestination", "plugins")).toString();
  for (int index = 0; index < ui->tabWidget->count(); ++index) {
    if (ui->tabWidget->tabBar()->tabData(index).toString() == tab)
      ui->tabWidget->setCurrentIndex(index);
  }
  const bool filters = settings.value("Fluorine/workspaceFilters",
                                      !ui->categoriesGroup->isHidden()).toBool();
  m_CompactWorkspace = settings.value("Fluorine/workspaceCompact", false).toBool();
  m_WorkspaceLayout->setDetailsState(settings.value("Fluorine/workspaceDetailsSizes").toByteArray());
  m_WorkspaceLayout->initialize(mode, filters);
  m_WorkspaceReady = true;
  setCategoryListVisible(filters);
  showWorkspaceDestination("mods");
  applyWorkspaceTheme();
}

void MainWindow::saveWorkspaceSettings()
{
  if (!m_WorkspaceReady) return;
  QSettings settings(m_OrganizerCore.settings().filename(), QSettings::IniFormat);
  settings.setValue("Fluorine/workspaceView", static_cast<int>(m_WorkspaceLayout->mode()));
  settings.setValue("Fluorine/workspaceLayoutVersion", 2);
  settings.setValue("Fluorine/workspaceFilters", m_WorkspaceLayout->filtersVisible());
  settings.setValue("Fluorine/workspaceCompact", m_CompactWorkspace);
  settings.setValue("Fluorine/workspaceTab",
      ui->tabWidget->tabBar()->tabData(ui->tabWidget->currentIndex()));
  settings.setValue("Fluorine/workspaceDetailsSizes", m_WorkspaceLayout->detailsState());
}

void MainWindow::applyWorkspaceTheme()
{
  FluorineTheme::apply(m_WorkspaceFrame, m_CompactWorkspace);
  FluorineTheme::apply(m_LibraryView);
  FluorineTheme::apply(ui->toolBar, true);
  updateProfileControl();
  if (auto* heading = m_WorkspaceFrame->findChild<QLabel*>("workspaceModsHeading"))
    heading->setFixedHeight(ui->tabWidget->tabBar()->sizeHint().height());
}

void MainWindow::queueSelectedModUpdate()
{
  if (m_WorkspaceLayout && m_WorkspaceLayout->inspecting() &&
      m_WorkspacePages->currentWidget() == m_WorkspaceFrame &&
      !m_SelectedModTimer.isActive()) m_SelectedModTimer.start();
}

void MainWindow::updateSelectedMod()
{
  if (!m_WorkspaceLayout->inspecting()) return;
  const auto rows = ui->modList->selectionModel()->selectedRows();
  if (rows.size() > 1) {
    m_SelectedModPanel->setMultipleSelection(rows.size());
    return;
  }
  const auto index = singleModIndex(ui->modList);
  const auto profile = m_OrganizerCore.currentProfile();
  if (!index || !profile) {
    m_SelectedModPanel->setSummary({});
    return;
  }
  const auto mod = ModInfo::getByIndex(*index);
  if (!mod || (!mod->isRegular() && !mod->isForeign())) {
    m_SelectedModPanel->setSummary({});
    return;
  }
  SelectedModSummary summary;
  summary.name = mod->name();
  summary.version = mod->version().displayString();
  summary.category = mod->categories().join(", ");
  summary.notes = mod->notes();
  summary.author = mod->author();
  summary.profile = profile->name();
  summary.separator = mod->isSeparator();
  summary.external = mod->isForeign();
  if (summary.external)
    summary.description = tr("This content is installed in the game's data directory and managed outside Fluorine.");
  summary.enabled = profile->modEnabled(*index);
  if (!summary.separator) summary.priority = profile->getModPriority(*index);
  m_SelectedModPanel->setSummary(std::move(summary));
}

void MainWindow::openSelectedMod(bool conflicts)
{
  const auto index = singleModIndex(ui->modList);
  if (!index) return;
  const auto mod = ModInfo::getByIndex(*index);
  if (!mod || !mod->isRegular() || mod->isSeparator()) return;
  ui->modList->actions().displayModInformation(*index,
      conflicts ? ModInfoTabIDs::Conflicts : ModInfoTabIDs::None);
  queueSelectedModUpdate();
}
