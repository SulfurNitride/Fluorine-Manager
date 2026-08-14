#include "settingsdialogupdates.h"

#include "fluorineupdateinstaller.h"
#include "fluorineupdater.h"
#include "settings.h"
#include "ui_settingsdialog.h"

#include <fluorine_build_info.h>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

UpdatesSettingsTab::UpdatesSettingsTab(Settings& s, SettingsDialog& d)
    : SettingsTab(s, d)
{
  // Build the tab contents programmatically so we don't have to hand-edit
  // settingsdialog.ui — the Updates section used to live on the General
  // tab; moving it to its own tab keeps General uncluttered and gives us
  // room for "Check now" + "Install & restart" buttons with progress.
  QWidget* page = new QWidget(ui->tabWidget);
  auto* layout  = new QVBoxLayout(page);

  // --- Current build info ------------------------------------------------
  auto* infoGroup  = new QGroupBox(tr("Current build"), page);
  auto* infoLayout = new QFormLayout(infoGroup);

  const QString currentVersion = QStringLiteral(FLUORINE_DISPLAY_VERSION);
  const QString channel        = QStringLiteral(FLUORINE_BUILD_CHANNEL);
  const QString commit         = QStringLiteral(FLUORINE_BUILD_COMMIT);
  const QString timestamp      = QStringLiteral(FLUORINE_BUILD_TIMESTAMP);

  m_currentVersionLabel = new QLabel(currentVersion, infoGroup);
  infoLayout->addRow(tr("Version:"), m_currentVersionLabel);

  QString buildLine = QStringLiteral("channel=%1").arg(channel);
  if (!commit.isEmpty())
    buildLine += QStringLiteral("  commit=%1").arg(commit);
  if (!timestamp.isEmpty())
    buildLine += QStringLiteral("  timestamp=%1").arg(timestamp);
  m_buildInfoLabel = new QLabel(buildLine, infoGroup);
  m_buildInfoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  infoLayout->addRow(tr("Build:"), m_buildInfoLabel);

  layout->addWidget(infoGroup);

  // --- Preferences -------------------------------------------------------
  auto* prefsGroup  = new QGroupBox(tr("Update preferences"), page);
  auto* prefsLayout = new QVBoxLayout(prefsGroup);

  m_checkForUpdates = new QCheckBox(tr("Check for updates on startup"),
                                    prefsGroup);
  m_checkForUpdates->setToolTip(
      tr("Query GitHub for a newer Fluorine Manager build when the app "
         "starts. Nothing is installed automatically — Fluorine will ask "
         "before downloading or restarting."));
  prefsLayout->addWidget(m_checkForUpdates);

  auto* channelRow = new QHBoxLayout();
  channelRow->addWidget(new QLabel(tr("Channel:"), prefsGroup));
  m_channelBox = new QComboBox(prefsGroup);
  m_channelBox->addItem(tr("Stable (tagged releases)"),
                        QStringLiteral("stable"));
  m_channelBox->addItem(tr("Nightly (rolling build from main)"),
                        QStringLiteral("nightly"));
  channelRow->addWidget(m_channelBox, 1);
  prefsLayout->addLayout(channelRow);

  layout->addWidget(prefsGroup);

  // --- Actions -----------------------------------------------------------
  auto* actionGroup  = new QGroupBox(tr("Actions"), page);
  auto* actionLayout = new QVBoxLayout(actionGroup);

  auto* buttonRow = new QHBoxLayout();
  m_checkNowButton =
      new QPushButton(tr("Check for updates now"), actionGroup);
  m_installButton =
      new QPushButton(tr("Install update && restart"), actionGroup);
  m_installButton->setEnabled(false);
  buttonRow->addWidget(m_checkNowButton);
  buttonRow->addWidget(m_installButton);
  buttonRow->addStretch(1);
  actionLayout->addLayout(buttonRow);

  m_progressBar = new QProgressBar(actionGroup);
  m_progressBar->setVisible(false);
  m_progressBar->setRange(0, 100);
  actionLayout->addWidget(m_progressBar);

  m_statusLabel = new QLabel(actionGroup);
  m_statusLabel->setTextFormat(Qt::RichText);
  m_statusLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
  m_statusLabel->setOpenExternalLinks(true);
  m_statusLabel->setWordWrap(true);
  actionLayout->addWidget(m_statusLabel);

  layout->addWidget(actionGroup);
  layout->addStretch(1);

  // Insert after General.
  const int insertIndex =
      std::max(1, std::min(ui->tabWidget->count(), 1));
  ui->tabWidget->insertTab(insertIndex, page, tr("Updates"));

  // --- State -------------------------------------------------------------
  m_checkForUpdates->setChecked(settings().checkForUpdates());
  const QString currentChannel = settings().fluorineUpdateChannel();
  const int idx = m_channelBox->findData(currentChannel);
  m_channelBox->setCurrentIndex(idx >= 0 ? idx : 0);

  m_updater = new FluorineUpdater(&d);
  m_installer = new FluorineUpdateInstaller(&d);

  QObject::connect(m_checkNowButton, &QPushButton::clicked, &d,
                   [this]() { onCheckNow(); });

  QObject::connect(m_installButton, &QPushButton::clicked, &d,
                   [this]() { onInstall(); });

  QObject::connect(
      m_channelBox, qOverload<int>(&QComboBox::currentIndexChanged), &d,
      [this]() {
        if (m_checkInProgress || m_installer->isBusy()) {
          return;
        }
        clearPendingUpdate();
        m_statusLabel->setText(
            tr("The update channel changed. Check again for this channel."));
      });

  QObject::connect(m_updater, &FluorineUpdater::updateAvailable, &d,
          [this](const FluorineUpdater::ReleaseInfo& info) {
            setCheckInProgress(false);
            if (info.channel != selectedChannel()) {
              clearPendingUpdate();
              m_statusLabel->setText(
                  tr("The update channel changed. Check again for this channel."));
              return;
            }
            m_pendingUpdate = info;
            m_updatePending = true;
            const QString url = info.htmlUrl.isEmpty() ? QString() : info.htmlUrl;
            QString line = tr("<b>Update available:</b> %1")
                               .arg(info.name.isEmpty() ? info.tagName
                                                        : info.name);
            if (!url.isEmpty()) {
              line += QStringLiteral(
                          " &mdash; <a href=\"%1\">view release</a>")
                          .arg(url);
            }
            m_statusLabel->setText(line);
            m_installButton->setEnabled(!info.downloadUrl.isEmpty());
            if (info.downloadUrl.isEmpty()) {
              m_installButton->setToolTip(
                  tr("This release has no installable archive attached."));
            } else {
              m_installButton->setToolTip(QString());
            }
          });
  QObject::connect(m_updater, &FluorineUpdater::upToDate, &d,
          [this](const FluorineUpdater::ReleaseInfo&) {
            setCheckInProgress(false);
            clearPendingUpdate();
            m_statusLabel->setText(
                tr("You're on the latest build for this channel."));
          });
  QObject::connect(m_updater, &FluorineUpdater::checkFailed, &d,
          [this](const QString& reason) {
            setCheckInProgress(false);
            clearPendingUpdate();
            m_statusLabel->setText(
                tr("<i>Update check failed:</i> %1").arg(reason));
          });
  QObject::connect(m_installer, &FluorineUpdateInstaller::statusChanged, &d,
                   [this](const QString& status) {
                     m_statusLabel->setText(status);
                     m_progressBar->setRange(0, 0);
                   });
  QObject::connect(
      m_installer, &FluorineUpdateInstaller::downloadProgress, &d,
      [this](qint64 received, qint64 total) {
        if (total > 0) {
          m_progressBar->setRange(0, 100);
          m_progressBar->setValue(
              static_cast<int>((received * 100) / total));
        } else {
          m_progressBar->setRange(0, 0);
        }
      });
  QObject::connect(m_installer, &FluorineUpdateInstaller::failed, &d,
                   [this](const QString& reason) {
                     m_statusLabel->setText(
                         tr("<i>Install failed:</i> %1").arg(reason));
                     m_progressBar->setVisible(false);
                     m_channelBox->setEnabled(true);
                     m_checkNowButton->setEnabled(true);
                     m_installButton->setEnabled(
                         m_updatePending && !m_pendingUpdate.downloadUrl.isEmpty() &&
                         m_pendingUpdate.channel == selectedChannel());
                   });
}

void UpdatesSettingsTab::update()
{
  settings().setCheckForUpdates(m_checkForUpdates->isChecked());
  const QString channel = m_channelBox->currentData().toString();
  settings().setFluorineUpdateChannel(channel);
  settings().setUsePrereleases(channel == QStringLiteral("nightly"));
}

void UpdatesSettingsTab::onCheckNow()
{
  if (settings().network().offlineMode()) {
    clearPendingUpdate();
    m_statusLabel->setText(
        tr("Cannot check for updates while Offline Mode is enabled."));
    return;
  }

  clearPendingUpdate();
  m_statusLabel->setText(tr("Checking…"));
  setCheckInProgress(true);

  m_updater->checkForUpdates(selectedChannel());
}

void UpdatesSettingsTab::onInstall()
{
  if (!m_updatePending || m_pendingUpdate.downloadUrl.isEmpty() ||
      m_pendingUpdate.channel != selectedChannel() || m_installer->isBusy()) {
    return;
  }

  m_installButton->setEnabled(false);
  m_checkNowButton->setEnabled(false);
  m_channelBox->setEnabled(false);
  m_progressBar->setVisible(true);
  m_progressBar->setRange(0, 100);
  m_progressBar->setValue(0);
  m_installer->install(m_pendingUpdate);
}

void UpdatesSettingsTab::clearPendingUpdate()
{
  m_pendingUpdate = {};
  m_updatePending = false;
  m_installButton->setEnabled(false);
  m_installButton->setToolTip(QString());
}

void UpdatesSettingsTab::setCheckInProgress(bool checking)
{
  m_checkInProgress = checking;
  const bool controlsEnabled = !checking && !m_installer->isBusy();
  m_checkNowButton->setEnabled(controlsEnabled);
  m_channelBox->setEnabled(controlsEnabled);
  if (checking) {
    m_installButton->setEnabled(false);
  }
}

FluorineUpdater::Channel UpdatesSettingsTab::selectedChannel() const
{
  return FluorineUpdater::channelFromString(
      m_channelBox->currentData().toString(), FluorineUpdater::buildChannel());
}
