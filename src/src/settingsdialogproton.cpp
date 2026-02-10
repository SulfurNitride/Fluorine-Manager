#include "settingsdialogproton.h"

#include "fluorineconfig.h"
#include "ui_settingsdialog.h"

#include <QtConcurrent/QtConcurrentRun>
#include <nak_ffi.h>
#include <atomic>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QProcess>
#include <QSettings>
#include <QScopeGuard>

namespace
{
std::atomic<ProtonSettingsTab*> g_activeInstallTab = nullptr;
}

ProtonSettingsTab::ProtonSettingsTab(Settings& s, SettingsDialog& d)
    : QObject(&d), SettingsTab(s, d)
{
  ui->protonProgressBar->setRange(0, 100);
  ui->protonProgressBar->setValue(0);
  ui->protonProgressBar->setVisible(false);

  ui->umuCheckBox->setChecked(QSettings().value("fluorine/use_umu", true).toBool());
  ui->umuSystemCheckBox->setChecked(
      QSettings().value("fluorine/prefer_system_umu", false).toBool());
  ui->steamRunCheckBox->setChecked(
      QSettings().value("fluorine/use_steam_run", false).toBool());

  ui->launchWrapperEdit->setPlaceholderText("mangohud --dlsym");
  ui->launchWrapperEdit->setText(QSettings().value("fluorine/launch_wrapper").toString());

  populateProtons();

  QObject::connect(ui->protonVersionCombo, &QComboBox::currentIndexChanged, this,
                   [this](int) {
                     if (auto cfg = FluorineConfig::load();
                         cfg.has_value() && cfg->prefixExists()) {
                       const QString protonName =
                           ui->protonVersionCombo->currentText().trimmed();
                       const QString protonPath = ui->protonVersionCombo
                                                      ->currentData(Qt::UserRole + 1)
                                                      .toString()
                                                      .trimmed();

                       if (!protonName.isEmpty() && !protonPath.isEmpty() &&
                           (cfg->proton_name != protonName ||
                            cfg->proton_path != protonPath)) {
                         cfg->proton_name = protonName;
                         cfg->proton_path = protonPath;
                         cfg->save();
                       }
                     }
                   });

  QObject::connect(ui->createPrefixButton, &QPushButton::clicked, this,
                   &ProtonSettingsTab::onCreatePrefix);
  QObject::connect(ui->deletePrefixButton, &QPushButton::clicked, this,
                   &ProtonSettingsTab::onDeletePrefix);
  QObject::connect(ui->recreatePrefixButton, &QPushButton::clicked, this,
                   &ProtonSettingsTab::onRecreatePrefix);
  QObject::connect(ui->openPrefixFolderButton, &QPushButton::clicked, this,
                   &ProtonSettingsTab::onOpenPrefixFolder);

  QObject::connect(&m_installWatcher, &QFutureWatcher<InstallResult>::finished, this,
                   &ProtonSettingsTab::onInstallFinished);

  refreshState();
}

void ProtonSettingsTab::update()
{
  QSettings().setValue("fluorine/use_umu", ui->umuCheckBox->isChecked());
  QSettings().setValue("fluorine/prefer_system_umu",
                       ui->umuSystemCheckBox->isChecked());
  QSettings().setValue("fluorine/use_steam_run",
                       ui->steamRunCheckBox->isChecked());
  QSettings().setValue("fluorine/launch_wrapper", ui->launchWrapperEdit->text());
}

void ProtonSettingsTab::populateProtons()
{
  ui->protonVersionCombo->clear();

  const NakProtonList protonList = nak_find_steam_protons();

  for (size_t i = 0; i < protonList.count; ++i) {
    const NakSteamProton& proton = protonList.protons[i];

    const QString protonName = QString::fromUtf8(proton.name ? proton.name : "");
    const QString protonPath = QString::fromUtf8(proton.path ? proton.path : "");

    if (protonName.isEmpty() || protonPath.isEmpty()) {
      continue;
    }

    ui->protonVersionCombo->addItem(protonName);
    ui->protonVersionCombo->setItemData(ui->protonVersionCombo->count() - 1, protonPath,
                                        Qt::UserRole + 1);
  }

  nak_proton_list_free(protonList);

  if (auto cfg = FluorineConfig::load(); cfg.has_value()) {
    const int idx = ui->protonVersionCombo->findText(cfg->proton_name);
    if (idx >= 0) {
      ui->protonVersionCombo->setCurrentIndex(idx);
    }
  }
}

void ProtonSettingsTab::refreshState()
{
  const auto prefix = FluorineConfig::prefixPath();
  const bool active = prefix.has_value();

  if (!m_busy) {
    ui->protonStatusLabel->setText(active ? tr("Prefix Active") : tr("No Prefix"));
    ui->protonProgressBar->setVisible(false);
  }

  ui->prefixPathValueLabel->setText(active ? *prefix : tr("(none)"));

  ui->createPrefixButton->setEnabled(!m_busy && !active);
  ui->deletePrefixButton->setEnabled(!m_busy && active);
  ui->recreatePrefixButton->setEnabled(!m_busy && active);
  ui->openPrefixFolderButton->setEnabled(!m_busy && active);
  ui->protonVersionCombo->setEnabled(!m_busy);
}

void ProtonSettingsTab::setBusy(bool busy)
{
  m_busy = busy;
  ui->protonProgressBar->setVisible(busy);

  if (!busy) {
    ui->protonProgressBar->setValue(0);
  }

  refreshState();
}

void ProtonSettingsTab::onCreatePrefix()
{
  if (m_busy) {
    return;
  }

  const QString protonName = ui->protonVersionCombo->currentText().trimmed();
  const QString protonPath =
      ui->protonVersionCombo->currentData(Qt::UserRole + 1).toString().trimmed();

  if (protonName.isEmpty() || protonPath.isEmpty()) {
    ui->protonStatusLabel->setText(tr("Select a Proton version first"));
    return;
  }

  setBusy(true);
  ui->protonStatusLabel->setText(tr("Creating Steam shortcut..."));

  const QByteArray protonNameUtf8 = protonName.toUtf8();
  NakShortcutResult result         = nak_add_mod_manager_shortcut(
      "Fluorine Manager", "/usr/bin/true", "/tmp", protonNameUtf8.constData());

  const QString prefixPath =
      QString::fromUtf8(result.prefix_path ? result.prefix_path : "");
  const QString error = QString::fromUtf8(result.error ? result.error : "");
  const uint32_t appId = result.app_id;

  nak_shortcut_result_free(result);

  if (!error.isEmpty() || prefixPath.isEmpty()) {
    setBusy(false);
    ui->protonStatusLabel->setText(error.isEmpty() ? tr("Failed to create prefix")
                                                    : tr("Error: %1").arg(error));
    return;
  }

  startInstallTask(appId, prefixPath, protonName, protonPath,
                   ui->umuCheckBox->isChecked(),
                   ui->umuSystemCheckBox->isChecked(),
                   ui->steamRunCheckBox->isChecked());
}

void ProtonSettingsTab::onDeletePrefix()
{
  if (m_busy) {
    return;
  }

  auto cfg = FluorineConfig::load();
  if (!cfg.has_value()) {
    ui->protonStatusLabel->setText(tr("No Prefix"));
    return;
  }

  if (char* error = nak_remove_steam_shortcut(cfg->app_id); error != nullptr) {
    nak_string_free(error);
  }

  cfg->destroyPrefix();

  ui->protonStatusLabel->setText(tr("No Prefix"));
  refreshState();
}

void ProtonSettingsTab::onRecreatePrefix()
{
  if (m_busy) {
    return;
  }

  auto cfg = FluorineConfig::load();
  if (!cfg.has_value() || !cfg->prefixExists()) {
    ui->protonStatusLabel->setText(tr("No existing prefix to recreate"));
    refreshState();
    return;
  }

  QDir prefixDir(cfg->prefix_path);
  if (prefixDir.exists() && !prefixDir.removeRecursively()) {
    ui->protonStatusLabel->setText(tr("Failed to delete existing prefix"));
    refreshState();
    return;
  }

  setBusy(true);
  ui->protonStatusLabel->setText(tr("Recreating prefix..."));

  startInstallTask(cfg->app_id, cfg->prefix_path, cfg->proton_name,
                   cfg->proton_path, ui->umuCheckBox->isChecked(),
                   ui->umuSystemCheckBox->isChecked(),
                   ui->steamRunCheckBox->isChecked());
}

void ProtonSettingsTab::onOpenPrefixFolder()
{
  auto path = FluorineConfig::prefixPath();
  if (!path.has_value()) {
    ui->protonStatusLabel->setText(tr("No Prefix"));
    return;
  }

  QProcess::startDetached("xdg-open", {*path});
}

void ProtonSettingsTab::startInstallTask(uint32_t appId, const QString& prefixPath,
                                         const QString& protonName,
                                         const QString& protonPath,
                                         bool useUmuForPrefix,
                                         bool preferSystemUmu,
                                         bool useSteamRun)
{
  m_pendingAppId      = appId;
  m_pendingPrefixPath = prefixPath;
  m_pendingProtonName = protonName;
  m_pendingProtonPath = protonPath;

  ui->protonProgressBar->setValue(0);

  g_activeInstallTab.store(this);

  m_installWatcher.setFuture(QtConcurrent::run([
      appId,
      prefixPath,
      protonName,
      protonPath,
      useUmuForPrefix,
      preferSystemUmu,
      useSteamRun]() -> InstallResult {
    const QByteArray prefixPathUtf8 = prefixPath.toUtf8();
    const QByteArray protonNameUtf8 = protonName.toUtf8();
    const QByteArray protonPathUtf8 = protonPath.toUtf8();
    const QByteArray bundledUmuPathUtf8 =
        QDir(QCoreApplication::applicationDirPath()).filePath("umu-run").toUtf8();

    qputenv("NAK_USE_UMU_FOR_PREFIX", useUmuForPrefix ? "1" : "0");
    qputenv("NAK_PREFER_SYSTEM_UMU", preferSystemUmu ? "1" : "0");
    qputenv("NAK_USE_STEAM_RUN", useSteamRun ? "1" : "0");

    if (QFileInfo::exists(QString::fromUtf8(bundledUmuPathUtf8))) {
      qputenv("NAK_BUNDLED_UMU_RUN", bundledUmuPathUtf8);
    } else {
      qunsetenv("NAK_BUNDLED_UMU_RUN");
    }

    const auto restoreNakEnv = qScopeGuard([] {
      qunsetenv("NAK_USE_UMU_FOR_PREFIX");
      qunsetenv("NAK_PREFER_SYSTEM_UMU");
      qunsetenv("NAK_USE_STEAM_RUN");
      qunsetenv("NAK_BUNDLED_UMU_RUN");
    });

    int cancelFlag = 0;
    char* error    = nak_install_all_dependencies(
        prefixPathUtf8.constData(), protonNameUtf8.constData(),
        protonPathUtf8.constData(), &ProtonSettingsTab::statusCallback,
        &ProtonSettingsTab::logCallback, &ProtonSettingsTab::progressCallback,
        &cancelFlag, appId);

    InstallResult r;
    if (error != nullptr) {
      r.error = QString::fromUtf8(error);
      nak_string_free(error);
    }

    return r;
  }));
}

void ProtonSettingsTab::enqueueStatus(const QString& message)
{
  QMetaObject::invokeMethod(this,
                            [this, message] {
                              if (m_busy) {
                                ui->protonStatusLabel->setText(message);
                              }
                            },
                            Qt::QueuedConnection);
}

void ProtonSettingsTab::enqueueProgress(float progress)
{
  QMetaObject::invokeMethod(this,
                            [this, progress] {
                              if (m_busy) {
                                const int clamped =
                                    qBound(0, static_cast<int>(progress * 100.0f), 100);
                                ui->protonProgressBar->setValue(clamped);
                              }
                            },
                            Qt::QueuedConnection);
}

void ProtonSettingsTab::statusCallback(const char* message)
{
  if (auto* tab = g_activeInstallTab.load(); tab != nullptr) {
    tab->enqueueStatus(QString::fromUtf8(message ? message : ""));
  }
}

void ProtonSettingsTab::logCallback(const char* message)
{
  Q_UNUSED(message);
}

void ProtonSettingsTab::progressCallback(float progress)
{
  if (auto* tab = g_activeInstallTab.load(); tab != nullptr) {
    tab->enqueueProgress(progress);
  }
}

void ProtonSettingsTab::onInstallFinished()
{
  g_activeInstallTab.store(nullptr);

  const InstallResult result = m_installWatcher.result();

  setBusy(false);

  if (!result.error.isEmpty()) {
    ui->protonStatusLabel->setText(tr("Error: %1").arg(result.error));
    return;
  }

  // Set up prefix directory structure (temp dir + game symlinks)
  {
    const QByteArray prefixPathUtf8 = m_pendingPrefixPath.toUtf8();
    nak_ensure_temp_directory(prefixPathUtf8.constData());
    nak_create_game_symlinks_auto(prefixPathUtf8.constData());
  }

  FluorineConfig cfg;
  cfg.app_id      = m_pendingAppId;
  cfg.prefix_path = m_pendingPrefixPath;
  cfg.proton_name = m_pendingProtonName;
  cfg.proton_path = m_pendingProtonPath;
  cfg.created     = QDateTime::currentDateTime().toString(Qt::ISODate);

  if (!cfg.save()) {
    ui->protonStatusLabel->setText(tr("Error saving Fluorine config"));
    refreshState();
    return;
  }

  ui->protonStatusLabel->setText(tr("Prefix Active"));
  refreshState();
}
