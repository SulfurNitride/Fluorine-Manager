#include "settingsdialogproton.h"

#include "fluorineconfig.h"
#include "fluorinepaths.h"
#include "fusemountoptions.h"
#include "fusepermissions.h"
#include "prefixsetupdialog.h"
#include "ui_settingsdialog.h"
#include "vfsbackend.h"

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
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QMetaObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressDialog>
#include <QSettings>
#include <QScopeGuard>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

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
  ui->fuseAllowOtherCheckBox->setChecked(
      instanceSettings.value(kFuseAllowOtherSetting, false).toBool());
  QObject::connect(ui->fuseAllowOtherCheckBox, &QCheckBox::clicked, this,
                   &ProtonSettingsTab::onFuseAllowOtherClicked);
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

  QObject::connect(ui->protonVersionCombo, &QComboBox::currentIndexChanged, this,
                   [this](int index) {
                     if (index < 0) {
                       return;
                     }

                     auto cfg = FluorineConfig::load();
                     if (!cfg.has_value()) {
                       return;
                     }

                     const QString protonName =
                         ui->protonVersionCombo->currentText().trimmed();
                     const QString protonPath = ui->protonVersionCombo
                                                    ->itemData(index, Qt::UserRole + 1)
                                                    .toString()
                                                    .trimmed();

                     if (protonName.isEmpty() || protonPath.isEmpty()) {
                       MOBase::log::warn("Proton combo change: name='{}' path='{}' — "
                                         "skipping save (empty)", protonName, protonPath);
                       return;
                     }

                     if (cfg->proton_name != protonName ||
                         cfg->proton_path != protonPath) {
                       cfg->proton_name = protonName;
                       cfg->proton_path = protonPath;
                       cfg->save();
                       MOBase::log::info("Updated Proton config: {} ({})",
                                         protonName, protonPath);
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

void ProtonSettingsTab::update()
{
  QSettings().setValue("fluorine/launch_wrapper", ui->launchWrapperEdit->text());
  QSettings().setValue("fluorine/disable_vfs_cache",
                       ui->disableVfsCacheCheckBox->isChecked());
  QSettings instanceSettings(settings().filename(), QSettings::IniFormat);
  instanceSettings.setValue(kFuseAllowOtherSetting,
                            ui->fuseAllowOtherCheckBox->isChecked());
  instanceSettings.setValue(kVfsBackendSetting,
                            ui->vfsBackendComboBox->currentData().toString());
  instanceSettings.setValue(kUsvfsExactQueryExhaustionSetting,
                            ui->usvfsExactQueryExhaustionCheckBox->isChecked());
  instanceSettings.setValue(kUsvfsSharedContextSetting,
                            ui->usvfsSharedContextCheckBox->isChecked());
}

void ProtonSettingsTab::onFuseAllowOtherClicked(bool checked)
{
  if (!checked) {
    return;
  }

  const auto result = ensureFuseUserAllowOther(
      QStringLiteral("/etc/fuse.conf"), [this](const QByteArray& directive) {
        const auto answer = QMessageBox::question(
            parentWidget(), tr("Enable FUSE Access for Other Users"),
            tr("Fluorine needs to add user_allow_other to /etc/fuse.conf.\n\n"
               "This system-wide permission lets local users choose to share "
               "their FUSE mounts. Fluorine will share this instance's mounts "
               "only while this option is enabled, subject to file permissions.\n\n"
               "The system change remains if you later cancel Settings or turn "
               "this option off. Your desktop will request administrator "
               "authentication to make the change.\n\nContinue?"),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) {
          return FusePermissionResult{FusePermissionStatus::Cancelled, {}};
        }

        // Resolve host tools only, never an executable from the app bundle or
        // an arbitrary user PATH entry. NixOS exposes these through /run.
        const QStringList systemPaths{QStringLiteral("/usr/bin"),
                                      QStringLiteral("/bin"),
                                      QStringLiteral("/run/wrappers/bin"),
                                      QStringLiteral("/run/current-system/sw/bin")};
        const QString pkexec = QStandardPaths::findExecutable("pkexec", systemPaths);
        const QString tee = QStandardPaths::findExecutable("tee", systemPaths);
        if (pkexec.isEmpty() || tee.isEmpty()) {
          return FusePermissionResult{
              FusePermissionStatus::Failed,
              tr("Administrator authentication tools are unavailable. Install "
                 "polkit and a desktop authentication agent, or ask an "
                 "administrator to enable user_allow_other in /etc/fuse.conf.")};
        }

        QProcess process;
        QEventLoop loop;
        QProgressDialog progress(tr("Waiting for administrator authentication…"),
                                 tr("Cancel"), 0, 0, parentWidget());
        progress.setWindowTitle(tr("Configure FUSE Permissions"));
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);
        bool cancelled = false;
        bool timedOut = false;
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &process, [&] {
          timedOut = true;
          process.kill();
        });
        QObject::connect(&progress, &QProgressDialog::canceled, &process, [&] {
          cancelled = true;
          process.kill();
        });
        QObject::connect(&process, &QProcess::started, &process, [&] {
          process.write(directive);
          process.closeWriteChannel();
        });
        QObject::connect(&process, &QProcess::finished, &loop, &QEventLoop::quit,
                         Qt::QueuedConnection);
        QObject::connect(&process, &QProcess::errorOccurred, &loop,
                         [&](QProcess::ProcessError error) {
                           if (error == QProcess::FailedToStart) {
                             loop.quit();
                           }
                         }, Qt::QueuedConnection);

        // No shell, scripts, or user-controlled arguments run as root. polkit
        // owns the password dialog; tee only appends this one fixed directive.
        process.start(pkexec, {QStringLiteral("--disable-internal-agent"), tee,
                              QStringLiteral("-a"), QStringLiteral("--"),
                              QStringLiteral("/etc/fuse.conf")});
        timeout.start(120000);
        loop.exec();
        progress.reset();
        if (timedOut) {
          return FusePermissionResult{FusePermissionStatus::Failed,
                                      tr("Administrator authentication timed out.")};
        }
        if (cancelled || (process.exitStatus() == QProcess::NormalExit &&
                          process.exitCode() == 126)) {
          return FusePermissionResult{FusePermissionStatus::Cancelled, {}};
        }
        if (process.error() == QProcess::FailedToStart ||
            process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
          const QString details = QString::fromLocal8Bit(process.readAllStandardError())
                                      .trimmed();
          return FusePermissionResult{FusePermissionStatus::Failed,
                                      details.isEmpty() ? process.errorString()
                                                        : details};
        }
        return FusePermissionResult{FusePermissionStatus::Enabled, {}};
      });

  if (result.status != FusePermissionStatus::Enabled) {
    const QSignalBlocker blocker(ui->fuseAllowOtherCheckBox);
    ui->fuseAllowOtherCheckBox->setChecked(false);
    if (result.status == FusePermissionStatus::Failed) {
      QMessageBox::warning(
          parentWidget(), tr("FUSE Permissions Not Enabled"),
          tr("Fluorine could not verify user_allow_other in /etc/fuse.conf. "
             "Access for other users has been left off.\n\n%1\n\n"
             "An administrator can enable user_allow_other in the host's "
             "/etc/fuse.conf, then you can try this option again.")
              .arg(result.error));
    }
  }
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
  }

  if (auto cfg = FluorineConfig::load(); cfg.has_value()) {
    const int idx = ui->protonVersionCombo->findText(cfg->proton_name);
    if (idx >= 0) {
      ui->protonVersionCombo->setCurrentIndex(idx);
    } else if (ui->protonVersionCombo->count() > 0) {
      // Saved Proton version no longer exists — select first available and
      // update the config so the stale path doesn't cause launch failures.
      MOBase::log::warn("Saved Proton '{}' not found, defaulting to '{}'",
                        cfg->proton_name,
                        ui->protonVersionCombo->itemText(0));
      ui->protonVersionCombo->setCurrentIndex(0);
      cfg->proton_name = ui->protonVersionCombo->itemText(0).trimmed();
      cfg->proton_path = ui->protonVersionCombo->itemData(0, Qt::UserRole + 1)
                             .toString().trimmed();
      cfg->save();
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

  if (active) {
    ui->prefixLocationEdit->setText(*prefix);
    ui->prefixLocationEdit->setReadOnly(true);
  } else {
    ui->prefixLocationEdit->setReadOnly(false);
    if (ui->prefixLocationEdit->text().isEmpty()) {
      ui->prefixLocationEdit->setText(
          fluorineDataDir() + "/Prefix");
    }
  }

  ui->prefixLocationBrowseButton->setEnabled(!m_busy && !active);
  ui->createPrefixButton->setEnabled(!m_busy && !active);
  ui->deletePrefixButton->setEnabled(!m_busy && active);
  ui->recreatePrefixButton->setEnabled(!m_busy && active);
  ui->openPrefixFolderButton->setEnabled(!m_busy && active);
  ui->winetricksButton->setEnabled(!m_busy && active);
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

  if (!QDir().mkpath(pfxPath)) {
    ui->protonStatusLabel->setText(tr("Failed to create prefix directory"));
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

  auto cfg = FluorineConfig::load();
  if (!cfg.has_value()) {
    ui->protonStatusLabel->setText(tr("No Prefix"));
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

  if (!cfg->canDestroyPrefix()) {
    QMessageBox::critical(
        parentWidget(), tr("Prefix Not Recreated"),
        tr("Fluorine refused to recreate this prefix because it could not "
           "verify ownership."));
    ui->protonStatusLabel->setText(tr("Prefix ownership could not be verified"));
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

  runPrefixSetupDialog(cfg->app_id, cfg->prefix_path, cfg->proton_name,
                       cfg->proton_path);
}

void ProtonSettingsTab::onOpenPrefixFolder()
{
  auto path = FluorineConfig::prefixPath();
  if (!path.has_value()) {
    ui->protonStatusLabel->setText(tr("No Prefix"));
    return;
  }

  MOBase::shell::Explore(QDir(*path));
}

void ProtonSettingsTab::onDownloadSLR()
{
  if (isSlrInstalled()) {
    QMessageBox::information(parentWidget(), tr("Steam Linux Runtime"),
        tr("Steam Linux Runtime is already installed."));
    return;
  }

  ui->downloadSLRButton->setEnabled(false);
  auto* progress = new QProgressDialog(
      tr("Downloading Steam Linux Runtime (~200 MB)...\n"
         "Check the MO2 log for progress details."),
      tr("Cancel"), 0, 0, parentWidget());
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
        ui->downloadSLRButton->setEnabled(true);

        const QString err = watcher->result();
        if (!err.isEmpty()) {
          MOBase::log::error("[SLR] Download failed: {}", err);
          QMessageBox::warning(parentWidget(), tr("Steam Linux Runtime"),
              tr("Download failed:\n%1").arg(err));
        } else {
          MOBase::log::info("[SLR] Steam Linux Runtime installed successfully");
          QMessageBox::information(parentWidget(), tr("Steam Linux Runtime"),
              tr("Steam Linux Runtime installed successfully."));
        }
        delete cancelFlag;
      });

  int* cancelPtr = cancelFlag;
  watcher->setFuture(QtConcurrent::run([cancelPtr]() -> QString {
    return downloadSlr(nullptr, nullptr, cancelPtr);
  }));
  progress->show();
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
  auto cfg = FluorineConfig::load();
  if (!cfg.has_value() || !cfg->prefixExists()) {
    ui->protonStatusLabel->setText(tr("No existing prefix"));
    refreshState();
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
  envFlags << QStringLiteral("WINEPREFIX=%1").arg(cfg->prefix_path);

  const QString protonWine = findProtonWine(cfg->proton_path);
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
  QProcess proc;
  proc.setProcessEnvironment(env);
  proc.setProgram(program);
  proc.setArguments(arguments);
  proc.startDetached();
}

void ProtonSettingsTab::runPrefixSetupDialog(uint32_t appId,
                                              const QString& prefixPath,
                                              const QString& protonName,
                                              const QString& protonPath)
{
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
    }
  } else {
    ui->protonStatusLabel->setText(tr("Prefix setup incomplete"));
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
