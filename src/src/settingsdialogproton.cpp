#include "settingsdialogproton.h"

#include "fluorineconfig.h"
#include "fluorinepaths.h"
#include "prefixsetupdialog.h"
#include "protonsettingsedit.h"
#include "settingsmigration.h"
#include "ui_settingsdialog.h"
#include "vfsbackend.h"
#include "winesavedeployment.h"
#include "wineruntimeconfig.h"

#include <algorithm>
#include <QtConcurrent/QtConcurrentRun>
#include <log.h>
#include <uibase/utility.h>
#include "steamdetection.h"
#include "slrmanager.h"
#include <atomic>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLockFile>
#include <QMessageBox>
#include <QPushButton>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QMetaObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressDialog>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>
#include <QVBoxLayout>

#include <memory>
#include <vector>

namespace
{
std::atomic<ProtonSettingsTab*> g_activeInstallTab = nullptr;

WineRuntimeConfig::Snapshot normalizedDefault(const FluorineConfig& config)
{
  WineRuntimeConfig::Inputs inputs;
  inputs.defaultConfigPath = FluorineConfig::configFilePath();
  inputs.defaultConfigPresent = true;
  inputs.defaultPrefix = config.prefix_path;
  inputs.defaultProton = config.proton_path;
  return WineRuntimeConfig::resolve(inputs);
}

struct RuntimeMutationLocks
{
  std::unique_ptr<QLockFile> config;
  std::vector<std::unique_ptr<QLockFile>> prefixes;
};

bool acquirePrefixPathMutationLock(
    const std::shared_ptr<RuntimeMutationLocks>& locks,
    const QString& prefixPath, QString& error)
{
  if (locks == nullptr || locks->config == nullptr ||
      !locks->config->isLocked() || prefixPath.trimmed().isEmpty()) {
    error = QStringLiteral("The Wine runtime configuration is not locked");
    return false;
  }
  auto prefixLock = std::make_unique<QLockFile>(
      WineSaveDeployment::leasePathFor(prefixPath, QString{}));
  prefixLock->setStaleLockTime(0);
  if (!prefixLock->tryLock(0)) {
    error = QStringLiteral(
        "The selected Wine prefix is in use by another Fluorine operation");
    return false;
  }
  locks->prefixes.push_back(std::move(prefixLock));
  return true;
}

bool acquireOwnedIncompletePrefixMutationLocks(
    const std::shared_ptr<RuntimeMutationLocks>& locks,
    const FluorineConfig& config, QString& error)
{
  if (!config.canDestroyPrefix()) {
    error = QStringLiteral(
        "Fluorine cannot authenticate ownership of the incomplete prefix");
    return false;
  }
  const QString configured =
      QDir::cleanPath(QFileInfo(config.prefix_path).absoluteFilePath());
  if (configured.isEmpty() || QFileInfo(configured).isSymLink()) {
    error = QStringLiteral("The configured prefix path is unsafe");
    return false;
  }
  const QString possiblePfx =
      QFileInfo(configured).fileName() == QStringLiteral("pfx")
          ? configured
          : QDir(configured).filePath(QStringLiteral("pfx"));
  if (QFileInfo(possiblePfx).isSymLink()) {
    error = QStringLiteral("The incomplete pfx path is an unsafe symlink");
    return false;
  }

  QStringList lockTargets{configured};
  if (possiblePfx != configured) lockTargets.append(possiblePfx);
  std::sort(lockTargets.begin(), lockTargets.end(),
            [](const QString& left, const QString& right) {
              return WineSaveDeployment::leasePathFor(left, QString{}) <
                     WineSaveDeployment::leasePathFor(right, QString{});
            });
  for (const QString& target : lockTargets) {
    if (!acquirePrefixPathMutationLock(locks, target, error)) return false;
  }
  for (const QString& target : lockTargets) {
    if (WineSaveDeployment::hasPersistedSessionLease(target)) {
      error = QStringLiteral(
          "The incomplete prefix has a persisted operation owner and cannot "
          "be deleted until it is recovered");
      return false;
    }
  }
  error.clear();
  return true;
}

bool acquirePrefixMutationLock(
    const std::shared_ptr<RuntimeMutationLocks>& locks,
    const WineRuntimeConfig::Snapshot& runtime, QString& error)
{
  if (locks == nullptr || locks->config == nullptr ||
      !locks->config->isLocked()) {
    error = QStringLiteral("The Wine runtime configuration is not locked");
    return false;
  }
  if (runtime.prefixPath.isEmpty()) {
    error = QStringLiteral("The selected Wine prefix is unavailable");
    return false;
  }
  QString runtimeError;
  if (!WineRuntimeConfig::revalidatePrefix(runtime, &runtimeError)) {
    error = runtimeError;
    return false;
  }
  if (!acquirePrefixPathMutationLock(locks, runtime.prefixPath, error)) {
    return false;
  }
  if (!WineRuntimeConfig::revalidatePrefix(runtime, &runtimeError)) {
    error = runtimeError;
    return false;
  }
  if (WineSaveDeployment::hasPersistedSessionLease(runtime.prefixPath)) {
    error = QStringLiteral(
        "The selected Wine prefix has a persisted launch owner and cannot be "
        "changed until that launch is recovered or finishes");
    return false;
  }
  error.clear();
  return true;
}

std::shared_ptr<RuntimeMutationLocks> acquireRuntimeMutationLocks(
    const WineRuntimeConfig::Snapshot* runtime, QString& error)
{
  auto locks = std::make_shared<RuntimeMutationLocks>();
  const QString configPath = FluorineConfig::configFilePath();
  if (!QDir().mkpath(QFileInfo(configPath).absolutePath())) {
    error = QStringLiteral("Could not prepare the Wine runtime configuration lock");
    return {};
  }
  locks->config = std::make_unique<QLockFile>(
      SettingsMigration::settingsLockPath(configPath));
  locks->config->setStaleLockTime(SettingsMigration::SettingsLockStaleMs);
  if (!locks->config->tryLock(SettingsMigration::SettingsLockTimeoutMs)) {
    error = QStringLiteral(
        "Another Fluorine process is changing the Wine runtime configuration");
    return {};
  }

  if (runtime != nullptr &&
      !acquirePrefixMutationLock(locks, *runtime, error)) {
    return {};
  }
  error.clear();
  return locks;
}
}

ProtonSettingsTab::ProtonSettingsTab(Settings& s, SettingsDialog& d)
    : QObject(&d), SettingsTab(s, d)
{
  connect(
      &d, &QDialog::finished, this,
      [this](int) {
        m_settingsDialogClosing = true;
        cancelAndWaitForSlrWorker();
      },
      Qt::DirectConnection);

  connect(&m_slrWatcher, &QFutureWatcher<QString>::finished, this, [this] {
    if (m_slrProgress) {
      m_slrProgress->close();
      m_slrProgress->deleteLater();
      m_slrProgress.clear();
    }

    const QString err = m_slrWatcher.result();
    if (m_settingsDialogClosing || slrOperationAdmissionSuppressed()) {
      return;
    }

    ui->downloadSLRButton->setEnabled(true);
    if (m_slrCancellation.isCancellationRequested()) {
      return;
    }
    if (!err.isEmpty()) {
      MOBase::log::error("[SLR] Download failed: {}", err);
      QMessageBox::warning(parentWidget(), tr("Steam Linux Runtime"),
                           tr("Download failed:\n%1").arg(err));
    } else {
      MOBase::log::info("[SLR] Steam Linux Runtime installed successfully");
      QMessageBox::information(
          parentWidget(), tr("Steam Linux Runtime"),
          tr("Steam Linux Runtime installed successfully."));
    }
  });

  ui->protonProgressBar->setRange(0, 100);
  ui->protonProgressBar->setValue(0);
  ui->protonProgressBar->setVisible(false);

  ui->launchWrapperEdit->setPlaceholderText("mangohud --dlsym");
  ui->launchWrapperEdit->setText(QSettings().value("fluorine/launch_wrapper").toString());

  ui->disableVfsCacheCheckBox->setChecked(
      QSettings().value("fluorine/disable_vfs_cache", false).toBool());

  ui->vfsBackendComboBox->addItem(tr("FUSE (default)"),
                                  vfsBackendSettingValue(VfsBackend::Fuse));
  ui->vfsBackendComboBox->addItem(
      tr("USVFS for Wine/Proton (experimental)"),
      vfsBackendSettingValue(VfsBackend::Usvfs));
  const QSettings instanceSettings(settings().filename(), QSettings::IniFormat);
  const QString configuredBackend =
      instanceSettings.value(kVfsBackendSetting, QStringLiteral("fuse"))
          .toString();
  const int backendIndex = ui->vfsBackendComboBox->findData(
      vfsBackendSettingValue(parseVfsBackend(configuredBackend)));
  ui->vfsBackendComboBox->setCurrentIndex(std::max(0, backendIndex));
  ui->usvfsExactQueryExhaustionCheckBox->setChecked(
      instanceSettings.value(kUsvfsExactQueryExhaustionSetting, false).toBool());
  ui->usvfsSharedContextCheckBox->setChecked(
      instanceSettings.value(kUsvfsSharedContextSetting, false).toBool());

  populateProtons();

  QObject::connect(ui->createPrefixButton, &QPushButton::clicked, this,
                   &ProtonSettingsTab::onCreatePrefix);
  QObject::connect(ui->deletePrefixButton, &QPushButton::clicked, this,
                   &ProtonSettingsTab::onDeletePrefix);
  QObject::connect(ui->recreatePrefixButton, &QPushButton::clicked, this,
                   &ProtonSettingsTab::onRecreatePrefix);
  QObject::connect(ui->openPrefixFolderButton, &QPushButton::clicked, this,
                   &ProtonSettingsTab::onOpenPrefixFolder);
  QObject::connect(ui->winetricksButton, &QPushButton::clicked, this,
                   &ProtonSettingsTab::onWinetricks);
  QObject::connect(ui->prefixLocationBrowseButton, &QPushButton::clicked, this,
                   &ProtonSettingsTab::onBrowsePrefixLocation);
  QObject::connect(ui->downloadSLRButton, &QPushButton::clicked, this,
                   &ProtonSettingsTab::onDownloadSLR);

  QObject::connect(&m_installWatcher, &QFutureWatcher<InstallResult>::finished, this,
                   &ProtonSettingsTab::onInstallFinished);

  // install log viewer
  ui->nakInstallLog->setVisible(false);
  QObject::connect(ui->toggleInstallLog, &QPushButton::toggled, this,
                   [this](bool checked) {
                     ui->nakInstallLog->setVisible(checked);
                     ui->toggleInstallLog->setText(
                         checked ? tr("Hide Install Log") : tr("Show Install Log"));
                   });

  refreshState();
}

ProtonSettingsTab::~ProtonSettingsTab()
{
  cancelAndWaitForSlrWorker();
}

void ProtonSettingsTab::cancelAndWaitForSlrWorker() noexcept
{
  m_slrCancellation.cancel();
  if (m_slrWatcher.isRunning()) {
    m_slrWatcher.waitForFinished();
  }
}

void ProtonSettingsTab::update()
{
  const int protonIndex = ui->protonVersionCombo->currentIndex();
  const QString storedProtonName =
      protonIndex >= 0
          ? ui->protonVersionCombo->itemData(protonIndex, Qt::UserRole + 2)
                .toString()
                .trimmed()
          : QString{};
  const QString protonName =
      !storedProtonName.isEmpty()
          ? storedProtonName
          : (protonIndex >= 0
                 ? ui->protonVersionCombo->currentText().trimmed()
                 : QString{});
  const QString protonPath =
      protonIndex >= 0
          ? ui->protonVersionCombo->itemData(protonIndex, Qt::UserRole + 1)
                .toString()
                .trimmed()
          : QString{};
  if (!ProtonSettingsEdit::persist(ui->launchWrapperEdit->text(),
                                   ui->disableVfsCacheCheckBox->isChecked(),
                                   protonName, protonPath)) {
    dialog().reportUpdateFailure(
        tr("the Proton and launch settings could not be saved"));
  }
  const auto activeRuntime = WineRuntimeConfig::current();
  if (activeRuntime.protonSource ==
          WineRuntimeConfig::Source::ApplicationDefault &&
      QFileInfo(protonPath).canonicalFilePath() != activeRuntime.protonPath) {
    dialog().setExitNeeded(Exit::Restart);
  }

  QSettings instanceSettings(settings().filename(), QSettings::IniFormat);
  instanceSettings.setValue(kVfsBackendSetting,
                            ui->vfsBackendComboBox->currentData().toString());
  instanceSettings.setValue(kUsvfsExactQueryExhaustionSetting,
                            ui->usvfsExactQueryExhaustionCheckBox->isChecked());
  instanceSettings.setValue(kUsvfsSharedContextSetting,
                            ui->usvfsSharedContextCheckBox->isChecked());
}

void ProtonSettingsTab::populateProtons()
{
  ui->protonVersionCombo->clear();

  const auto protonList = findSteamProtons();

  for (int i = 0; i < protonList.size(); ++i) {
    const SteamProtonInfo& proton = protonList[i];

    if (proton.name.isEmpty() || proton.path.isEmpty()) {
      continue;
    }

    ui->protonVersionCombo->addItem(proton.name);
    ui->protonVersionCombo->setItemData(ui->protonVersionCombo->count() - 1, proton.path,
                                        Qt::UserRole + 1);
    ui->protonVersionCombo->setItemData(ui->protonVersionCombo->count() - 1,
                                        proton.name, Qt::UserRole + 2);
    ui->protonVersionCombo->setItemData(ui->protonVersionCombo->count() - 1,
                                        proton.path, Qt::ToolTipRole);
  }

  if (auto cfg = FluorineConfig::load(); cfg.has_value()) {
    const QString savedCanonical =
        QFileInfo(cfg->proton_path).canonicalFilePath();
    int idx = -1;
    for (int i = 0; i < ui->protonVersionCombo->count(); ++i) {
      const QString candidate =
          ui->protonVersionCombo->itemData(i, Qt::UserRole + 1).toString();
      const QString candidateCanonical = QFileInfo(candidate).canonicalFilePath();
      if ((!savedCanonical.isEmpty() && candidateCanonical == savedCanonical) ||
          (savedCanonical.isEmpty() &&
           QDir::cleanPath(candidate) == QDir::cleanPath(cfg->proton_path))) {
        idx = i;
        break;
      }
    }
    if (idx >= 0) {
      ui->protonVersionCombo->setCurrentIndex(idx);
    } else if (!cfg->proton_path.trimmed().isEmpty()) {
      // Preserve an unavailable runner as the selected value. Settings must
      // never silently replace it with row zero merely because another
      // installation uses the same display name or sorts first.
      const QString displayName =
          cfg->proton_name.trimmed().isEmpty()
              ? tr("Configured Proton (missing)")
              : tr("%1 (missing)").arg(cfg->proton_name);
      ui->protonVersionCombo->addItem(displayName);
      idx = ui->protonVersionCombo->count() - 1;
      ui->protonVersionCombo->setItemData(idx, cfg->proton_path,
                                          Qt::UserRole + 1);
      ui->protonVersionCombo->setItemData(idx, cfg->proton_name,
                                          Qt::UserRole + 2);
      ui->protonVersionCombo->setItemData(
          idx, tr("Configured path is unavailable: %1").arg(cfg->proton_path),
          Qt::ToolTipRole);
      ui->protonVersionCombo->setCurrentIndex(idx);
      MOBase::log::warn("Configured Proton '{}' at '{}' is unavailable; "
                        "preserving it until the user explicitly selects a "
                        "replacement",
                        cfg->proton_name, cfg->proton_path);
    }
  }
}

void ProtonSettingsTab::refreshState()
{
  const auto cfg = FluorineConfig::load();
  const auto runtime = cfg.has_value() ? normalizedDefault(*cfg)
                                       : WineRuntimeConfig::Snapshot{};
  const bool configured = cfg.has_value();
  const bool prefixUsable = configured && runtime.prefixError.isEmpty() &&
                            !runtime.prefixPath.isEmpty();
  const bool runtimeUsable = prefixUsable && runtime.protonError.isEmpty() &&
                             !runtime.protonPath.isEmpty();

  if (!m_busy) {
    if (runtimeUsable) {
      ui->protonStatusLabel->setText(tr("Prefix Active"));
    } else if (configured) {
      ui->protonStatusLabel->setText(
          !runtime.prefixError.isEmpty() ? runtime.prefixError
                                         : runtime.protonError);
    } else if (QFileInfo::exists(FluorineConfig::configFilePath())) {
      ui->protonStatusLabel->setText(tr("Fluorine runtime config is invalid"));
    } else {
      ui->protonStatusLabel->setText(tr("No Prefix"));
    }
    ui->protonProgressBar->setVisible(false);
  }

  if (configured) {
    ui->prefixLocationEdit->setText(prefixUsable ? runtime.prefixPath
                                                 : cfg->prefix_path);
    ui->prefixLocationEdit->setReadOnly(true);
  } else {
    ui->prefixLocationEdit->setReadOnly(false);
    if (ui->prefixLocationEdit->text().isEmpty()) {
      ui->prefixLocationEdit->setText(
          fluorineDataDir() + "/Prefix");
    }
  }

  ui->prefixLocationBrowseButton->setEnabled(!m_busy && !configured);
  ui->createPrefixButton->setEnabled(!m_busy && !configured);
  ui->deletePrefixButton->setEnabled(!m_busy && configured);
  ui->recreatePrefixButton->setEnabled(
      !m_busy && runtimeUsable && !runtime.compatDataPath.isEmpty());
  ui->openPrefixFolderButton->setEnabled(!m_busy && prefixUsable);
  ui->winetricksButton->setEnabled(!m_busy && runtimeUsable);
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

  const QString basePath = ui->prefixLocationEdit->text().trimmed();
  if (basePath.isEmpty()) {
    ui->protonStatusLabel->setText(tr("Select a prefix location first"));
    return;
  }

  const QString pfxPath = QDir(basePath).filePath("pfx");
  const QFileInfo baseInfo(basePath);
  const QFileInfo pfxInfo(pfxPath);
  FluorineConfig ownership;
  ownership.prefix_path = pfxPath;

  const bool baseHasContent =
      QDir(basePath).exists() &&
      !QDir(basePath)
           .entryList(QDir::AllEntries | QDir::NoDotAndDotDot)
           .isEmpty();
  if (baseInfo.isSymLink() || pfxInfo.isSymLink() ||
      (baseHasContent && !ownership.canDestroyPrefix())) {
    QMessageBox::warning(
        parentWidget(), tr("Unsafe Prefix Location"),
        tr("Fluorine will not initialize an existing or externally managed "
           "prefix at:\n%1\n\nChoose an empty directory reserved for Fluorine.")
            .arg(basePath));
    ui->protonStatusLabel->setText(tr("Select an empty Fluorine prefix location"));
    return;
  }

  QString mutationError;
  const auto mutationLocks =
      acquireRuntimeMutationLocks(nullptr, mutationError);
  if (mutationLocks == nullptr) {
    QMessageBox::warning(parentWidget(), tr("Wine Runtime Busy"),
                         mutationError);
    return;
  }

  if (!QDir().mkpath(pfxPath)) {
    ui->protonStatusLabel->setText(tr("Failed to create prefix directory"));
    return;
  }

  if (!acquirePrefixPathMutationLock(mutationLocks, pfxPath,
                                     mutationError)) {
    QMessageBox::warning(parentWidget(), tr("Prefix In Use"), mutationError);
    return;
  }

  if (!ownership.markPrefixOwned()) {
    ui->protonStatusLabel->setText(tr("Failed to mark prefix as Fluorine-managed"));
    return;
  }

  runPrefixSetupDialog(0, pfxPath, protonName, protonPath);
}

void ProtonSettingsTab::onDeletePrefix()
{
  if (m_busy) {
    return;
  }

  QString mutationError;
  const auto mutationLocks =
      acquireRuntimeMutationLocks(nullptr, mutationError);
  if (mutationLocks == nullptr) {
    QMessageBox::warning(parentWidget(), tr("Wine Runtime Busy"),
                         mutationError);
    return;
  }

  auto cfg = FluorineConfig::load();
  if (!cfg.has_value()) {
    ui->protonStatusLabel->setText(tr("No Prefix"));
    return;
  }
  const auto runtime = normalizedDefault(*cfg);
  const bool mutationOwned =
      runtime.prefixError.isEmpty()
          ? acquirePrefixMutationLock(mutationLocks, runtime, mutationError)
          : acquireOwnedIncompletePrefixMutationLocks(
                mutationLocks, *cfg, mutationError);
  if (!mutationOwned) {
    QMessageBox::warning(
        parentWidget(), tr("Prefix In Use"),
        mutationError.isEmpty() ? runtime.prefixError : mutationError);
    return;
  }

  const auto answer = QMessageBox::warning(
      parentWidget(), tr("Delete Prefix"),
      tr("This will permanently delete Fluorine's Wine prefix at:\n%1\n\n"
         "Continue?")
          .arg(cfg->compatDataPath()),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (answer != QMessageBox::Yes) {
    return;
  }

  if (!cfg->destroyPrefix()) {
    QMessageBox::critical(
        parentWidget(), tr("Prefix Not Deleted"),
        tr("Fluorine refused to delete this prefix because it could not verify "
           "ownership. Remove it manually if it is no longer needed."));
    ui->protonStatusLabel->setText(tr("Prefix ownership could not be verified"));
    return;
  }

  ui->prefixLocationEdit->clear();
  ui->protonStatusLabel->setText(tr("No Prefix"));
  dialog().markRuntimeLifecycleChanged();
  refreshState();
}

void ProtonSettingsTab::onRecreatePrefix()
{
  if (m_busy) {
    return;
  }

  QString mutationError;
  const auto mutationLocks =
      acquireRuntimeMutationLocks(nullptr, mutationError);
  if (mutationLocks == nullptr) {
    QMessageBox::warning(parentWidget(), tr("Wine Runtime Busy"),
                         mutationError);
    return;
  }

  auto cfg = FluorineConfig::load();
  if (!cfg.has_value() || !cfg->prefixExists()) {
    ui->protonStatusLabel->setText(tr("No existing prefix to recreate"));
    refreshState();
    return;
  }

  if (!cfg->canDestroyPrefix()) {
    QMessageBox::critical(
        parentWidget(), tr("Prefix Not Recreated"),
        tr("Fluorine refused to recreate this prefix because it could not "
           "verify ownership."));
    ui->protonStatusLabel->setText(tr("Prefix ownership could not be verified"));
    return;
  }

  const auto runtime = normalizedDefault(*cfg);
  if (!runtime.valid() || runtime.prefixPath.isEmpty() ||
      runtime.protonPath.isEmpty()) {
    QMessageBox::critical(parentWidget(), tr("Prefix Not Recreated"),
                          !runtime.prefixError.isEmpty() ? runtime.prefixError
                                                        : runtime.protonError);
    return;
  }
  if (runtime.compatDataPath.isEmpty()) {
    QMessageBox::information(
        parentWidget(), tr("Direct Prefix Cannot Be Recreated"),
        tr("This Wine runtime uses a direct prefix layout. Proton recreation "
           "requires a managed compatdata/pfx layout and could otherwise "
           "write outside the selected prefix. Delete it and create a new "
           "managed prefix instead."));
    return;
  }
  if (!acquirePrefixMutationLock(mutationLocks, runtime, mutationError)) {
    QMessageBox::warning(parentWidget(), tr("Prefix In Use"), mutationError);
    return;
  }

  const auto answer = QMessageBox::warning(
      parentWidget(), tr("Recreate Prefix"),
      tr("This will delete and rebuild Fluorine's Wine prefix at:\n%1\n\n"
         "Continue?")
          .arg(cfg->prefix_path),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (answer != QMessageBox::Yes) {
    return;
  }

  if (!cfg->resetPrefixForRecreation()) {
    ui->protonStatusLabel->setText(
        tr("Failed to safely prepare the existing prefix for recreation"));
    refreshState();
    return;
  }

  // The old runtime generation is gone even if setup is canceled or fails.
  // Keep the restart requirement independently of the dialog's final result.
  dialog().markRuntimeLifecycleChanged();

  runPrefixSetupDialog(cfg->app_id, runtime.prefixPath, cfg->proton_name,
                       runtime.protonPath);
}

void ProtonSettingsTab::onOpenPrefixFolder()
{
  const auto cfg = FluorineConfig::load();
  if (!cfg.has_value()) {
    ui->protonStatusLabel->setText(tr("No Prefix"));
    return;
  }
  const auto runtime = normalizedDefault(*cfg);
  if (!runtime.prefixError.isEmpty()) {
    ui->protonStatusLabel->setText(runtime.prefixError);
    return;
  }
  MOBase::shell::Explore(QDir(runtime.prefixPath));
}

void ProtonSettingsTab::onDownloadSLR()
{
  if (slrOperationAdmissionSuppressed() || m_slrWatcher.isRunning()) {
    return;
  }
  if (isSlrInstalled()) {
    QMessageBox::information(parentWidget(), tr("Steam Linux Runtime"),
        tr("Steam Linux Runtime is already installed."));
    return;
  }

  ui->downloadSLRButton->setEnabled(false);
  m_slrProgress = new QProgressDialog(
      tr("Downloading Steam Linux Runtime (~200 MB)...\n"
         "Check the MO2 log for progress details."),
      tr("Cancel"), 0, 0, parentWidget());
  m_slrProgress->setWindowTitle(tr("Steam Linux Runtime"));
  m_slrProgress->setWindowModality(Qt::WindowModal);
  m_slrProgress->setAttribute(Qt::WA_ShowWithoutActivating);
  m_slrProgress->setMinimumDuration(0);

  m_slrCancellation = SlrCancellationSource();
  connect(m_slrProgress, &QProgressDialog::canceled, this, [this] {
    m_slrCancellation.cancel();
  });

  m_slrWatcher.setFuture(
      QtConcurrent::run([token = m_slrCancellation.token()]() -> QString {
        return downloadSlr(nullptr, nullptr, token);
      }));
  m_slrProgress->show();
}

void ProtonSettingsTab::onBrowsePrefixLocation()
{
  const QString dir = QFileDialog::getExistingDirectory(
      parentWidget(), tr("Select Prefix Location"), ui->prefixLocationEdit->text());
  if (!dir.isEmpty()) {
    ui->prefixLocationEdit->setText(dir);
  }
}

QString ProtonSettingsTab::ensureWinetricks()
{
  const QString nakWinetricks = fluorineDataDir() + "/bin/winetricks";
  if (QFileInfo::exists(nakWinetricks)) {
    return nakWinetricks;
  }

  const QString systemWinetricks = QStandardPaths::findExecutable("winetricks");
  if (!systemWinetricks.isEmpty()) {
    return systemWinetricks;
  }

  const QString nakBinDir = fluorineDataDir() + "/bin";
  QDir().mkpath(nakBinDir);

  QString downloadTool;
  QStringList downloadArgs;

  if (!QStandardPaths::findExecutable("curl").isEmpty()) {
    downloadTool = "curl";
    downloadArgs = {"-L", "-o", nakWinetricks,
                    "https://raw.githubusercontent.com/Winetricks/winetricks/master/src/"
                    "winetricks"};
  } else if (!QStandardPaths::findExecutable("wget").isEmpty()) {
    downloadTool = "wget";
    downloadArgs = {"-O", nakWinetricks,
                    "https://raw.githubusercontent.com/Winetricks/winetricks/master/src/"
                    "winetricks"};
  } else {
    return {};
  }

  QProcess proc;
  proc.start(downloadTool, downloadArgs);
  proc.waitForFinished(30000);

  if (proc.exitCode() != 0 || !QFileInfo::exists(nakWinetricks)) {
    return {};
  }

  QFile::setPermissions(nakWinetricks,
                        QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                            QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                            QFileDevice::ExeGroup | QFileDevice::ReadOther |
                            QFileDevice::ExeOther);

  return nakWinetricks;
}

QString ProtonSettingsTab::findProtonWine(const QString& protonPath)
{
  QString wine = QDir(protonPath).filePath("files/bin/wine");
  if (QFileInfo::exists(wine)) {
    return wine;
  }

  wine = QDir(protonPath).filePath("dist/bin/wine");
  if (QFileInfo::exists(wine)) {
    return wine;
  }

  return {};
}

void ProtonSettingsTab::onWinetricks()
{
  QString mutationError;
  const auto mutationLocks =
      acquireRuntimeMutationLocks(nullptr, mutationError);
  if (mutationLocks == nullptr) {
    QMessageBox::warning(parentWidget(), tr("Wine Runtime Busy"),
                         mutationError);
    return;
  }
  auto cfg = FluorineConfig::load();
  if (!cfg.has_value() || !cfg->prefixExists()) {
    ui->protonStatusLabel->setText(tr("No existing prefix"));
    refreshState();
    return;
  }
  const auto runtime = normalizedDefault(*cfg);
  if (!runtime.valid() ||
      !acquirePrefixMutationLock(mutationLocks, runtime, mutationError)) {
    ui->protonStatusLabel->setText(
        !runtime.prefixError.isEmpty()
            ? runtime.prefixError
            : !runtime.protonError.isEmpty() ? runtime.protonError
                                             : mutationError);
    return;
  }

  const QString winetricksPath = ensureWinetricks();
  if (winetricksPath.isEmpty()) {
    QMessageBox::warning(
        parentWidget(), tr("Winetricks Not Found"),
        tr("Could not find or download winetricks.\n\n"
           "Please install winetricks manually:\n"
           "  Arch: pacman -S winetricks\n"
           "  Ubuntu: apt install winetricks\n"
           "  Fedora: dnf install winetricks"));
    return;
  }

  // Build env vars for winetricks
  QStringList envFlags;
  envFlags << QStringLiteral("WINEPREFIX=%1").arg(runtime.prefixPath);

  const QString protonWine = findProtonWine(runtime.protonPath);
  if (!protonWine.isEmpty()) {
    envFlags << QStringLiteral("WINE=%1").arg(protonWine);
    const QString wineserver =
        QFileInfo(protonWine).dir().filePath("wineserver");
    if (QFileInfo::exists(wineserver)) {
      envFlags << QStringLiteral("WINESERVER=%1").arg(wineserver);
    }
  }

  QString program = winetricksPath;
  QStringList arguments;
  arguments << QStringLiteral("--gui");

  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

  // Restore the original host LD_LIBRARY_PATH so that winetricks (and any
  // GUI helpers it spawns, e.g. kdialog/zenity) don't pick up Fluorine's
  // bundled Qt libraries, which cause symbol-lookup errors.
  // Uses the same restore-or-strip logic as protonlauncher.cpp.
  auto restoreOrStrip = [&](const QString& var, const QString& origVar) {
    if (env.contains(origVar)) {
      const QString orig = env.value(origVar);
      if (orig.isEmpty())
        env.remove(var);
      else
        env.insert(var, orig);
      env.remove(origVar);
    } else {
      // Fallback: strip Fluorine's bundled library paths by pattern.
      const QString value = env.value(var);
      if (value.isEmpty()) return;
      QStringList kept;
      for (const QString& p : value.split(':')) {
        if (p.contains("fluorine") || p.contains(".mount_Fluori")) {
          continue;
        }
        kept.append(p);
      }
      if (kept.isEmpty()) {
        env.remove(var);
      } else {
        env.insert(var, kept.join(':'));
      }
    }
  };
  restoreOrStrip("LD_LIBRARY_PATH", "FLUORINE_ORIG_LD_LIBRARY_PATH");
  restoreOrStrip("LD_PRELOAD", "FLUORINE_ORIG_LD_PRELOAD");
  restoreOrStrip("QT_PLUGIN_PATH", "FLUORINE_ORIG_QT_PLUGIN_PATH");
  env.remove("QT_QPA_PLATFORM_PLUGIN_PATH");

  for (const QString& flag : envFlags) {
    const int eq = flag.indexOf('=');
    if (eq > 0) {
      env.insert(flag.left(eq), flag.mid(eq + 1));
    }
  }
  const QString mutationOwner =
      QStringLiteral("winetricks-") +
      QUuid::createUuid().toString(QUuid::WithoutBraces);
  const auto session = WineSaveDeployment::beginSessionLease(
      runtime.prefixPath, runtime.prefixPath, mutationOwner);
  if (!session) {
    QMessageBox::warning(parentWidget(), tr("Prefix In Use"), session.error);
    return;
  }
  dialog().beginRuntimeMutation();
  QPointer<SettingsDialog> settingsDialog(&dialog());

  // Keep both the global runtime-config lock and the exact prefix lock for
  // the complete Winetricks process lifetime. A detached process would drop
  // ownership as soon as this slot returned and could race the next launch.
  auto* proc = new QProcess(QCoreApplication::instance());
  proc->setProcessEnvironment(env);
  proc->setProgram(program);
  proc->setArguments(arguments);
  QObject::connect(
      proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), proc,
      [proc, mutationLocks, settingsDialog, prefixPath = runtime.prefixPath,
       mutationOwner](int, QProcess::ExitStatus) {
        const auto retired = WineSaveDeployment::endSessionLease(
            prefixPath, prefixPath, mutationOwner);
        if (!retired) {
          MOBase::log::error(
              "Winetricks finished but its prefix-session marker could not be "
              "retired: {}",
              retired.error);
        }
        if (settingsDialog != nullptr) {
          settingsDialog->endRuntimeMutation();
        }
        proc->deleteLater();
      });
  proc->start();
  if (!proc->waitForStarted(5000)) {
    QMessageBox::warning(parentWidget(), tr("Winetricks Failed"),
                         proc->errorString());
    const auto retired = WineSaveDeployment::endSessionLease(
        runtime.prefixPath, runtime.prefixPath, mutationOwner);
    if (!retired) {
      MOBase::log::error("Could not retire failed Winetricks session: {}",
                         retired.error);
    }
    dialog().endRuntimeMutation();
    delete proc;
  }
}

void ProtonSettingsTab::runPrefixSetupDialog(uint32_t appId,
                                              const QString& prefixPath,
                                              const QString& protonName,
                                              const QString& protonPath)
{
  if (!QDir().mkpath(prefixPath)) {
    ui->protonStatusLabel->setText(
        tr("Could not prepare the prefix for setup ownership"));
    return;
  }
  const QString setupOwner =
      QStringLiteral("prefix-setup-") +
      QUuid::createUuid().toString(QUuid::WithoutBraces);
  const auto setupSession = WineSaveDeployment::beginSessionLease(
      prefixPath, prefixPath, setupOwner);
  if (!setupSession) {
    QMessageBox::warning(parentWidget(), tr("Prefix In Use"),
                         setupSession.error);
    return;
  }
  PrefixSetupDialog dialog(prefixPath, protonPath, appId, parentWidget());
  const int result = dialog.exec();

  if (result == QDialog::Accepted && dialog.succeeded()) {
    // All steps succeeded — save config to mark prefix as complete.
    FluorineConfig cfg;
    cfg.app_id      = appId;
    cfg.prefix_path = prefixPath;
    cfg.proton_name = protonName;
    cfg.proton_path = protonPath;
    cfg.created     = QDateTime::currentDateTime().toString(Qt::ISODate);

    if (!cfg.save()) {
      ui->protonStatusLabel->setText(tr("Error saving Fluorine config"));
    } else {
      ui->protonStatusLabel->setText(tr("Prefix Active"));
      this->dialog().markRuntimeLifecycleChanged();
    }
  } else {
    ui->protonStatusLabel->setText(tr("Prefix setup incomplete"));
  }

  if (!dialog.quiescent()) {
    MOBase::log::error(
        "Prefix setup dialog returned before its worker proved quiescence; "
        "retaining persistent ownership for recovery");
    ui->protonStatusLabel->setText(
        tr("Prefix setup teardown is incomplete; recovery is required"));
    refreshState();
    return;
  }

  const auto retired = WineSaveDeployment::endSessionLease(
      prefixPath, prefixPath, setupOwner);
  if (!retired) {
    QMessageBox::critical(
        parentWidget(), tr("Prefix Recovery Required"),
        tr("Prefix setup finished, but Fluorine could not retire its durable "
           "setup owner. The prefix will remain blocked to protect it from "
           "concurrent launches.\n\n%1")
            .arg(retired.error));
  }

  refreshState();
}

void ProtonSettingsTab::appendInstallLog(const QString& message)
{
  ui->nakInstallLog->append(message);
}

void ProtonSettingsTab::logCallback(const char* message)
{
  if (message && *message) {
    MOBase::log::info("{}", message);
  }

  if (auto* tab = g_activeInstallTab.load(); tab != nullptr && message && *message) {
    const QString msg = QString::fromUtf8(message);
    QMetaObject::invokeMethod(tab,
                              [tab, msg] {
                                tab->appendInstallLog(msg);
                              },
                              Qt::QueuedConnection);
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

  ui->protonStatusLabel->setText(tr("Done"));
  refreshState();
}
