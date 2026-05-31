#include "settingsdialogupdates.h"

#include "fluorineupdater.h"
#include "settings.h"
#include "shared/util.h"
#include "ui_settingsdialog.h"

#include <fluorine_build_info.h>
#include <log.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <sys/types.h>
#include <unistd.h>

#include <algorithm>

namespace
{

bool isLauncherFile(const QString& path)
{
  const QFileInfo info(path);
  return info.isFile();
}

}  // namespace

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
         "starts. Nothing is installed automatically — you'll see a notice "
         "here and can install from the button below."));
  prefsLayout->addWidget(m_checkForUpdates);

  auto* channelRow = new QHBoxLayout();
  channelRow->addWidget(new QLabel(tr("Channel:"), prefsGroup));
  m_channelBox = new QComboBox(prefsGroup);
  m_channelBox->addItem(tr("Stable (tagged releases)"),
                        QStringLiteral("stable"));
  m_channelBox->addItem(tr("Beta (rolling build from main)"),
                        QStringLiteral("beta"));
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

  QObject::connect(m_checkNowButton, &QPushButton::clicked, &d,
                   [this]() { onCheckNow(); });

  QObject::connect(m_installButton, &QPushButton::clicked, &d,
                   [this]() { onInstall(); });

  QObject::connect(m_updater, &FluorineUpdater::updateAvailable, &d,
          [this](const FluorineUpdater::ReleaseInfo& info) {
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
            m_checkNowButton->setEnabled(true);
            m_installButton->setEnabled(!info.downloadUrl.isEmpty());
            if (info.downloadUrl.isEmpty()) {
              m_installButton->setToolTip(
                  tr("This release has no .tar.gz asset attached."));
            } else {
              m_installButton->setToolTip(QString());
            }
          });
  QObject::connect(m_updater, &FluorineUpdater::upToDate, &d,
          [this](const FluorineUpdater::ReleaseInfo&) {
            m_updatePending = false;
            m_installButton->setEnabled(false);
            m_statusLabel->setText(
                tr("You're on the latest build for this channel."));
            m_checkNowButton->setEnabled(true);
          });
  QObject::connect(m_updater, &FluorineUpdater::checkFailed, &d,
          [this](const QString& reason) {
            m_statusLabel->setText(
                tr("<i>Update check failed:</i> %1").arg(reason));
            m_checkNowButton->setEnabled(true);
          });
}

void UpdatesSettingsTab::update()
{
  settings().setCheckForUpdates(m_checkForUpdates->isChecked());
  const QString channel = m_channelBox->currentData().toString();
  settings().setFluorineUpdateChannel(channel);
  settings().setUsePrereleases(channel == QStringLiteral("beta"));
}

void UpdatesSettingsTab::onCheckNow()
{
  m_statusLabel->setText(tr("Checking…"));
  m_checkNowButton->setEnabled(false);
  m_installButton->setEnabled(false);

  const QString channel = m_channelBox->currentData().toString();
  const FluorineUpdater::Channel c = FluorineUpdater::channelFromString(
      channel, FluorineUpdater::buildChannel());
  m_updater->checkForUpdates(c);
}

void UpdatesSettingsTab::onInstall()
{
  if (!m_updatePending || m_pendingUpdate.downloadUrl.isEmpty()) {
    return;
  }

  const QString downloadUrl = m_pendingUpdate.downloadUrl;

  // Staging dir for the downloaded tarball. Separate from the live bin/
  // so we can fall back cleanly if anything fails mid-extract.
  const QString dataRoot =
      QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
      QStringLiteral("/fluorine");
  const QString stagingDir = dataRoot + QStringLiteral("/update-staging");
  QDir().mkpath(stagingDir);
  // Pick archive file extension from the download URL so tar vs unzip
  // picks the right tool below.
  const bool isZip =
      downloadUrl.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive);
  const QString archivePath =
      stagingDir + (isZip ? QStringLiteral("/download.zip")
                          : QStringLiteral("/download.tar.gz"));
  const QString extractDir = stagingDir + QStringLiteral("/extract");

  QFile::remove(archivePath);
  QDir(extractDir).removeRecursively();
  QDir().mkpath(extractDir);

  m_installButton->setEnabled(false);
  m_checkNowButton->setEnabled(false);
  m_progressBar->setVisible(true);
  m_progressBar->setRange(0, 100);
  m_progressBar->setValue(0);
  m_statusLabel->setText(tr("Downloading update…"));

  auto* nam = new QNetworkAccessManager(m_installButton);
  QNetworkRequest req{QUrl(downloadUrl)};
  req.setRawHeader("User-Agent", "Fluorine-Manager/updater");
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::NoLessSafeRedirectPolicy);
  QNetworkReply* reply = nam->get(req);

  auto* outFile = new QFile(archivePath, reply);
  if (!outFile->open(QIODevice::WriteOnly)) {
    m_statusLabel->setText(
        tr("<i>Install failed:</i> cannot open '%1' for writing")
            .arg(archivePath));
    m_progressBar->setVisible(false);
    m_installButton->setEnabled(true);
    m_checkNowButton->setEnabled(true);
    reply->deleteLater();
    return;
  }

  QObject::connect(reply, &QNetworkReply::readyRead, reply,
                   [reply, outFile]() {
                     outFile->write(reply->readAll());
                   });
  QObject::connect(
      reply, &QNetworkReply::downloadProgress, m_progressBar,
      [this](qint64 received, qint64 total) {
        if (total > 0) {
          m_progressBar->setRange(0, 100);
          m_progressBar->setValue(
              static_cast<int>((received * 100) / total));
        } else {
          m_progressBar->setRange(0, 0);  // indeterminate
        }
      });
  QObject::connect(
      reply, &QNetworkReply::finished, m_installButton,
      [this, reply, outFile, archivePath, extractDir, stagingDir, isZip]() {
        outFile->close();
        const QNetworkReply::NetworkError err = reply->error();
        const QString errString               = reply->errorString();
        reply->deleteLater();

        if (err != QNetworkReply::NoError) {
          m_statusLabel->setText(
              tr("<i>Download failed:</i> %1").arg(errString));
          m_progressBar->setVisible(false);
          m_installButton->setEnabled(true);
          m_checkNowButton->setEnabled(true);
          return;
        }

        m_statusLabel->setText(tr("Extracting update…"));
        m_progressBar->setRange(0, 0);  // indeterminate while extract runs

        auto* extractor = new QProcess(m_installButton);
        extractor->setWorkingDirectory(extractDir);
        if (isZip) {
          extractor->setProgram(QStringLiteral("unzip"));
          extractor->setArguments(
              {QStringLiteral("-q"), archivePath});
        } else {
          extractor->setProgram(QStringLiteral("tar"));
          extractor->setArguments(
              {QStringLiteral("xzf"), archivePath});
        }
        QObject::connect(
            extractor,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            m_installButton,
            [this, extractor, archivePath, extractDir, stagingDir](
                int code, QProcess::ExitStatus st) {
              extractor->deleteLater();
              if (st != QProcess::NormalExit || code != 0) {
                m_statusLabel->setText(
                    tr("<i>Extraction failed:</i> extractor exited with %1")
                        .arg(code));
                m_progressBar->setVisible(false);
                m_installButton->setEnabled(true);
                m_checkNowButton->setEnabled(true);
                return;
              }

              // Release assets are not consistent: tarballs are packed as
              // `fluorine-manager/...`, while zips may extract files directly
              // into the root. Accept both layouts.
              QDir extract(extractDir);
              QString newLauncher =
                  extract.absoluteFilePath(QStringLiteral("fluorine-manager"));
              if (!isLauncherFile(newLauncher)) {
                const QStringList tops = extract.entryList(
                    QDir::Dirs | QDir::NoDotAndDotDot);
                for (const QString& top : tops) {
                  const QString candidate = extract.absoluteFilePath(
                      top + QStringLiteral("/fluorine-manager"));
                  if (isLauncherFile(candidate)) {
                    newLauncher = candidate;
                    break;
                  }
                }
              }
              if (isLauncherFile(newLauncher)) {
                QFile::setPermissions(
                    newLauncher,
                    QFile::permissions(newLauncher) | QFile::ExeOwner |
                        QFile::ExeGroup | QFile::ExeOther);
              }
              if (!isLauncherFile(newLauncher) ||
                  !QFileInfo(newLauncher).isExecutable()) {
                m_statusLabel->setText(
                    tr("<i>Install failed:</i> launcher not found in "
                       "extracted archive, or it is not executable"));
                m_progressBar->setVisible(false);
                m_installButton->setEnabled(true);
                m_checkNowButton->setEnabled(true);
                return;
              }

              // Write a tiny helper script that waits for the current
              // Fluorine process to exit (the fluorine-manager launcher
              // refuses to re-sync itself while we're still holding bin/
              // open) then execs the new launcher, which performs its
              // own sync into ~/.local/share/fluorine/bin/.
              const QString helperPath =
                  stagingDir + QStringLiteral("/install.sh");
              const qint64 currentPid = ::getpid();
              QFile helper(helperPath);
              if (!helper.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                m_statusLabel->setText(
                    tr("<i>Install failed:</i> cannot write install helper"));
                m_progressBar->setVisible(false);
                m_installButton->setEnabled(true);
                m_checkNowButton->setEnabled(true);
                return;
              }
              const QString script =
                  QStringLiteral(
                      "#!/usr/bin/env bash\n"
                      "set -u\n"
                      "OLD_PID=\"$1\"\n"
                      "NEW_LAUNCHER=\"$2\"\n"
                      "for _ in $(seq 1 200); do\n"
                      "  if ! kill -0 \"$OLD_PID\" 2>/dev/null; then break; fi\n"
                      "  sleep 0.1\n"
                      "done\n"
                      "exec \"$NEW_LAUNCHER\"\n");
              helper.write(script.toUtf8());
              helper.close();
              QFile::setPermissions(
                  helperPath,
                  QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                      QFile::ReadGroup | QFile::ExeGroup |
                      QFile::ReadOther | QFile::ExeOther);

              m_statusLabel->setText(
                  tr("Update staged. Restarting Fluorine Manager…"));

              // Detach the helper so it survives our exit.
              QProcess::startDetached(QStringLiteral("/usr/bin/env"),
                                      {QStringLiteral("bash"), helperPath,
                                       QString::number(currentPid),
                                       newLauncher});

              MOBase::log::info(
                  "update installer: spawned helper to restart into {}",
                  newLauncher);

              // Give the signal a beat to propagate, then quit cleanly.
              QTimer::singleShot(250, qApp,
                                 []() { ExitModOrganizer(Exit::Force); });
            });
        extractor->start();
      });
}
