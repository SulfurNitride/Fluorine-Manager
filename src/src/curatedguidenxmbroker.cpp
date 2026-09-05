#include "curatedguidenxmbroker.h"

#include <uibase/nxmurl.h>

CuratedGuideNxmBroker& CuratedGuideNxmBroker::instance()
{
  static CuratedGuideNxmBroker broker;
  return broker;
}

void CuratedGuideNxmBroker::expect(const QString& artifactId, const QString& domain,
                                   int modId, int fileId)
{
  clearConsumer(QStringLiteral("curated-guide"));
  expectForConsumer(QStringLiteral("curated-guide"), artifactId, domain, modId,
                    fileId);
}

void CuratedGuideNxmBroker::expectForConsumer(const QString& consumer,
                                              const QString& requestId,
                                              const QString& domain, int modId,
                                              int fileId, int expectedUserId)
{
  m_pending.push_back(
      {consumer, requestId, domain, modId, fileId, expectedUserId});
}

void CuratedGuideNxmBroker::clear(const QString& artifactId)
{
  if (artifactId.isEmpty()) {
    m_pending.clear();
    return;
  }
  m_pending.removeIf([&](const PendingRequest& request) {
    return request.requestId == artifactId;
  });
}

void CuratedGuideNxmBroker::clearConsumer(const QString& consumer)
{
  m_pending.removeIf([&](const PendingRequest& request) {
    return request.consumer == consumer;
  });
}

bool CuratedGuideNxmBroker::tryConsume(const QString& url)
{
  if (m_pending.isEmpty()) {
    return false;
  }
  try {
    const NXMUrl parsed(url);
    if (parsed.isCollection()) return false;
    for (qsizetype i = 0; i < m_pending.size(); ++i) {
      const auto request = m_pending.at(i);
      if (parsed.game().compare(request.domain, Qt::CaseInsensitive) != 0
          || parsed.modId() != request.modId || parsed.fileId() != request.fileId) {
        continue;
      }
      m_pending.removeAt(i);
      if (request.expectedUserId > 0 && parsed.userId() != request.expectedUserId) {
        emit rejectedForConsumer(
            request.consumer, request.requestId,
            tr("The Nexus browser account does not match Fluorine's Nexus account."));
        return true;
      }
      emit acceptedForConsumer(request.consumer, request.requestId, url);
      if (request.consumer == QStringLiteral("curated-guide"))
        emit accepted(request.requestId, url);
      return true;
    }
    return false;
  } catch (...) {
    return false;
  }
}
