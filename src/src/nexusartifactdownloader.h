#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

struct NexusArtifactRequest
{
  QString id;
  QString name;
  QString domain;
  int modId{0};
  QString fileNameContains;
  QString minimumVersion;
};

// Resolves and downloads one reviewed Nexus tool at a time. Premium accounts
// receive a direct link; regular accounts are paused until the embedded Nexus
// browser returns the matching NXM authorization URL.
class NexusArtifactDownloader : public QObject
{
  Q_OBJECT

public:
  explicit NexusArtifactDownloader(QObject* parent = nullptr);

  bool isBusy() const { return !m_request.id.isEmpty(); }
  void start(const NexusArtifactRequest& request);
  void provideAuthorization(const QString& requestId, const QString& nxmUrl);
  void rejectAuthorization(const QString& requestId, const QString& reason);
  void cancel();

signals:
  void authorizationRequired(QString requestId, QString artifactName,
                             QString domain, int modId, int fileId,
                             qint64 expectedSize);
  void progress(QString artifactId, qint64 received, qint64 total);
  void completed(QString artifactId, QString path, QJsonObject metadata);
  void failed(QString artifactId, QString reason);

private:
  NexusArtifactRequest m_request;
  QJsonObject m_fileMetadata;
  QString m_authorizationRequestId;
  QNetworkAccessManager* m_network{};
  QPointer<QNetworkReply> m_reply;

  void resolveMetadata();
  void metadataReady(QNetworkReply* reply);
  void resolveDownloadLink(const QString& nxmUrl = {});
  void downloadLinkReady(QNetworkReply* reply);
  void download(const QUrl& url);
  void finish(const QString& path);
  void fail(const QString& reason);
  void reset();

  QString cacheDirectory() const;
  QString cachedPath() const;
  bool useVerifiedCache(QString* path) const;
};
