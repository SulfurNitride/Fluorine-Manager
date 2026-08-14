#include "slrmanager.h"

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTimer>
#include <uibase/log.h>

namespace {

// Pin SLR releases explicitly. Valve's latest-public-beta directory alias can
// fail at the edge cache even while the corresponding versioned directory is
// available. Update this URL deliberately when adopting a newer runtime.
const char* BASE_URL =
    "https://repo.steampowered.com/steamrt4/images/4.0.20260714.251823";
const char* ARCHIVE_NAME = "SteamLinuxRuntime_4.tar.xz";
const char* EXTRACTED_DIR = "SteamLinuxRuntime_4";

// steamrt4 (Debian bookworm-based) ships without xrandr, which Proton-GE
// and some protonfixes require at launch. We inject it from the Debian
// x11-xserver-utils package.
const char* XRANDR_DEB_URL = "http://ftp.debian.org/debian/pool/main/x/x11-xserver-utils/"
                             "x11-xserver-utils_7.7+11_amd64.deb";

const QString CANCELLED_ERROR =
    QStringLiteral("Steam Linux Runtime operation was cancelled");

SlrOperationTracker& operationTracker()
{
  // Update checks use detached workers. Keep the process tracker alive through
  // C++ static teardown so a late lease can always retire safely.
  static SlrOperationTracker* tracker = new SlrOperationTracker;
  return *tracker;
}

QString slrInstallDir()
{
  return QDir::homePath() + "/.local/share/fluorine/steamrt";
}

QString slrRunScriptPath()
{
  return slrInstallDir() + "/" + EXTRACTED_DIR + "/run";
}

QString localBuildIdPath()
{
  return slrInstallDir() + "/BUILD_ID.txt";
}

/// Blocking HTTP GET that returns the response body as QByteArray.
QByteArray httpGet(const QString& url, const SlrOperationTracker::Operation& operation,
                   const std::function<void(float)>& progressCb = nullptr,
                   const QString& destFile = {})
{
  if (operation.isCancellationRequested()) {
    return {};
  }

  QNetworkAccessManager mgr;
  QNetworkRequest request{QUrl(url)};
  request.setRawHeader("User-Agent", "Fluorine-Manager/slr");
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

  QNetworkReply* reply = mgr.get(request);
  QEventLoop loop;

  QFile outFile;
  if (!destFile.isEmpty()) {
    outFile.setFileName(destFile);
    if (!outFile.open(QIODevice::WriteOnly))
      return {};
  }

  QByteArray inMemoryBuf;
  qint64 totalBytes = -1;
  qint64 received = 0;

  QObject::connect(reply, &QNetworkReply::readyRead, [&]() {
    if (totalBytes < 0)
      totalBytes = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
    QByteArray chunk = reply->readAll();
    received += chunk.size();
    if (outFile.isOpen())
      outFile.write(chunk);
    else
      inMemoryBuf.append(chunk);
    if (progressCb && totalBytes > 0)
      progressCb(static_cast<float>(received) / static_cast<float>(totalBytes));
  });

  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

  // The network API is event-loop driven, so poll both caller cancellation
  // and process fail-stop cancellation while this worker thread is blocked.
  QTimer cancelTimer;
  QObject::connect(&cancelTimer, &QTimer::timeout, [&]() {
    if (operation.isCancellationRequested()) {
      reply->abort();
      loop.quit();
    }
  });
  cancelTimer.start(100);

  loop.exec();

  if (outFile.isOpen())
    outFile.close();

  if (operation.isCancellationRequested() || reply->error() != QNetworkReply::NoError) {
    MOBase::log::warn("SLR download request {} failed with network error {}",
                      MOBase::log::safeUrlForLog(url), reply->error());
    reply->deleteLater();
    if (!destFile.isEmpty())
      QFile::remove(destFile);
    return {};
  }

  reply->deleteLater();
  return inMemoryBuf;
}

QString readLocalBuildId()
{
  QFile f(localBuildIdPath());
  if (!f.open(QIODevice::ReadOnly)) {
    return {};
  }
  return QString::fromUtf8(f.readAll()).trimmed();
}

bool makeExecutable(const QString& path)
{
  return QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                         QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                                         QFileDevice::ExeGroup | QFileDevice::ReadOther |
                                         QFileDevice::ExeOther);
}

bool waitForProcess(QProcess& process, const SlrOperationTracker::Operation& operation,
                    int timeoutMs)
{
  QElapsedTimer elapsed;
  elapsed.start();

  while (process.state() != QProcess::NotRunning) {
    if (operation.isCancellationRequested() || elapsed.elapsed() >= timeoutMs) {
      process.terminate();
      if (!process.waitForFinished(1000)) {
        process.kill();
        process.waitForFinished(1000);
      }
      return false;
    }
    process.waitForFinished(100);
  }
  return !operation.isCancellationRequested();
}

bool writeLocalBuildId(const QString& buildId, SlrOperationTracker::Operation& operation)
{
  bool ok = false;
  const bool allowed =
      operation.runDurableStepIfAllowed(SlrDurableStage::BuildIdCommit, [&] {
        QSaveFile file(localBuildIdPath());
        if (!file.open(QIODevice::WriteOnly)) {
          return;
        }
        const QByteArray contents = buildId.toUtf8() + '\n';
        if (file.write(contents) != contents.size() ||
            operation.isCancellationRequested()) {
          file.cancelWriting();
          return;
        }
        ok = file.commit();
      });
  return allowed && ok;
}

bool replaceRuntimeAtomically(const QString& stagedRuntimeDir,
                              SlrOperationTracker::Operation& operation, QString& err)
{
  const QString installDir = slrInstallDir();
  const QString extractedDir = installDir + "/" + EXTRACTED_DIR;
  const QString backupDir = extractedDir + ".previous";

  if (!QFileInfo::exists(stagedRuntimeDir + "/run")) {
    err = QStringLiteral("staged runtime is missing run script");
    return false;
  }
  if (operation.isCancellationRequested()) {
    err = CANCELLED_ERROR;
    return false;
  }
  if (!makeExecutable(stagedRuntimeDir + "/run")) {
    err = QStringLiteral("failed to make staged runtime executable");
    return false;
  }

  if (QFileInfo::exists(backupDir)) {
    bool recovered = true;
    const bool allowed =
        operation.runDurableStepIfAllowed(SlrDurableStage::BackupCleanup, [&] {
          if (!QFileInfo::exists(extractedDir)) {
            recovered = QDir().rename(backupDir, extractedDir);
          } else {
            recovered = QDir(backupDir).removeRecursively();
          }
        });
    if (!allowed) {
      err = CANCELLED_ERROR;
      return false;
    }
    if (!recovered) {
      err = QStringLiteral("failed to recover previous SLR update backup");
      return false;
    }
  }

  bool hadPrevious = false;
  bool committed = false;
  const bool allowed =
      operation.runDurableStepIfAllowed(SlrDurableStage::RuntimeSwap, [&] {
        if (!QDir().mkpath(installDir)) {
          err = QStringLiteral("failed to create SLR install directory");
          return;
        }

        hadPrevious = QFileInfo::exists(extractedDir);
        if (hadPrevious && !QDir().rename(extractedDir, backupDir)) {
          err = QStringLiteral("failed to move existing runtime aside");
          return;
        }

        if (operation.isCancellationRequested()) {
          if (hadPrevious) {
            QDir().rename(backupDir, extractedDir);
          }
          err = CANCELLED_ERROR;
          return;
        }

        if (!QDir().rename(stagedRuntimeDir, extractedDir)) {
          if (hadPrevious) {
            QDir().rename(backupDir, extractedDir);
          }
          err = QStringLiteral("failed to install staged runtime");
          return;
        }
        committed = true;
      });

  if (!allowed) {
    err = CANCELLED_ERROR;
    return false;
  }
  if (!committed) {
    return false;
  }

  // Backup cleanup is not required for correctness. If fail-stop wins after
  // the atomic swap, leave it behind rather than mutating storage again.
  if (hadPrevious) {
    operation.runDurableStepIfAllowed(SlrDurableStage::BackupCleanup,
                                      [&] { QDir(backupDir).removeRecursively(); });
  }
  return true;
}

SlrUpdateInfo checkSlrUpdateImpl(const SlrOperationTracker::Operation& operation)
{
  SlrUpdateInfo info;
  info.installed = isSlrInstalled();
  info.localBuildId = readLocalBuildId();

  const QByteArray remoteBuildIdRaw =
      httpGet(QStringLiteral("%1/BUILD_ID.txt").arg(QLatin1String(BASE_URL)), operation);
  if (operation.isCancellationRequested()) {
    info.error = CANCELLED_ERROR;
  } else if (remoteBuildIdRaw.isEmpty()) {
    info.error = QStringLiteral("Failed to fetch SLR BUILD_ID");
  } else {
    info.remoteBuildId = QString::fromUtf8(remoteBuildIdRaw).trimmed();
    info.updateAvailable = info.installed && info.localBuildId != info.remoteBuildId;
  }
  return info;
}

} // namespace

bool isSlrInstalled()
{
  const QString script = slrRunScriptPath();
  QFileInfo fi(script);
  return fi.exists() && fi.isExecutable();
}

QString xrandrInjectedPath()
{
  return slrInstallDir() + "/xrandr-bin/xrandr";
}

bool isXrandrInjected()
{
  QFileInfo fi(xrandrInjectedPath());
  return fi.exists() && fi.isExecutable();
}

// Download + extract xrandr from the Debian x11-xserver-utils package into
// the SLR install dir. Called standalone for users who installed the
// runtime before the xrandr step existed, and inline from downloadSlr() for
// fresh installs.
static bool installXrandrAssets(SlrOperationTracker::Operation& operation,
                                const std::function<void(const QString&)>& statusCb)
{
  auto status = [&](const QString& msg) {
    if (statusCb)
      statusCb(msg);
  };

  if (operation.isCancellationRequested()) {
    return false;
  }

  bool installDirReady = false;
  if (!operation.runDurableStepIfAllowed(SlrDurableStage::PrepareInstallRoot, [&] {
        installDirReady = QDir().mkpath(slrInstallDir());
      })) {
    return false;
  }
  if (!installDirReady) {
    MOBase::log::warn("Failed to create SLR install directory for xrandr");
    return false;
  }
  if (operation.isCancellationRequested()) {
    return false;
  }

  QTemporaryDir stagingRoot(slrInstallDir() + "/xrandr-update-XXXXXX");
  if (!stagingRoot.isValid()) {
    MOBase::log::warn("Failed to create xrandr staging directory");
    return false;
  }

  const QString debPath = stagingRoot.filePath("x11-xserver-utils.deb");
  status(QStringLiteral("Downloading xrandr..."));
  httpGet(QString::fromLatin1(XRANDR_DEB_URL), operation, nullptr, debPath);
  if (!QFileInfo::exists(debPath)) {
    if (!operation.isCancellationRequested()) {
      MOBase::log::warn("Failed to download xrandr .deb — runtime will lack xrandr");
    }
    return false;
  }

  const QString tmpExtract = stagingRoot.filePath("extract");
  if (!QDir().mkpath(tmpExtract)) {
    return false;
  }

  QProcess ar;
  if (operation.isCancellationRequested()) {
    return false;
  }
  ar.setWorkingDirectory(tmpExtract);
  ar.start(QStringLiteral("ar"),
           {QStringLiteral("x"), debPath, QStringLiteral("data.tar.xz")});
  if (!waitForProcess(ar, operation, 30000) || ar.exitCode() != 0) {
    return false;
  }

  QProcess untar;
  if (operation.isCancellationRequested()) {
    return false;
  }
  untar.setWorkingDirectory(tmpExtract);
  untar.start(QStringLiteral("tar"), {QStringLiteral("xf"), QStringLiteral("data.tar.xz"),
                                      QStringLiteral("./usr/bin/xrandr")});
  if (!waitForProcess(untar, operation, 30000) || untar.exitCode() != 0) {
    return false;
  }

  const QString xrandrSrc = tmpExtract + "/usr/bin/xrandr";
  QFile source(xrandrSrc);
  if (!source.open(QIODevice::ReadOnly)) {
    MOBase::log::warn("xrandr .deb extracted but binary not found");
    return false;
  }
  const QByteArray contents = source.readAll();
  source.close();

  bool ok = false;
  const bool allowed =
      operation.runDurableStepIfAllowed(SlrDurableStage::XrandrCommit, [&] {
        const QString xrandrDir = slrInstallDir() + "/xrandr-bin";
        if (!QDir().mkpath(xrandrDir)) {
          return;
        }

        const QString dst = xrandrDir + "/xrandr";
        QSaveFile output(dst);
        if (!output.open(QIODevice::WriteOnly) ||
            output.write(contents) != contents.size()) {
          return;
        }
        output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                              QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                              QFileDevice::ExeGroup | QFileDevice::ReadOther |
                              QFileDevice::ExeOther);
        if (operation.isCancellationRequested()) {
          output.cancelWriting();
          return;
        }
        ok = output.commit();
      });

  if (allowed && ok) {
    MOBase::log::info("Installed xrandr to {}", xrandrInjectedPath().toStdString());
  } else if (allowed) {
    MOBase::log::warn("Failed to atomically install xrandr helper");
  }
  return allowed && ok;
}

bool ensureXrandrInstalled(SlrCancellationToken cancellation,
                           const std::function<void(const QString&)>& statusCb)
{
  auto operation = operationTracker().tryBegin(std::move(cancellation));
  if (!operation) {
    return false;
  }
  return isXrandrInjected() || installXrandrAssets(*operation, statusCb);
}

QString getSlrRunScript()
{
  return isSlrInstalled() ? slrRunScriptPath() : QString();
}

SlrUpdateInfo checkSlrUpdate(SlrCancellationToken cancellation)
{
  auto operation = operationTracker().tryBegin(std::move(cancellation));
  if (!operation) {
    SlrUpdateInfo info;
    info.error = CANCELLED_ERROR;
    return info;
  }
  return checkSlrUpdateImpl(*operation);
}

QString downloadSlr(const std::function<void(float)>& progressCb,
                    const std::function<void(const QString&)>& statusCb,
                    SlrCancellationToken cancellation)
{
  auto operation = operationTracker().tryBegin(std::move(cancellation));
  if (!operation) {
    return CANCELLED_ERROR;
  }

  auto status = [&](const QString& msg) {
    if (statusCb)
      statusCb(msg);
  };
  auto progress = [&](float p) {
    if (progressCb)
      progressCb(p);
  };

  // 1. Check for updates.
  status(QStringLiteral("Checking Steam Linux Runtime version..."));

  const SlrUpdateInfo updateInfo = checkSlrUpdateImpl(*operation);
  if (!updateInfo.error.isEmpty())
    return updateInfo.error;

  const QString remoteBuildId = updateInfo.remoteBuildId;
  const QString localBuildId = updateInfo.localBuildId;

  if (localBuildId == remoteBuildId && isSlrInstalled()) {
    MOBase::log::info("Steam Linux Runtime is already up to date");
    // Existing installs from earlier Fluorine versions may not have the
    // xrandr helper (issue #49). Back-fill it so Proton-GE prefix init
    // doesn't silently fail on distros without host xrandr exposed.
    if (!isXrandrInjected()) {
      status(QStringLiteral("Injecting xrandr into existing runtime..."));
      installXrandrAssets(*operation, statusCb);
      if (operation->isCancellationRequested()) {
        return CANCELLED_ERROR;
      }
    }
    status(QStringLiteral("Steam Linux Runtime is already up to date"));
    progress(1.0f);
    return {};
  }

  MOBase::log::info("Downloading Steam Linux Runtime (BUILD_ID: {})", remoteBuildId);

  const QString installDir = slrInstallDir();
  bool installDirReady = false;
  if (!operation->runDurableStepIfAllowed(SlrDurableStage::PrepareInstallRoot, [&] {
        installDirReady = QDir().mkpath(installDir);
      })) {
    return CANCELLED_ERROR;
  }
  if (!installDirReady) {
    return QStringLiteral("Failed to create SLR install directory");
  }
  if (operation->isCancellationRequested()) {
    return CANCELLED_ERROR;
  }

  QTemporaryDir stagingRoot(installDir + "/slr-update-XXXXXX");
  if (!stagingRoot.isValid()) {
    return QStringLiteral("Failed to create SLR staging directory");
  }
  const QString archivePath = stagingRoot.filePath(ARCHIVE_NAME);

  // 2. Download.
  status(QStringLiteral("Downloading Steam Linux Runtime (steamrt4, ~200 MB)..."));
  httpGet(
      QStringLiteral("%1/%2").arg(QLatin1String(BASE_URL), QLatin1String(ARCHIVE_NAME)),
      *operation, progress, archivePath);
  progress(1.0f);

  if (operation->isCancellationRequested()) {
    return CANCELLED_ERROR;
  }
  if (!QFileInfo::exists(archivePath))
    return QStringLiteral("Download failed or was cancelled");

  // 3. Extract.
  status(QStringLiteral("Extracting Steam Linux Runtime..."));

  if (operation->isCancellationRequested()) {
    return CANCELLED_ERROR;
  }
  QProcess tar;
  tar.setWorkingDirectory(stagingRoot.path());
  tar.start(QStringLiteral("tar"), {QStringLiteral("xJf"), archivePath});
  const bool extractionFinished = waitForProcess(tar, *operation, 600000);
  QFile::remove(archivePath);

  if (operation->isCancellationRequested()) {
    return CANCELLED_ERROR;
  }
  if (!extractionFinished || tar.exitStatus() != QProcess::NormalExit ||
      tar.exitCode() != 0)
    return QStringLiteral("tar extraction failed (exit code %1)").arg(tar.exitCode());

  const QString stagedRuntimeDir = stagingRoot.filePath(EXTRACTED_DIR);
  if (!QFileInfo::exists(stagedRuntimeDir + "/run"))
    return QStringLiteral("Extraction succeeded but run script not found");

  QString replaceError;
  if (!replaceRuntimeAtomically(stagedRuntimeDir, *operation, replaceError)) {
    return replaceError;
  }

  // 4. Inject xrandr into the container (steamrt4 ships without it, but
  // Proton-GE and several protonfixes invoke xrandr during launch).
  status(QStringLiteral("Injecting xrandr into runtime..."));
  installXrandrAssets(*operation, statusCb);
  if (operation->isCancellationRequested()) {
    return CANCELLED_ERROR;
  }

  // 5. Save BUILD_ID.
  if (!writeLocalBuildId(remoteBuildId, *operation)) {
    if (operation->isCancellationRequested()) {
      return CANCELLED_ERROR;
    }
    MOBase::log::warn("Failed to write SLR BUILD_ID marker");
  }

  MOBase::log::info("Steam Linux Runtime installed successfully");
  status(QStringLiteral("Steam Linux Runtime ready"));
  return {};
}

void suppressSlrOperationsForFailedRollback() noexcept
{
  operationTracker().suppressAndCancel();
}

bool slrOperationAdmissionSuppressed() noexcept
{
  return operationTracker().admissionSuppressed();
}

bool runSlrUiCommitIfAllowed(const std::function<void()>& mutation)
{
  auto operation = operationTracker().tryBegin();
  return operation &&
         operation->runDurableStepIfAllowed(SlrDurableStage::UiPersistence, mutation);
}
