#ifndef ASYNCTASK_H
#define ASYNCTASK_H

#include <uibase/log.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace async_task
{

// A launch reserves its execution slot before spawning a process. Each slot
// owns an already-running jthread that waits for exactly one submitted task;
// submit() therefore cannot encounter a post-launch thread/detach failure and
// no process-long task can block dispatch of another admitted launch.
//
// The production instance deliberately has process lifetime. Organizer work
// uses QPointer guards, and process exit remains the final boundary for active
// launches; synchronously joining from aboutToQuit would deadlock queued Qt
// cleanup continuations. Completed slots are joined promptly by the reaper.
class ManagedTaskExecutor
{
  struct TaskState;
  struct WorkerState;

public:
  using Task = std::function<void(std::stop_token)>;
  using FailureHandler = std::function<void()>;
  using ThreadStarter = std::function<std::jthread(Task)>;
  using PrepareHook = std::function<void()>;

  class Lease
  {
  public:
    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;

    ~Lease()
    {
      if (m_Executor != nullptr && m_State) {
        m_Executor->releaseUnused(m_State);
      }
    }

    const void* owner() const { return m_Owner; }

  private:
    friend class ManagedTaskExecutor;

    Lease(ManagedTaskExecutor& executor, const void* owner,
          std::shared_ptr<TaskState> state)
        : m_Executor(&executor), m_Owner(owner), m_State(std::move(state))
    {}

    ManagedTaskExecutor* m_Executor;
    const void* m_Owner;
    std::shared_ptr<TaskState> m_State;
  };

  using LeasePtr = std::shared_ptr<Lease>;

  explicit ManagedTaskExecutor(
      std::size_t capacity = 64, ThreadStarter workerStarter = {},
      ThreadStarter reaperStarter = {}, PrepareHook prepareHook = {})
      : m_Capacity(std::max<std::size_t>(capacity, 1)),
        m_WorkerStarter(workerStarter ? std::move(workerStarter)
                                      : defaultThreadStarter()),
        m_ReaperStarter(reaperStarter ? std::move(reaperStarter)
                                      : defaultThreadStarter()),
        m_PrepareHook(std::move(prepareHook))
  {
    // All container storage is reserved before a launch can be admitted.
    m_Workers.reserve(m_Capacity);
  }

  ManagedTaskExecutor(const ManagedTaskExecutor&) = delete;
  ManagedTaskExecutor& operator=(const ManagedTaskExecutor&) = delete;

  ~ManagedTaskExecutor() { shutdown(); }

  LeasePtr reserve(const void* owner)
  {
    std::lock_guard lock(m_Mutex);
    if (m_ShuttingDown || m_Admissions >= m_Capacity ||
        !ensureReaperLocked()) {
      return {};
    }

    try {
      auto state = std::make_shared<TaskState>();
      auto lease = LeasePtr(new Lease(*this, owner, state));
      auto worker = std::make_shared<WorkerState>(state);
      auto thread = m_WorkerStarter(
          [this, state](std::stop_token stop) { runSlot(state, stop); });
      if (!thread.joinable()) {
        return {};
      }
      worker->thread = std::move(thread);
      m_Workers.push_back(std::move(worker));
      ++m_Admissions;
      return lease;
    } catch (const std::exception& e) {
      MOBase::log::error(
          "async task executor: pre-launch slot creation failed: {}",
          e.what());
    } catch (...) {
      MOBase::log::error(
          "async task executor: pre-launch slot creation failed");
    }
    return {};
  }

  // Stores every potentially allocating callable before a process exists.
  // Tests may inject a preparation failure through PrepareHook; the reserved
  // slot remains inactive and can be safely released.
  bool prepare(const LeasePtr& lease, Task task,
               FailureHandler failureHandler = {})
  {
    if (!lease || !task || lease->m_Executor != this || !lease->m_State) {
      return false;
    }

    try {
      if (m_PrepareHook) {
        m_PrepareHook();
      }
      const auto state = lease->m_State;
      std::lock_guard lock(state->mutex);
      if (state->prepared.load(std::memory_order_acquire) ||
          state->activated.load(std::memory_order_acquire) ||
          state->cancelled.load(std::memory_order_acquire) || state->done) {
        return false;
      }
      state->task = std::move(task);
      state->failureHandler = std::move(failureHandler);
      state->prepared.store(true, std::memory_order_release);
      return true;
    } catch (const std::exception& e) {
      MOBase::log::error("async task executor: task preparation failed: {}",
                         e.what());
    } catch (...) {
      MOBase::log::error("async task executor: task preparation failed");
    }
    return false;
  }

  // Activation performs no allocation and never throws. It is the only
  // operation required after a successful process spawn.
  bool activate(const LeasePtr& lease) noexcept
  {
    if (!lease || lease->m_Executor != this || !lease->m_State) {
      return false;
    }
    const auto& state = lease->m_State;
    if (!state->prepared.load(std::memory_order_acquire) ||
        state->cancelled.load(std::memory_order_acquire) ||
        state->done.load(std::memory_order_acquire)) {
      return false;
    }
    bool expected = false;
    if (!state->activated.compare_exchange_strong(
            expected, true, std::memory_order_release,
            std::memory_order_relaxed)) {
      return false;
    }
    state->condition.notify_one();
    return true;
  }

  bool submit(const LeasePtr& lease, Task task,
              FailureHandler failureHandler = {})
  {
    return prepare(lease, std::move(task), std::move(failureHandler)) &&
           activate(lease);
  }

  std::size_t activeAdmissions() const
  {
    std::lock_guard lock(m_Mutex);
    return m_Admissions;
  }

  // Production intentionally leaks its singleton. This defensive shutdown is
  // for local test instances after their submitted work has completed.
  void shutdown()
  {
    std::vector<std::shared_ptr<WorkerState>> workers;
    {
      std::lock_guard lock(m_Mutex);
      if (m_ShuttingDown) {
        return;
      }
      m_ShuttingDown = true;
      m_Reaper.request_stop();
      workers = m_Workers;
      for (const auto& worker : workers) {
        worker->thread.request_stop();
        cancelState(worker->state);
      }
    }
    m_Condition.notify_all();

    if (m_Reaper.joinable()) {
      m_Reaper.join();
    }
    for (const auto& worker : workers) {
      if (worker->thread.joinable()) {
        worker->thread.join();
      }
    }

    std::lock_guard lock(m_Mutex);
    m_Workers.clear();
    m_Admissions = 0;
  }

private:
  struct TaskState
  {
    std::mutex mutex;
    std::condition_variable condition;
    Task task;
    FailureHandler failureHandler;
    std::atomic<bool> prepared{false};
    std::atomic<bool> activated{false};
    std::atomic<bool> cancelled{false};
    std::atomic<bool> done{false};
  };

  struct WorkerState
  {
    explicit WorkerState(std::shared_ptr<TaskState> taskState)
        : state(std::move(taskState))
    {}

    std::shared_ptr<TaskState> state;
    std::jthread thread;
  };

  static ThreadStarter defaultThreadStarter()
  {
    return [](Task task) { return std::jthread(std::move(task)); };
  }

  bool ensureReaperLocked()
  {
    if (m_Reaper.joinable()) {
      return true;
    }
    try {
      auto reaper =
          m_ReaperStarter([this](std::stop_token stop) { reap(stop); });
      if (!reaper.joinable()) {
        return false;
      }
      m_Reaper = std::move(reaper);
      return true;
    } catch (const std::exception& e) {
      MOBase::log::error("async task executor: reaper creation failed: {}",
                         e.what());
    } catch (...) {
      MOBase::log::error("async task executor: reaper creation failed");
    }
    return false;
  }

  void runSlot(const std::shared_ptr<TaskState>& state, std::stop_token stop)
  {
    Task task;
    FailureHandler failureHandler;
    {
      std::unique_lock lock(state->mutex);
      state->condition.wait(lock, [&]() {
        return stop.stop_requested() ||
               state->activated.load(std::memory_order_acquire) ||
               state->cancelled.load(std::memory_order_acquire);
      });
      if (!stop.stop_requested() &&
          state->activated.load(std::memory_order_acquire) &&
          !state->cancelled.load(std::memory_order_acquire)) {
        task = std::move(state->task);
        failureHandler = std::move(state->failureHandler);
      }
    }

    bool failed = false;
    if (task) {
      try {
        task(stop);
      } catch (const std::exception& e) {
        failed = true;
        MOBase::log::error("async task executor: task failed: {}", e.what());
      } catch (...) {
        failed = true;
        MOBase::log::error("async task executor: task failed");
      }
    }
    if (failed && failureHandler) {
      try {
        failureHandler();
      } catch (...) {
        MOBase::log::error(
            "async task executor: task failure handler failed");
      }
    }

    state->done.store(true, std::memory_order_release);
    m_Condition.notify_one();
  }

  void reap(std::stop_token stop)
  {
    while (!stop.stop_requested()) {
      std::vector<std::shared_ptr<WorkerState>> completed;
      {
        std::unique_lock lock(m_Mutex);
        m_Condition.wait(lock, [&]() {
          return stop.stop_requested() ||
                 std::ranges::any_of(m_Workers, [](const auto& worker) {
                   return worker->state->done.load(std::memory_order_acquire);
                 });
        });
        if (stop.stop_requested()) {
          return;
        }

        auto firstCompleted = std::remove_if(
            m_Workers.begin(), m_Workers.end(), [&](const auto& worker) {
              if (!worker->state->done.load(std::memory_order_acquire)) {
                return false;
              }
              completed.push_back(worker);
              --m_Admissions;
              return true;
            });
        m_Workers.erase(firstCompleted, m_Workers.end());
      }

      // Every selected slot has already returned from its task. Join/destruct
      // it on the reaper, never on the UI/launching thread.
      completed.clear();
    }
  }

  void releaseUnused(const std::shared_ptr<TaskState>& state)
  {
    std::lock_guard lock(state->mutex);
    if (!state->activated.load(std::memory_order_acquire) &&
        !state->done.load(std::memory_order_acquire)) {
      state->cancelled.store(true, std::memory_order_release);
      state->condition.notify_one();
    }
  }

  static void cancelState(const std::shared_ptr<TaskState>& state)
  {
    std::lock_guard lock(state->mutex);
    state->cancelled.store(true, std::memory_order_release);
    state->condition.notify_one();
  }

  const std::size_t m_Capacity;
  ThreadStarter m_WorkerStarter;
  ThreadStarter m_ReaperStarter;
  PrepareHook m_PrepareHook;
  mutable std::mutex m_Mutex;
  std::condition_variable m_Condition;
  std::vector<std::shared_ptr<WorkerState>> m_Workers;
  std::jthread m_Reaper;
  std::size_t m_Admissions{0};
  bool m_ShuttingDown{false};
};

inline ManagedTaskExecutor& executor()
{
  // Active process monitors may still exist while Qt's static objects are
  // being dismantled. Deliberately retain the executor until the OS tears down
  // the process instead of invoking std::jthread joins from static teardown.
  static auto* instance = new ManagedTaskExecutor();
  return *instance;
}

}  // namespace async_task

#endif  // ASYNCTASK_H
