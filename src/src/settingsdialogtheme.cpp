#include "settingsdialogtheme.h"
#include "categoriesdialog.h"
#include "colortable.h"
#include "modlist.h"
#include "shared/appconfig.h"
#include "stylesheetpath.h"
#include "ui_settingsdialog.h"

#include <QFontDatabase>
#include <QStyleFactory>

#include <questionboxmemory.h>
#include <utility.h>

using namespace MOBase;

ThemeSettingsTab::ThemeSettingsTab(Settings& s, SettingsDialog& d) : SettingsTab(s, d)
{
  // style
  addStyles();
  selectStyle();
  selectQssFontSize();
  updateDefaultFontSizeHint();
  populateFontFamilies();
  selectFontFamily();

  QObject::connect(ui->styleBox, &QComboBox::currentIndexChanged, [&] {
    updateDefaultFontSizeHint();
  });

  // colors
  ui->colorTable->load(s);

  QObject::connect(ui->resetColorsBtn, &QPushButton::clicked, [&] {
    ui->colorTable->resetColors();
  });

  QObject::connect(ui->exploreStyles, &QPushButton::clicked, [&] {
    onExploreStyles();
  });
}

void ThemeSettingsTab::update()
{
  // style
  const QString oldStyle = settings().interface().styleName().value_or("");
  const QString newStyle =
      ui->styleBox->itemData(ui->styleBox->currentIndex()).toString();
  const int oldQssFontSize = settings().interface().qssFontSize();
  const int newQssFontSize = ui->qssFontSizeSpinBox->value();
  const QString oldFontFamily = settings().interface().fontFamily();
  const QString newFontFamily =
      ui->fontFamilyCombo->itemData(ui->fontFamilyCombo->currentIndex()).toString();

  if (oldStyle != newStyle) {
    settings().interface().setStyleName(newStyle);
  }

  if (oldQssFontSize != newQssFontSize) {
    settings().interface().setQssFontSize(newQssFontSize);
  }

  if (oldFontFamily != newFontFamily) {
    settings().interface().setFontFamily(newFontFamily);
  }

  if (oldStyle != newStyle || oldQssFontSize != newQssFontSize ||
      oldFontFamily != newFontFamily) {
    emit settings().styleChanged(newStyle);
  }

  // colors
  ui->colorTable->commitColors();
}

void ThemeSettingsTab::addStyles()
{
  ui->styleBox->addItem("None", "");
  for (auto&& key : QStyleFactory::keys()) {
    ui->styleBox->addItem(key, key);
  }

  ui->styleBox->insertSeparator(ui->styleBox->count());

  QString instanceDirectory;
  if (qApp->property("fluorinePortableInstance").toBool()) {
    instanceDirectory = qApp->property("dataPath").toString();
  }
  const auto directories = StyleSheetPath::searchDirectories(
      QCoreApplication::applicationDirPath(), instanceDirectory);
  for (const QString& name : StyleSheetPath::available(directories)) {
    ui->styleBox->addItem(QFileInfo(name).completeBaseName(), name);
  }
}

void ThemeSettingsTab::selectStyle()
{
  const int currentID =
      ui->styleBox->findData(settings().interface().styleName().value_or(""));

  if (currentID != -1) {
    ui->styleBox->setCurrentIndex(currentID);
  }
}

void ThemeSettingsTab::selectQssFontSize()
{
  ui->qssFontSizeSpinBox->setValue(settings().interface().qssFontSize());
}

void ThemeSettingsTab::updateDefaultFontSizeHint()
{
  const QString styleName =
      ui->styleBox->itemData(ui->styleBox->currentIndex()).toString();
  const bool isStylesheet =
      !styleName.isEmpty() &&
      QStyleFactory::keys().indexOf(styleName, 0, Qt::CaseSensitive) < 0;

  if (isStylesheet) {
    ui->qssFontSizeSpinBox->setSpecialValueText(
        QStringLiteral("Theme default"));
    return;
  }

  ui->qssFontSizeSpinBox->setSpecialValueText(
      QStringLiteral("Application default"));
}

void ThemeSettingsTab::populateFontFamilies()
{
  ui->fontFamilyCombo->addItem("Default (DejaVu Sans)", QString());

  QStringList families = QFontDatabase::families();
  families.sort();
  for (const QString& family : families) {
    ui->fontFamilyCombo->addItem(family, family);
  }
}

void ThemeSettingsTab::selectFontFamily()
{
  const QString family = settings().interface().fontFamily();
  const int idx = ui->fontFamilyCombo->findData(family);
  if (idx != -1) {
    ui->fontFamilyCombo->setCurrentIndex(idx);
  }
}

void ThemeSettingsTab::onExploreStyles()
{
  QString root = QCoreApplication::applicationDirPath();
  if (qApp->property("fluorinePortableInstance").toBool()) {
    root = qApp->property("dataPath").toString();
  }
  const QString ssPath = QDir(root).filePath(
      QString::fromStdWString(AppConfig::stylesheetsPath()));
  QDir().mkpath(ssPath);
  shell::Explore(ssPath);
}
