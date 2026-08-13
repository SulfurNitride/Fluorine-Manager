#include "externalmessagequeue.h"

#include <utility>

bool ExternalMessageQueue::enqueue(QString message, Scope scope) {
  if (m_phase == Phase::Stopping ||
      (m_phase != Phase::Ready && scope == Scope::CurrentGeneration)) {
    return false;
  }

  const qsizetype bytes = message.toUtf8().size();
  if (m_messages.size() >= MaximumMessages || bytes > MaximumBytes - m_bytes) {
    return false;
  }

  m_messages.enqueue({std::move(message), bytes,
                      scope == Scope::CurrentGeneration
                          ? std::optional<quint64>(m_generation)
                          : std::nullopt});
  m_bytes += bytes;
  return true;
}

std::optional<QString> ExternalMessageQueue::takeNext() {
  if (m_phase != Phase::Ready || m_messages.isEmpty()) {
    return std::nullopt;
  }

  Message message = m_messages.dequeue();
  m_bytes -= message.bytes;
  if (message.generation && *message.generation != m_generation) {
    return std::nullopt;
  }
  return std::move(message.text);
}

qsizetype ExternalMessageQueue::discardStale() {
  qsizetype discarded = 0;
  QQueue<Message> retained;
  while (!m_messages.isEmpty()) {
    Message message = m_messages.dequeue();
    if (message.generation && *message.generation != m_generation) {
      m_bytes -= message.bytes;
      ++discarded;
    } else {
      retained.enqueue(std::move(message));
    }
  }
  m_messages.swap(retained);
  return discarded;
}

ExternalMessageQueue::DispatchAction
ExternalMessageQueue::dispatchAction(bool exitAttemptInProgress,
                                     bool exitAuthorized) const {
  if (exitAuthorized) {
    return DispatchAction::Stop;
  }
  if (m_phase != Phase::Ready || m_messages.isEmpty() ||
      exitAttemptInProgress) {
    return DispatchAction::Wait;
  }
  return DispatchAction::Dispatch;
}

void ExternalMessageQueue::pause() {
  if (m_phase != Phase::Stopping) {
    m_phase = Phase::Waiting;
  }
}

qsizetype ExternalMessageQueue::resume() {
  if (m_phase == Phase::Stopping) {
    return 0;
  }
  if (m_phase == Phase::Ready) {
    return discardStale();
  }
  ++m_generation;
  m_phase = Phase::Ready;
  return discardStale();
}

qsizetype ExternalMessageQueue::stop() {
  m_phase = Phase::Stopping;
  const qsizetype discarded = m_messages.size();
  m_messages.clear();
  m_bytes = 0;
  return discarded;
}
