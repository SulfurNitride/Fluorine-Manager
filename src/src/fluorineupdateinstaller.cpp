#include "fluorineupdateinstaller.h"
#include "processlifetime.h"
#include "updaterrestartpolicy.h"

#include "shared/util.h"

#include <log.h>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <sys/types.h>
#include <unistd.h>

namespace
{
bool isLauncherFile(const QString& path)
{
  return QFileInfo(path).isFile();
}
}  // namespace

FluorineUpdateInstaller::FluorineUpdateInstaller(QObject* parent)
    : QObject(parent)
{}

void FluorineUpdateInstaller::fail(const QString& reason)
{
  m_busy = false;
  emit failed(reason);
}

void FluorineUpdateInstaller::install(
    const FluorineUpdater::ReleaseInfo& info)
{
  if (m_busy) {
    return;
  }
  if (info.downloadUrl.isEmpty()) {
    fail(tr("This release does not contain an installable archive."));
    return;
  }

  m_busy = true;

  const QString dataRoot =
      QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
      QStringLiteral("/fluorine");
  const QString stagingDir = dataRoot + QStringLiteral("/update-staging");
  const QString extractDir = stagingDir + QStringLiteral("/extract");
  const bool isZip =
      info.downloadUrl.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive);
  const QString archivePath =
      stagingDir + (isZip ? QStringLiteral("/download.zip")
                          : QStringLiteral("/download.tar.gz"));

  if (!QDir().mkpath(stagingDir)) {
    fail(tr("Cannot create update staging directory '%1'.").arg(stagingDir));
    return;
  }
  QFile::remove(archivePath);
  QDir(extractDir).removeRecursively();
  if (!QDir().mkpath(extractDir)) {
    fail(tr("Cannot create update extraction directory '%1'.").arg(extractDir));
    return;
  }

  emit statusChanged(tr("Downloading update…"));

  auto* nam = new QNetworkAccessManager(this);
  QNetworkRequest request{QUrl(info.downloadUrl)};
  request.setRawHeader("User-Agent", "Fluorine-Manager/updater");
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  QNetworkReply* reply = nam->get(request);

  auto* outFile = new QFile(archivePath, reply);
  if (!outFile->open(QIODevice::WriteOnly)) {
    reply->abort();
    reply->deleteLater();
    nam->deleteLater();
    fail(tr("Cannot open '%1' for writing.").arg(archivePath));
    return;
  }

  connect(reply, &QNetworkReply::readyRead, reply,
          [reply, outFile]() { outFile->write(reply->readAll()); });
  connect(reply, &QNetworkReply::downloadProgress, this,
          &FluorineUpdateInstaller::downloadProgress);
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, outFile, archivePath, extractDir, stagingDir, isZip,
           nam]() {
            outFile->write(reply->readAll());
            outFile->close();
            const auto error = reply->error();
            const QString errorString = reply->errorString();
            reply->deleteLater();
            nam->deleteLater();

            if (error != QNetworkReply::NoError) {
              fail(tr("Download failed: %1").arg(errorString));
              return;
            }

            emit statusChanged(tr("Extracting update…"));
            auto* extractor = new QProcess(this);
            extractor->setWorkingDirectory(extractDir);
            if (isZip) {
              extractor->setProgram(QStringLiteral("unzip"));
              extractor->setArguments({QStringLiteral("-q"), archivePath});
            } else {
              extractor->setProgram(QStringLiteral("tar"));
              extractor->setArguments({QStringLiteral("xzf"), archivePath});
            }

            connect(
                extractor,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this,
                [this, extractor, extractDir, stagingDir](
                    int code, QProcess::ExitStatus status) {
                  extractor->deleteLater();
                  if (status != QProcess::NormalExit || code != 0) {
                    fail(tr("Extraction failed: extractor exited with %1.")
                             .arg(code));
                    return;
                  }

                  QDir extracted(extractDir);
                  QString newLauncher = extracted.absoluteFilePath(
                      QStringLiteral("fluorine-manager"));
                  if (!isLauncherFile(newLauncher)) {
                    const QStringList topDirectories = extracted.entryList(
                        QDir::Dirs | QDir::NoDotAndDotDot);
                    for (const QString& top : topDirectories) {
                      const QString candidate = extracted.absoluteFilePath(
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
                    fail(tr("The extracted archive does not contain an "
                            "executable Fluorine launcher."));
                    return;
                  }

                  const QString helperPath =
                      stagingDir + QStringLiteral("/install.sh");
                  QFile helper(helperPath);
                  if (!helper.open(QIODevice::WriteOnly |
                                   QIODevice::Truncate)) {
                    fail(tr("Cannot write the update restart helper."));
                    return;
                  }
                  helper.write(updater_restart::helperScript());
                  helper.close();
                  QFile::setPermissions(
                      helperPath,
                      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                          QFile::ReadGroup | QFile::ExeGroup |
                          QFile::ReadOther | QFile::ExeOther);

                  emit statusChanged(tr("Update staged. Restarting when running "
                                        "applications have closed…"));
                  const auto oldStartTime =
                      process_lifetime::processStartTime(::getpid());
                  if (!oldStartTime) {
                    fail(tr("Unable to identify the current Fluorine process "
                            "generation safely."));
                    return;
                  }
                  const bool helperStarted = QProcess::startDetached(
                      QStringLiteral("/usr/bin/env"),
                      {QStringLiteral("bash"), helperPath,
                       QString::number(static_cast<qint64>(::getpid())),
                       QString::number(static_cast<qulonglong>(*oldStartTime)),
                       newLauncher});
                  if (!helperStarted) {
                    fail(tr("Unable to start the update restart helper."));
                    return;
                  }
                  MOBase::log::info(
                      "update installer: spawned helper to restart into {}",
                      newLauncher);
                  auto* exitRetry = new QTimer(qApp);
                  exitRetry->setInterval(250);
                  connect(exitRetry, &QTimer::timeout, qApp, [exitRetry]() {
                    const auto result = ExitModOrganizer(
                        Exit::Force, /*silentActiveLaunch=*/true);
                    if (!updater_restart::shouldRetryExit(result)) {
                      exitRetry->stop();
                      exitRetry->deleteLater();
                    }
                  });
                  exitRetry->start();
                });
            extractor->start();
          });
}
