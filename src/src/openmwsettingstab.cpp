#include "openmwsettingstab.h"

#include <algorithm>
#include <climits>

#include <QCheckBox>
#include <QComboBox>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QScreen>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

using namespace OpenMWSettings;

namespace
{
// 1 -> "1.0", 0.333 -> "0.333": what OpenMW's own files look like.
QString formatFloat(double value)
{
  QString text = QString::number(value, 'g', 9);
  if (!text.contains('.') && !text.contains('e') && !text.contains("inf") &&
      !text.contains("nan"))
    text += QStringLiteral(".0");
  return text;
}

bool sameValue(const Setting& setting, const QString& a, const QString& b)
{
  if (a == b)
    return true;
  if (setting.type == Type::Float) {
    bool okA = false, okB = false;
    const double x = a.toDouble(&okA), y = b.toDouble(&okB);
    return okA && okB && qFuzzyCompare(1.0 + x, 1.0 + y);
  }
  return false;
}

QString effectiveValue(const File& file, const Setting& setting)
{
  return file.contains(setting.section, setting.key)
             ? file.value(setting.section, setting.key)
             : setting.defaultValue;
}
}  // namespace

const QList<OpenMWSettingsTab::PageDefinition>& OpenMWSettingsTab::pages()
{
  static const QList<PageDefinition> list = {
      {tr("Display"), {"Video"}},
      {tr("Gameplay"), {"Game"}},
      {tr("Interface"), {"GUI", "HUD", "Map", "Windows"}},
      {tr("Graphics"),
       {"Shaders", "Water", "Shadows", "Terrain", "Fog", "Post Processing", "Models",
        "Groundcover", "Stereo", "Stereo View"}},
      {tr("Camera"), {"Camera"}},
      {tr("Audio"), {"Sound"}},
      {tr("Controls"), {"Input"}},
      {tr("General"), {"General", "Saves"}},
      {tr("Advanced"), {"Cells", "Navigator", "Physics", "Lua"}},
  };
  return list;
}

OpenMWSettingsTab::OpenMWSettingsTab(QWidget* parent)
    : QWidget(parent), m_schema(Schema::bundled())
{
  setObjectName("openmwSettingsTab");
  buildUi();
}

OpenMWSettingsTab::~OpenMWSettingsTab() = default;

QString OpenMWSettingsTab::settingsPathFor(bool localSettings, const QString& profileDir)
{
  if (localSettings)
    return QDir(profileDir).filePath(QStringLiteral("settings.cfg"));
  const QString xdg = qEnvironmentVariable("XDG_CONFIG_HOME");
  const QString native =
      (xdg.isEmpty() ? QDir::home().filePath(".config") : xdg) +
      QStringLiteral("/openmw/settings.cfg");
  const QString flatpak = QDir::home().filePath(
      ".var/app/org.openmw.OpenMW/config/openmw/settings.cfg");
  if (!QFileInfo::exists(native) && QFileInfo::exists(flatpak))
    return flatpak;
  return native;
}

void OpenMWSettingsTab::buildUi()
{
  auto* root = new QVBoxLayout(this);

  auto* top = new QHBoxLayout;
  m_search  = new QLineEdit;
  m_search->setPlaceholderText(tr("Search every OpenMW setting…"));
  m_search->setClearButtonEnabled(true);
  m_changedOnly = new QCheckBox(tr("Changed only"));
  m_changedOnly->setToolTip(
      tr("Show only settings that are set in this settings file (the rest use OpenMW's defaults)."));
  m_revert = new QPushButton(tr("Revert"));
  m_save   = new QPushButton(tr("Save"));
  top->addWidget(m_search, 1);
  top->addWidget(m_changedOnly);
  top->addWidget(m_revert);
  top->addWidget(m_save);
  root->addLayout(top);

  m_location = new QLabel;
  m_location->setProperty("secondary", true);
  m_location->setWordWrap(true);
  m_location->setTextInteractionFlags(Qt::TextSelectableByMouse);
  root->addWidget(m_location);

  auto* body = new QHBoxLayout;
  m_pageList = new QListWidget;
  m_pageList->setMaximumWidth(190);
  for (const auto& page : pages())
    m_pageList->addItem(page.title);
  m_pageList->addItem(tr("Other"));
  m_pageList->setCurrentRow(0);
  body->addWidget(m_pageList);

  m_scroll = new QScrollArea;
  m_scroll->setWidgetResizable(true);
  m_scroll->setFrameShape(QFrame::NoFrame);
  m_content       = new QWidget;
  m_contentLayout = new QVBoxLayout(m_content);
  m_contentLayout->setAlignment(Qt::AlignTop);
  m_scroll->setWidget(m_content);
  body->addWidget(m_scroll, 1);
  root->addLayout(body, 1);

  m_message = new QLabel;
  m_message->setProperty("secondary", true);
  m_message->setWordWrap(true);
  root->addWidget(m_message);

  connect(m_pageList, &QListWidget::currentRowChanged, this, [this] { rebuildContent(); });
  connect(m_search, &QLineEdit::textChanged, this, [this] { rebuildContent(); });
  connect(m_changedOnly, &QCheckBox::toggled, this, [this] { rebuildContent(); });
  connect(m_save, &QPushButton::clicked, this, [this] { save(); });
  connect(m_revert, &QPushButton::clicked, this, [this] {
    reload(m_file.path());
    m_message->setText(tr("Reverted to what is saved."));
  });
  updateButtons();
}

bool OpenMWSettingsTab::hasUnsavedChanges() const
{
  return m_file.isModified();
}

bool OpenMWSettingsTab::promptToSave()
{
  if (!hasUnsavedChanges())
    return true;
  const auto answer = QMessageBox::question(
      this, tr("OpenMW settings"),
      tr("Save your changes to the OpenMW settings?"),
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
  if (answer == QMessageBox::Cancel)
    return false;
  if (answer == QMessageBox::Save)
    return save();
  return true;
}

bool OpenMWSettingsTab::setSettingsFile(const QString& path, const QString& description)
{
  if (m_file.path() != path && !promptToSave())
    return false;
  const bool changed = m_file.path() != path;
  m_description      = description;
  // Keep unsaved edits when the same file is shown again.
  if (changed || !m_file.isModified())
    reload(path);
  return true;
}

void OpenMWSettingsTab::reload(const QString& path)
{
  QString error;
  if (!m_file.load(path, &error))
    m_message->setText(tr("Cannot read the settings file: %1").arg(error));
  m_location->setText(QStringLiteral("%1\n%2").arg(m_description, m_file.path()));

  // Keys the schema does not know (newer OpenMW, other tools) stay editable.
  m_unknown.clear();
  for (const auto& entry : m_file.entries()) {
    if (m_schema.find(entry.first, entry.second))
      continue;
    Setting extra;
    extra.section      = entry.first;
    extra.key          = entry.second;
    extra.defaultValue = QString();
    extra.type         = Type::Text;
    extra.comment      = tr("Not in OpenMW's documented defaults.");
    m_unknown.append(extra);
  }
  rebuildContent();
  updateButtons();
}

QList<const Setting*> OpenMWSettingsTab::visibleSettings() const
{
  const QString needle = m_search->text().trimmed();
  const bool changedOnly = m_changedOnly->isChecked();
  const int row = m_pageList->currentRow();
  QStringList sections;
  bool other = false;
  if (needle.isEmpty() && row >= 0 && row < pages().size())
    sections = pages()[row].sections;
  else if (needle.isEmpty())
    other = true;

  QStringList known;
  for (const auto& page : pages())
    known << page.sections;

  QList<const Setting*> all;
  for (const auto& s : m_schema.settings())
    all.append(&s);
  for (const auto& s : m_unknown)
    all.append(&s);

  QList<const Setting*> out;
  for (const Setting* s : all) {
    if (needle.isEmpty()) {
      const bool inOther = !known.contains(s->section);
      if (other ? !inOther : !sections.contains(s->section))
        continue;
    } else if (!s->label().contains(needle, Qt::CaseInsensitive) &&
               !s->comment.contains(needle, Qt::CaseInsensitive) &&
               !s->section.contains(needle, Qt::CaseInsensitive)) {
      continue;
    }
    if (changedOnly && !m_file.contains(s->section, s->key))
      continue;
    out.append(s);
  }
  return out;
}

void OpenMWSettingsTab::rebuildContent()
{
  if (!m_contentLayout)
    return;
  m_rebuilding = true;
  while (auto* item = m_contentLayout->takeAt(0)) {
    if (auto* widget = item->widget()) {
      // Gone from the screen immediately; freed once control returns to Qt.
      widget->hide();
      widget->setParent(nullptr);
      widget->deleteLater();
    }
    delete item;
  }
  const auto settings = visibleSettings();
  const int pageRow   = m_pageList ? m_pageList->currentRow() : -1;
  if (m_search->text().trimmed().isEmpty() && !m_changedOnly->isChecked() && pageRow >= 0 &&
      pageRow < pages().size() && pages()[pageRow].title == tr("Advanced"))
    m_contentLayout->addWidget(makeNavmeshPanel());
  QString lastSection;
  QGroupBox* box        = nullptr;
  QVBoxLayout* boxLayout = nullptr;
  int shown              = 0;
  // Limit the page size so a broad search stays responsive.
  constexpr int kMaxRows = 250;
  for (const Setting* s : settings) {
    if (shown >= kMaxRows)
      break;
    if (s->section != lastSection || !box) {
      box       = new QGroupBox(s->section);
      boxLayout = new QVBoxLayout(box);
      m_contentLayout->addWidget(box);
      lastSection = s->section;
    }
    if (s->section == QLatin1String("Video") && s->key == QLatin1String("resolution x") &&
        m_search->text().trimmed().isEmpty())
      boxLayout->addWidget(makeResolutionRow(box));
    boxLayout->addWidget(makeRow(*s, box));
    ++shown;
  }
  if (settings.isEmpty()) {
    auto* empty = new QLabel(m_changedOnly->isChecked()
                                 ? tr("No setting here is changed from OpenMW's defaults.")
                                 : tr("No settings match."));
    empty->setProperty("secondary", true);
    m_contentLayout->addWidget(empty);
  } else if (settings.size() > kMaxRows) {
    auto* more = new QLabel(tr("Showing the first %1 of %2 matches. Search for more specific text.")
                                .arg(kMaxRows)
                                .arg(settings.size()));
    more->setProperty("secondary", true);
    m_contentLayout->addWidget(more);
  }
  m_rebuilding = false;
  updatePageCounts();
  fitNotes();
}

void OpenMWSettingsTab::fitNotes()
{
  if (!m_scroll || !m_content)
    return;
  const int width = qMax(240, m_scroll->viewport()->width() - 56);
  for (auto* note : m_content->findChildren<QLabel*>(QStringLiteral("settingNote")))
    note->setMinimumHeight(note->heightForWidth(width));
}

void OpenMWSettingsTab::resizeEvent(QResizeEvent* event)
{
  QWidget::resizeEvent(event);
  fitNotes();
}

void OpenMWSettingsTab::showEvent(QShowEvent* event)
{
  QWidget::showEvent(event);
  fitNotes();
}

QWidget* OpenMWSettingsTab::makeRow(const Setting& setting, QWidget* parent)
{
  // Plain box layouts: a grid holding word-wrapped labels misreports its
  // height and squashes long pages.
  auto* row   = new QWidget(parent);
  auto* outer = new QVBoxLayout(row);
  outer->setContentsMargins(0, 2, 0, 2);
  outer->setSpacing(2);
  auto* line = new QHBoxLayout;
  outer->addLayout(line);

  auto* title = new QLabel(setting.label());
  title->setToolTip(setting.comment);
  line->addWidget(title, 3);

  const QString current = effectiveValue(m_file, setting);
  const QString id      = setting.id();
  QWidget* control      = nullptr;

  // Writes a new value, or forgets the key when it equals the default.
  auto commit = [this, &setting](const QString& value) {
    if (m_rebuilding)
      return;
    if (sameValue(setting, value, setting.defaultValue) &&
        !m_file.contains(setting.section, setting.key))
      return;
    m_file.setValue(setting.section, setting.key, value);
    updateButtons();
    updatePageCounts();
  };

  if (!setting.choices.isEmpty()) {
    auto* combo = new QComboBox;
    int index   = -1;
    for (int i = 0; i < setting.choices.size(); ++i) {
      combo->addItem(setting.choices[i].label, setting.choices[i].value);
      if (setting.choices[i].value == current)
        index = i;
    }
    if (index < 0) {  // a value outside the documented list stays selectable
      combo->addItem(current, current);
      index = combo->count() - 1;
    }
    combo->setCurrentIndex(index);
    connect(combo, &QComboBox::activated, this, [combo, commit](int i) {
      commit(combo->itemData(i).toString());
    });
    control = combo;
  } else if (setting.type == Type::Bool) {
    auto* box = new QCheckBox;
    box->setChecked(current == QLatin1String("true"));
    connect(box, &QCheckBox::toggled, this, [commit](bool on) {
      commit(on ? QStringLiteral("true") : QStringLiteral("false"));
    });
    control = box;
  } else if (setting.type == Type::Int) {
    auto* spin = new QSpinBox;
    spin->setRange(INT_MIN, INT_MAX);
    spin->setValue(current.toInt());
    connect(spin, &QSpinBox::editingFinished, this, [spin, commit] {
      commit(QString::number(spin->value()));
    });
    control = spin;
  } else if (setting.type == Type::Float) {
    auto* spin = new QDoubleSpinBox;
    spin->setRange(-1e9, 1e9);
    spin->setDecimals(4);
    spin->setValue(current.toDouble());
    connect(spin, &QDoubleSpinBox::editingFinished, this, [spin, commit] {
      commit(formatFloat(spin->value()));
    });
    control = spin;
  } else {
    auto* edit = new QLineEdit(current);
    connect(edit, &QLineEdit::editingFinished, this, [edit, commit] {
      commit(edit->text().trimmed());
    });
    control = edit;
  }
  control->setToolTip(setting.comment);
  line->addWidget(control, 2);

  auto* reset = new QToolButton;
  reset->setText(QStringLiteral("↺"));
  reset->setToolTip(tr("Use OpenMW's default (%1)").arg(
      setting.defaultValue.isEmpty() ? tr("empty") : setting.defaultValue));
  reset->setEnabled(m_file.contains(setting.section, setting.key));
  line->addWidget(reset);
  connect(reset, &QToolButton::clicked, this, [this, &setting] {
    m_file.remove(setting.section, setting.key);
    updateButtons();
    rebuildContent();
  });

  if (!setting.comment.isEmpty()) {
    auto* note = new QLabel(setting.comment);
    note->setObjectName(QStringLiteral("settingNote"));
    note->setWordWrap(true);
    note->setProperty("secondary", true);
    outer->addWidget(note);
  }
  if (m_file.contains(setting.section, setting.key)) {
    QFont font = title->font();
    font.setBold(true);
    title->setFont(font);
  }
  return row;
}

QWidget* OpenMWSettingsTab::makeNavmeshPanel()
{
  auto* box    = new QGroupBox(tr("Navigation meshes"));
  box->setObjectName(QStringLiteral("navmeshPanel"));
  auto* layout = new QVBoxLayout(box);
  auto* note   = new QLabel(
      tr("OpenMW's navigation meshes let creatures and followers find their way around. "
         "Building them takes a while the first time, and they should be rebuilt after the "
         "mod list changes. They are saved with this profile."));
  note->setObjectName(QStringLiteral("settingNote"));
  note->setWordWrap(true);
  note->setProperty("secondary", true);
  layout->addWidget(note);
  auto* line   = new QHBoxLayout;
  auto* button = new QPushButton(tr("Build navigation meshes"));
  button->setObjectName(QStringLiteral("buildNavmeshButton"));
  button->setEnabled(m_navmeshAvailable);
  line->addWidget(button);
  line->addStretch(1);
  layout->addLayout(line);
  if (!m_navmeshAvailable) {
    auto* missing = new QLabel(
        tr("This instance has no bundled OpenMW. Install the list with CLF3 (clf3 momw install) "
           "to get one, or run openmw-navmeshtool yourself."));
    missing->setObjectName(QStringLiteral("settingNote"));
    missing->setWordWrap(true);
    missing->setProperty("secondary", true);
    layout->addWidget(missing);
  }
  connect(button, &QPushButton::clicked, this, [this] {
    // Settings are read when the tool starts, so save them first.
    if (promptToSave())
      Q_EMIT navmeshRequested();
  });
  return box;
}

void OpenMWSettingsTab::setNavmeshAvailable(bool available)
{
  if (m_navmeshAvailable == available)
    return;
  m_navmeshAvailable = available;
  rebuildContent();
}

QWidget* OpenMWSettingsTab::makeResolutionRow(QWidget* parent)
{
  const auto valueOf = [this](const char* key, const char* fallback) {
    const QString v = m_file.contains(QStringLiteral("Video"), QLatin1String(key))
                          ? m_file.value(QStringLiteral("Video"), QLatin1String(key))
                          : QString::fromLatin1(fallback);
    return v.toInt();
  };
  const QSize current(valueOf("resolution x", "800"), valueOf("resolution y", "600"));

  // This machine's screens first, then common sizes that fit the largest one.
  QList<QSize> sizes;
  QSize largest(0, 0);
  for (const QScreen* screen : QGuiApplication::screens()) {
    const QSize native = screen->size() * screen->devicePixelRatio();
    if (!sizes.contains(native))
      sizes.append(native);
    if (native.width() * native.height() > largest.width() * largest.height())
      largest = native;
  }
  for (const QSize common : {QSize(1280, 720), QSize(1366, 768), QSize(1600, 900),
                             QSize(1920, 1080), QSize(2560, 1080), QSize(2560, 1440),
                             QSize(3440, 1440), QSize(3840, 2160)}) {
    if ((largest.isEmpty() || (common.width() <= largest.width() &&
                               common.height() <= largest.height())) &&
        !sizes.contains(common))
      sizes.append(common);
  }
  if (!sizes.contains(current))
    sizes.append(current);
  std::sort(sizes.begin(), sizes.end(), [](const QSize& a, const QSize& b) {
    return a.width() != b.width() ? a.width() < b.width() : a.height() < b.height();
  });

  auto* row  = new QWidget(parent);
  auto* line = new QHBoxLayout(row);
  line->setContentsMargins(0, 2, 0, 2);
  auto* title = new QLabel(tr("Resolution"));
  title->setToolTip(tr("Pick a size for the OpenMW window or screen. This sets the two "
                       "resolution values below."));
  line->addWidget(title, 3);
  auto* combo = new QComboBox;
  combo->setObjectName(QStringLiteral("resolutionPicker"));
  for (const QSize& size : sizes)
    combo->addItem(QStringLiteral("%1 × %2").arg(size.width()).arg(size.height()), size);
  combo->setCurrentIndex(int(sizes.indexOf(current)));
  combo->setToolTip(title->toolTip());
  line->addWidget(combo, 2);
  connect(combo, &QComboBox::activated, this, [this, combo](int i) {
    const QSize size = combo->itemData(i).toSize();
    m_file.setValue(QStringLiteral("Video"), QStringLiteral("resolution x"),
                    QString::number(size.width()));
    m_file.setValue(QStringLiteral("Video"), QStringLiteral("resolution y"),
                    QString::number(size.height()));
    updateButtons();
    // Refresh the two number fields below; deferred so the combo can finish.
    QMetaObject::invokeMethod(this, [this] { rebuildContent(); }, Qt::QueuedConnection);
  });
  return row;
}

void OpenMWSettingsTab::updatePageCounts()
{
  // Show how many settings of each page are set in the file.
  QStringList known;
  for (const auto& page : pages())
    known << page.sections;
  const auto entries = m_file.entries();
  for (int i = 0; i < m_pageList->count(); ++i) {
    int count = 0;
    for (const auto& entry : entries) {
      const bool inPage =
          i < pages().size() ? pages()[i].sections.contains(entry.first) : !known.contains(entry.first);
      if (inPage)
        ++count;
    }
    const QString base = i < pages().size() ? pages()[i].title : tr("Other");
    m_pageList->item(i)->setText(count ? QStringLiteral("%1  (%2)").arg(base).arg(count) : base);
  }
}

void OpenMWSettingsTab::updateButtons()
{
  const bool dirty = m_file.isModified();
  m_save->setEnabled(dirty);
  m_revert->setEnabled(dirty);
  if (dirty)
    m_message->setText(tr("You have unsaved changes. OpenMW reads them the next time it starts."));
}

bool OpenMWSettingsTab::save()
{
  QString error;
  if (!m_file.save(&error)) {
    m_message->setText(tr("Could not save: %1").arg(error));
    return false;
  }
  m_message->setText(tr("Saved. OpenMW uses the new settings the next time it starts."));
  updateButtons();
  return true;
}

OpenMWSettingsDialog::OpenMWSettingsDialog(QWidget* parent) : QDialog(parent)
{
  setObjectName("openmwSettingsDialog");
  setWindowTitle(tr("OpenMW Settings"));
  resize(1080, 760);
  auto* layout = new QVBoxLayout(this);
  m_tab        = new OpenMWSettingsTab;
  layout->addWidget(m_tab, 1);
  connect(m_tab, &OpenMWSettingsTab::navmeshRequested, this,
          &OpenMWSettingsDialog::navmeshRequested);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
}

bool OpenMWSettingsDialog::setSettingsFile(const QString& path, const QString& description)
{
  return m_tab->setSettingsFile(path, description);
}

void OpenMWSettingsDialog::closeEvent(QCloseEvent* event)
{
  if (!m_tab->promptToSave()) {
    event->ignore();
    return;
  }
  QDialog::closeEvent(event);
}
