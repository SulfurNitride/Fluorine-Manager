#pragma once

#include <QFile>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <memory>

class QNetworkReply;

// Fetches CLF3 independently of Fluorine releases. Each download is staged and
// checked before an atomic manifest update makes it the current engine.
class Clf3EngineManager : public QObject
{
  Q_OBJECT
public:
  explicit Clf3EngineManager(QObject* parent = nullptr,
                            QNetworkAccessManager* network = nullptr,
                            const QString& cacheRoot = {});
  ~Clf3EngineManager() override;
  void prepare();
  void cancel();
  QString cachedEnginePath() const;

signals:
  void ready(QString path);
  void statusChanged(QString message);
  void failed(QString message);
  void cancelled();

private:
  QNetworkAccessManager* m_network;
  QPointer<QNetworkReply> m_reply;
  QString m_root;
  QJsonObject m_release;
  std::unique_ptr<QTemporaryDir> m_staging;
  QFile m_download;
  QProcess m_process;
  QTimer m_timeout;
  bool m_busy{false};
  bool m_probing{false};

  QJsonObject cachedRelease() const;
  void fetchRelease();
  void downloadRelease(const QJsonObject& release);
  void extractRelease();
  void promoteRelease();
  void finish(const QString& path);
  void fail(const QString& message);
  void cleanup();
};
