#include "clf3enginemanager.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <QVersionNumber>

namespace {
const QVersionNumber minimumVersion(0, 2, 5);

QVersionNumber version(QString text)
{
  if (text.startsWith('v')) text.remove(0, 1);
  return QVersionNumber::fromString(text);
}

QNetworkRequest request(const QUrl& url)
{
  QNetworkRequest result(url);
  result.setRawHeader("User-Agent", "Fluorine-CLF3-Updater");
  result.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                      QNetworkRequest::NoLessSafeRedirectPolicy);
  result.setTransferTimeout(30000);
  return result;
}
}

Clf3EngineManager::Clf3EngineManager(QObject* parent,
                                   QNetworkAccessManager* network,
                                   const QString& cacheRoot)
    : QObject(parent),
      m_network(network ? network : new QNetworkAccessManager(this)),
      m_root(cacheRoot.isEmpty()
                 ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                       + "/fluorine/tools/clf3"
                 : cacheRoot)
{
  m_timeout.setSingleShot(true);
  connect(&m_timeout, &QTimer::timeout, this,
          [this] { fail(tr("Preparing CLF3 timed out. Please retry.")); });
  connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (m_busy && error == QProcess::FailedToStart)
      fail(tr("Could not run the CLF3 updater helper: %1").arg(m_process.errorString()));
  });
  connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [this](int code, QProcess::ExitStatus status) {
    if (!m_busy) return;
    m_timeout.stop();
    if (code != 0 || status != QProcess::NormalExit) {
      fail(tr("Could not %1 CLF3: %2")
               .arg(m_probing ? tr("validate") : tr("extract"),
                    QString::fromUtf8(m_process.readAllStandardError()).left(1000)));
      return;
    }
    if (m_probing) {
      const QString output = QString::fromUtf8(m_process.readAllStandardOutput()).trimmed();
      const auto match = QRegularExpression("^clf3\\s+(\\d+\\.\\d+\\.\\d+)(?:\\s|$)").match(output);
      if (!match.hasMatch() || version(match.captured(1)) < minimumVersion
          || version(match.captured(1)) != version(m_release["tag"].toString())) {
        fail(tr("The downloaded CLF3 executable has an unexpected version: %1").arg(output));
        return;
      }
      promoteRelease();
    } else {
      for (const auto& name : {"clf3", "7zz"}) {
        const QString path = m_staging->filePath(QStringLiteral("package/") + name);
        if (!QFileInfo(path).isFile() || QFileInfo(path).isSymLink()
            || !QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) {
          fail(tr("The CLF3 release is missing a usable %1 executable.").arg(name));
          return;
        }
      }
      m_probing = true;
      m_timeout.start(30000);
      m_process.start(m_staging->filePath("package/clf3"), {"--version"});
    }
  });
}

Clf3EngineManager::~Clf3EngineManager()
{
  m_busy = false;
  cleanup();
}

QJsonObject Clf3EngineManager::cachedRelease() const
{
  QFile file(m_root + "/current.json");
  if (!file.open(QIODevice::ReadOnly)) return {};
  return QJsonDocument::fromJson(file.readAll()).object();
}

QString Clf3EngineManager::cachedEnginePath() const
{
  const auto cached = cachedRelease();
  const QString directory = cached["directory"].toString();
  if (version(cached["tag"].toString()) < minimumVersion
      || !QRegularExpression("^engine-[a-zA-Z0-9-]+$").match(directory).hasMatch()) return {};
  const QString path = m_root + '/' + directory;
  if (!QFileInfo(path + "/clf3").isExecutable()
      || !QFileInfo(path + "/7zz").isExecutable()) return {};
  return path + "/clf3";
}

void Clf3EngineManager::prepare()
{
  if (m_busy) return;
  m_busy = true;
  m_probing = false;
  m_release = {};
  emit statusChanged(tr("Checking the latest CLF3 release…"));
  fetchRelease();
}

void Clf3EngineManager::fetchRelease()
{
  auto metadataRequest = request(QUrl("https://api.github.com/repos/SulfurNitride/CLF3/releases/latest"));
  metadataRequest.setRawHeader("Cache-Control", "no-cache");
  metadataRequest.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
  auto* reply = m_network->get(metadataRequest);
  m_reply = reply;
  m_timeout.start(60000);
  connect(reply, &QNetworkReply::finished, this, [this, reply] {
    m_timeout.stop();
    m_reply = nullptr;
    reply->deleteLater();
    if (!m_busy) return;
    if (reply->error() != QNetworkReply::NoError) {
      const QString cached = cachedEnginePath();
      if (!cached.isEmpty()) {
        emit statusChanged(tr("Could not check GitHub; using cached CLF3 %1.")
                               .arg(cachedRelease()["tag"].toString()));
        finish(cached);
      } else {
        fail(tr("Could not check the latest CLF3 release: %1").arg(reply->errorString()));
      }
      return;
    }
    const auto release = QJsonDocument::fromJson(reply->readAll()).object();
    const QString tag = release["tag_name"].toString();
    if (release["draft"].toBool() || release["prerelease"].toBool()
        || version(tag) < minimumVersion) {
      fail(tr("CLF3 %1 or newer is required. The latest release is %2.")
               .arg(minimumVersion.toString(), tag));
      return;
    }
    for (const auto& value : release["assets"].toArray()) {
      const auto asset = value.toObject();
      if (asset["name"].toString() != "clf3-linux-x64.zip") continue;
      const QUrl url(asset["browser_download_url"].toString());
      const QString digest = asset["digest"].toString();
      if (url.scheme() != "https" || url.host() != "github.com"
          || !url.path().startsWith("/SulfurNitride/CLF3/releases/download/")
          || !QRegularExpression("^sha256:[a-fA-F0-9]{64}$").match(digest).hasMatch()) {
        fail(tr("The CLF3 release has an invalid download URL or SHA-256 checksum."));
        return;
      }
      QJsonObject selected{{"tag", tag}, {"url", url.toString()}, {"digest", digest.toLower()},
                           {"asset_id", asset["id"]}};
      const auto cached = cachedRelease();
      const auto cachedPath = cachedEnginePath();
      if (!cachedPath.isEmpty() && cached["asset_id"] == selected["asset_id"]
          && cached["digest"] == selected["digest"] && cached["tag"] == selected["tag"]) {
        finish(cachedPath);
      } else {
        downloadRelease(selected);
      }
      return;
    }
    fail(tr("The latest CLF3 release has no Linux x64 download."));
  });
}

void Clf3EngineManager::downloadRelease(const QJsonObject& release)
{
  m_release = release;
  if (!QDir().mkpath(m_root)) {
    fail(tr("Could not create the CLF3 download directory."));
    return;
  }
  m_staging = std::make_unique<QTemporaryDir>(m_root + "/download-XXXXXX");
  if (!m_staging->isValid()) {
    fail(tr("Could not create a temporary CLF3 download directory."));
    return;
  }
  m_download.setFileName(m_staging->filePath("release.zip"));
  if (!m_download.open(QIODevice::WriteOnly)) {
    fail(tr("Could not save the CLF3 release: %1").arg(m_download.errorString()));
    return;
  }
  emit statusChanged(tr("Downloading CLF3 %1…").arg(release["tag"].toString()));
  auto* reply = m_network->get(request(QUrl(release["url"].toString())));
  m_reply = reply;
  m_timeout.start(300000);
  connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
    const auto data = reply->readAll();
    if (m_download.write(data) != data.size())
      fail(tr("Could not write the CLF3 download: %1").arg(m_download.errorString()));
  });
  connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
    if (total > 0) emit statusChanged(tr("Downloading CLF3 %1… %2%")
        .arg(m_release["tag"].toString()).arg(received * 100 / total));
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply] {
    m_timeout.stop();
    m_reply = nullptr;
    reply->deleteLater();
    if (!m_busy) return;
    const auto remaining = reply->readAll();
    if (reply->error() != QNetworkReply::NoError
        || m_download.write(remaining) != remaining.size() || !m_download.flush()) {
      fail(tr("Could not download CLF3: %1").arg(reply->errorString()));
      return;
    }
    m_download.close();
    QFile archive(m_download.fileName());
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!archive.open(QIODevice::ReadOnly) || !hash.addData(&archive)
        || "sha256:" + QString::fromLatin1(hash.result().toHex()) != m_release["digest"].toString()) {
      fail(tr("The CLF3 download failed its SHA-256 check. Please retry."));
      return;
    }
    extractRelease();
  });
}

void Clf3EngineManager::extractRelease()
{
  emit statusChanged(tr("Preparing CLF3 %1…").arg(m_release["tag"].toString()));
  const QString destination = m_staging->filePath("package");
  QDir().mkpath(destination);
  QString extractor;
  for (const auto& name : {"7zz", "7z", "unzip"}) {
    const QString bundled = QCoreApplication::applicationDirPath() + '/' + name;
    extractor = QFileInfo(bundled).isExecutable() ? bundled : QStandardPaths::findExecutable(name);
    if (!extractor.isEmpty()) break;
  }
  if (extractor.isEmpty()) {
    fail(tr("Could not extract CLF3: install 7-Zip or unzip."));
    return;
  }
  const QStringList args = QFileInfo(extractor).fileName() == "unzip"
      ? QStringList{"-o", m_download.fileName(), "clf3", "7zz", "-d", destination}
      : QStringList{"e", m_download.fileName(), "clf3", "7zz", "-o" + destination, "-y"};
  m_timeout.start(60000);
  m_process.start(extractor, args);
}

void Clf3EngineManager::promoteRelease()
{
  const QString directory = "engine-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
  if (!QDir().rename(m_staging->filePath("package"), m_root + '/' + directory)) {
    fail(tr("Could not install the downloaded CLF3 release."));
    return;
  }
  m_release["directory"] = directory;
  QSaveFile manifest(m_root + "/current.json");
  const auto data = QJsonDocument(m_release).toJson();
  if (!manifest.open(QIODevice::WriteOnly) || manifest.write(data) != data.size() || !manifest.commit()) {
    QDir(m_root + '/' + directory).removeRecursively();
    fail(tr("Could not save the installed CLF3 version."));
    return;
  }
  finish(m_root + '/' + directory + "/clf3");
}

void Clf3EngineManager::cleanup()
{
  m_timeout.stop();
  if (m_reply) {
    disconnect(m_reply, nullptr, this, nullptr);
    m_reply->abort();
    m_reply->deleteLater();
    m_reply = nullptr;
  }
  if (m_process.state() != QProcess::NotRunning) {
    m_process.kill();
    m_process.waitForFinished(1000);
  }
  m_download.close();
  m_staging.reset();
}

void Clf3EngineManager::finish(const QString& path)
{
  m_busy = false;
  cleanup();
  emit ready(path);
}

void Clf3EngineManager::fail(const QString& message)
{
  if (!m_busy) return;
  m_busy = false;
  cleanup();
  emit failed(message);
}

void Clf3EngineManager::cancel()
{
  if (!m_busy) return;
  m_busy = false;
  cleanup();
  emit cancelled();
}
