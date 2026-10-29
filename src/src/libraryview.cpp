#include <cerrno>
#include "libraryview.h"
#include "clf3installerdialog.h"
#include "clf3installutils.h"
#include "createinstancedialog.h"
#include "filesystemutilities.h"
#include "instancemanager.h"
#include "librarysetuppanel.h"
#include "plugincontainer.h"
#include "protonlauncher.h"
#include "settings.h"
#include "shared/appconfig.h"
#include "shared/util.h"

#include <algorithm>
#include <QAction>
#include <QAbstractItemView>
#include <QBoxLayout>
#include <QCheckBox>
#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFontMetrics>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListView>
#include <QModelIndex>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QSplitter>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStyle>
#include <QStyleOptionFocusRect>
#include <QStyledItemDelegate>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QMenu>
#include <QProgressDialog>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QToolButton>
#include <QtConcurrent/QtConcurrent>
#include <iplugingame.h>
#include <log.h>
#include "slrmanager.h"
#include <report.h>
#include <utility.h>
#include <utility>

using namespace MOBase;

// returns the icon for the given instance or an empty 32x32 icon if the game
// plugin couldn't be found
//
QIcon instanceIcon(PluginContainer& pc, const Instance& i)
{
  auto* game = InstanceManager::singleton().gamePluginForDirectory(i.directory(), pc);

  if (!game) {
    QPixmap empty(32, 32);
    empty.fill(QColor(0, 0, 0, 0));
    return QIcon(empty);
  }

  // it's possible to have the game installed in a way that the game plugin
  // couldn't auto detect; in this case, the instance would have a valid game
  // directory, but the plugin wouldn't know about it
  //
  // it's also possible, but unlikely, to have multiple installations of the
  // same game that have different icons for the same exe
  //
  // so the game directory specified for the instance needs to be given to the
  // game plugin to get the appropriate icon, but since these game plugin
  // objects are created on startup and are global, they should retain their
  // auto detected path
  //
  // if not, creating a new instance for a specific plugin would use the game
  // directory of the instance for which the icon was most recently shown, which
  // would be really inconsistent
  //
  //
  // this game plugin could also be the currently active plugin for the
  // current instance, which should _definitely_ keep pointing to the same
  // directory as before

  // remember old game directory
  //
  // note that gameDirectory() returns a QDir, which doesn't support empty
  // strings (they get converted to "." automatically!), but the plugin _will_
  // try to return an empty string when the game has not been auto-detected
  //
  // so gameDirectory() _cannot_ reliably be used if `isInstalled()` is false
  const QString old = game->isInstalled() ? game->gameDirectory().path() : "";

  // revert
  Guard const g([&] {
    game->setGamePath(old);
  });

  // set directory for this instance
  game->setGamePath(i.gameDirectory());

  return game->gameIcon();
}

namespace
{
constexpr int GameNameRole = Qt::UserRole + 1;
constexpr int CurrentSetupRole = Qt::UserRole + 2;
constexpr int SetupNameRole = Qt::UserRole + 3;

QFont scaledFont(QFont font, qreal factor)
{
  if (font.pointSizeF() > 0.0) {
    font.setPointSizeF(font.pointSizeF() * factor);
  } else if (font.pixelSize() > 0) {
    font.setPixelSize(qMax(1, qRound(font.pixelSize() * factor)));
  }
  return font;
}

QString instanceGameLabel(const Instance& instance)
{
  const QString game = instance.gameName();
  return game.compare(instance.displayName(), Qt::CaseInsensitive) == 0
             ? QString()
             : game;
}

class SetupItemDelegate final : public QStyledItemDelegate
{
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  QSize sizeHint(const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override
  {
    Q_UNUSED(index);
    const QFontMetrics metrics(option.font);
    return {option.rect.width(), metrics.height() * 3 + metrics.height() / 2};
  }

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override
  {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const bool selected = option.state.testFlag(QStyle::State_Selected);
    const bool focused  = option.state.testFlag(QStyle::State_HasFocus);
    const QPalette palette = option.palette;
    const QRect card = option.rect.adjusted(4, 3, -4, -3);
    const QColor background = selected ? palette.highlight().color() : palette.base().color();
    const QColor foreground = selected ? palette.highlightedText().color()
                                       : palette.text().color();
    painter->setPen(palette.mid().color());
    painter->setBrush(background);
    painter->drawRoundedRect(card, 6, 6);

    const int inset = option.fontMetrics.height() / 2 + 4;
    const QRect inner = card.adjusted(inset, 5, -inset, -5);
    const QSize iconSize(qMax(32, option.fontMetrics.height() * 2),
                         qMax(32, option.fontMetrics.height() * 2));
    const QRect iconRect(inner.left(), inner.center().y() - iconSize.height() / 2,
                         iconSize.width(), iconSize.height());
    const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
    if (!icon.isNull()) {
      icon.paint(painter, iconRect, Qt::AlignCenter,
                 selected ? QIcon::Selected : QIcon::Normal);
    }

    const int gap = option.fontMetrics.height() / 2;
    QRect textRect = inner.adjusted(iconSize.width() + gap, 0, 0, 0);
    const QString title = index.data(SetupNameRole).toString();
    const QString game = index.data(GameNameRole).toString();
    const bool current = index.data(CurrentSetupRole).toBool();
    const int rowHeight = option.fontMetrics.height();

    QFont titleFont = option.font;
    titleFont.setBold(true);
    painter->setFont(titleFont);
    painter->setPen(foreground);
    const QFontMetrics titleMetrics(titleFont);
    const QString elidedTitle = titleMetrics.elidedText(title, Qt::ElideRight,
                                                        textRect.width());
    painter->drawText(QRect(textRect.left(), textRect.top(), textRect.width(), rowHeight),
                      Qt::AlignVCenter | Qt::AlignLeft, elidedTitle);

    QFont detailFont = scaledFont(option.font, 0.92);
    painter->setFont(detailFont);
    const QFontMetrics detailMetrics(detailFont);
    QString detail = game.isEmpty() ? tr("Game not configured") : game;
    if (current) {
      detail = detail.isEmpty() ? tr("Current setup")
                                : tr("Current setup · %1").arg(detail);
    }
    painter->setPen(selected ? foreground
                             : (current ? palette.highlight().color()
                                        : palette.placeholderText().color()));
    const QString elidedDetail = detailMetrics.elidedText(detail, Qt::ElideRight,
                                                          textRect.width());
    painter->drawText(QRect(textRect.left(), textRect.top() + rowHeight,
                            textRect.width(), rowHeight),
                      Qt::AlignVCenter | Qt::AlignLeft, elidedDetail);

    if (focused && option.widget) {
      QStyleOptionFocusRect focus;
      focus.initFrom(option.widget);
      focus.rect = card.adjusted(2, 2, -2, -2);
      focus.backgroundColor = background;
      option.widget->style()->drawPrimitive(QStyle::PE_FrameFocusRect, &focus,
                                            painter, option.widget);
    }

    painter->restore();
  }
};
}  // namespace

struct Ui::LibraryViewUi
{
  QWidget* heading = nullptr;
  QPushButton* backToWorkspace = nullptr;
  QBoxLayout* actionsLayout = nullptr;
  QPushButton* createNew = nullptr;
  QPushButton* openExisting = nullptr;
  QPushButton* installWabbajack = nullptr;
  QWidget* pendingInstall = nullptr;
  QLabel* pendingSummary = nullptr;
  QLineEdit* filter = nullptr;
  QListView* list = nullptr;
  QSplitter* splitter = nullptr;
  LibrarySetupPanel* details = nullptr;
  QPushButton* switchToInstance = nullptr;
  QAction* convertToPortable = nullptr;
  QAction* convertToGlobal = nullptr;
  QAction* openINI = nullptr;
  QAction* removeFromList = nullptr;
  QAction* deleteInstance = nullptr;
};

// pops up a dialog to ask for an instance name when renaming
//
QString getInstanceName(QWidget* parent, const QString& title, const QString& moreText,
                        const QString& label, const QString& oldName = {})
{
  auto& m = InstanceManager::singleton();

  QDialog dlg(parent);
  dlg.setWindowTitle(title);

  auto* ly = new QVBoxLayout(&dlg);

  auto* bb = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);

  auto* text = new QLineEdit(oldName);
  text->selectAll();

  auto* error = new QLabel;

  if (!moreText.isEmpty()) {
    auto* lb = new QLabel(moreText);
    lb->setWordWrap(true);
    ly->addWidget(lb);
    ly->addSpacing(10);
  }

  auto* lb = new QLabel(label);
  lb->setWordWrap(true);
  ly->addWidget(lb);

  ly->addWidget(text);
  ly->addWidget(error);
  ly->addStretch();
  ly->addWidget(bb);

  auto check = [&] {
    bool okay = false;

    if (text->text().isEmpty()) {
      error->setText("");
    } else if (!MOBase::validFileName(text->text())) {
      error->setText(QObject::tr("The setup name must be a valid folder name."));
    } else {
      const auto name = MOBase::sanitizeFileName(text->text());

      if ((name != oldName) && m.instanceExists(text->text())) {
        error->setText(QObject::tr("A setup with this name already exists."));
      } else {
        okay = true;
      }
    }

    error->setVisible(!okay);
    bb->button(QDialogButtonBox::Ok)->setEnabled(okay);
  };

  QObject::connect(text, &QLineEdit::textChanged, [&] {
    check();
  });
  QObject::connect(bb, &QDialogButtonBox::accepted, [&] {
    dlg.accept();
  });
  QObject::connect(bb, &QDialogButtonBox::rejected, [&] {
    dlg.reject();
  });

  check();

  dlg.resize({400, 120});
  if (dlg.exec() != QDialog::Accepted) {
    return {};
  }

  return MOBase::sanitizeFileName(text->text());
}

LibraryView::~LibraryView() = default;

LibraryView::LibraryView(PluginContainer& pc, QWidget* parent)
    : QWidget(parent), ui(new Ui::LibraryViewUi), m_pc(pc)
{
  setObjectName(QStringLiteral("fluorineLibraryView"));
  const int textHeight = fontMetrics().height();
  const int spacing = qMax(8, textHeight / 2);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(spacing * 2, spacing * 2, spacing * 2, spacing * 2);
  layout->setSpacing(spacing);

  auto* headingRow = new QHBoxLayout;
  headingRow->setSpacing(spacing);
  auto* headingText = new QVBoxLayout;
  headingText->setSpacing(qMax(2, spacing / 3));
  auto* title = new QLabel(tr("Your setups"), this);
  title->setObjectName(QStringLiteral("libraryHeading"));
  title->setAccessibleName(title->text());
  QFont headingFont = title->font();
  headingFont = scaledFont(headingFont, 1.45);
  headingFont.setBold(true);
  title->setFont(headingFont);
  headingText->addWidget(title);

  auto* subtitle = new QLabel(
      tr("Choose a setup to continue. Each setup keeps its own mods and profiles."),
      this);
  subtitle->setObjectName(QStringLiteral("libraryDescription"));
  subtitle->setWordWrap(true);
  subtitle->setForegroundRole(QPalette::PlaceholderText);
  headingText->addWidget(subtitle);
  headingRow->addLayout(headingText, 1);

  ui->backToWorkspace = new QPushButton(tr("Back to workspace"), this);
  ui->backToWorkspace->setObjectName(QStringLiteral("libraryBackToWorkspace"));
  ui->backToWorkspace->setAccessibleName(ui->backToWorkspace->text());
  ui->backToWorkspace->setToolTip(tr("Return to the active setup's workspace."));
  ui->backToWorkspace->setIcon(QIcon(QStringLiteral(":/MO/gui/previous")));
  ui->backToWorkspace->setMinimumHeight(qMax(36, textHeight * 2));
  headingRow->addWidget(ui->backToWorkspace, 0, Qt::AlignTop);
  layout->addLayout(headingRow);

  ui->actionsLayout = new QBoxLayout(QBoxLayout::LeftToRight);
  ui->actionsLayout->setSpacing(spacing);
  ui->createNew = new QPushButton(tr("New setup"), this);
  ui->createNew->setObjectName(QStringLiteral("libraryCreateSetupButton"));
  ui->createNew->setAccessibleName(tr("Create a new setup"));
  ui->createNew->setToolTip(tr("Create a new setup with the existing setup wizard."));
  ui->createNew->setIcon(QIcon(QStringLiteral(":/MO/gui/add")));
  ui->openExisting = new QPushButton(tr("Add existing"), this);
  ui->openExisting->setObjectName(QStringLiteral("libraryAddExistingButton"));
  ui->openExisting->setAccessibleName(tr("Add an existing setup"));
  ui->openExisting->setToolTip(tr("Register a portable setup folder in the library."));
  ui->openExisting->setIcon(QIcon(QStringLiteral(":/MO/gui/open_folder")));
  ui->installWabbajack = new QPushButton(tr("Install modlist"), this);
  ui->installWabbajack->setObjectName(QStringLiteral("libraryInstallModlistButton"));
  ui->installWabbajack->setAccessibleName(tr("Install a Wabbajack modlist"));
  ui->installWabbajack->setToolTip(
      tr("Install a Wabbajack modlist with CLF3, or browse Nexus Collections (installation is disabled)."));
  ui->installWabbajack->setIcon(QIcon(QStringLiteral(":/MO/gui/installer")));
  for (QPushButton* button : {ui->createNew, ui->openExisting, ui->installWabbajack}) {
    button->setMinimumHeight(qMax(36, textHeight * 2));
    ui->actionsLayout->addWidget(button);
  }
  ui->actionsLayout->addStretch();
  layout->addLayout(ui->actionsLayout);

  ui->pendingInstall = new QWidget(this);
  ui->pendingInstall->setObjectName(QStringLiteral("libraryPendingInstall"));
  auto* pendingLayout = new QHBoxLayout(ui->pendingInstall);
  pendingLayout->setContentsMargins(spacing, spacing, spacing, spacing);
  ui->pendingSummary = new QLabel(ui->pendingInstall);
  ui->pendingSummary->setObjectName(QStringLiteral("libraryPendingInstallSummary"));
  ui->pendingSummary->setTextFormat(Qt::PlainText);
  ui->pendingSummary->setWordWrap(true);
  pendingLayout->addWidget(ui->pendingSummary, 1);
  auto* resumeInstall = new QPushButton(tr("Resume installation"), ui->pendingInstall);
  resumeInstall->setObjectName(QStringLiteral("libraryResumeInstallButton"));
  resumeInstall->setProperty("primary", true);
  pendingLayout->addWidget(resumeInstall);
  connect(resumeInstall, &QPushButton::clicked, this,
          [this] { openModlistInstaller(true); });
  layout->addWidget(ui->pendingInstall);
  updatePendingInstallation();

  auto* listHeading = new QLabel(tr("Library setups"), this);
  listHeading->setObjectName(QStringLiteral("libraryListHeading"));
  QFont listHeadingFont = listHeading->font();
  listHeadingFont.setBold(true);
  listHeading->setFont(listHeadingFont);
  layout->addWidget(listHeading);

  ui->splitter = new QSplitter(Qt::Horizontal, this);
  ui->splitter->setObjectName(QStringLiteral("librarySetupSplitter"));
  ui->splitter->setChildrenCollapsible(false);
  ui->splitter->setHandleWidth(qMax(4, textHeight / 3));

  auto* listPane = new QWidget(ui->splitter);
  listPane->setObjectName(QStringLiteral("librarySetupListPane"));
  auto* listLayout = new QVBoxLayout(listPane);
  listLayout->setContentsMargins(0, 0, 0, 0);
  listLayout->setSpacing(spacing);
  ui->filter = new QLineEdit(listPane);
  ui->filter->setObjectName(QStringLiteral("librarySearch"));
  ui->filter->setPlaceholderText(tr("Search setups or games…"));
  ui->filter->setAccessibleName(tr("Search setups or games"));
  ui->filter->setClearButtonEnabled(true);
  ui->list = new QListView(listPane);
  ui->list->setObjectName(QStringLiteral("librarySetupList"));
  ui->list->setAccessibleName(tr("Library setups"));
  ui->list->setEditTriggers(QAbstractItemView::NoEditTriggers);
  ui->list->setSelectionMode(QAbstractItemView::SingleSelection);
  ui->list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  ui->list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  ui->list->setUniformItemSizes(false);
  ui->list->setSpacing(qMax(4, spacing / 2));
  ui->list->setItemDelegate(new SetupItemDelegate(ui->list));
  ui->list->setFocusPolicy(Qt::StrongFocus);
  listLayout->addWidget(ui->filter);
  listLayout->addWidget(ui->list, 1);
  ui->splitter->addWidget(listPane);

  ui->details = new LibrarySetupPanel(ui->splitter);
  ui->details->setObjectName(QStringLiteral("librarySetupDetails"));
  ui->splitter->addWidget(ui->details);
  ui->splitter->setStretchFactor(0, 0);
  ui->splitter->setStretchFactor(1, 1);
  layout->addWidget(ui->splitter, 1);

  auto* footer = new QHBoxLayout;
  footer->addStretch();
  ui->switchToInstance = new QPushButton(tr("Open setup"), this);
  ui->switchToInstance->setObjectName(QStringLiteral("openSelectedSetup"));
  ui->switchToInstance->setAccessibleName(tr("Open selected setup"));
  ui->switchToInstance->setIcon(QIcon(QStringLiteral(":/MO/gui/next")));
  ui->switchToInstance->setMinimumHeight(qMax(38, textHeight * 2));
  ui->switchToInstance->setDefault(true);
  ui->switchToInstance->setProperty("primary", true);
  footer->addWidget(ui->switchToInstance);
  layout->addLayout(footer);

  ui->convertToPortable = new QAction(tr("Convert to portable"), this);
  ui->convertToGlobal = new QAction(tr("Convert to global"), this);
  ui->openINI = new QAction(tr("Open configuration"), this);
  ui->removeFromList = new QAction(tr("Remove from library"), this);
  ui->removeFromList->setToolTip(
      tr("Remove this setup from your library without deleting any files."));
  ui->deleteInstance = new QAction(QIcon(QStringLiteral(":/MO/gui/remove")),
                                   tr("Delete setup…"), this);

  auto* moreActions = new QMenu(ui->details);
  moreActions->addAction(ui->openINI);
  moreActions->addAction(ui->convertToPortable);
  moreActions->addAction(ui->convertToGlobal);
  moreActions->addSeparator();
  moreActions->addAction(ui->removeFromList);
  moreActions->addAction(ui->deleteInstance);
  ui->details->moreActions()->setMenu(moreActions);

  m_model = new QStandardItemModel(this);
  ui->list->setModel(m_model);
  m_filter.setEdit(ui->filter);
  // FilterWidget supplies a generic placeholder; keep this journey specific.
  ui->filter->setPlaceholderText(tr("Search setups or games…"));
  m_filter.setList(ui->list);
  m_filter.setFilteredBorder(false);

  connect(ui->createNew, &QPushButton::clicked, this, &LibraryView::createNew);
  connect(ui->openExisting, &QPushButton::clicked, this,
          &LibraryView::openExistingPortable);
  connect(ui->installWabbajack, &QPushButton::clicked, this,
          &LibraryView::installWabbajack);
  connect(ui->backToWorkspace, &QPushButton::clicked, this,
          &LibraryView::closeRequested);
  connect(ui->list->selectionModel(), &QItemSelectionModel::selectionChanged,
          this, &LibraryView::onSelection);
  connect(ui->list, &QListView::activated, this,
          [this](const QModelIndex&) { openSelectedInstance(); });
  connect(ui->details, &LibrarySetupPanel::renameRequested, this,
          &LibraryView::rename);
  connect(ui->details, &LibrarySetupPanel::openSetupFolder, this,
          &LibraryView::exploreLocation);
  connect(ui->details, &LibrarySetupPanel::openDataFolder, this,
          &LibraryView::exploreBaseDirectory);
  connect(ui->details, &LibrarySetupPanel::openGameFolder, this,
          &LibraryView::exploreGame);
  connect(ui->convertToGlobal, &QAction::triggered, this,
          &LibraryView::convertToGlobal);
  connect(ui->convertToPortable, &QAction::triggered, this,
          &LibraryView::convertToPortable);
  connect(ui->openINI, &QAction::triggered, this, &LibraryView::openINI);
  connect(ui->removeFromList, &QAction::triggered, this,
          &LibraryView::removeFromList);
  connect(ui->deleteInstance, &QAction::triggered, this,
          &LibraryView::deleteInstance);

  connect(ui->details->steamDrmCheckBox(), &QCheckBox::toggled, this,
          [this](bool checked) {
            const auto* inst = singleSelection();
            if (!inst) return;
            const QString ini = inst->iniPath();
            if (ini.isEmpty()) return;
            QSettings settings(ini, QSettings::IniFormat);
            settings.setValue("fluorine/steam_drm", checked);
          });
  connect(ui->details->rootBuilderCheckBox(), &QCheckBox::toggled, this,
          [this](bool checked) {
            const auto* inst = singleSelection();
            if (!inst) return;
            const QString ini = inst->iniPath();
            if (ini.isEmpty()) return;
            QSettings settings(ini, QSettings::IniFormat);
            settings.setValue("fluorine/vfs_root_builder", checked);
          });
  connect(ui->switchToInstance, &QPushButton::clicked, this,
          &LibraryView::openSelectedInstance);

  refresh();
  selectActiveInstance();
}

void LibraryView::resizeEvent(QResizeEvent* event)
{
  QWidget::resizeEvent(event);
  updateResponsiveLayout();
}

void LibraryView::setModalPresentation(bool modal)
{
  const QString label = modal ? tr("Close") : tr("Back to workspace");
  ui->backToWorkspace->setText(label);
  ui->backToWorkspace->setAccessibleName(label);
  ui->backToWorkspace->setToolTip(
      modal ? tr("Close the library manager.")
            : tr("Return to the active setup's workspace."));
}

void LibraryView::updateResponsiveLayout()
{
  if (!ui || !ui->splitter) return;

  const int preferredListWidth =
      qMax(fontMetrics().horizontalAdvance(tr("Search setups or games…")) +
               fontMetrics().height() * 3,
           fontMetrics().horizontalAdvance(tr("A setup name and game title")) +
               fontMetrics().height() * 4);
  const int preferredDetailsWidth =
      qMax(fontMetrics().horizontalAdvance(tr("Enable VFS Root Builder")) * 2,
           fontMetrics().height() * 28);
  const int spacing = qMax(8, fontMetrics().height() / 2);
  const int margins = layout() ? layout()->contentsMargins().left() +
                                     layout()->contentsMargins().right()
                               : 0;
  const int splitterSpacing = layout() ? layout()->spacing() : 0;
  const bool narrow = width() < preferredListWidth + preferredDetailsWidth +
                          margins + splitterSpacing;
  const int actionWidths = ui->createNew->minimumSizeHint().width() +
                           ui->openExisting->minimumSizeHint().width() +
                           ui->installWabbajack->minimumSizeHint().width() +
                           spacing * 2 + margins;
  const auto actionDirection = width() < actionWidths
                                   ? QBoxLayout::TopToBottom
                                   : QBoxLayout::LeftToRight;
  if (ui->actionsLayout->direction() != actionDirection) {
    ui->actionsLayout->setDirection(actionDirection);
  }
  if (narrow != m_verticalSplitter) {
    m_verticalSplitter = narrow;
    ui->splitter->setOrientation(narrow ? Qt::Vertical : Qt::Horizontal);
    if (narrow) {
      ui->splitter->setSizes({qMax(fontMetrics().height() * 14, height() / 3),
                              qMax(fontMetrics().height() * 22, height() * 2 / 3)});
    } else {
      ui->splitter->setSizes({preferredListWidth, qMax(preferredDetailsWidth,
                                                       width() - preferredListWidth)});
    }
  }
}

void LibraryView::changeEvent(QEvent* event)
{
  QWidget::changeEvent(event);
  if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange ||
      event->type() == QEvent::PaletteChange) {
    if (ui && ui->list) {
      ui->list->doItemsLayout();
      ui->list->viewport()->update();
    }
    updateResponsiveLayout();
    updateGeometry();
  }
}

void LibraryView::refresh()
{
  updatePendingInstallation();
  QString selectedPath;
  const auto selectedIndex = singleSelectionIndex();
  if (selectedIndex < m_instances.size()) {
    selectedPath = m_instances[selectedIndex]->directory();
  }
  updateInstances();
  updateList();
  if (!selectedPath.isEmpty() && selectByPath(selectedPath)) {
    return;
  }
  selectActiveInstance();
}

void LibraryView::updateInstances()
{
  auto& m = InstanceManager::singleton();

  m_instances.clear();
  QSet<QString> knownPaths;
  const auto canonicalPath = [](const QString& path) {
    const QString canonical = QFileInfo(path).canonicalFilePath();
    return canonical.isEmpty() ? QDir(path).absolutePath() : canonical;
  };

  for (auto&& d : m.globalInstancePaths()) {
    m_instances.push_back(std::make_unique<Instance>(d, false));
    knownPaths.insert(canonicalPath(d));
  }

  // sort first, prepend portable after so it's always on top
  std::sort(m_instances.begin(), m_instances.end(), [](auto&& a, auto&& b) {
    return (MOBase::naturalCompare(a->displayName(), b->displayName()) < 0);
  });

  // add registered portable instances (non-default paths)
  const QString defaultPortable = QDir(InstanceManager::portablePath()).absolutePath();
  const QString defaultPortableCanonical = canonicalPath(defaultPortable);
  for (const auto& path : InstanceManager::registeredPortablePaths()) {
    // skip the default portable path (handled separately below)
    if (QDir(path).absolutePath() == defaultPortable) {
      continue;
    }
    // skip paths where ModOrganizer.ini no longer exists
    if (!QFileInfo::exists(QDir(path).filePath("ModOrganizer.ini"))) {
      continue;
    }
    const QString canonical = canonicalPath(path);
    if (knownPaths.contains(canonical)) {
      continue;
    }
    knownPaths.insert(canonical);
    m_instances.push_back(std::make_unique<Instance>(path, true));
  }

  // An absolute -i setup need not appear in either registry. Include it for
  // discovery without registering it or modifying its files.
  const auto active = m.currentInstance();
  if (active && QFileInfo::exists(QDir(active->directory()).filePath("ModOrganizer.ini"))) {
    const QString activePath = canonicalPath(active->directory());
    auto existing = std::find_if(m_instances.begin(), m_instances.end(),
        [&](const auto& instance) { return canonicalPath(instance->directory()) == activePath; });
    if (existing != m_instances.end()) {
      // Prefer the running setup's spelling over a registered symlink alias.
      // Instance::isActive() uses that spelling to protect rename/delete and
      // to choose the return-without-restart action.
      *existing = std::make_unique<Instance>(active->directory(), active->isPortable());
    } else if (activePath != defaultPortableCanonical) {
      knownPaths.insert(activePath);
      m_instances.push_back(
          std::make_unique<Instance>(active->directory(), active->isPortable()));
    }
  }

  // re-sort to interleave registered portables alphabetically
  std::sort(m_instances.begin(), m_instances.end(), [](auto&& a, auto&& b) {
    return (MOBase::naturalCompare(a->displayName(), b->displayName()) < 0);
  });

  if (InstanceManager::portableInstanceExists() &&
      !knownPaths.contains(defaultPortableCanonical)) {
    m_instances.insert(m_instances.begin(),
                       std::make_unique<Instance>(InstanceManager::portablePath(), true));
  }

  // read all inis, ignore errors
  for (auto&& i : m_instances) {
    i->readFromIni();
  }
}

void LibraryView::updateList()
{
  const auto prevSelIndex = singleSelectionIndex();
  const auto* prevSel     = singleSelection();

  m_model->clear();

  std::size_t sel = NoSelection;

  // creating items for instances
  for (std::size_t i = 0; i < m_instances.size(); ++i) {
    const auto& ii = *m_instances[i];

    const QString gameLabel = instanceGameLabel(ii);
    QString filterText = ii.displayName();
    if (ii.isActive()) filterText += QStringLiteral("\n") + tr("Current setup");
    if (!gameLabel.isEmpty()) filterText += QStringLiteral("\n") + gameLabel;
    auto* item = new QStandardItem(filterText);
    item->setIcon(instanceIcon(m_pc, ii));
    item->setToolTip(ii.displayName() + "\n" + ii.gameName() + "\n" + ii.directory());
    item->setData(gameLabel, GameNameRole);
    item->setData(ii.isActive(), CurrentSetupRole);
    item->setData(ii.displayName(), SetupNameRole);
    item->setAccessibleText(ii.displayName());
    QString accessibleDescription = ii.displayName();
    if (!gameLabel.isEmpty()) accessibleDescription += QStringLiteral(", ") + gameLabel;
    if (ii.isActive()) accessibleDescription += QStringLiteral(", ") + tr("current setup");
    item->setAccessibleDescription(accessibleDescription);
    QFont font = item->font();
    font.setBold(ii.isActive());
    item->setFont(font);

    m_model->appendRow(item);

    if (&ii == prevSel) {
      sel = i;
    }
  }

  // keep current selection or select the next one if there was a selection;
  // there's no selection when opening the dialog, that's handled in the ctor
  if (prevSel) {
    if (m_instances.empty()) {
      select(NoSelection);
    } else {
      if (sel == NoSelection) {
        if (prevSelIndex >= m_instances.size()) {
          sel = m_instances.size() - 1;
        } else {
          sel = prevSelIndex;
        }
      }

      select(sel);
    }
  }
}

void LibraryView::select(std::size_t i)
{
  if (i < m_instances.size()) {
    const auto& ii = m_instances[i];
    fillData(*ii);

    const QModelIndex sourceIndex = m_filter.sourceModel()->index(static_cast<int>(i), 0);
    QModelIndex visibleIndex = m_filter.mapFromSource(sourceIndex);
    if (!visibleIndex.isValid()) {
      ui->filter->clear();
      visibleIndex = m_filter.mapFromSource(sourceIndex);
    }
    if (visibleIndex.isValid()) {
      ui->list->selectionModel()->select(visibleIndex,
                                         QItemSelectionModel::ClearAndSelect);
      ui->list->scrollTo(visibleIndex);
    }
  } else {
    clearData();
  }
}

void LibraryView::select(const QString& name)
{
  for (std::size_t i = 0; i < m_instances.size(); ++i) {
    if (m_instances[i]->displayName() == name) {
      select(i);
      return;
    }
  }

  log::error("can't select instance {}, not in list", name);
}

bool LibraryView::selectByPath(const QString& path)
{
  const QString target = QFileInfo(path).canonicalFilePath().isEmpty()
                             ? QDir(path).absolutePath()
                             : QFileInfo(path).canonicalFilePath();
  for (std::size_t i = 0; i < m_instances.size(); ++i) {
    const QString candidate = QFileInfo(m_instances[i]->directory()).canonicalFilePath().isEmpty()
                                  ? QDir(m_instances[i]->directory()).absolutePath()
                                  : QFileInfo(m_instances[i]->directory()).canonicalFilePath();
    if (candidate == target) {
      select(i);
      return true;
    }
  }
  return false;
}

void LibraryView::selectActiveInstance()
{
  const auto active = InstanceManager::singleton().currentInstance();

  if (active) {
    const QString activeDir = QDir(active->directory()).absolutePath();
    for (std::size_t i = 0; i < m_instances.size(); ++i) {
      if (QDir(m_instances[i]->directory()).absolutePath() == activeDir) {
        select(i);

        ui->list->scrollTo(m_filter.mapFromSource(
            m_filter.sourceModel()->index(static_cast<int>(i), 0)));

        return;
      }
    }
  }

  select(0);
}

void LibraryView::openSelectedInstance()
{
  const auto i = singleSelectionIndex();
  if (i == NoSelection) {
    return;
  }

  const auto& to = *m_instances[i];

  // Returning to the running setup does not change selection or restart the app.
  if (m_restartOnSelect && to.isActive()) {
    emit returnToSetupRequested();
    return;
  }

  if (!confirmSwitch(to)) {
    return;
  }

  if (to.isPortable()) {
    // Store the actual directory for portable instances so we can distinguish
    // between the default portable path and user-selected portable locations.
    // An empty string means "use default portable path".
    auto& m = InstanceManager::singleton();
    if (to.directory() == InstanceManager::portablePath()) {
      InstanceManager::setCurrentInstance("");
    } else {
      InstanceManager::setCurrentInstance(to.directory());
    }
  } else {
    InstanceManager::singleton().setCurrentInstance(to.displayName());
  }

  if (m_restartOnSelect) {
    ExitModOrganizer(Exit::Restart);
  }

  emit setupSelectionAccepted();
}

bool LibraryView::confirmSwitch(const Instance& to)
{
  // there might not be a global Settings object if this is called on startup
  // when there's no current instance
  const auto* s = Settings::maybeInstance();

  // if there is are no settings, no instances are loaded and the confirmation
  // wouldn't make sense
  if (!s) {
    return true;
  }

  if (!s->interface().showChangeGameConfirmation()) {
    // user disabled confirmation
    return true;
  }

  MOBase::TaskDialog dlg(this);

  const auto r = dlg.title(tr("Open setup"))
                     .main(tr("Fluorine Manager must restart to open the setup '%1'.")
                               .arg(to.displayName()))
                     .content(tr("This confirmation can be disabled in the settings."))
                     .icon(QMessageBox::Question)
                     .button({tr("Restart Fluorine Manager"), QMessageBox::Ok})
                     .button({tr("Cancel"), QMessageBox::Cancel})
                     .exec();

  return (r == QMessageBox::Ok);
}

void LibraryView::rename()
{
  const auto* i = singleSelection();
  if (!i) {
    return;
  }

  const auto selIndex = singleSelectionIndex();

  if (i->isActive()) {
    QMessageBox::information(this, tr("Rename setup"),
                             tr("Open another setup before renaming this one."));
    return;
  }

  // getting new name
  const auto newName = getInstanceName(this, tr("Rename setup"), "",
                                       tr("Setup name"), i->displayName());

  if (newName.isEmpty()) {
    return;
  }

  // renaming
  const QString src      = i->directory();
  const bool wasPortable = i->isPortable();
  const QString dest =
      QDir::toNativeSeparators(QFileInfo(src).dir().path() + "/" + newName);

  log::info("renaming {} to {}", src, dest);

  const auto r = shell::Rename(QFileInfo(src), QFileInfo(dest), false);

  if (!r) {
    QMessageBox::critical(this, tr("Error"),
                          tr(R"(Failed to rename "%1" to "%2": %3)")
                              .arg(src)
                              .arg(dest)
                              .arg(r.toString()));

    return;
  }

  // portable instances are tracked by absolute path in GlobalSettings; update
  // the registry so the renamed dir keeps showing up in the list.
  if (wasPortable) {
    InstanceManager::unregisterPortableInstance(src);
    InstanceManager::registerPortableInstance(dest);
  }

  // updating ui
  auto newInstance = std::make_unique<Instance>(dest, wasPortable);
  newInstance->readFromIni();
  i                = newInstance.get();

  auto* item = m_model->item(static_cast<int>(selIndex));
  const QString gameLabel = instanceGameLabel(*i);
  QString filterText = i->displayName();
  if (i->isActive()) filterText += QStringLiteral("\n") + tr("Current setup");
  if (!gameLabel.isEmpty()) filterText += QStringLiteral("\n") + gameLabel;
  item->setText(filterText);
  item->setIcon(instanceIcon(m_pc, *i));
  item->setToolTip(i->displayName() + "\n" + i->gameName() + "\n" + i->directory());
  item->setData(gameLabel, GameNameRole);
  item->setData(i->isActive(), CurrentSetupRole);
  item->setData(i->displayName(), SetupNameRole);
  item->setAccessibleText(i->displayName());
  QString accessibleDescription = i->displayName();
  if (!gameLabel.isEmpty()) accessibleDescription += QStringLiteral(", ") + gameLabel;
  if (i->isActive()) accessibleDescription += QStringLiteral(", ") + tr("current setup");
  item->setAccessibleDescription(accessibleDescription);
  QFont font = item->font();
  font.setBold(i->isActive());
  item->setFont(font);
  m_instances[selIndex] = std::move(newInstance);

  fillData(*i);
}

void LibraryView::exploreLocation()
{
  if (const auto* i = singleSelection()) {
    shell::Explore(i->directory());
  }
}

void LibraryView::exploreBaseDirectory()
{
  if (const auto* i = singleSelection()) {
    shell::Explore(i->baseDirectory());
  }
}

void LibraryView::exploreGame()
{
  if (const auto* i = singleSelection()) {
    shell::Explore(i->gameDirectory());
  }
}

void LibraryView::openINI()
{
  if (const auto* i = singleSelection()) {
    shell::Open(i->iniPath());
  }
}

void LibraryView::removeFromList()
{
  const auto* i = singleSelection();
  if (!i) {
    return;
  }

  if (i->isActive()) {
    QMessageBox::information(this, tr("Remove from library"),
                             tr("The active instance cannot be removed."));
    return;
  }

  const auto r = QMessageBox::question(
      this, tr("Remove from library"),
      tr("Remove \"%1\" from your library?\n\n"
         "No files will be deleted.")
          .arg(i->displayName()),
      QMessageBox::Yes | QMessageBox::Cancel);

  if (r != QMessageBox::Yes) {
    return;
  }

  if (i->isPortable()) {
    InstanceManager::unregisterPortableInstance(i->directory());
  } else {
    // for global instances, rename the INI so it's no longer auto-discovered
    const QString ini = i->iniPath();
    if (!ini.isEmpty() && QFile::exists(ini)) {
      QFile::rename(ini, ini + ".disabled");
    }
  }

  refresh();
}

void LibraryView::deleteInstance()
{
  const auto* i = singleSelection();
  if (!i) {
    return;
  }

  if (i->isActive()) {
    QMessageBox::information(this, tr("Deleting instance"),
                             tr("The active instance cannot be deleted."));
    return;
  }

  // creating dialog

  const auto Delete  = QMessageBox::Yes;
  const auto Cancel  = QMessageBox::Cancel;

  const auto files = i->objectsForDeletion();

  MOBase::TaskDialog dlg(this);

  dlg.title(tr("Deleting instance"))
      .main(tr("These files and folders will be permanently deleted"))
      .content(tr("All checked items will be deleted."))
      .icon(QMessageBox::Warning)
      .button({tr("Delete permanently"), Delete})
      .button({tr("Cancel"), Cancel});

  auto* list = new QListWidget();
  list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  list->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  list->setMaximumHeight(160);

  // filling the list
  for (const auto& f : files) {
    auto* item = new QListWidgetItem(f.path);

    if (f.mandatoryDelete) {
      // disable, cannot uncheck mandatory items
      item->setFlags(item->flags() & (~Qt::ItemIsEnabled));

      // checked by default
      item->setCheckState(Qt::Checked);
    } else {
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);

      // unchecked by default
      item->setCheckState(Qt::Unchecked);
    }

    list->addItem(item);
  }

  dlg.addContent(list);
  dlg.setWidth(600);

  const auto r = dlg.exec();

  if (r != Delete) {
    return;
  }

  // gathering all the selected items
  QStringList selected;

  for (int i = 0; i < list->count(); ++i) {
    if (list->item(i)->checkState() == Qt::Checked) {
      selected.append(list->item(i)->text());
    }
  }

  if (selected.isEmpty()) {
    QMessageBox::information(this, tr("Deleting instance"), tr("Nothing to delete."));

    return;
  }

  // deleting
  if (!doDelete(selected, false)) {
    return;
  }

  // unregister portable instance from the persistent list
  if (i->isPortable()) {
    InstanceManager::singleton().unregisterPortableInstance(i->directory());
  }

  // updating ui
  refresh();
}

void LibraryView::setRestartOnSelect(bool b)
{
  m_restartOnSelect = b;
  if (const auto* setup = singleSelection()) {
    fillData(*setup);
  }
  if (m_model) {
    updateList();
  }
}

bool LibraryView::doDelete(const QStringList& files, bool recycle)
{
  // logging
  for (auto&& f : files) {
    if (recycle) {
      log::info("will recycle {}", f);
    } else {
      log::info("will delete {}", f);
    }
  }

  if (MOBase::shellDelete(files, recycle, this)) {
    return true;
  }

  const auto e = errno;
  if (e == ECANCELED) {
    log::debug("deletion cancelled by user");
  } else {
    log::error("failed to delete, {}", nativeErrorString(e));
  }

  return false;
}

void LibraryView::convertToGlobal()
{
  // not implemented
}

void LibraryView::convertToPortable()
{
  // not implemented
}

void LibraryView::onSelection()
{
  const auto i = singleSelectionIndex();
  if (i == NoSelection) {
    clearData();
    return;
  }

  select(i);
}

void LibraryView::createNew()
{
  // there might not be settings available; the dialog can be shown when the
  // last selected instance doesn't exist anymore
  CreateInstanceDialog dlg(m_pc, Settings::maybeInstance(), this);

  if (dlg.exec() != QDialog::Accepted) {
    return;
  }

  if (dlg.switching()) {
    // The wizard has already recorded the new setup and scheduled the restart.
    emit setupSelectionAccepted();
    return;
  }

  refresh();
  select(dlg.creationInfo().instanceName);
}

std::size_t LibraryView::singleSelectionIndex() const
{
  const auto sel =
      m_filter.mapSelectionToSource(ui->list->selectionModel()->selection());

  if (sel.size() != 1) {
    return NoSelection;
  }

  const auto indexes = sel.indexes();
  if (indexes.size() != 1 || !indexes[0].isValid()) {
    return NoSelection;
  }

  const int row = indexes[0].row();
  if (row < 0 || static_cast<std::size_t>(row) >= m_instances.size()) {
    return NoSelection;
  }

  return static_cast<std::size_t>(row);
}

const Instance* LibraryView::singleSelection() const
{
  const auto i = singleSelectionIndex();
  if (i == NoSelection || i >= m_instances.size()) {
    return nullptr;
  }

  return m_instances[i].get();
}

void LibraryView::fillData(const Instance& ii)
{
  LibrarySetupInfo setup;
  setup.name = ii.displayName();
  setup.gameName = ii.gameName();
  setup.setupPath = ii.directory();
  setup.dataPath = ii.baseDirectory();
  setup.gamePath = ii.gameDirectory();
  setup.current = m_restartOnSelect && ii.isActive();
  setup.icon = instanceIcon(m_pc, ii);
  if (auto* game = InstanceManager::singleton().gamePluginForDirectory(ii.directory(), m_pc)) {
    setup.gameShortName = game->gameShortName();
    setup.steamId = game->steamAPPId();
  }
  const QString ini = ii.iniPath();
  if (!ini.isEmpty() && QFile::exists(ini)) {
    QSettings const settings(ini, QSettings::IniFormat);
    setup.steamDrm =
        ProtonLauncher::steamDrmForInstance(ini, ii.gameDirectory()).enabled;
    setup.rootBuilder = settings.value("fluorine/vfs_root_builder", true).toBool();
  }
  ui->details->setSetup(setup);
  setButtonsEnabled(true);
  ui->switchToInstance->setText(setup.current ? tr("Return to setup") : tr("Open setup"));
  ui->switchToInstance->setAccessibleName(
      setup.current ? tr("Return to the active setup") : tr("Open selected setup"));
  ui->switchToInstance->setToolTip(
      setup.current
          ? tr("Return to the active setup without restarting.")
          : (m_restartOnSelect
                 ? tr("Open this setup. Fluorine Manager will restart to activate it.")
                 : tr("Open this setup as the initial setup.")));
  ui->removeFromList->setEnabled(!ii.isActive());
  ui->deleteInstance->setEnabled(!ii.isActive());

  if (ii.isPortable()) {
    ui->convertToPortable->setVisible(false);
    ui->convertToGlobal->setVisible(true);
    ui->convertToGlobal->setEnabled(true);
  } else {
    ui->convertToPortable->setVisible(true);
    ui->convertToGlobal->setVisible(false);

    if (InstanceManager::portableInstanceExists()) {
      ui->convertToPortable->setEnabled(false);
      ui->convertToPortable->setToolTip(tr("A portable instance already exists."));
    } else {
      ui->convertToPortable->setEnabled(false);
      ui->convertToPortable->setToolTip("");
    }
  }

  // not implemented, hide the buttons
  ui->convertToPortable->setVisible(false);
  ui->convertToGlobal->setVisible(false);
}

void LibraryView::clearData()
{
  ui->details->clear();
  ui->switchToInstance->setText(tr("Open setup"));
  ui->switchToInstance->setAccessibleName(tr("Open selected setup"));
  ui->switchToInstance->setToolTip(tr("Select a setup to open it."));

  setButtonsEnabled(false);

  ui->convertToPortable->setVisible(false);
  ui->convertToGlobal->setVisible(false);
}

void LibraryView::setButtonsEnabled(bool b)
{
  ui->details->moreActions()->setEnabled(b);
  ui->openINI->setEnabled(b);
  ui->convertToPortable->setEnabled(b);
  ui->convertToGlobal->setEnabled(b);
  ui->removeFromList->setEnabled(b);
  ui->deleteInstance->setEnabled(b);
  ui->switchToInstance->setEnabled(b);
}

void LibraryView::openExistingPortable()
{
  // On Flatpak, the native file dialog goes through the XDG Desktop Portal,
  const QString dir = QFileDialog::getExistingDirectory(
      this, tr("Select existing setup folder"),
      QStandardPaths::writableLocation(QStandardPaths::HomeLocation));

  if (dir.isEmpty()) {
    return;
  }

  const QString ini = QDir(dir).filePath("ModOrganizer.ini");
  if (!QFileInfo::exists(ini)) {
    QMessageBox::warning(
        this, tr("Setup not found"),
        tr("The selected folder does not contain a ModOrganizer.ini file."));
    return;
  }

  // Register the portable instance so it persists in the sidebar
  InstanceManager::registerPortableInstance(dir);

  // Refresh the instance list and select the newly added entry
  refresh();
  selectByPath(dir);
}

void LibraryView::installWabbajack()
{
  openModlistInstaller(false);
}

void LibraryView::updatePendingInstallation()
{
  auto settings = Clf3InstallUtils::openSettings();
  settings->beginGroup(QStringLiteral("clf3/pending"));
  const bool pending = !settings->value(QStringLiteral("source")).toString().isEmpty();
  ui->pendingInstall->setVisible(pending);
  if (!pending) return;
  QString title = settings->value(QStringLiteral("listTitle")).toString();
  if (title.isEmpty()) title = settings->value(QStringLiteral("instanceName")).toString();
  if (title.isEmpty()) title = tr("Modlist");
  const bool setupOnly = settings->value(QStringLiteral("stage")).toString()
                         == QStringLiteral("post-install");
  ui->pendingSummary->setText(
      setupOnly ? tr("%1 · Compatibility setup unfinished\nInstalled files are retained.").arg(title)
                : tr("%1 · Installation unfinished\nResume using the saved folders and downloads.").arg(title));
  ui->pendingSummary->setToolTip(settings->value(QStringLiteral("output")).toString());
}

void LibraryView::openModlistInstaller(bool resumePending)
{
  auto* dialog = new Clf3InstallerDialog(this, resumePending);
  const int result = dialog->exec();
  refresh();
  if (result == QDialog::Accepted && !dialog->createdInstanceDir().isEmpty()) {
    selectByPath(dialog->createdInstanceDir());
    if (dialog->shouldSwitchToInstance()) openSelectedInstance();
  }
  dialog->deleteLater();
}

void LibraryView::downloadSLRIfNeeded()
{
  if (isSlrInstalled()) {
    return;
  }

  auto* progress = new QProgressDialog(
      tr("Downloading Steam Linux Runtime (~200 MB)...\n"
         "This only happens once. Check the MO2 log for details."),
      tr("Cancel"), 0, 0, this); // 0,0 = indeterminate
  progress->setWindowTitle(tr("Steam Linux Runtime"));
  progress->setWindowModality(Qt::WindowModal);
  progress->setAttribute(Qt::WA_ShowWithoutActivating);
  progress->setMinimumDuration(0);

  auto* cancelFlag = new int(0);

  connect(progress, &QProgressDialog::canceled, this, [cancelFlag] {
    *cancelFlag = 1;
  });

  auto* watcher = new QFutureWatcher<QString>(this);

  connect(watcher, &QFutureWatcher<QString>::finished, this,
      [this, watcher, progress, cancelFlag] {
        progress->close();
        watcher->deleteLater();
        progress->deleteLater();

        const QString err = watcher->result();
        if (!err.isEmpty()) {
          MOBase::log::error("[SLR] Download failed: {}", err);
          QMessageBox::warning(this, tr("Steam Linux Runtime"),
              tr("Download failed:\n%1").arg(err));
        } else {
          MOBase::log::info("[SLR] Steam Linux Runtime installed successfully");
          progress->setLabelText(tr("Steam Linux Runtime is ready."));
        }
        delete cancelFlag;
      });

  int* cancelPtr = cancelFlag;
  watcher->setFuture(QtConcurrent::run([cancelPtr]() -> QString {
    return downloadSlr(nullptr, nullptr, cancelPtr);
  }));

  progress->show();
}
