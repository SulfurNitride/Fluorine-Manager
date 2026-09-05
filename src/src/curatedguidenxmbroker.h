#pragma once

#include <QObject>
#include <QList>
#include <QString>

class CuratedGuideNxmBroker : public QObject
{
  Q_OBJECT

public:
  static CuratedGuideNxmBroker& instance();

  void expect(const QString& artifactId, const QString& domain, int modId, int fileId);
  void expectForConsumer(const QString& consumer, const QString& requestId,
                         const QString& domain, int modId, int fileId,
                         int expectedUserId = 0);
  void clear(const QString& artifactId = {});
  void clearConsumer(const QString& consumer);
  bool tryConsume(const QString& url);
  bool isWaiting() const { return !m_pending.isEmpty(); }

signals:
  void accepted(QString artifactId, QString url);
  void acceptedForConsumer(QString consumer, QString requestId, QString url);
  void rejectedForConsumer(QString consumer, QString requestId, QString reason);

private:
  struct PendingRequest
  {
    QString consumer;
    QString requestId;
    QString domain;
    int modId{0};
    int fileId{0};
    int expectedUserId{0};
  };
  QList<PendingRequest> m_pending;
};
