#include "settingsdialogproton.h"

#include "fluorineconfig.h"
#include "fluorinepaths.h"
#include "ui_settingsdialog.h"

#include <QtConcurrent/QtConcurrentRun>
#include <log.h>
#include <nak_ffi.h>
#include <atomic>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QMetaObject>
#include <QProcess>
#include <QSettings>
#include <QScopeGuard>
#include <QStandardPaths>
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
  QObject::connect(ui->fixGameRegistriesButton, &QPushButton::clicked, this,
                   &ProtonSettingsTab::onFixGameRegistries);
  QObject::connect(ui->winetricksButton, &QPushButton::clicked, this,
                   &ProtonSettingsTab::onWinetricks);
  QObject::connect(ui->prefixLocationBrowseButton, &QPushButton::clicked, this,
                   &ProtonSettingsTab::onBrowsePrefixLocation);

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
  ui->fixGameRegistriesButton->setEnabled(!m_busy && active);
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
  if (!QDir().mkpath(pfxPath)) {
    ui->protonStatusLabel->setText(tr("Failed to create prefix directory"));
    return;
  }

  setBusy(true);
  ui->protonStatusLabel->setText(tr("Creating prefix..."));

  startInstallTask(0, pfxPath, protonName, protonPath,
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

  cfg->destroyPrefix();

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

void ProtonSettingsTab::onBrowsePrefixLocation()
{
  QFileDialog::Options opts;
#ifndef _WIN32
  if (qEnvironmentVariableIsSet("FLATPAK_ID"))
    opts |= QFileDialog::DontUseNativeDialog;
#endif
  const QString dir = QFileDialog::getExistingDirectory(
      parentWidget(), tr("Select Prefix Location"), ui->prefixLocationEdit->text(),
      opts);
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

  // In Flatpak, wine/winetricks must run on the host via flatpak-spawn --host
  // because Proton binaries need the host's linker and Steam Runtime libs.
  const bool flatpak = QFileInfo::exists(QStringLiteral("/.flatpak-info"));

  QString program;
  QStringList arguments;

  if (flatpak) {
    program = QStringLiteral("flatpak-spawn");
    arguments << QStringLiteral("--host");
    for (const QString& flag : envFlags) {
      arguments << QStringLiteral("--env=%1").arg(flag);
    }
    arguments << winetricksPath << QStringLiteral("--gui");
  } else {
    program = winetricksPath;
    arguments << QStringLiteral("--gui");

    // For non-Flatpak, set env vars directly on the process
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
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
    return;
  }

  QProcess::startDetached(program, arguments);
}

void ProtonSettingsTab::onFixGameRegistries()
{
  if (m_busy) {
    return;
  }

  showGameRegistryDialog();
}

void ProtonSettingsTab::showGameRegistryDialog()
{
  auto cfg = FluorineConfig::load();
  if (!cfg.has_value() || !cfg->prefixExists()) {
    ui->protonStatusLabel->setText(tr("No existing prefix"));
    refreshState();
    return;
  }

  QDialog dialog(parentWidget());
  dialog.setWindowTitle(tr("Fix Game Registries"));
  dialog.setMinimumWidth(500);

  auto* layout = new QVBoxLayout(&dialog);

  layout->addWidget(new QLabel(tr("Select the game to apply registry settings for:"),
                               &dialog));

  auto* gameCombo = new QComboBox(&dialog);

  size_t knownCount  = 0;
  const NakKnownGame* knownGames = nak_get_known_games(&knownCount);

  for (size_t i = 0; i < knownCount; ++i) {
    const QString name =
        QString::fromUtf8(knownGames[i].name ? knownGames[i].name : "");
    if (!name.isEmpty()) {
      gameCombo->addItem(name);
    }
  }

  layout->addWidget(gameCombo);

  // Game installation path
  layout->addWidget(new QLabel(tr("Game installation path:"), &dialog));

  auto* pathLayout = new QHBoxLayout();
  auto* pathEdit   = new QLineEdit(&dialog);
  pathEdit->setPlaceholderText(tr("e.g. /home/user/.local/share/Steam/steamapps/common/Skyrim Special Edition"));
  auto* browseBtn = new QPushButton(tr("Browse..."), &dialog);
  pathLayout->addWidget(pathEdit);
  pathLayout->addWidget(browseBtn);
  layout->addLayout(pathLayout);

  QObject::connect(browseBtn, &QPushButton::clicked, &dialog, [&dialog, pathEdit]() {
    QFileDialog::Options opts;
#ifndef _WIN32
    if (qEnvironmentVariableIsSet("FLATPAK_ID"))
      opts |= QFileDialog::DontUseNativeDialog;
#endif
    const QString dir = QFileDialog::getExistingDirectory(
        &dialog, QObject::tr("Select Game Installation Folder"),
        pathEdit->text().isEmpty() ? QDir::homePath() : pathEdit->text(), opts);
    if (!dir.isEmpty()) {
      pathEdit->setText(dir);
    }
  });

  // Try to auto-detect path when game selection changes
  auto autoDetect = [pathEdit, gameCombo]() {
    const QString gameName = gameCombo->currentText();
    if (gameName.isEmpty()) return;

    // Use nak_detect_all_games to find the install path
    NakGameList gameList = nak_detect_all_games();
    for (size_t i = 0; i < gameList.count; ++i) {
      const QString detected =
          QString::fromUtf8(gameList.games[i].name ? gameList.games[i].name : "");
      if (detected.contains(gameName, Qt::CaseInsensitive) ||
          gameName.contains(detected, Qt::CaseInsensitive)) {
        const QString path = QString::fromUtf8(
            gameList.games[i].install_path ? gameList.games[i].install_path : "");
        if (!path.isEmpty()) {
          pathEdit->setText(path);
          break;
        }
      }
    }
    nak_game_list_free(gameList);
  };

  QObject::connect(gameCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                   &dialog, autoDetect);
  // Auto-detect for initially selected game
  autoDetect();

  auto* buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);

  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  const QString selectedGame  = gameCombo->currentText();
  const QString gamePath      = pathEdit->text();
  const QString prefixPath    = cfg->prefix_path;
  const QString protonName    = cfg->proton_name;
  const QString protonPath    = cfg->proton_path;

  if (gamePath.isEmpty()) {
    QMessageBox::warning(parentWidget(), tr("No Path"),
                         tr("Please provide the game installation path."));
    return;
  }

  setBusy(true);
  ui->protonStatusLabel->setText(tr("Fixing game registries..."));

  g_activeInstallTab.store(this);

  m_pendingPrefixPath = prefixPath;
  m_pendingProtonName = protonName;
  m_pendingProtonPath = protonPath;

  m_installWatcher.setFuture(
      QtConcurrent::run([prefixPath, protonName, protonPath,
                         selectedGame, gamePath]() -> InstallResult {
        const QByteArray prefixUtf8    = prefixPath.toUtf8();
        const QByteArray protonNmUtf8  = protonName.toUtf8();
        const QByteArray protonPthUtf8 = protonPath.toUtf8();
        const QByteArray gameUtf8      = selectedGame.toUtf8();
        const QByteArray pathUtf8      = gamePath.toUtf8();

        char* error = nak_apply_registry_for_game_path(
            prefixUtf8.constData(), protonNmUtf8.constData(),
            protonPthUtf8.constData(), gameUtf8.constData(),
            pathUtf8.constData(), &ProtonSettingsTab::logCallback);

        InstallResult r;
        if (error != nullptr) {
          r.error = QString::fromUtf8(error);
          nak_string_free(error);
        }

        return r;
      }));
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

    // Set WINEPREFIX so NAK (and its child processes like winetricks) always
    // target the correct prefix, even if Proton init via umu-run fails.
    qputenv("WINEPREFIX", prefixPathUtf8);

    // Point WINE/WINESERVER at Proton's binaries so winetricks and regedit
    // use Proton's wine instead of falling back to system wine.
    QByteArray protonWineUtf8;
    QByteArray protonWineserverUtf8;
    for (const char* subdir : {"files/bin", "dist/bin"}) {
      const QString candidate = QDir(protonPath).filePath(
          QString::fromLatin1(subdir) + "/wine");
      if (QFileInfo::exists(candidate)) {
        protonWineUtf8 = candidate.toUtf8();
        protonWineserverUtf8 =
            QDir(protonPath)
                .filePath(QString::fromLatin1(subdir) + "/wineserver")
                .toUtf8();
        break;
      }
    }
    if (!protonWineUtf8.isEmpty()) {
      qputenv("WINE", protonWineUtf8);
      qputenv("WINESERVER", protonWineserverUtf8);
    }

    const auto restoreNakEnv = qScopeGuard([protonWineUtf8] {
      qunsetenv("NAK_USE_UMU_FOR_PREFIX");
      qunsetenv("NAK_PREFER_SYSTEM_UMU");
      qunsetenv("NAK_USE_STEAM_RUN");
      qunsetenv("NAK_BUNDLED_UMU_RUN");
      qunsetenv("WINEPREFIX");
      if (!protonWineUtf8.isEmpty()) {
        qunsetenv("WINE");
        qunsetenv("WINESERVER");
      }
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
  if (message && *message) {
    MOBase::log::info("{}", message);
  }
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

  // Ensure DXVK config exists for game launches
  if (char* dxvkErr = nak_ensure_dxvk_conf(); dxvkErr != nullptr) {
    MOBase::log::warn("Failed to create dxvk.conf: {}", dxvkErr);
    nak_string_free(dxvkErr);
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
