#include "nexusartifactdownloader.h"

#include "apiuseraccount.h"
#include "nexusinterface.h"
#include "nxmaccessmanager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrlQuery>
#include <QVersionNumber>
#include <nxmurl.h>

namespace
{
bool activeNexusFile(const QJsonObject& file)
{
  const QString category = file.value(QStringLiteral("category_name"))
                               .toString()
                               .trimmed()
                               .toUpper();
  return category != QStringLiteral("ARCHIVED")
         && category != QStringLiteral("REMOVED");
}

qint64 nexusTimestamp(const QJsonObject& file)
{
  const auto value = file.value(QStringLiteral("uploaded_timestamp"));
  return value.isDouble() ? value.toVariant().toLongLong()
                          : file.value(QStringLiteral("file_id")).toInt();
}

QString safeFilename(const QJsonObject& file)
{
  const QString raw = file.value(QStringLiteral("file_name")).toString();
  return QFileInfo(QDir::fromNativeSeparators(raw)).fileName();
}

QByteArray fileSha256(const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  QCryptographicHash hash(QCryptographicHash::Sha256);
  while (!file.atEnd()) {
    const QByteArray block = file.read(1024 * 1024);
    if (block.isEmpty() && file.error() != QFileDevice::NoError) return {};
    hash.addData(block);
  }
  return hash.result().toHex();
}

QJsonObject selectNexusFile(const QJsonArray& files,
                            const NexusArtifactRequest& request)
{
  QJsonObject newest;
  for (const auto& value : files) {
    const QJsonObject candidate = value.toObject();
    if (!activeNexusFile(candidate)) continue;
    const QString filename = safeFilename(candidate);
    const QString label = candidate.value(QStringLiteral("name")).toString();
    if (!request.fileNameContains.isEmpty()
        && !filename.contains(request.fileNameContains, Qt::CaseInsensitive)
        && !label.contains(request.fileNameContains, Qt::CaseInsensitive))
      continue;
    if (!request.minimumVersion.isEmpty()) {
      const auto candidateVersion =
          QVersionNumber::fromString(candidate.value(QStringLiteral("version")).toString());
      const auto minimum = QVersionNumber::fromString(request.minimumVersion);
      if (!candidateVersion.isNull() && !minimum.isNull()
          && QVersionNumber::compare(candidateVersion, minimum) < 0)
        continue;
    }
    if (newest.isEmpty() || nexusTimestamp(candidate) > nexusTimestamp(newest))
      newest = candidate;
  }
  return newest;
}
}

NexusArtifactDownloader::NexusArtifactDownloader(QObject* parent)
    : QObject(parent), m_network(new QNetworkAccessManager(this))
{}

void NexusArtifactDownloader::start(const NexusArtifactRequest& request)
{
  if (isBusy()) {
    emit failed(request.id, tr("Another Nexus tool download is already active."));
    return;
  }
  if (request.id.isEmpty() || request.domain.isEmpty() || request.modId <= 0) {
    emit failed(request.id, tr("The Nexus tool definition is incomplete."));
    return;
  }
  m_request = request;
  resolveMetadata();
}

void NexusArtifactDownloader::resolveMetadata()
{
  const QUrl endpoint(
      QStringLiteral("https://api.nexusmods.com/v1/games/%1/mods/%2/files.json")
          .arg(m_request.domain)
          .arg(m_request.modId));
  auto* manager = NexusInterface::instance().getAccessManager();
  m_reply = manager ? manager->makeAuthenticatedGetRequest(endpoint) : nullptr;
  if (!m_reply) return fail(tr("Sign in to Nexus before downloading Linux setup tools."));
  auto* reply = m_reply.data();
  connect(reply, &QNetworkReply::finished, this,
          [this, reply] { metadataReady(reply); });
}

void NexusArtifactDownloader::metadataReady(QNetworkReply* reply)
{
  const auto error = reply->error();
  const QString message = reply->errorString();
  const QByteArray body = reply->readAll();
  reply->deleteLater();
  m_reply.clear();
  if (error != QNetworkReply::NoError)
    return fail(tr("Cannot resolve the Nexus tool release: %1").arg(message));

  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(body, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject())
    return fail(tr("Nexus returned invalid file metadata."));
  m_fileMetadata = selectNexusFile(
      document.object().value(QStringLiteral("files")).toArray(), m_request);
  const QString filename = safeFilename(m_fileMetadata);
  if (m_fileMetadata.isEmpty() || filename.isEmpty()
      || m_fileMetadata.value(QStringLiteral("file_id")).toInt() <= 0) {
    return fail(m_request.fileNameContains.isEmpty()
                    ? tr("Nexus has no active compatible file for %1.").arg(m_request.name)
                    : tr("Nexus has no active %1 file containing '%2'.")
                          .arg(m_request.name, m_request.fileNameContains));
  }

  QString cached;
  if (useVerifiedCache(&cached)) return finish(cached);

  const auto account = NexusInterface::instance().getAPIUserAccount();
  if (account.type() == APIUserAccountTypes::None)
    return fail(tr("Sign in to Nexus before downloading %1.").arg(m_request.name));
  if (account.type() == APIUserAccountTypes::Premium)
    return resolveDownloadLink();

  const int fileId = m_fileMetadata.value(QStringLiteral("file_id")).toInt();
  m_authorizationRequestId = QStringLiteral("post-install:%1:%2")
                                 .arg(m_request.id)
                                 .arg(fileId);
  emit authorizationRequired(
      m_authorizationRequestId, m_request.name, m_request.domain, m_request.modId,
      fileId, m_fileMetadata.value(QStringLiteral("size_kb")).toVariant().toLongLong()
                  * 1024);
}

void NexusArtifactDownloader::provideAuthorization(const QString& requestId,
                                                    const QString& nxmUrl)
{
  if (!isBusy() || requestId != m_authorizationRequestId) return;
  try {
    const NXMUrl parsed(nxmUrl);
    if (parsed.game().compare(m_request.domain, Qt::CaseInsensitive) != 0
        || parsed.modId() != m_request.modId
        || parsed.fileId()
               != m_fileMetadata.value(QStringLiteral("file_id")).toInt()) {
      return fail(tr("The Nexus authorization URL does not match the requested tool."));
    }
  } catch (...) {
    return fail(tr("Nexus returned an invalid authorization URL."));
  }
  resolveDownloadLink(nxmUrl);
}

void NexusArtifactDownloader::rejectAuthorization(const QString& requestId,
                                                   const QString& reason)
{
  if (isBusy() && requestId == m_authorizationRequestId) fail(reason);
}

void NexusArtifactDownloader::resolveDownloadLink(const QString& nxmUrl)
{
  QUrl endpoint(
      QStringLiteral("https://api.nexusmods.com/v1/games/%1/mods/%2/files/%3/download_link.json")
          .arg(m_request.domain)
          .arg(m_request.modId)
          .arg(m_fileMetadata.value(QStringLiteral("file_id")).toInt()));
  if (!nxmUrl.isEmpty()) {
    try {
      const NXMUrl parsed(nxmUrl);
      QUrlQuery query;
      query.addQueryItem(QStringLiteral("key"), parsed.key());
      query.addQueryItem(QStringLiteral("expires"),
                         QString::number(parsed.expires()));
      endpoint.setQuery(query);
    } catch (...) {
      return fail(tr("Nexus returned an invalid authorization URL."));
    }
  }
  auto* manager = NexusInterface::instance().getAccessManager();
  m_reply = manager ? manager->makeAuthenticatedGetRequest(endpoint) : nullptr;
  if (!m_reply) return fail(tr("Nexus authentication is unavailable."));
  auto* reply = m_reply.data();
  connect(reply, &QNetworkReply::finished, this,
          [this, reply] { downloadLinkReady(reply); });
}

void NexusArtifactDownloader::downloadLinkReady(QNetworkReply* reply)
{
  const auto error = reply->error();
  const QString message = reply->errorString();
  const QByteArray body = reply->readAll();
  reply->deleteLater();
  m_reply.clear();
  if (error != QNetworkReply::NoError)
    return fail(tr("Nexus download authorization failed: %1").arg(message));
  const auto links = QJsonDocument::fromJson(body).array();
  for (const auto& value : links) {
    const QUrl uri(value.toObject().value(QStringLiteral("URI")).toString());
    if (uri.isValid() && uri.scheme().compare(QStringLiteral("https"),
                                               Qt::CaseInsensitive) == 0) {
      download(uri);
      return;
    }
  }
  fail(tr("Nexus returned no usable download location for %1.").arg(m_request.name));
}

void NexusArtifactDownloader::download(const QUrl& url)
{
  if (!QDir().mkpath(cacheDirectory()))
    return fail(tr("Cannot create the Nexus tool cache."));
  QNetworkRequest request(url);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  m_reply = m_network->get(request);
  auto* reply = m_reply.data();
  auto* output = new QSaveFile(cachedPath(), reply);
  if (!output->open(QIODevice::WriteOnly)) {
    reply->abort();
    reply->deleteLater();
    return fail(tr("Cannot write the Nexus tool cache file."));
  }
  connect(reply, &QNetworkReply::readyRead, output,
          [reply, output] { output->write(reply->readAll()); });
  connect(reply, &QNetworkReply::downloadProgress, this,
          [this](qint64 received, qint64 total) {
            emit progress(m_request.id, received, total);
          });
  connect(reply, &QNetworkReply::finished, this, [this, reply, output] {
    output->write(reply->readAll());
    const auto error = reply->error();
    const QString message = reply->errorString();
    const bool committed = error == QNetworkReply::NoError && output->commit();
    reply->deleteLater();
    output->deleteLater();
    m_reply.clear();
    if (!committed) return fail(tr("Tool download failed: %1").arg(message));
    finish(cachedPath());
  });
}

QString NexusArtifactDownloader::cacheDirectory() const
{
  const QString safeId = QString(m_request.id).replace(
      QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
  return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
         + QStringLiteral("/nexus-tools-v1/") + safeId;
}

QString NexusArtifactDownloader::cachedPath() const
{
  return QDir(cacheDirectory()).filePath(
      QStringLiteral("%1-%2")
          .arg(m_fileMetadata.value(QStringLiteral("file_id")).toInt())
          .arg(safeFilename(m_fileMetadata)));
}

bool NexusArtifactDownloader::useVerifiedCache(QString* path) const
{
  QFile manifest(QDir(cacheDirectory()).filePath(QStringLiteral("manifest.json")));
  if (!manifest.open(QIODevice::ReadOnly)) return false;
  const auto saved = QJsonDocument::fromJson(manifest.readAll()).object();
  if (saved.value(QStringLiteral("fileId")).toInt()
          != m_fileMetadata.value(QStringLiteral("file_id")).toInt())
    return false;
  const QString candidate = cachedPath();
  if (!QFileInfo(candidate).isFile()) return false;
  const QByteArray expected =
      saved.value(QStringLiteral("sha256")).toString().toLatin1();
  if (expected.isEmpty() || fileSha256(candidate) != expected) return false;
  if (path) *path = candidate;
  return true;
}

void NexusArtifactDownloader::finish(const QString& path)
{
  if (!QFileInfo(path).isFile()) return fail(tr("The downloaded Nexus tool is missing."));
  const QByteArray hash = fileSha256(path);
  if (hash.isEmpty()) return fail(tr("Cannot verify the downloaded Nexus tool."));
  QDir().mkpath(cacheDirectory());
  QSaveFile manifest(QDir(cacheDirectory()).filePath(QStringLiteral("manifest.json")));
  QJsonObject saved{{QStringLiteral("fileId"),
                     m_fileMetadata.value(QStringLiteral("file_id"))},
                    {QStringLiteral("filename"), safeFilename(m_fileMetadata)},
                    {QStringLiteral("version"),
                     m_fileMetadata.value(QStringLiteral("version"))},
                    {QStringLiteral("sha256"), QString::fromLatin1(hash)}};
  if (!manifest.open(QIODevice::WriteOnly)
      || manifest.write(QJsonDocument(saved).toJson(QJsonDocument::Indented)) < 0
      || !manifest.commit())
    return fail(tr("Cannot record the verified Nexus tool download."));

  const QString id = m_request.id;
  const QJsonObject metadata = m_fileMetadata;
  reset();
  emit completed(id, path, metadata);
}

void NexusArtifactDownloader::fail(const QString& reason)
{
  const QString id = m_request.id;
  reset();
  emit failed(id, reason);
}

void NexusArtifactDownloader::cancel()
{
  if (m_reply) {
    disconnect(m_reply, nullptr, this, nullptr);
    m_reply->abort();
    m_reply->deleteLater();
  }
  reset();
}

void NexusArtifactDownloader::reset()
{
  m_reply.clear();
  m_request = {};
  m_fileMetadata = {};
  m_authorizationRequestId.clear();
}
