#ifndef FLUORINE_EXTERNALMESSAGEQUEUE_H
#define FLUORINE_EXTERNALMESSAGEQUEUE_H

#include <QQueue>
#include <QString>

#include <optional>

class ExternalMessageQueue {
public:
  enum class Phase { Waiting, Ready, Stopping };
  enum class Scope { CurrentGeneration, AnyGeneration };
  enum class DispatchAction { Wait, Dispatch, Stop };

  static constexpr qsizetype MaximumMessages = 64;
  static constexpr qsizetype MaximumBytes = 1024 * 1024;

  bool enqueue(QString message, Scope scope);
  std::optional<QString> takeNext();
  qsizetype discardStale();
  DispatchAction dispatchAction(bool exitAttemptInProgress,
                                bool exitAuthorized) const;

  void pause();
  qsizetype resume();
  qsizetype stop();

  Phase phase() const { return m_phase; }
  bool ready() const { return m_phase == Phase::Ready; }
  qsizetype size() const { return m_messages.size(); }
  qsizetype bytes() const { return m_bytes; }

private:
  struct Message {
    QString text;
    qsizetype bytes = 0;
    std::optional<quint64> generation;
  };

  Phase m_phase = Phase::Waiting;
  QQueue<Message> m_messages;
  qsizetype m_bytes = 0;
  quint64 m_generation = 0;
};

#endif
