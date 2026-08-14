#include "lootmanager.h"
#include "fluorinepaths.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <uibase/log.h>

namespace
{

QByteArray httpGet(const QString& url, const int* cancelFlag,
                   const std::function<void(float)>& progressCb = nullptr,
                   const QString& destFile = {})
{
  QNetworkAccessManager mgr;
  QNetworkRequest req{QUrl(url)};
  req.setRawHeader("User-Agent", "Fluorine-Manager/loot");
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::NoLessSafeRedirectPolicy);
  req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

  QNetworkReply* reply = mgr.get(req);
  QEventLoop loop;

  QFile outFile;
  if (!destFile.isEmpty()) {
    outFile.setFileName(destFile);
    if (!outFile.open(QIODevice::WriteOnly))
      return {};
  }

  QByteArray inMemory;
  qint64 total = -1, received = 0;

  QObject::connect(reply, &QNetworkReply::readyRead, [&]() {
    if (total < 0)
      total = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
    QByteArray chunk = reply->readAll();
    received += chunk.size();
    if (outFile.isOpen())
      outFile.write(chunk);
    else
      inMemory.append(chunk);
    if (progressCb && total > 0)
      progressCb(static_cast<float>(received) / static_cast<float>(total));
  });
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

  QTimer cancelTimer;
  if (cancelFlag) {
    QObject::connect(&cancelTimer, &QTimer::timeout, [&]() {
      if (*cancelFlag) { reply->abort(); loop.quit(); }
    });
    cancelTimer.start(200);
  }

  loop.exec();
  if (outFile.isOpen()) outFile.close();

  if (reply->error() != QNetworkReply::NoError) {
    MOBase::log::warn("LOOT download request {} failed with network error {}",
                      MOBase::log::safeUrlForLog(url), reply->error());
    reply->deleteLater();
    if (!destFile.isEmpty()) QFile::remove(destFile);
    return {};
  }
  reply->deleteLater();
  return inMemory;
}

// Try available 7z tools in order: bundled 7zz, then system 7z/7za/7zz.
bool extract7z(const QString& archivePath, const QString& destDir)
{
  QStringList candidates;
  const QString bundled = fluorineDataDir() + "/bin/7zz";
  if (QFileInfo::exists(bundled))
    candidates << bundled;
  for (const QString& n : {QStringLiteral("7z"), QStringLiteral("7za"),
                            QStringLiteral("7zz")}) {
    const QString found = QStandardPaths::findExecutable(n);
    if (!found.isEmpty() && !candidates.contains(found))
      candidates << found;
  }

  for (const QString& exe : candidates) {
    QProcess proc;
    proc.start(exe, {QStringLiteral("x"), archivePath,
                     QStringLiteral("-o") + destDir, QStringLiteral("-y")});
    if (proc.waitForFinished(300000) &&
        proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0)
      return true;
  }
  return false;
}

// Recursively find a file by name (case-insensitive) under dir.
QString findFileInDir(const QString& dir, const QString& name)
{
  for (const auto& entry :
       QDir(dir).entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot)) {
    if (entry.isFile() && entry.fileName().compare(name, Qt::CaseInsensitive) == 0)
      return entry.filePath();
    if (entry.isDir()) {
      const QString found = findFileInDir(entry.filePath(), name);
      if (!found.isEmpty()) return found;
    }
  }
  return {};
}

// Recursively move all entries from src into dst.
bool moveDirContents(const QString& src, const QString& dst)
{
  QDir().mkpath(dst);
  for (const auto& entry :
       QDir(src).entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot)) {
    const QString dstPath = dst + "/" + entry.fileName();
    if (entry.isDir()) {
      if (!moveDirContents(entry.filePath(), dstPath)) return false;
      QDir(entry.filePath()).removeRecursively();
    } else {
      QFile::remove(dstPath);
      if (!QFile::rename(entry.filePath(), dstPath)) return false;
    }
  }
  return true;
}

}  // namespace

QString lootInstallDir()
{
  return fluorineDataDir() + "/tools/loot";
}

bool isLootInstalled()
{
  return QFileInfo::exists(getLootExePath());
}

QString getLootExePath()
{
  const QString path = lootInstallDir() + "/LOOT.exe";
  return QFileInfo::exists(path) ? path : QString{};
}

QString downloadLoot(const std::function<void(float)>& progressCb,
                     const std::function<void(const QString&)>& statusCb,
                     const int* cancelFlag)
{
  auto status   = [&](const QString& msg) { if (statusCb) statusCb(msg); };
  auto progress = [&](float p)            { if (progressCb) progressCb(p); };

  status(QStringLiteral("Checking latest LOOT release..."));

  const QByteArray apiData = httpGet(
      QStringLiteral("https://api.github.com/repos/loot/loot/releases/latest"),
      cancelFlag);
  if (apiData.isEmpty())
    return QStringLiteral("Failed to fetch LOOT release info from GitHub");

  const QJsonObject release = QJsonDocument::fromJson(apiData).object();
  const QJsonArray  assets  = release[QStringLiteral("assets")].toArray();

  QString downloadUrl;
  QString assetName;
  for (const auto& a : assets) {
    const QJsonObject asset = a.toObject();
    const QString name      = asset[QStringLiteral("name")].toString();
    // Prefer win64 portable archive (not installer .exe)
    if (name.contains(QStringLiteral("win64"), Qt::CaseInsensitive) &&
        (name.endsWith(QStringLiteral(".7z"), Qt::CaseInsensitive) ||
         name.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive))) {
      downloadUrl = asset[QStringLiteral("browser_download_url")].toString();
      assetName   = name;
      break;
    }
  }
  if (downloadUrl.isEmpty())
    return QStringLiteral("Could not find a LOOT win64 archive in the latest release");

  const QString tagName = release[QStringLiteral("tag_name")].toString();
  MOBase::log::info("Downloading LOOT {} ({})", tagName, assetName);
  status(QStringLiteral("Downloading LOOT %1...").arg(tagName));

  const QString toolsDir = fluorineDataDir() + "/tools";
  if (!QDir().mkpath(toolsDir)) {
    MOBase::log::error("Failed to create LOOT tools directory '{}'", toolsDir);
    return QStringLiteral("Failed to create LOOT tools directory");
  }

  QTemporaryDir staging(toolsDir + "/loot-download-XXXXXX");
  if (!staging.isValid()) {
    MOBase::log::error("Failed to create LOOT staging directory in '{}'", toolsDir);
    return QStringLiteral("Failed to create temporary download directory");
  }

  const QString archivePath = staging.filePath(assetName);
  httpGet(downloadUrl, cancelFlag, progress, archivePath);
  progress(1.0f);

  if (!QFileInfo::exists(archivePath))
    return QStringLiteral("LOOT download failed or was cancelled");

  status(QStringLiteral("Extracting LOOT..."));
  const QString extractDir = staging.filePath(QStringLiteral("extracted"));
  QDir().mkpath(extractDir);

  if (!extract7z(archivePath, extractDir))
    return QStringLiteral("Failed to extract LOOT archive (no 7z tool found)");

  // Find LOOT.exe in the extraction output (may be directly or in a subdirectory).
  const QString lootExe = findFileInDir(extractDir, QStringLiteral("LOOT.exe"));
  if (lootExe.isEmpty())
    return QStringLiteral("LOOT.exe not found after extraction");

  const QString lootRoot = QFileInfo(lootExe).absolutePath();

  // Install to lootInstallDir(), replacing any previous version.
  const QString installDir = lootInstallDir();
  QDir(installDir).removeRecursively();
  QDir().mkpath(installDir);

  if (!moveDirContents(lootRoot, installDir))
    return QStringLiteral("Failed to move LOOT files to install directory");

  MOBase::log::info("LOOT installed to {}", installDir.toStdString());
  status(QStringLiteral("LOOT installed successfully"));
  return {};
}
