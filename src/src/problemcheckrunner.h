#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

// Executes one diagnosis callback per owning-thread event turn. Queued callbacks
// are context-bound to this QObject and generation-checked, so cancel() is a
// synchronous teardown barrier without waiting or pumping a nested event loop.
class ProblemCheckRunner : public QObject
{
public:
  using Task       = std::function<std::size_t()>;
  using Completion = std::function<void(std::size_t)>;
  using Callback   = std::function<void()>;
  using Scheduler  = std::function<void(Callback)>;

  explicit ProblemCheckRunner(Scheduler scheduler = {})
  {
    if (scheduler) {
      m_Schedule = std::move(scheduler);
    } else {
      m_Schedule = [this](Callback callback) {
        QTimer::singleShot(0, this, std::move(callback));
      };
    }
  }

  ProblemCheckRunner(const ProblemCheckRunner&) = delete;
  ProblemCheckRunner& operator=(const ProblemCheckRunner&) = delete;

  ~ProblemCheckRunner() override { cancel(); }

  bool running() const noexcept { return m_Running; }

  void start(std::vector<Task> tasks, Completion completion,
             Callback failure = {})
  {
    if (m_Running) {
      return;
    }
    m_Quiesced   = {};
    m_Tasks      = std::move(tasks);
    m_Completion = std::move(completion);
    m_Failure    = std::move(failure);
    m_Index      = 0;
    m_Total      = 0;
    m_Running    = true;
    const auto generation = ++m_Generation;
    queueStep(generation);
  }

  void cancel(Callback quiesced = {})
  {
    ++m_Generation;
    m_Tasks.clear();
    m_Completion = {};
    m_Failure    = {};
    m_Index      = 0;
    m_Total      = 0;

    if (!m_Running) {
      if (quiesced) {
        schedule(std::move(quiesced));
      }
      return;
    }

    m_Quiesced = std::move(quiesced);
    if (!m_InTask) {
      finishCancellation();
    }
  }

private:
  void schedule(Callback callback)
  {
    QPointer<ProblemCheckRunner> self(this);
    m_Schedule([self, callback = std::move(callback)]() mutable {
      if (!self.isNull()) {
        callback();
      }
    });
  }

  void queueStep(std::uint64_t generation)
  {
    schedule([this, generation]() { runStep(generation); });
  }

  void runStep(std::uint64_t generation)
  {
    if (!m_Running || generation != m_Generation) {
      return;
    }
    if (m_Index == m_Tasks.size()) {
      finishSuccess();
      return;
    }

    Task task = std::move(m_Tasks[m_Index++]);
    m_InTask = true;
    QPointer<ProblemCheckRunner> self(this);
    std::size_t contribution = 0;
    bool failed             = false;
    try {
      contribution = task();
    } catch (...) {
      failed = true;
    }

    // A plugin may enter a nested event loop. The owner can be destroyed while
    // that callback is on the stack, so do not touch member state afterward.
    if (self.isNull()) {
      return;
    }

    m_Total += contribution;
    if (failed && m_Failure) {
      auto failure = m_Failure;
      failure();
      if (self.isNull()) {
        return;
      }
    }
    m_InTask = false;

    if (generation != m_Generation) {
      finishCancellation();
      return;
    }
    queueStep(generation);
  }

  void finishSuccess()
  {
    m_Running = false;
    m_Tasks.clear();
    auto completion = std::move(m_Completion);
    m_Failure       = {};
    if (completion) {
      completion(m_Total);
    }
  }

  void finishCancellation()
  {
    m_Running = false;
    m_InTask  = false;
    m_Tasks.clear();
    auto quiesced = std::move(m_Quiesced);
    if (quiesced) {
      schedule(std::move(quiesced));
    }
  }

  Scheduler m_Schedule;
  std::vector<Task> m_Tasks;
  Completion m_Completion;
  Callback m_Failure;
  Callback m_Quiesced;
  std::size_t m_Index{0};
  std::size_t m_Total{0};
  std::uint64_t m_Generation{0};
  bool m_Running{false};
  bool m_InTask{false};
};
