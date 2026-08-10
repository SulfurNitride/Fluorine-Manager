#include "applicationcompletion.h"
#include "applicationrunnerregistry.h"
#include "asynctask.h"
#include "afterrunrefreshqueue.h"
#include "processlifetime.h"
#include "processlaunchcontext.h"
#include "rootprocesscompletion.h"

#include <gtest/gtest.h>

#include <uibase/log.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>

#include <atomic>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <future>
#include <limits>
#include <memory>
#include <stdexcept>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace
{

using namespace std::chrono_literals;

class ThrowingPayload
{
public:
  explicit ThrowingPayload(int value) : value(value) {}

  ThrowingPayload(ThrowingPayload&& other)
  {
    if (throwOnMove) {
      throw std::runtime_error("injected payload move failure");
    }
    value = other.value;
  }

  ThrowingPayload& operator=(ThrowingPayload&&) = delete;
  ThrowingPayload(const ThrowingPayload&)        = delete;
  ThrowingPayload& operator=(const ThrowingPayload&) = delete;

  int value = 0;
  static inline bool throwOnMove = false;
};

class ProcessLifetimeTest : public ::testing::Test
{
protected:
  static void SetUpTestSuite()
  {
    if (QCoreApplication::instance() == nullptr) {
      static int argc = 1;
      static char applicationName[] = "test_processlifetime";
      static char* argv[] = {applicationName, nullptr};
      s_Application = std::make_unique<QCoreApplication>(argc, argv);
    }

    MOBase::log::LoggerConfiguration configuration;
    configuration.name = "test_processlifetime";
    configuration.pattern = "%v";
    configuration.maxLevel = MOBase::log::Warning;
    MOBase::log::createDefault(std::move(configuration));
  }

  static void TearDownTestSuite()
  {
    // Destroy the Qt application while the test and logging infrastructure is
    // still alive. Leaving this to static destruction makes its order relative
    // to those globals unspecified and crashes after otherwise successful
    // tests in the official builder.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    s_Application.reset();
  }

  static inline std::unique_ptr<QCoreApplication> s_Application;
};

struct ManagedQProcess
{
  std::unique_ptr<QProcess> process;
  std::shared_ptr<process_lifetime::RootProcessCompletion> completion;
  pid_t pid = -1;
  std::uint64_t startTime = process_lifetime::UnknownProcessStartTime;
};

struct PreparedRootBinding
{
  pid_t pid = -1;
  std::uint64_t startTime = process_lifetime::UnknownProcessStartTime;
  std::shared_ptr<process_lifetime::RootProcessCompletion> completion;
};

ManagedQProcess startManagedShell(
    const QString& script, const QString& launchToken = {})
{
  ManagedQProcess launched;
  launched.process = std::make_unique<QProcess>();
  launched.completion =
      std::make_shared<process_lifetime::RootProcessCompletion>();
  process_lifetime::observeQProcessRoot(*launched.process,
                                         launched.completion);
  launched.process->setProgram(QStringLiteral("/bin/sh"));
  launched.process->setArguments({QStringLiteral("-c"), script});
  if (!launchToken.isEmpty()) {
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(
        QString::fromLatin1(process_lifetime::LaunchTokenEnvironment),
        launchToken);
    launched.process->setProcessEnvironment(environment);
  }
  launched.process->start();
  if (!launched.process->waitForStarted(3000)) {
    return launched;
  }

  launched.completion->markStarted();
  launched.pid = static_cast<pid_t>(launched.process->processId());
  launched.startTime =
      process_lifetime::processStartTime(launched.pid)
          .value_or(process_lifetime::UnknownProcessStartTime);
  return launched;
}

bool pumpEventsUntil(const std::function<bool()>& predicate,
                     std::chrono::milliseconds timeout)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    std::this_thread::sleep_for(1ms);
  }
  return predicate();
}

TEST_F(ProcessLifetimeTest, QProcessExitNormalizationIsStrictAndTerminalIsFirst)
{
  EXPECT_EQ(process_lifetime::normalizeQProcessExit(
                0, QProcess::NormalExit),
            0u);
  EXPECT_EQ(process_lifetime::normalizeQProcessExit(
                47, QProcess::NormalExit),
            47u);
  EXPECT_EQ(process_lifetime::normalizeQProcessExit(
                SIGTERM, QProcess::CrashExit),
            static_cast<std::uint32_t>(128 + SIGTERM));
  EXPECT_FALSE(process_lifetime::normalizeQProcessExit(
      -1, QProcess::NormalExit));
  EXPECT_FALSE(process_lifetime::normalizeQProcessExit(
      0, QProcess::CrashExit));
  EXPECT_FALSE(process_lifetime::normalizeQProcessExit(
      128, QProcess::CrashExit));

  process_lifetime::RootProcessCompletion completed;
  completed.markStarted();
  completed.reportError(QProcess::FailedToStart);
  EXPECT_EQ(completed.snapshot().state,
            process_lifetime::RootProcessCompletion::State::Running);
  completed.finish(47, QProcess::NormalExit);
  completed.finish(0, QProcess::NormalExit);
  EXPECT_EQ(completed.snapshot().state,
            process_lifetime::RootProcessCompletion::State::Exited);
  EXPECT_EQ(completed.snapshot().exitCode, 47u);

  process_lifetime::RootProcessCompletion unknownCrash;
  unknownCrash.markStarted();
  unknownCrash.finish(0, QProcess::CrashExit);
  EXPECT_EQ(unknownCrash.snapshot().state,
            process_lifetime::RootProcessCompletion::State::Exited);
  EXPECT_EQ(unknownCrash.snapshot().exitCode,
            process_lifetime::RootProcessCompletion::InvalidExitCode);

  process_lifetime::RootProcessCompletion failedToStart;
  failedToStart.reportError(QProcess::FailedToStart);
  failedToStart.finish(0, QProcess::NormalExit);
  EXPECT_EQ(failedToStart.snapshot().state,
            process_lifetime::RootProcessCompletion::State::Failed);
}

TEST_F(ProcessLifetimeTest,
       ScheduleAsyncRefreshReservedSlotKeepsOwnerResponsive)
{
  async_task::ManagedTaskExecutor executor;

  // The execution thread is established before the process exists, matching
  // ProcessRunner::runBinary(). prepare() stores every callable before spawn.
  auto lease = executor.reserve(QCoreApplication::instance());
  ASSERT_TRUE(lease);
  auto binding = std::make_shared<PreparedRootBinding>();
  std::atomic<int> taskRuns = 0;
  std::atomic<int> waitpidAttempts = 0;
  std::atomic<int> cleanup = 0;
  std::atomic<int> refresh = 0;
  std::atomic<int> ownerTicks = 0;
  std::atomic<int> result = static_cast<int>(process_lifetime::Result::Error);
  std::atomic<std::uint32_t> exitCode = 0;
  ASSERT_TRUE(executor.prepare(
      lease, [&, binding](std::stop_token stop) {
        ++taskRuns;
        process_lifetime::Callbacks callbacks;
        callbacks.waitpidAttempted = [&](pid_t) { ++waitpidAttempts; };
        callbacks.control = [stop]() {
          return stop.stop_requested() ? process_lifetime::Control::Error
                                       : process_lifetime::Control::Continue;
        };
        std::uint32_t observed = 0;
        const auto lifetime = process_lifetime::waitForPid(
            binding->pid, &observed, callbacks, {}, false, {}, false,
            binding->startTime, binding->completion);
        result = static_cast<int>(lifetime);
        exitCode = observed;
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [&]() {
              ++cleanup;
              ++refresh;
            },
            Qt::QueuedConnection);
      }));

  auto launched = startManagedShell(QStringLiteral("sleep 0.08; exit 47"));
  ASSERT_GT(launched.pid, 0);
  binding->pid = launched.pid;
  binding->startTime = launched.startTime;
  binding->completion = launched.completion;

  const auto submittedAt = std::chrono::steady_clock::now();
  ASSERT_TRUE(executor.activate(lease));
  EXPECT_FALSE(executor.activate(lease));
  lease.reset();
  const auto submitElapsed = std::chrono::steady_clock::now() - submittedAt;

  QMetaObject::invokeMethod(
      QCoreApplication::instance(), [&]() { ++ownerTicks; },
      Qt::QueuedConnection);
  ASSERT_TRUE(pumpEventsUntil(
      [&]() { return cleanup.load() == 1 && ownerTicks.load() == 1; },
      3000ms));
  ASSERT_TRUE(pumpEventsUntil(
      [&]() { return executor.activeAdmissions() == 0; }, 1000ms));

  EXPECT_LT(submitElapsed, 100ms);
  EXPECT_EQ(taskRuns.load(), 1);
  EXPECT_EQ(result.load(), static_cast<int>(process_lifetime::Result::Completed));
  EXPECT_EQ(exitCode.load(), 47u);
  EXPECT_EQ(waitpidAttempts.load(), 0);
  EXPECT_EQ(cleanup.load(), 1);
  EXPECT_EQ(refresh.load(), 1);
}

TEST_F(ProcessLifetimeTest,
       ExecutorRejectsBeforeLaunchAndRunsTaskFailureClosureOnce)
{
  async_task::ManagedTaskExecutor unavailable(
      1, {}, [](async_task::ManagedTaskExecutor::Task) -> std::jthread {
        throw std::system_error(
            std::make_error_code(std::errc::resource_unavailable_try_again));
      });
  EXPECT_FALSE(unavailable.reserve(QCoreApplication::instance()));
  EXPECT_EQ(unavailable.activeAdmissions(), 0u);

  async_task::ManagedTaskExecutor noWorker(
      1, [](async_task::ManagedTaskExecutor::Task) -> std::jthread {
        throw std::system_error(
            std::make_error_code(std::errc::resource_unavailable_try_again));
      });
  EXPECT_FALSE(noWorker.reserve(QCoreApplication::instance()));
  EXPECT_EQ(noWorker.activeAdmissions(), 0u);

  async_task::ManagedTaskExecutor prepareFails(
      1, {}, {}, []() { throw std::bad_alloc(); });
  auto inactive = prepareFails.reserve(QCoreApplication::instance());
  ASSERT_TRUE(inactive);
  EXPECT_FALSE(prepareFails.prepare(inactive, [](std::stop_token) {}));
  EXPECT_FALSE(prepareFails.activate(inactive));
  inactive.reset();
  ASSERT_TRUE(pumpEventsUntil(
      [&]() { return prepareFails.activeAdmissions() == 0; }, 1000ms));

  async_task::ManagedTaskExecutor executor;
  auto lease = executor.reserve(QCoreApplication::instance());
  ASSERT_TRUE(lease);
  std::atomic<int> taskRuns = 0;
  std::atomic<int> failures = 0;
  ASSERT_TRUE(executor.submit(
      lease,
      [&](std::stop_token) {
        ++taskRuns;
        throw std::runtime_error("injected monitor failure");
      },
      [&]() { ++failures; }));
  lease.reset();
  ASSERT_TRUE(pumpEventsUntil(
      [&]() { return executor.activeAdmissions() == 0; }, 1000ms));
  EXPECT_EQ(taskRuns.load(), 1);
  EXPECT_EQ(failures.load(), 1);
}

TEST_F(ProcessLifetimeTest,
       ReservedSlotsDoNotStarveAndRepeatedFailuresRejectBeforeLaunch)
{
  std::atomic<int> workerStarts = 0;
  async_task::ManagedTaskExecutor executor(
      4, [&](async_task::ManagedTaskExecutor::Task task) -> std::jthread {
        ++workerStarts;
        return std::jthread(std::move(task));
      });

  auto firstLease = executor.reserve(reinterpret_cast<void*>(1));
  auto secondLease = executor.reserve(reinterpret_cast<void*>(2));
  auto thirdLease = executor.reserve(reinterpret_cast<void*>(3));
  auto fourthLease = executor.reserve(reinterpret_cast<void*>(4));
  ASSERT_TRUE(firstLease);
  ASSERT_TRUE(secondLease);
  ASSERT_TRUE(thirdLease);
  ASSERT_TRUE(fourthLease);
  EXPECT_FALSE(executor.reserve(reinterpret_cast<void*>(5)));

  std::promise<void> firstStarted;
  auto firstStartedFuture = firstStarted.get_future();
  std::promise<void> releaseFirst;
  const auto releaseFirstFuture = releaseFirst.get_future().share();
  std::promise<void> firstCompleted;
  std::promise<void> laterCompleted;
  auto firstCompletedFuture = firstCompleted.get_future();
  auto laterCompletedFuture = laterCompleted.get_future();
  std::atomic<int> firstRuns = 0;
  std::atomic<int> laterRuns = 0;

  ASSERT_TRUE(executor.submit(
      firstLease, [&](std::stop_token) {
        ++firstRuns;
        firstStarted.set_value();
        releaseFirstFuture.wait();
        firstCompleted.set_value();
      }));
  firstLease.reset();
  ASSERT_EQ(firstStartedFuture.wait_for(1000ms), std::future_status::ready);

  const auto runLater = [&](std::stop_token) {
    if (++laterRuns == 3) {
      laterCompleted.set_value();
    }
  };
  ASSERT_TRUE(executor.submit(secondLease, runLater));
  ASSERT_TRUE(executor.submit(thirdLease, runLater));
  ASSERT_TRUE(executor.submit(fourthLease, runLater));
  secondLease.reset();
  thirdLease.reset();
  fourthLease.reset();

  // Every admitted launch already owns an execution thread, so all later
  // tasks complete while the first process-long monitor remains blocked.
  EXPECT_EQ(laterCompletedFuture.wait_for(1000ms), std::future_status::ready);
  EXPECT_EQ(firstCompletedFuture.wait_for(0ms),
            std::future_status::timeout);
  releaseFirst.set_value();
  EXPECT_EQ(firstCompletedFuture.wait_for(1000ms),
            std::future_status::ready);
  ASSERT_TRUE(pumpEventsUntil(
      [&]() { return executor.activeAdmissions() == 0; }, 1000ms));
  EXPECT_EQ(firstRuns.load(), 1);
  EXPECT_EQ(laterRuns.load(), 3);
  EXPECT_EQ(workerStarts.load(), 4);

  // Repeated worker-construction failures reject the reservation before any
  // process could be spawned and never consume admission capacity.
  std::atomic<int> transientStarts = 0;
  async_task::ManagedTaskExecutor transient(
      4, [&](async_task::ManagedTaskExecutor::Task task) -> std::jthread {
        const int attempt = ++transientStarts;
        if (attempt % 2 == 1) {
          throw std::system_error(
              std::make_error_code(std::errc::resource_unavailable_try_again));
        }
        return std::jthread(std::move(task));
      });
  for (int attempt = 0; attempt < 3; ++attempt) {
    EXPECT_FALSE(transient.reserve(reinterpret_cast<void*>(10 + attempt)));
    EXPECT_EQ(transient.activeAdmissions(), 0u);

    auto retry = transient.reserve(reinterpret_cast<void*>(20 + attempt));
    ASSERT_TRUE(retry);
    EXPECT_EQ(transient.activeAdmissions(), 1u);
    retry.reset();
    ASSERT_TRUE(pumpEventsUntil(
        [&]() { return transient.activeAdmissions() == 0; }, 1000ms));
  }
  EXPECT_EQ(transientStarts.load(), 6);
}

TEST_F(ProcessLifetimeTest,
       MonitorApplicationReservedSlotCompletesAndCleansExactlyOnce)
{
  async_task::ManagedTaskExecutor executor;

  auto lease = executor.reserve(QCoreApplication::instance());
  ASSERT_TRUE(lease);
  auto completion = std::make_shared<ApplicationCompletion>(
      0, false, QStringLiteral("Default"));
  auto binding = std::make_shared<PreparedRootBinding>();
  completion->requestRefresh();

  std::atomic<int> taskRuns = 0;
  std::atomic<int> waitpidAttempts = 0;
  std::atomic<int> cleanup = 0;
  std::atomic<int> refresh = 0;
  std::atomic<int> failureClosures = 0;
  completion->onCleanupFinished([&]() { ++cleanup; });

  ASSERT_TRUE(executor.prepare(
      lease,
      [&, binding](std::stop_token stop) {
        ++taskRuns;
        process_lifetime::Callbacks callbacks;
        callbacks.waitpidAttempted = [&](pid_t) { ++waitpidAttempts; };
        callbacks.control = [stop]() {
          return stop.stop_requested() ? process_lifetime::Control::Error
                                       : process_lifetime::Control::Continue;
        };
        std::uint32_t observed = 0;
        const auto lifetime = process_lifetime::waitForPid(
            binding->pid, &observed, callbacks, {}, false, {}, false,
            binding->startTime, binding->completion);
        completion->finishLifetime(
            lifetime == process_lifetime::Result::Completed
                ? ApplicationCompletion::Result::Completed
                : ApplicationCompletion::Result::Error,
            observed);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [&, completion]() {
              const bool triggerRefresh =
                  completion->claimRefreshForCleanup();
              completion->finishCleanup();
              if (triggerRefresh) {
                ++refresh;
                completion->finishRefresh();
              }
            },
            Qt::QueuedConnection);
      },
      [&]() {
        ++failureClosures;
        completion->finishLifetime(ApplicationCompletion::Result::Error,
                                   static_cast<std::uint32_t>(-1));
        completion->finishCleanup();
      }));

  auto launched = startManagedShell(QStringLiteral("sleep 0.08; exit 47"));
  ASSERT_GT(launched.pid, 0);
  binding->pid = launched.pid;
  binding->startTime = launched.startTime;
  binding->completion = launched.completion;
  completion->bindRootPid(launched.pid);

  const auto submittedAt = std::chrono::steady_clock::now();
  ASSERT_TRUE(executor.activate(lease));
  EXPECT_FALSE(executor.activate(lease));
  lease.reset();
  const auto submitElapsed = std::chrono::steady_clock::now() - submittedAt;

  ASSERT_TRUE(pumpEventsUntil(
      [&]() {
        const auto snapshot = completion->snapshot();
        return snapshot.cleanupFinished && snapshot.refreshFinished;
      },
      3000ms));
  ASSERT_TRUE(pumpEventsUntil(
      [&]() { return executor.activeAdmissions() == 0; }, 1000ms));

  const auto snapshot = completion->snapshot();
  EXPECT_LT(submitElapsed, 100ms);
  EXPECT_EQ(taskRuns.load(), 1);
  EXPECT_EQ(failureClosures.load(), 0);
  EXPECT_EQ(waitpidAttempts.load(), 0);
  EXPECT_EQ(snapshot.result, ApplicationCompletion::Result::Completed);
  EXPECT_EQ(snapshot.exitCode, 47u);
  EXPECT_EQ(cleanup.load(), 1);
  EXPECT_EQ(refresh.load(), 1);
}

TEST_F(ProcessLifetimeTest,
       ManagedQProcessIsSoleStatusOwnerForNormalNonzeroAndSignalExit)
{
  struct ExitCase
  {
    QString script;
    std::uint32_t expected;
  };
  const std::array cases{
      ExitCase{QStringLiteral("sleep 0.02; exit 0"), 0u},
      ExitCase{QStringLiteral("sleep 0.02; exit 47"), 47u},
      ExitCase{QStringLiteral("sleep 0.02; kill -TERM $$"),
               static_cast<std::uint32_t>(128 + SIGTERM)},
  };

  for (int repetition = 0; repetition < 5; ++repetition) {
    for (const auto& testCase : cases) {
      auto launched = startManagedShell(testCase.script);
      ASSERT_GT(launched.pid, 0);
      ASSERT_NE(launched.startTime,
                process_lifetime::UnknownProcessStartTime);

      std::atomic<int> waitpidAttempts = 0;
      process_lifetime::Callbacks callbacks;
      callbacks.waitpidAttempted = [&](pid_t) { ++waitpidAttempts; };
      std::uint32_t exitCode =
          process_lifetime::RootProcessCompletion::InvalidExitCode;
      const auto result = process_lifetime::waitForPid(
          launched.pid, &exitCode, callbacks, {}, false, {}, false,
          launched.startTime, launched.completion);

      EXPECT_EQ(result, process_lifetime::Result::Completed);
      EXPECT_EQ(exitCode, testCase.expected);
      EXPECT_EQ(waitpidAttempts.load(), 0);
    }
  }
}

TEST_F(ProcessLifetimeTest, ManagedQProcessLateWaitUsesCachedTerminalStatus)
{
  for (int repetition = 0; repetition < 5; ++repetition) {
    auto launched =
        startManagedShell(QStringLiteral("sleep 0.01; exit 47"));
    ASSERT_GT(launched.pid, 0);
    ASSERT_TRUE(launched.process->waitForFinished(3000));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    ASSERT_EQ(launched.completion->snapshot().state,
              process_lifetime::RootProcessCompletion::State::Exited);

    std::atomic<int> waitpidAttempts = 0;
    process_lifetime::Callbacks callbacks;
    callbacks.waitpidAttempted = [&](pid_t) { ++waitpidAttempts; };
    std::uint32_t exitCode = 0;
    EXPECT_EQ(process_lifetime::waitForPid(
                  launched.pid, &exitCode, callbacks, {}, false, {}, false,
                  launched.startTime, launched.completion),
              process_lifetime::Result::Completed);
    EXPECT_EQ(exitCode, 47u);
    EXPECT_EQ(waitpidAttempts.load(), 0);
  }
}

TEST_F(ProcessLifetimeTest,
       ManagedTerminalUnknownExitCompletesAndStatusSurvivesObservationRetry)
{
  const QString token = QStringLiteral("fm-qprocess-retry-") +
                        QString::number(::getpid());
  auto launched = startManagedShell(
      QStringLiteral("(exec /bin/sleep 0.45) & /bin/sleep 0.03; exit 47"),
      token);
  ASSERT_GT(launched.pid, 0);

  std::atomic<int> waitpidAttempts = 0;
  process_lifetime::Callbacks unavailable;
  unavailable.waitpidAttempted = [&](pid_t) { ++waitpidAttempts; };
  unavailable.procUnavailableTimeout = 200ms;
  unavailable.procFaultInjector =
      [](process_lifetime::ProcOperation operation,
         pid_t) -> std::optional<process_lifetime::ProcFault> {
    if (operation == process_lifetime::ProcOperation::ReadStat) {
      return process_lifetime::ProcFault{EIO, std::nullopt};
    }
    return std::nullopt;
  };

  std::uint32_t exitCode = 0;
  int afterRunCalls = 0;
  bool launchOwnership = true;
  const auto started = std::chrono::steady_clock::now();
  EXPECT_EQ(process_lifetime::waitForPid(
                launched.pid, &exitCode, unavailable,
                {QStringLiteral("sleep")}, false, token, false,
                launched.startTime, launched.completion),
            process_lifetime::Result::Error);
  EXPECT_EQ(exitCode, 47u);
  EXPECT_EQ(afterRunCalls, 0);
  EXPECT_TRUE(launchOwnership);

  process_lifetime::Callbacks recovered;
  recovered.waitpidAttempted = [&](pid_t) { ++waitpidAttempts; };
  const auto recoveredResult = process_lifetime::waitForPid(
      launched.pid, &exitCode, recovered, {QStringLiteral("sleep")}, false,
      token, false, launched.startTime, launched.completion);
  if (recoveredResult == process_lifetime::Result::Completed) {
    ++afterRunCalls;
    launchOwnership = false;
  }
  EXPECT_EQ(recoveredResult, process_lifetime::Result::Completed);
  EXPECT_EQ(exitCode, 47u);
  EXPECT_EQ(waitpidAttempts.load(), 0);
  EXPECT_EQ(afterRunCalls, 1);
  EXPECT_FALSE(launchOwnership);
  EXPECT_GE(std::chrono::steady_clock::now() - started, 350ms);

  auto terminalUnknown =
      std::make_shared<process_lifetime::RootProcessCompletion>();
  terminalUnknown->markStarted();
  terminalUnknown->finish(0, QProcess::CrashExit);
  exitCode = 0;
  EXPECT_EQ(process_lifetime::waitForPid(
                launched.pid, &exitCode, recovered, {}, false, {}, false,
                launched.startTime, terminalUnknown),
            process_lifetime::Result::Completed);
  EXPECT_EQ(exitCode,
            process_lifetime::RootProcessCompletion::InvalidExitCode);
  EXPECT_EQ(waitpidAttempts.load(), 0);
}

TEST_F(ProcessLifetimeTest,
       ManagedNativeTokenTransientProcFaultCompletesOnceAfterActualExit)
{
  const QString token = QStringLiteral("fm-native-transient-") +
                        QString::number(::getpid());
  auto launched = startManagedShell(QStringLiteral("sleep 0.35; exit 53"),
                                    token);
  ASSERT_GT(launched.pid, 0);
  ASSERT_NE(launched.startTime,
            process_lifetime::UnknownProcessStartTime);

  std::atomic<int> waitpidAttempts = 0;
  std::atomic<int> injectedFaults = 0;
  int afterRunCalls = 0;
  bool launchOwnership = true;
  process_lifetime::Callbacks callbacks;
  callbacks.procUnavailableTimeout = 800ms;
  callbacks.waitpidAttempted = [&](pid_t) { ++waitpidAttempts; };
  callbacks.procFaultInjector =
      [&](process_lifetime::ProcOperation operation,
          pid_t target) -> std::optional<process_lifetime::ProcFault> {
    if (operation == process_lifetime::ProcOperation::ReadStat &&
        target == launched.pid && injectedFaults.fetch_add(1) < 3) {
      return process_lifetime::ProcFault{EIO, std::nullopt};
    }
    return std::nullopt;
  };

  const auto started = std::chrono::steady_clock::now();
  const auto result = process_lifetime::waitForPidAndNotify(
      launched.pid, callbacks, {}, false,
      [&](process_lifetime::Result completionResult, std::uint32_t exitCode) {
        EXPECT_EQ(completionResult, process_lifetime::Result::Completed);
        EXPECT_EQ(exitCode, 53u);
        EXPECT_TRUE(launchOwnership);
        ++afterRunCalls;
        launchOwnership = false;
        EXPECT_FALSE(process_lifetime::processIdentityIsAlive(
            launched.pid, launched.startTime));
      },
      token, false, launched.startTime, launched.completion);

  EXPECT_EQ(result, process_lifetime::Result::Completed);
  EXPECT_EQ(afterRunCalls, 1);
  EXPECT_FALSE(launchOwnership);
  EXPECT_EQ(waitpidAttempts.load(), 0);
  EXPECT_GE(injectedFaults.load(), 3);
  EXPECT_GE(std::chrono::steady_clock::now() - started, 250ms);
}

TEST_F(ProcessLifetimeTest, ManagedQProcessBackgroundObserverUsesSharedStatus)
{
  auto launched =
      startManagedShell(QStringLiteral("sleep 0.05; exit 47"));
  ASSERT_GT(launched.pid, 0);

  std::atomic<int> waitpidAttempts = 0;
  auto result = std::async(std::launch::async, [&]() {
    process_lifetime::Callbacks callbacks;
    callbacks.waitpidAttempted = [&](pid_t) { ++waitpidAttempts; };
    std::uint32_t exitCode = 0;
    const auto lifetime = process_lifetime::waitForPid(
        launched.pid, &exitCode, callbacks, {}, false, {}, false,
        launched.startTime, launched.completion);
    return std::pair{lifetime, exitCode};
  });

  ASSERT_TRUE(pumpEventsUntil(
      [&]() { return result.wait_for(0ms) == std::future_status::ready; },
      3000ms));
  const auto [lifetime, exitCode] = result.get();
  EXPECT_EQ(lifetime, process_lifetime::Result::Completed);
  EXPECT_EQ(exitCode, 47u);
  EXPECT_EQ(waitpidAttempts.load(), 0);
}

TEST_F(ProcessLifetimeTest,
       ManagedLauncherPreservesStatusAcrossTokenScopedCompanionLifetime)
{
  const QString token = QStringLiteral("fm-qprocess-companion-") +
                        QString::number(::getpid());
  auto launched = startManagedShell(
      QStringLiteral("(exec /bin/sleep 0.55) & /bin/sleep 0.05; exit 47"),
      token);
  ASSERT_GT(launched.pid, 0);

  std::atomic<int> waitpidAttempts = 0;
  process_lifetime::Callbacks callbacks;
  callbacks.waitpidAttempted = [&](pid_t) { ++waitpidAttempts; };
  const auto started = std::chrono::steady_clock::now();
  std::uint32_t exitCode = 0;
  const auto result = process_lifetime::waitForPid(
      launched.pid, &exitCode, callbacks, {QStringLiteral("sleep")}, false,
      token, true, process_lifetime::UnknownProcessStartTime,
      launched.completion);
  const auto elapsed = std::chrono::steady_clock::now() - started;

  EXPECT_EQ(result, process_lifetime::Result::Completed);
  EXPECT_EQ(exitCode, 47u);
  EXPECT_EQ(waitpidAttempts.load(), 0);
  EXPECT_GE(elapsed, 450ms);
}

TEST(AfterRunRefreshQueueTest, CompletesOnlyTheAssignedGeneration)
{
  AfterRunRefreshQueue queue;
  int first = 0;
  int second = 0;

  queue.assign(41, {[&first]() { ++first; }});
  queue.enqueue({QStringLiteral("Default"), [&second]() { ++second; }});

  auto pending = queue.takePending();
  ASSERT_EQ(pending.size(), 1u);
  std::vector<std::function<void()>> secondCompletion;
  secondCompletion.push_back(std::move(pending.front().complete));
  queue.assign(42, std::move(secondCompletion));

  for (auto& complete : queue.takeAssigned(41)) {
    complete();
  }
  EXPECT_EQ(first, 1);
  EXPECT_EQ(second, 0);
  EXPECT_TRUE(queue.takeAssigned(41).empty());

  for (auto& complete : queue.takeAssigned(42)) {
    complete();
  }
  EXPECT_EQ(second, 1);
}

pid_t spawnLauncherWithLongLivedChild(
    useconds_t childLifetimeUsec = 800'000,
    useconds_t launcherLifetimeUsec = 200'000)
{
  const pid_t launcher = ::fork();
  if (launcher != 0) {
    return launcher;
  }

  ::prctl(PR_SET_NAME, "fm-launcher", 0, 0, 0);
  const pid_t child = ::fork();
  if (child < 0) {
    _exit(125);
  }

  if (child == 0) {
    ::prctl(PR_SET_NAME, "fm-life-child", 0, 0, 0);
    ::usleep(childLifetimeUsec);
    _exit(0);
  }

  ::usleep(launcherLifetimeUsec);
  _exit(17);
}

pid_t startDetachedSleep(const QString& token, const QString& seconds)
{
  QProcess process;
  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert(
      QString::fromLatin1(process_lifetime::LaunchTokenEnvironment), token);
  process.setProcessEnvironment(environment);
  process.setProgram(QStringLiteral("/bin/sleep"));
  process.setArguments({seconds});

  qint64 detachedPid = 0;
  if (!process.startDetached(&detachedPid)) {
    return -1;
  }
  return static_cast<pid_t>(detachedPid);
}

pid_t spawnLauncherWithDetachedChild(const QString& token,
                                     const QString& seconds)
{
  const pid_t launcher = ::fork();
  if (launcher != 0) {
    return launcher;
  }

  ::prctl(PR_SET_NAME, "fm-launcher", 0, 0, 0);
  const pid_t detached = startDetachedSleep(token, seconds);
  _exit(detached > 0 ? 23 : 125);
}

pid_t spawnAlreadyExitedLauncher(int exitCode)
{
  const pid_t launcher = ::fork();
  if (launcher != 0) {
    ::usleep(100'000);
    return launcher;
  }

  ::prctl(PR_SET_NAME, "fm-launcher", 0, 0, 0);
  _exit(exitCode);
}

pid_t spawnLauncherWithLateDetachedChild(const QString& token)
{
  const pid_t launcher = ::fork();
  if (launcher != 0) {
    return launcher;
  }

  ::prctl(PR_SET_NAME, "fm-launcher", 0, 0, 0);
  const pid_t delayedSpawner = ::fork();
  if (delayedSpawner < 0) {
    _exit(125);
  }
  if (delayedSpawner == 0) {
    ::prctl(PR_SET_NAME, "fm-delay", 0, 0, 0);
    ::usleep(1'800'000);
    const pid_t detached = startDetachedSleep(token, QStringLiteral("0.5"));
    _exit(detached > 0 ? 0 : 125);
  }

  _exit(37);
}

pid_t spawnLauncherWithTransitioningChildren()
{
  const pid_t launcher = ::fork();
  if (launcher != 0) {
    return launcher;
  }

  ::prctl(PR_SET_NAME, "fm-root", 0, 0, 0);
  const pid_t first = ::fork();
  if (first < 0) {
    _exit(125);
  }
  if (first == 0) {
    ::prctl(PR_SET_NAME, "fm-phase-one", 0, 0, 0);
    ::usleep(450'000);
    _exit(0);
  }
  ::waitpid(first, nullptr, 0);
  ::usleep(350'000);

  const pid_t second = ::fork();
  if (second < 0) {
    _exit(125);
  }
  if (second == 0) {
    ::prctl(PR_SET_NAME, "fm-phase-two", 0, 0, 0);
    ::usleep(450'000);
    _exit(0);
  }
  ::waitpid(second, nullptr, 0);
  _exit(29);
}

pid_t spawnLongRootWithShortCompanion()
{
  const pid_t root = ::fork();
  if (root != 0) {
    return root;
  }

  ::prctl(PR_SET_NAME, "fm-union-root", 0, 0, 0);
  const pid_t child = ::fork();
  if (child < 0) {
    _exit(125);
  }
  if (child == 0) {
    ::prctl(PR_SET_NAME, "fm-short-life", 0, 0, 0);
    ::usleep(300'000);
    _exit(0);
  }

  ::waitpid(child, nullptr, 0);
  ::usleep(2'200'000);
  _exit(19);
}

std::pair<pid_t, pid_t> spawnLongRootWithMatchingChild()
{
  int pipeFds[2] = {-1, -1};
  if (::pipe(pipeFds) != 0) {
    return {-1, -1};
  }

  const pid_t root = ::fork();
  if (root == 0) {
    ::close(pipeFds[0]);
    ::prctl(PR_SET_NAME, "fm-exact-root", 0, 0, 0);
    const pid_t child = ::fork();
    if (child == 0) {
      ::close(pipeFds[1]);
      ::prctl(PR_SET_NAME, "fm-exact-child", 0, 0, 0);
      ::usleep(5'000'000);
      _exit(0);
    }
    const auto written = ::write(pipeFds[1], &child, sizeof(child));
    ::close(pipeFds[1]);
    if (child < 0 || written != sizeof(child)) {
      _exit(125);
    }
    ::usleep(5'000'000);
    _exit(0);
  }

  ::close(pipeFds[1]);
  pid_t child = -1;
  const auto received = ::read(pipeFds[0], &child, sizeof(child));
  ::close(pipeFds[0]);
  return {root, received == sizeof(child) ? child : -1};
}

std::array<pid_t, 3> spawnRootWithIntermediateAndMatchingGrandchild()
{
  int pipeFds[2] = {-1, -1};
  if (::pipe(pipeFds) != 0) {
    return {-1, -1, -1};
  }

  const pid_t root = ::fork();
  if (root == 0) {
    ::close(pipeFds[0]);
    ::prctl(PR_SET_NAME, "fm-chain-root", 0, 0, 0);
    const pid_t intermediary = ::fork();
    if (intermediary == 0) {
      ::prctl(PR_SET_NAME, "fm-chain-middle", 0, 0, 0);
      const pid_t child = ::fork();
      if (child == 0) {
        ::close(pipeFds[1]);
        ::prctl(PR_SET_NAME, "fm-chain-game", 0, 0, 0);
        ::usleep(5'000'000);
        _exit(0);
      }
      const std::array<pid_t, 2> processes{::getpid(), child};
      const auto written =
          ::write(pipeFds[1], processes.data(), sizeof(processes));
      ::close(pipeFds[1]);
      if (child < 0 || written != sizeof(processes)) {
        _exit(125);
      }
      ::usleep(5'000'000);
      _exit(0);
    }
    ::close(pipeFds[1]);
    if (intermediary < 0) {
      _exit(125);
    }
    ::usleep(5'000'000);
    _exit(0);
  }

  ::close(pipeFds[1]);
  std::array<pid_t, 2> descendants{-1, -1};
  const auto received =
      ::read(pipeFds[0], descendants.data(), sizeof(descendants));
  ::close(pipeFds[0]);
  return received == sizeof(descendants)
             ? std::array<pid_t, 3>{root, descendants[0], descendants[1]}
             : std::array<pid_t, 3>{root, -1, -1};
}

std::pair<pid_t, pid_t>
spawnRootWithDetachedTokenWorker(const QString& token)
{
  int resultPipe[2] = {-1, -1};
  int workerPipe[2] = {-1, -1};
  if (::pipe(resultPipe) != 0 || ::pipe(workerPipe) != 0) {
    if (resultPipe[0] >= 0) {
      ::close(resultPipe[0]);
      ::close(resultPipe[1]);
    }
    if (workerPipe[0] >= 0) {
      ::close(workerPipe[0]);
      ::close(workerPipe[1]);
    }
    return {-1, -1};
  }

  const pid_t root = ::fork();
  if (root == 0) {
    ::close(resultPipe[0]);
    ::setenv(process_lifetime::LaunchTokenEnvironment,
             token.toUtf8().constData(), 1);
    ::prctl(PR_SET_NAME, "fm-token-root", 0, 0, 0);
    const pid_t intermediary = ::fork();
    if (intermediary == 0) {
      ::close(workerPipe[0]);
      ::close(resultPipe[1]);
      const pid_t worker = ::fork();
      if (worker == 0) {
        ::close(workerPipe[1]);
        ::setsid();
        ::prctl(PR_SET_NAME, "fm-token-worker", 0, 0, 0);
        // exec makes the inherited token part of the process's initial
        // environment, which is what /proc/<pid>/environ exposes.
        ::execl("/bin/sleep", "sleep", "5", nullptr);
        _exit(126);
      }
      const auto written = ::write(workerPipe[1], &worker, sizeof(worker));
      ::close(workerPipe[1]);
      _exit(worker > 0 && written == sizeof(worker) ? 0 : 125);
    }
    ::close(workerPipe[1]);
    if (intermediary < 0) {
      _exit(125);
    }
    pid_t worker = -1;
    const auto received = ::read(workerPipe[0], &worker, sizeof(worker));
    ::close(workerPipe[0]);
    int intermediaryStatus = 0;
    ::waitpid(intermediary, &intermediaryStatus, 0);
    ::usleep(50'000);
    const auto written = ::write(resultPipe[1], &worker, sizeof(worker));
    ::close(resultPipe[1]);
    if (received != sizeof(worker) || written != sizeof(worker) ||
        !WIFEXITED(intermediaryStatus) || WEXITSTATUS(intermediaryStatus) != 0) {
      _exit(125);
    }
    // The worker is now outside this root's descendant tree, while retaining
    // the exact inherited launch token.
    ::usleep(5'000'000);
    _exit(0);
  }

  ::close(resultPipe[1]);
  ::close(workerPipe[0]);
  ::close(workerPipe[1]);
  pid_t worker = -1;
  const auto received = ::read(resultPipe[0], &worker, sizeof(worker));
  ::close(resultPipe[0]);
  return {root, received == sizeof(worker) ? worker : -1};
}

pid_t spawnRootWithSimultaneousCompanions(const QString& token)
{
  const pid_t root = ::fork();
  if (root != 0) {
    return root;
  }

  ::setenv(process_lifetime::LaunchTokenEnvironment,
           token.toUtf8().constData(), 1);
  ::prctl(PR_SET_NAME, "fm-multi-root", 0, 0, 0);
  const pid_t shortChild = ::fork();
  if (shortChild == 0) {
    ::prctl(PR_SET_NAME, "fm-multi-short", 0, 0, 0);
    ::usleep(300'000);
    _exit(0);
  }
  if (shortChild < 0) {
    _exit(125);
  }

  const pid_t longChild = ::fork();
  if (longChild == 0) {
    ::prctl(PR_SET_NAME, "fm-multi-long", 0, 0, 0);
    ::usleep(1'200'000);
    _exit(0);
  }
  if (longChild < 0) {
    _exit(125);
  }

  ::usleep(200'000);
  _exit(31);
}

pid_t spawnDetachedNonDumpableRoot(const QString& token,
                                   useconds_t lifetimeUsec = 500'000)
{
  int pipeFds[2] = {-1, -1};
  if (::pipe(pipeFds) != 0) {
    return -1;
  }

  const pid_t intermediary = ::fork();
  if (intermediary == 0) {
    ::close(pipeFds[0]);
    const pid_t detached = ::fork();
    if (detached == 0) {
      ::setsid();
      ::setenv(process_lifetime::LaunchTokenEnvironment,
               token.toUtf8().constData(), 1);
      ::prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
      ::prctl(PR_SET_NAME, "fm-nondump", 0, 0, 0);
      ::usleep(lifetimeUsec);
      _exit(61);
    }
    const auto written = ::write(pipeFds[1], &detached, sizeof(detached));
    _exit(written == sizeof(detached) ? 0 : 125);
  }

  ::close(pipeFds[1]);
  pid_t detached = -1;
  const auto received = ::read(pipeFds[0], &detached, sizeof(detached));
  ::close(pipeFds[0]);
  ::waitpid(intermediary, nullptr, 0);
  return received == sizeof(detached) ? detached : -1;
}

struct UnownedRoot
{
  pid_t pid = -1;
  pid_t ownerPid = -1;
  int savedSubreaper = -1;
  int restoredSubreaper = -1;
  bool restorationSucceeded = false;
};

class ChildSubreaperGuard
{
public:
  ChildSubreaperGuard()
  {
    if (::prctl(PR_GET_CHILD_SUBREAPER, &m_Saved) == 0 &&
        ::prctl(PR_SET_CHILD_SUBREAPER, 0) == 0) {
      m_Active = true;
    }
  }

  ~ChildSubreaperGuard() { restore(); }

  bool ready() const { return m_Active; }
  int saved() const { return m_Saved; }

  bool restore()
  {
    if (!m_Active) {
      return m_Restored;
    }
    int restored = -1;
    m_Restored = ::prctl(PR_SET_CHILD_SUBREAPER, m_Saved) == 0 &&
                 ::prctl(PR_GET_CHILD_SUBREAPER, &restored) == 0 &&
                 restored == m_Saved;
    if (m_Restored) {
      m_Active = false;
    }
    return m_Restored;
  }

private:
  int m_Saved = -1;
  bool m_Active = false;
  bool m_Restored = false;
};

UnownedRoot spawnUnownedDumpableRoot(useconds_t lifetimeUsec = 2'000'000)
{
  UnownedRoot result;
  ChildSubreaperGuard subreaper;
  result.savedSubreaper = subreaper.saved();
  const auto finish = [&](pid_t pid) {
    result.pid = pid;
    result.restorationSucceeded = subreaper.restore();
    if (::prctl(PR_GET_CHILD_SUBREAPER, &result.restoredSubreaper) != 0) {
      result.restoredSubreaper = -1;
    }
    return result;
  };
  if (!subreaper.ready()) {
    return finish(-1);
  }

  int pipeFds[2] = {-1, -1};
  if (::pipe(pipeFds) != 0) {
    return finish(-1);
  }

  const pid_t intermediary = ::fork();
  if (intermediary < 0) {
    ::close(pipeFds[0]);
    ::close(pipeFds[1]);
    return finish(-1);
  }
  if (intermediary == 0) {
    ::close(pipeFds[0]);
    const pid_t detached = ::fork();
    if (detached == 0) {
      ::close(pipeFds[1]);
      ::setsid();
      ::prctl(PR_SET_NAME, "fm-unowned", 0, 0, 0);
      ::usleep(lifetimeUsec);
      _exit(0);
    }
    const auto written = ::write(pipeFds[1], &detached, sizeof(detached));
    ::close(pipeFds[1]);
    if (detached <= 0 || written != sizeof(detached)) {
      _exit(125);
    }
    // Stay alive as the root's actual parent. This makes the root unowned by
    // the test even when the test is PID 1 inside a container, where an
    // ordinary double-fork orphan would be reparented straight back to it.
    ::waitpid(detached, nullptr, 0);
    _exit(0);
  }

  ::close(pipeFds[1]);
  pid_t detached = -1;
  const auto received = ::read(pipeFds[0], &detached, sizeof(detached));
  ::close(pipeFds[0]);
  result.ownerPid = intermediary;
  if (received != sizeof(detached)) {
    ::kill(intermediary, SIGKILL);
    ::waitpid(intermediary, nullptr, 0);
    result.ownerPid = -1;
    return finish(-1);
  }
  return finish(detached);
}

pid_t spawnLauncherWithUnreadableDetachedChild(const QString& token)
{
  const pid_t launcher = ::fork();
  if (launcher != 0) {
    return launcher;
  }

  ::prctl(PR_SET_NAME, "fm-launcher", 0, 0, 0);
  const pid_t detached = ::fork();
  if (detached < 0) {
    _exit(125);
  }
  if (detached == 0) {
    ::setsid();
    ::setenv(process_lifetime::LaunchTokenEnvironment,
             token.toUtf8().constData(), 1);
    ::prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
    ::prctl(PR_SET_NAME, "fm-nondump", 0, 0, 0);
    ::usleep(5'300'000);
    _exit(0);
  }

  // Keep the child in the captured root's descendant tree long enough for
  // the tracker to authenticate it by lineage before it is reparented.
  ::usleep(500'000);
  _exit(62);
}

bool kernelProcessIsRunning(pid_t pid)
{
  QFile statFile(QStringLiteral("/proc/%1/stat").arg(pid));
  if (!statFile.open(QIODevice::ReadOnly)) {
    return false;
  }

  const QByteArray stat = statFile.readAll();
  const qsizetype commandEnd = stat.lastIndexOf(')');
  return commandEnd >= 0 && commandEnd + 2 < stat.size() &&
         stat.at(commandEnd + 2) != 'Z' && stat.at(commandEnd + 2) != 'X';
}

QByteArray procStatWithDifferentStartTime(pid_t pid)
{
  QFile statFile(QStringLiteral("/proc/%1/stat").arg(pid));
  if (!statFile.open(QIODevice::ReadOnly)) {
    return {};
  }
  QByteArray stat = statFile.readAll();
  const qsizetype commandEnd = stat.lastIndexOf(')');
  if (commandEnd < 0 || commandEnd + 2 >= stat.size()) {
    return {};
  }
  QList<QByteArray> fields = stat.mid(commandEnd + 2).split(' ');
  if (fields.size() <= 19) {
    return {};
  }
  bool ok = false;
  const auto startTime = fields[19].toULongLong(&ok);
  if (!ok || startTime == 0) {
    return {};
  }
  fields[19] = QByteArray::number(startTime - 1);
  return stat.left(commandEnd + 2) + fields.join(' ');
}

TEST_F(ProcessLifetimeTest, NotifiesOnceOnlyAfterDifferentlyNamedChildExits)
{
  const pid_t launcher = spawnLauncherWithLongLivedChild();
  ASSERT_GT(launcher, 0);

  pid_t adoptedPid = 0;
  int afterRunCalls = 0;
  std::uint32_t reportedExitCode = 0;
  const auto started = std::chrono::steady_clock::now();
  std::chrono::milliseconds notificationElapsed{0};

  process_lifetime::Callbacks callbacks;
  callbacks.updateProcess = [&](pid_t pid, const QString& name) {
    if (name.compare(QStringLiteral("fm-life-child"), Qt::CaseInsensitive) == 0) {
      adoptedPid = pid;
    }
  };

  const auto result = process_lifetime::waitForPidAndNotify(
      launcher, callbacks, {QStringLiteral("fm-life-child")}, false,
      [&](process_lifetime::Result waitResult, std::uint32_t exitCode) {
        EXPECT_EQ(waitResult, process_lifetime::Result::Completed);
        ++afterRunCalls;
        reportedExitCode = exitCode;
        notificationElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);

        ASSERT_GT(adoptedPid, 0);
        EXPECT_FALSE(kernelProcessIsRunning(adoptedPid));
      });

  EXPECT_EQ(result, process_lifetime::Result::Completed);
  EXPECT_EQ(afterRunCalls, 1);
  EXPECT_EQ(reportedExitCode, 17u);
  EXPECT_GE(notificationElapsed, 650ms);
  EXPECT_LT(notificationElapsed, 5s);
}

TEST_F(ProcessLifetimeTest, AddsNormalizedCompanionNamesToExpectedExecutables)
{
  const QStringList expected = process_lifetime::buildExpectedExecutables(
      QFileInfo(QStringLiteral("/usr/bin/openmw-launcher")), QString{},
      {QStringLiteral("OPENMW"), QStringLiteral("/app/bin/openmw")});

  EXPECT_EQ(expected,
            QStringList({QStringLiteral("openmw-launcher"), QStringLiteral("openmw")}));
}

TEST_F(ProcessLifetimeTest, TracksARealDetachedCompanionByLaunchToken)
{
  const QString token = QStringLiteral("detached-companion-token");
  const pid_t launcher =
      spawnLauncherWithDetachedChild(token, QStringLiteral("0.8"));
  ASSERT_GT(launcher, 0);

  // Model a busy UI thread that misses the launcher's transient child tree.
  ::usleep(250'000);

  pid_t adoptedPid = 0;
  process_lifetime::Callbacks callbacks;
  callbacks.updateProcess = [&](pid_t pid, const QString& name) {
    if (name.compare(QStringLiteral("sleep"), Qt::CaseInsensitive) == 0) {
      adoptedPid = pid;
    }
  };

  const auto started = std::chrono::steady_clock::now();
  std::uint32_t exitCode = 0;
  const auto result = process_lifetime::waitForPid(
      launcher, &exitCode, callbacks, {QStringLiteral("sleep")}, false,
      token, true);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);

  EXPECT_EQ(result, process_lifetime::Result::Completed);
  EXPECT_EQ(exitCode, 23u);
  EXPECT_GT(adoptedPid, 0);
  EXPECT_FALSE(kernelProcessIsRunning(adoptedPid));
  EXPECT_GE(elapsed, 400ms);
  EXPECT_LT(elapsed, 4s);
}

TEST_F(ProcessLifetimeTest, DoesNotAdoptSameNameFromAnotherLaunch)
{
  const pid_t unrelated = startDetachedSleep(
      QStringLiteral("unrelated-token"), QStringLiteral("2.0"));
  ASSERT_GT(unrelated, 0);

  const QString token = QStringLiteral("intended-token");
  const pid_t launcher =
      spawnLauncherWithDetachedChild(token, QStringLiteral("0.5"));
  ASSERT_GT(launcher, 0);

  const auto started = std::chrono::steady_clock::now();
  std::uint32_t exitCode = 0;
  const auto result = process_lifetime::waitForPid(
      launcher, &exitCode, {}, {QStringLiteral("sleep")}, false, token, true);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);

  EXPECT_EQ(result, process_lifetime::Result::Completed);
  EXPECT_EQ(exitCode, 23u);
  EXPECT_LT(elapsed, 1500ms);
  EXPECT_TRUE(kernelProcessIsRunning(unrelated));
  ::kill(unrelated, SIGTERM);
}

TEST_F(ProcessLifetimeTest, PollsCancelDuringDetachedCompanionGrace)
{
  const pid_t launcher = spawnAlreadyExitedLauncher(41);
  ASSERT_GT(launcher, 0);

  int controlPolls = 0;
  process_lifetime::Callbacks callbacks;
  callbacks.control = [&]() {
    ++controlPolls;
    return process_lifetime::Control::Cancel;
  };

  const auto started = std::chrono::steady_clock::now();
  std::uint32_t exitCode = 0;
  const auto result = process_lifetime::waitForPid(
      launcher, &exitCode, callbacks, {QStringLiteral("missing-companion")},
      false, QStringLiteral("missing-companion-token"), true);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);

  EXPECT_EQ(result, process_lifetime::Result::Cancelled);
  EXPECT_EQ(exitCode, 41u);
  EXPECT_EQ(controlPolls, 1);
  EXPECT_LT(elapsed, 500ms);
}

TEST_F(ProcessLifetimeTest, PollsForceUnlockDuringDetachedCompanionGrace)
{
  const pid_t launcher = spawnAlreadyExitedLauncher(42);
  ASSERT_GT(launcher, 0);

  int controlPolls = 0;
  process_lifetime::Callbacks callbacks;
  callbacks.control = [&]() {
    ++controlPolls;
    return process_lifetime::Control::ForceUnlock;
  };

  const auto started = std::chrono::steady_clock::now();
  std::uint32_t exitCode = 0;
  const auto result = process_lifetime::waitForPid(
      launcher, &exitCode, callbacks, {QStringLiteral("missing-companion")},
      false, QStringLiteral("missing-companion-token"), true);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);

  EXPECT_EQ(result, process_lifetime::Result::ForceUnlocked);
  EXPECT_EQ(exitCode, 42u);
  EXPECT_EQ(controlPolls, 1);
  EXPECT_LT(elapsed, 500ms);
}

TEST_F(ProcessLifetimeTest, PreservesUnknownStatusWhenRootWasAlreadyReaped)
{
  const pid_t launcher = spawnAlreadyExitedLauncher(73);
  ASSERT_GT(launcher, 0);
  int status = 0;
  ASSERT_EQ(::waitpid(launcher, &status, 0), launcher);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(WEXITSTATUS(status), 73);

  std::uint32_t exitCode = 0;
  const auto result = process_lifetime::waitForPid(
      launcher, &exitCode, {}, {}, false);

  EXPECT_EQ(result, process_lifetime::Result::Completed);
  EXPECT_EQ(exitCode, static_cast<std::uint32_t>(-1));
}

TEST_F(ProcessLifetimeTest, UnknownIdentityNeverReportsLiveRootCompleted)
{
  const pid_t root = startDetachedSleep(QStringLiteral("unknown-identity"),
                                        QStringLiteral("1.0"));
  ASSERT_GT(root, 0);

  int polls = 0;
  process_lifetime::Callbacks callbacks;
  callbacks.control = [&]() {
    return ++polls >= 4 ? process_lifetime::Control::ForceUnlock
                        : process_lifetime::Control::Continue;
  };

  std::uint32_t exitCode = 0;
  const auto result = process_lifetime::waitForPid(
      root, &exitCode, callbacks, {}, false, {}, false,
      process_lifetime::UnknownProcessStartTime);

  EXPECT_EQ(result, process_lifetime::Result::ForceUnlocked);
  EXPECT_EQ(exitCode, static_cast<std::uint32_t>(-1));
  EXPECT_TRUE(kernelProcessIsRunning(root));
  ::kill(root, SIGTERM);
}

TEST_F(ProcessLifetimeTest, WaitsForLateInitialDetachedCompanion)
{
  const QString token = QStringLiteral("late-companion-token");
  const pid_t launcher = spawnLauncherWithLateDetachedChild(token);
  ASSERT_GT(launcher, 0);

  const auto started = std::chrono::steady_clock::now();
  std::uint32_t exitCode = 0;
  const auto result = process_lifetime::waitForPid(
      launcher, &exitCode, {}, {QStringLiteral("sleep")}, false, token, true);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);

  EXPECT_EQ(result, process_lifetime::Result::Completed);
  EXPECT_EQ(exitCode, 37u);
  EXPECT_GE(elapsed, 2000ms);
  EXPECT_LT(elapsed, 6s);
}

TEST_F(ProcessLifetimeTest, WaitsAcrossTrackedProcessTransitionGap)
{
  const pid_t launcher = spawnLauncherWithTransitioningChildren();
  ASSERT_GT(launcher, 0);

  const auto started = std::chrono::steady_clock::now();
  std::uint32_t exitCode = 0;
  const auto result = process_lifetime::waitForPid(
      launcher, &exitCode, {},
      {QStringLiteral("fm-phase-one"), QStringLiteral("fm-phase-two")}, false,
      {}, true);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);

  EXPECT_EQ(result, process_lifetime::Result::Completed);
  EXPECT_EQ(exitCode, 29u);
  EXPECT_GE(elapsed, 1100ms);
  EXPECT_LT(elapsed, 4s);
}

TEST_F(ProcessLifetimeTest, CompanionExtendsButDoesNotReplaceRootLifetime)
{
  const pid_t root = spawnLongRootWithShortCompanion();
  ASSERT_GT(root, 0);

  const auto started = std::chrono::steady_clock::now();
  std::uint32_t exitCode = 0;
  const auto result = process_lifetime::waitForPid(
      root, &exitCode, {}, {QStringLiteral("fm-short-life")}, false, {},
      true);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);

  EXPECT_EQ(result, process_lifetime::Result::Completed);
  EXPECT_EQ(exitCode, 19u);
  EXPECT_FALSE(kernelProcessIsRunning(root));
  EXPECT_GE(elapsed, 2400ms);
  EXPECT_LT(elapsed, 5s);
}

TEST_F(ProcessLifetimeTest, WaitsForAllSimultaneousCompanions)
{
  const QString token = QStringLiteral("simultaneous-companions-token");
  const pid_t root = spawnRootWithSimultaneousCompanions(token);
  ASSERT_GT(root, 0);

  const auto started = std::chrono::steady_clock::now();
  std::uint32_t exitCode = 0;
  const auto result = process_lifetime::waitForPid(
      root, &exitCode, {},
      {QStringLiteral("fm-multi-short"), QStringLiteral("fm-multi-long")},
      false, token, true);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);

  EXPECT_EQ(result, process_lifetime::Result::Completed);
  EXPECT_EQ(exitCode, 31u);
  EXPECT_GE(elapsed, 1000ms);
  EXPECT_LT(elapsed, 3s);
}

TEST_F(ProcessLifetimeTest, UnreadableEnvironmentDoesNotMeanDetachedRootExited)
{
  const QString token = QStringLiteral("non-dumpable-root-token");
  const pid_t root = spawnDetachedNonDumpableRoot(token);
  ASSERT_GT(root, 0);

  const auto rootStart = process_lifetime::processStartTime(root);
  ASSERT_TRUE(rootStart.has_value());
  const auto started = std::chrono::steady_clock::now();
  std::uint32_t exitCode = 0;
  const auto result = process_lifetime::waitForPid(
      root, &exitCode, {}, {}, false, token, false, *rootStart);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);

  EXPECT_EQ(result, process_lifetime::Result::Completed);
  EXPECT_TRUE(exitCode == 61u ||
              exitCode == std::numeric_limits<std::uint32_t>::max());
  EXPECT_NE(exitCode, 0u);
  EXPECT_GE(elapsed, 300ms);
  EXPECT_LT(elapsed, 3s);
}

TEST_F(ProcessLifetimeTest, KeepsWaitingForUnreadableObservedDescendant)
{
  const QString token = QStringLiteral("unreadable-detached-token");
  const pid_t launcher = spawnLauncherWithUnreadableDetachedChild(token);
  ASSERT_GT(launcher, 0);

  const auto started = std::chrono::steady_clock::now();
  std::uint32_t exitCode = 0;
  const auto result = process_lifetime::waitForPid(
      launcher, &exitCode, {}, {QStringLiteral("fm-nondump")}, false,
      token, true);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);

  EXPECT_EQ(result, process_lifetime::Result::Completed);
  EXPECT_EQ(exitCode, 62u);
  EXPECT_GE(elapsed, 5100ms);
  EXPECT_LT(elapsed, 8s);
}

TEST_F(ProcessLifetimeTest, IgnoresOlderUnreadableUnrelatedProcess)
{
  const pid_t unrelated = spawnDetachedNonDumpableRoot(
      QStringLiteral("unrelated-unreadable-token"), 7'000'000);
  ASSERT_GT(unrelated, 0);

  const pid_t launcher = spawnAlreadyExitedLauncher(47);
  ASSERT_GT(launcher, 0);

  const auto started = std::chrono::steady_clock::now();
  std::uint32_t exitCode = 0;
  const auto result = process_lifetime::waitForPid(
      launcher, &exitCode, {}, {QStringLiteral("fm-nondump")}, false,
      QStringLiteral("intended-unreadable-token"), true);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);

  EXPECT_EQ(result, process_lifetime::Result::Completed);
  EXPECT_EQ(exitCode, 47u);
  EXPECT_LT(elapsed, 6s);
  EXPECT_TRUE(kernelProcessIsRunning(unrelated));
  ::kill(unrelated, SIGTERM);
}

TEST_F(ProcessLifetimeTest, NonKillingUnlockCanTransferToEventualCompletion)
{
  const QString token = QStringLiteral("unlock-transfer-token");
  const pid_t launcher =
      spawnLauncherWithDetachedChild(token, QStringLiteral("0.8"));
  ASSERT_GT(launcher, 0);

  process_lifetime::Callbacks unlockCallbacks;
  unlockCallbacks.control = []() {
    return process_lifetime::Control::ForceUnlock;
  };
  std::uint32_t firstExitCode = 0;
  EXPECT_EQ(process_lifetime::waitForPid(
                launcher, &firstExitCode, unlockCallbacks,
                {QStringLiteral("sleep")}, false, token, true),
            process_lifetime::Result::ForceUnlocked);

  int notifications = 0;
  const auto started = std::chrono::steady_clock::now();
  const auto result = process_lifetime::waitForPidAndNotify(
      launcher, {}, {QStringLiteral("sleep")}, false,
      [&](process_lifetime::Result completionResult, std::uint32_t) {
        EXPECT_EQ(completionResult, process_lifetime::Result::Completed);
        ++notifications;
      },
      token, true);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);

  EXPECT_EQ(result, process_lifetime::Result::Completed);
  EXPECT_EQ(notifications, 1);
  EXPECT_GE(elapsed, 500ms);
  EXPECT_LT(elapsed, 3s);
}

TEST_F(ProcessLifetimeTest, StableAdoptedIdentityAvoidsRepeatedProcScans)
{
  const pid_t launcher =
      spawnLauncherWithLongLivedChild(1'500'000, 400'000);
  ASSERT_GT(launcher, 0);

  int directoryScans = 0;
  int scansAtAdoption = -1;
  process_lifetime::Callbacks callbacks;
  callbacks.procFaultInjector =
      [&](process_lifetime::ProcOperation operation,
          pid_t) -> std::optional<process_lifetime::ProcFault> {
    if (operation == process_lifetime::ProcOperation::OpenDirectory) {
      ++directoryScans;
    }
    return std::nullopt;
  };
  callbacks.updateProcess = [&](pid_t, const QString& name) {
    if (scansAtAdoption < 0 &&
        name.compare(QStringLiteral("fm-life-child"),
                     Qt::CaseInsensitive) == 0) {
      scansAtAdoption = directoryScans;
    }
  };

  const auto started = std::chrono::steady_clock::now();
  std::uint32_t exitCode = 0;
  const auto result = process_lifetime::waitForPid(
      launcher, &exitCode, callbacks, {QStringLiteral("fm-life-child")},
      false);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);

  EXPECT_EQ(result, process_lifetime::Result::Completed);
  EXPECT_EQ(exitCode, 17u);
  ASSERT_GE(scansAtAdoption, 0);
  EXPECT_GE(elapsed, 1s);
  // One transition scan is expected after the exact adopted identity exits;
  // a 50 ms global-poll loop would add roughly twenty scans here.
  EXPECT_LE(directoryScans - scansAtAdoption, 4);
}

TEST_F(ProcessLifetimeTest, UnmatchedRootDiscoveryUsesSlowerCadence)
{
  const pid_t root = ::fork();
  ASSERT_GE(root, 0);
  if (root == 0) {
    ::prctl(PR_SET_NAME, "fm-cadence-root", 0, 0, 0);
    ::usleep(5'000'000);
    _exit(0);
  }

  std::vector<std::chrono::steady_clock::time_point> scanTimes;
  int controlPolls = 0;
  process_lifetime::Callbacks callbacks;
  callbacks.procFaultInjector =
      [&](process_lifetime::ProcOperation operation,
          pid_t) -> std::optional<process_lifetime::ProcFault> {
    if (operation == process_lifetime::ProcOperation::OpenDirectory) {
      scanTimes.push_back(std::chrono::steady_clock::now());
    }
    return std::nullopt;
  };
  callbacks.control = [&]() {
    return ++controlPolls >= 8 ? process_lifetime::Control::ForceUnlock
                               : process_lifetime::Control::Continue;
  };

  std::uint32_t exitCode = 0;
  const auto result = process_lifetime::waitForPid(
      root, &exitCode, callbacks,
      {QStringLiteral("missing-acquisition-target")}, false);

  EXPECT_EQ(result, process_lifetime::Result::ForceUnlocked);
  ASSERT_GE(scanTimes.size(), 3u);
  for (std::size_t i = 1; i < scanTimes.size(); ++i) {
    EXPECT_GE(scanTimes[i] - scanTimes[i - 1], 80ms);
  }

  ::kill(root, SIGKILL);
  ::waitpid(root, nullptr, 0);
}

TEST_F(ProcessLifetimeTest, UnknownStartIsCapturedForOwnedForceUnlock)
{
  const pid_t root = ::fork();
  ASSERT_GE(root, 0);
  if (root == 0) {
    ::prctl(PR_SET_NAME, "fm-owned-unknown", 0, 0, 0);
    ::usleep(5'000'000);
    _exit(0);
  }

  const auto exactStart = process_lifetime::processStartTime(root);
  ASSERT_TRUE(exactStart.has_value());
  process_lifetime::Callbacks callbacks;
  callbacks.control = []() {
    return process_lifetime::Control::ForceUnlock;
  };

  std::uint32_t exitCode = 0;
  const auto result = process_lifetime::waitForPid(
      root, &exitCode, callbacks, {}, true, {}, false,
      process_lifetime::UnknownProcessStartTime);

  EXPECT_EQ(result, process_lifetime::Result::ForceUnlocked);
  EXPECT_EQ(process_lifetime::processIdentityState(root, *exactStart),
            process_lifetime::IdentityState::Exited);
  ::waitpid(root, nullptr, 0);
}

TEST_F(ProcessLifetimeTest, ForceUnlockNeverSignalsWrongGeneration)
{
  const auto [root, matchingChild] = spawnLongRootWithMatchingChild();
  ASSERT_GT(root, 0);
  ASSERT_GT(matchingChild, 0);

  const auto exactStart = process_lifetime::processStartTime(root);
  ASSERT_TRUE(exactStart.has_value());
  process_lifetime::Callbacks callbacks;
  callbacks.control = []() {
    return process_lifetime::Control::ForceUnlock;
  };

  std::uint32_t exitCode = 0;
  const auto result = process_lifetime::waitForPid(
      root, &exitCode, callbacks, {QStringLiteral("fm-exact-child")}, true,
      {}, false, *exactStart + 1);

  EXPECT_EQ(result, process_lifetime::Result::Error);
  EXPECT_TRUE(kernelProcessIsRunning(root));
  EXPECT_TRUE(kernelProcessIsRunning(matchingChild));
  ::kill(root, SIGKILL);
  ::kill(matchingChild, SIGKILL);
  ::waitpid(root, nullptr, 0);
}

TEST_F(ProcessLifetimeTest,
       DescendantDiscoveryRejectsGenerationChangedAfterTreeSnapshot)
{
  const auto [root, matchingChild] = spawnLongRootWithMatchingChild();
  ASSERT_GT(root, 0);
  ASSERT_GT(matchingChild, 0);
  const QByteArray staleStat = procStatWithDifferentStartTime(matchingChild);
  ASSERT_FALSE(staleStat.isEmpty());

  int childStatReads = 0;
  int adoptedChild = 0;
  process_lifetime::Callbacks callbacks;
  callbacks.procFaultInjector =
      [&](process_lifetime::ProcOperation operation,
          pid_t target) -> std::optional<process_lifetime::ProcFault> {
    if (operation == process_lifetime::ProcOperation::ReadStat &&
        target == matchingChild && ++childStatReads % 2 == 1) {
      // The tree scan observed an older occupant of this PID; the immediately
      // following authentication read sees the real replacement generation.
      return process_lifetime::ProcFault{0, std::nullopt, staleStat};
    }
    return std::nullopt;
  };
  callbacks.updateProcess = [&](pid_t observed, const QString& name) {
    if (name.compare(QStringLiteral("fm-exact-child"),
                     Qt::CaseInsensitive) == 0) {
      adoptedChild = observed;
    }
  };
  callbacks.control = []() {
    return process_lifetime::Control::ForceUnlock;
  };

  std::uint32_t exitCode = 0;
  const auto result = process_lifetime::waitForPid(
      root, &exitCode, callbacks, {QStringLiteral("fm-exact-child")}, false);

  EXPECT_EQ(result, process_lifetime::Result::ForceUnlocked);
  EXPECT_EQ(adoptedChild, 0);
  EXPECT_GE(childStatReads, 2);
  EXPECT_TRUE(kernelProcessIsRunning(root));
  EXPECT_TRUE(kernelProcessIsRunning(matchingChild));
  ::kill(root, SIGKILL);
  ::kill(matchingChild, SIGKILL);
  ::waitpid(root, nullptr, 0);
}

TEST_F(ProcessLifetimeTest,
       DescendantDiscoveryRejectsReusedIntermediateGeneration)
{
  const auto [root, intermediary, matchingChild] =
      spawnRootWithIntermediateAndMatchingGrandchild();
  ASSERT_GT(root, 0);
  ASSERT_GT(intermediary, 0);
  ASSERT_GT(matchingChild, 0);
  const QByteArray staleStat = procStatWithDifferentStartTime(intermediary);
  ASSERT_FALSE(staleStat.isEmpty());

  int intermediaryStatReads = 0;
  int adoptedChild = 0;
  process_lifetime::Callbacks callbacks;
  callbacks.procFaultInjector =
      [&](process_lifetime::ProcOperation operation,
          pid_t target) -> std::optional<process_lifetime::ProcFault> {
    if (operation == process_lifetime::ProcOperation::ReadStat &&
        target == intermediary && ++intermediaryStatReads % 2 == 1) {
      return process_lifetime::ProcFault{0, std::nullopt, staleStat};
    }
    return std::nullopt;
  };
  callbacks.updateProcess = [&](pid_t observed, const QString& name) {
    if (name.compare(QStringLiteral("fm-chain-game"),
                     Qt::CaseInsensitive) == 0) {
      adoptedChild = observed;
    }
  };
  callbacks.control = []() {
    return process_lifetime::Control::ForceUnlock;
  };

  std::uint32_t exitCode = 0;
  const auto result = process_lifetime::waitForPid(
      root, &exitCode, callbacks, {QStringLiteral("fm-chain-game")}, false);

  EXPECT_EQ(result, process_lifetime::Result::ForceUnlocked);
  EXPECT_EQ(adoptedChild, 0);
  EXPECT_GE(intermediaryStatReads, 4);
  EXPECT_TRUE(kernelProcessIsRunning(intermediary));
  EXPECT_TRUE(kernelProcessIsRunning(matchingChild));
  ::kill(root, SIGKILL);
  ::kill(intermediary, SIGKILL);
  ::kill(matchingChild, SIGKILL);
  ::waitpid(root, nullptr, 0);
}

TEST_F(ProcessLifetimeTest,
       ForceUnlockRejectsGenerationChangedDescendantBeforeSignal)
{
  const auto [root, matchingChild] = spawnLongRootWithMatchingChild();
  ASSERT_GT(root, 0);
  ASSERT_GT(matchingChild, 0);
  const QByteArray staleStat = procStatWithDifferentStartTime(matchingChild);
  ASSERT_FALSE(staleStat.isEmpty());
  const auto childStart = process_lifetime::processStartTime(matchingChild);
  ASSERT_TRUE(childStart.has_value());

  int childStatReads = 0;
  process_lifetime::Callbacks callbacks;
  callbacks.procFaultInjector =
      [&](process_lifetime::ProcOperation operation,
          pid_t target) -> std::optional<process_lifetime::ProcFault> {
    if (operation == process_lifetime::ProcOperation::ReadStat &&
        target == matchingChild && ++childStatReads % 2 == 1) {
      return process_lifetime::ProcFault{0, std::nullopt, staleStat};
    }
    return std::nullopt;
  };
  callbacks.control = []() {
    return process_lifetime::Control::ForceUnlock;
  };

  std::uint32_t exitCode = 0;
  const auto result = process_lifetime::waitForPid(
      root, &exitCode, callbacks, {QStringLiteral("fm-exact-child")}, true);

  EXPECT_EQ(result, process_lifetime::Result::Error);
  EXPECT_GE(childStatReads, 4);
  EXPECT_EQ(process_lifetime::processIdentityState(matchingChild, *childStart),
            process_lifetime::IdentityState::Running);
  ::kill(root, SIGKILL);
  ::kill(matchingChild, SIGKILL);
  ::waitpid(root, nullptr, 0);
}

TEST_F(ProcessLifetimeTest,
       ForceUnlockKillsDetachedDifferentlyNamedExactTokenWorker)
{
  const QString token = QStringLiteral("force-unlock-all-token-workers");
  const auto [root, worker] = spawnRootWithDetachedTokenWorker(token);
  ASSERT_GT(root, 0);
  ASSERT_GT(worker, 0);
  const auto rootStart = process_lifetime::processStartTime(root);
  const auto workerStart = process_lifetime::processStartTime(worker);
  ASSERT_TRUE(rootStart.has_value());
  ASSERT_TRUE(workerStart.has_value());
  ASSERT_TRUE(kernelProcessIsRunning(worker));

  process_lifetime::Callbacks callbacks;
  callbacks.control = []() {
    return process_lifetime::Control::ForceUnlock;
  };
  std::uint32_t exitCode = 0;
  const auto result = process_lifetime::waitForPid(
      root, &exitCode, callbacks,
      {QStringLiteral("fm-policy-name-that-is-not-the-worker")}, true, token,
      false, *rootStart);

  EXPECT_EQ(result, process_lifetime::Result::ForceUnlocked);
  EXPECT_EQ(process_lifetime::processIdentityState(worker, *workerStart),
            process_lifetime::IdentityState::Exited);
  ::kill(root, SIGKILL);
  ::kill(worker, SIGKILL);
  ::waitpid(root, nullptr, 0);
}

TEST_F(ProcessLifetimeTest,
       ForceUnlockRefusesUnreadablePossibleTokenWorker)
{
  const QString token = QStringLiteral("force-unlock-unreadable-worker");
  const auto [root, worker] = spawnRootWithDetachedTokenWorker(token);
  ASSERT_GT(root, 0);
  ASSERT_GT(worker, 0);
  const auto rootStart = process_lifetime::processStartTime(root);
  const auto workerStart = process_lifetime::processStartTime(worker);
  ASSERT_TRUE(rootStart.has_value());
  ASSERT_TRUE(workerStart.has_value());

  int deniedEnvironmentReads = 0;
  process_lifetime::Callbacks callbacks;
  callbacks.procFaultInjector =
      [&](process_lifetime::ProcOperation operation,
          pid_t target) -> std::optional<process_lifetime::ProcFault> {
    if (operation == process_lifetime::ProcOperation::ReadEnvironment &&
        target == worker) {
      ++deniedEnvironmentReads;
      return process_lifetime::ProcFault{EACCES, std::nullopt};
    }
    return std::nullopt;
  };
  callbacks.control = []() {
    return process_lifetime::Control::ForceUnlock;
  };
  std::uint32_t exitCode = 0;
  const auto result = process_lifetime::waitForPid(
      root, &exitCode, callbacks,
      {QStringLiteral("fm-policy-name-that-is-not-the-worker")}, true, token,
      false, *rootStart);

  EXPECT_EQ(result, process_lifetime::Result::Error);
  EXPECT_GT(deniedEnvironmentReads, 0);
  EXPECT_EQ(process_lifetime::processIdentityState(worker, *workerStart),
            process_lifetime::IdentityState::Running);
  ::kill(root, SIGKILL);
  ::kill(worker, SIGKILL);
  ::waitpid(root, nullptr, 0);
}

TEST_F(ProcessLifetimeTest, DetachedUnknownStartCannotForceUnlockByPid)
{
  const auto unowned = spawnUnownedDumpableRoot();
  ASSERT_TRUE(unowned.restorationSucceeded);
  ASSERT_GE(unowned.savedSubreaper, 0);
  ASSERT_EQ(unowned.restoredSubreaper, unowned.savedSubreaper);
  const pid_t root = unowned.pid;
  ASSERT_GT(root, 0);
  ASSERT_GT(unowned.ownerPid, 0);
  int ownershipStatus = 0;
  errno = 0;
  ASSERT_EQ(::waitpid(root, &ownershipStatus, WNOHANG), -1);
  ASSERT_EQ(errno, ECHILD);

  process_lifetime::Callbacks callbacks;
  callbacks.control = []() {
    return process_lifetime::Control::ForceUnlock;
  };

  std::uint32_t exitCode = 0;
  const auto result = process_lifetime::waitForPid(
      root, &exitCode, callbacks, {}, true, {}, false,
      process_lifetime::UnknownProcessStartTime);

  EXPECT_EQ(result, process_lifetime::Result::Error);
  EXPECT_TRUE(kernelProcessIsRunning(root));
  ::kill(root, SIGTERM);
  ::waitpid(unowned.ownerPid, nullptr, 0);
}

TEST_F(ProcessLifetimeTest,
       ExactGenerationRawAttachWaitsForUnownedPidUntilActualExit)
{
  const auto unowned = spawnUnownedDumpableRoot(400'000);
  ASSERT_TRUE(unowned.restorationSucceeded);
  ASSERT_EQ(unowned.restoredSubreaper, unowned.savedSubreaper);
  ASSERT_GT(unowned.pid, 0);
  ASSERT_GT(unowned.ownerPid, 0);

  const auto startTime = process_lifetime::processStartTime(unowned.pid);
  ASSERT_TRUE(startTime.has_value());
  ASSERT_EQ(process_lifetime::processIdentityState(unowned.pid, *startTime),
            process_lifetime::IdentityState::Running);

  std::atomic<int> waitpidAttempts = 0;
  const auto started = std::chrono::steady_clock::now();
  auto wait = std::async(std::launch::async, [&]() {
    process_lifetime::Callbacks callbacks;
    callbacks.waitpidAttempted = [&](pid_t attempted) {
      EXPECT_EQ(attempted, unowned.pid);
      ++waitpidAttempts;
    };
    std::uint32_t exitCode = 0;
    const auto result = process_lifetime::waitForPid(
        unowned.pid, &exitCode, callbacks, {}, false, {}, false, *startTime);
    return std::pair{result, exitCode};
  });

  EXPECT_EQ(wait.wait_for(100ms), std::future_status::timeout);
  EXPECT_EQ(process_lifetime::processIdentityState(unowned.pid, *startTime),
            process_lifetime::IdentityState::Running);
  ASSERT_TRUE(pumpEventsUntil(
      [&]() { return wait.wait_for(0ms) == std::future_status::ready; },
      3000ms));
  const auto [result, exitCode] = wait.get();

  EXPECT_EQ(result, process_lifetime::Result::Completed);
  EXPECT_EQ(exitCode, process_lifetime::RootProcessCompletion::InvalidExitCode);
  EXPECT_GT(waitpidAttempts.load(), 0);
  EXPECT_GE(std::chrono::steady_clock::now() - started, 300ms);
  EXPECT_EQ(process_lifetime::processIdentityState(unowned.pid, *startTime),
            process_lifetime::IdentityState::Exited);
  ::waitpid(unowned.ownerPid, nullptr, 0);
}

TEST_F(ProcessLifetimeTest,
       ExactGenerationRawAttachCancelAndUnlockNeverSignalUnownedPid)
{
  struct ControlCase
  {
    process_lifetime::Control control;
    process_lifetime::Result expected;
  };
  const std::array cases{
      ControlCase{process_lifetime::Control::Cancel,
                  process_lifetime::Result::Cancelled},
      ControlCase{process_lifetime::Control::ForceUnlock,
                  process_lifetime::Result::ForceUnlocked},
  };

  for (const auto& testCase : cases) {
    const auto unowned = spawnUnownedDumpableRoot(250'000);
    ASSERT_TRUE(unowned.restorationSucceeded);
    ASSERT_EQ(unowned.restoredSubreaper, unowned.savedSubreaper);
    ASSERT_GT(unowned.pid, 0);
    ASSERT_GT(unowned.ownerPid, 0);
    const auto startTime = process_lifetime::processStartTime(unowned.pid);
    ASSERT_TRUE(startTime.has_value());

    process_lifetime::Callbacks callbacks;
    callbacks.control = [control = testCase.control]() { return control; };
    std::uint32_t exitCode = 0;
    const auto result = process_lifetime::waitForPid(
        unowned.pid, &exitCode, callbacks, {}, false, {}, false, *startTime);

    EXPECT_EQ(result, testCase.expected);
    EXPECT_EQ(process_lifetime::processIdentityState(unowned.pid, *startTime),
              process_lifetime::IdentityState::Running);
    EXPECT_EQ(::kill(unowned.pid, 0), 0);
    ::waitpid(unowned.ownerPid, nullptr, 0);
  }
}

TEST_F(ProcessLifetimeTest, PersistentProcFailuresReturnBoundedError)
{
  struct FailureCase
  {
    process_lifetime::ProcOperation operation;
    process_lifetime::ProcFault fault;
    const char* name;
  };

  const std::vector<FailureCase> cases{
      {process_lifetime::ProcOperation::OpenDirectory, {EMFILE, std::nullopt},
       "opendir-emfile"},
      {process_lifetime::ProcOperation::StatEntry, {EACCES, std::nullopt},
       "entry-stat-permission"},
      {process_lifetime::ProcOperation::ReadStat, {EIO, std::nullopt},
       "stat-read"},
      {process_lifetime::ProcOperation::ReadComm, {EIO, std::nullopt},
       "comm-read"},
      {process_lifetime::ProcOperation::ReadCmdline, {EIO, std::nullopt},
       "cmdline-read"},
      {process_lifetime::ProcOperation::ReadComm, {EIO, std::size_t{2}},
       "comm-partial-read"},
      {process_lifetime::ProcOperation::ReadCmdline, {EIO, std::size_t{2}},
       "cmdline-partial-read"},
  };

  for (const auto& failure : cases) {
    SCOPED_TRACE(failure.name);
    const pid_t launcher = spawnAlreadyExitedLauncher(81);
    ASSERT_GT(launcher, 0);

    int injected = 0;
    int completedNotifications = 0;
    process_lifetime::Callbacks callbacks;
    callbacks.procUnavailableTimeout = 120ms;
    callbacks.procFaultInjector =
        [&](process_lifetime::ProcOperation operation,
            pid_t) -> std::optional<process_lifetime::ProcFault> {
      if (operation == failure.operation) {
        ++injected;
        return failure.fault;
      }
      return std::nullopt;
    };

    const auto started = std::chrono::steady_clock::now();
    const auto result = process_lifetime::waitForPidAndNotify(
        launcher, callbacks, {QStringLiteral("fm-never-exists")}, false,
        [&](process_lifetime::Result completionResult, std::uint32_t) {
          if (completionResult == process_lifetime::Result::Completed) {
            ++completedNotifications;
          }
        },
        QStringLiteral("proc-fault-token"));
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);

    EXPECT_EQ(result, process_lifetime::Result::Error);
    EXPECT_GT(injected, 0);
    EXPECT_EQ(completedNotifications, 0);
    EXPECT_GE(elapsed, 100ms);
    EXPECT_LT(elapsed, 2s);
  }
}

TEST_F(ProcessLifetimeTest, UnreadableCandidateEnvironmentReturnsBoundedError)
{
  const pid_t launcher = spawnAlreadyExitedLauncher(82);
  ASSERT_GT(launcher, 0);
  const pid_t candidate = startDetachedSleep(
      QStringLiteral("environment-fault-token"), QStringLiteral("2.0"));
  ASSERT_GT(candidate, 0);

  int environmentReads = 0;
  process_lifetime::Callbacks callbacks;
  callbacks.procUnavailableTimeout = 120ms;
  callbacks.procFaultInjector =
      [&](process_lifetime::ProcOperation operation,
          pid_t target) -> std::optional<process_lifetime::ProcFault> {
    if (operation == process_lifetime::ProcOperation::ReadEnvironment &&
        target == candidate) {
      ++environmentReads;
      return process_lifetime::ProcFault{EACCES, std::nullopt};
    }
    return std::nullopt;
  };

  std::uint32_t exitCode = 0;
  const auto result = process_lifetime::waitForPid(
      launcher, &exitCode, callbacks, {QStringLiteral("sleep")}, false,
      QStringLiteral("intended-token"), false);

  EXPECT_EQ(result, process_lifetime::Result::Error);
  EXPECT_GT(environmentReads, 0);
  EXPECT_TRUE(kernelProcessIsRunning(candidate));
  ::kill(candidate, SIGTERM);
}

TEST_F(ProcessLifetimeTest, TransientProcFailuresRecoverToCompletion)
{
  const pid_t root = ::fork();
  ASSERT_GE(root, 0);
  if (root == 0) {
    ::prctl(PR_SET_NAME, "fm-transient", 0, 0, 0);
    ::usleep(350'000);
    _exit(83);
  }

  int failedStatReads = 0;
  int failedEnvironmentReads = 0;
  int failedScans = 0;
  process_lifetime::Callbacks callbacks;
  callbacks.procUnavailableTimeout = 500ms;
  callbacks.procFaultInjector =
      [&](process_lifetime::ProcOperation operation,
          pid_t target) -> std::optional<process_lifetime::ProcFault> {
    if (operation == process_lifetime::ProcOperation::ReadStat &&
        target == root && failedStatReads++ == 0) {
      return process_lifetime::ProcFault{EIO, std::nullopt};
    }
    if (operation == process_lifetime::ProcOperation::ReadEnvironment &&
        target == root && failedEnvironmentReads++ == 0) {
      return process_lifetime::ProcFault{EACCES, std::nullopt};
    }
    if (operation == process_lifetime::ProcOperation::OpenDirectory &&
        failedScans++ < 2) {
      return process_lifetime::ProcFault{EMFILE, std::nullopt};
    }
    return std::nullopt;
  };

  std::uint32_t exitCode = 0;
  const auto result = process_lifetime::waitForPid(
      root, &exitCode, callbacks, {QStringLiteral("fm-never-exists")}, false,
      {}, false);

  EXPECT_EQ(result, process_lifetime::Result::Completed);
  EXPECT_EQ(exitCode, 83u);
  EXPECT_GT(failedStatReads, 1);
  EXPECT_GT(failedEnvironmentReads, 1);
  EXPECT_GT(failedScans, 2);
}

TEST_F(ProcessLifetimeTest, ForceUnlockFailsWhenKillTreeCannotBeEnumerated)
{
  const pid_t root = ::fork();
  ASSERT_GE(root, 0);
  if (root == 0) {
    ::prctl(PR_SET_NAME, "fm-kill-fault", 0, 0, 0);
    ::usleep(5'000'000);
    _exit(0);
  }

  process_lifetime::Callbacks callbacks;
  callbacks.control = []() {
    return process_lifetime::Control::ForceUnlock;
  };
  callbacks.procFaultInjector =
      [](process_lifetime::ProcOperation operation,
         pid_t) -> std::optional<process_lifetime::ProcFault> {
    if (operation == process_lifetime::ProcOperation::OpenDirectory) {
      return process_lifetime::ProcFault{EMFILE, std::nullopt};
    }
    return std::nullopt;
  };

  std::uint32_t exitCode = 0;
  const auto result = process_lifetime::waitForPid(
      root, &exitCode, callbacks, {}, true);
  EXPECT_EQ(result, process_lifetime::Result::Error);
  EXPECT_NE(result, process_lifetime::Result::ForceUnlocked);

  ::kill(root, SIGKILL);
  ::waitpid(root, nullptr, 0);
}

TEST_F(ProcessLifetimeTest, ForceUnlockDoesNotEraseUnreadablePrefixUncertainty)
{
  const pid_t root = ::fork();
  ASSERT_GE(root, 0);
  if (root == 0) {
    ::prctl(PR_SET_NAME, "fm-prefix-fault", 0, 0, 0);
    ::usleep(5'000'000);
    _exit(0);
  }

  process_lifetime::Callbacks callbacks;
  callbacks.control = []() {
    return process_lifetime::Control::ForceUnlock;
  };
  callbacks.procFaultInjector =
      [root](process_lifetime::ProcOperation operation,
             pid_t target) -> std::optional<process_lifetime::ProcFault> {
    if (operation == process_lifetime::ProcOperation::ReadEnvironment &&
        target == root) {
      return process_lifetime::ProcFault{EACCES, std::nullopt};
    }
    return std::nullopt;
  };

  std::uint32_t exitCode = 0;
  const auto result = process_lifetime::waitForPid(
      root, &exitCode, callbacks, {QStringLiteral("wine-game.exe")}, true);
  EXPECT_EQ(result, process_lifetime::Result::Error);
  EXPECT_NE(result, process_lifetime::Result::ForceUnlocked);

  ::kill(root, SIGKILL);
  ::waitpid(root, nullptr, 0);
}

TEST_F(ProcessLifetimeTest, ForceUnlockTerminatesDefaultPrefixWineserver)
{
  const pid_t root = ::fork();
  ASSERT_GE(root, 0);
  if (root == 0) {
    ::prctl(PR_SET_NAME, "fm-default-wine", 0, 0, 0);
    ::usleep(5'000'000);
    _exit(0);
  }

  const pid_t wineserver = ::fork();
  ASSERT_GE(wineserver, 0);
  if (wineserver == 0) {
    ::unsetenv("WINEPREFIX");
    ::prctl(PR_SET_NAME, "wineserver", 0, 0, 0);
    ::usleep(5'000'000);
    _exit(0);
  }

  process_lifetime::Callbacks callbacks;
  callbacks.control = []() {
    return process_lifetime::Control::ForceUnlock;
  };

  std::uint32_t exitCode = 0;
  const auto result = process_lifetime::waitForPid(
      root, &exitCode, callbacks, {QStringLiteral("wine-game.exe")}, true);
  EXPECT_EQ(result, process_lifetime::Result::ForceUnlocked);
  EXPECT_EQ(process_lifetime::processIdentityState(
                wineserver,
                process_lifetime::processStartTime(wineserver).value_or(0)),
            process_lifetime::IdentityState::Exited);

  ::kill(root, SIGKILL);
  ::kill(wineserver, SIGKILL);
  ::waitpid(root, nullptr, 0);
  ::waitpid(wineserver, nullptr, 0);
}

TEST_F(ProcessLifetimeTest, OpaqueApplicationHandlesAreUniqueAndSingleConsumer)
{
  const auto ownStart = process_lifetime::processStartTime(::getpid());
  ASSERT_TRUE(ownStart.has_value());
  EXPECT_TRUE(
      process_lifetime::processIdentityIsAlive(::getpid(), *ownStart));
  EXPECT_FALSE(
      process_lifetime::processIdentityIsAlive(::getpid(), *ownStart + 1));
  EXPECT_FALSE(process_lifetime::processIdentityIsAlive(
      ::getpid(), process_lifetime::UnknownProcessStartTime));

  ApplicationRunnerRegistry<QString, 2> registry;
  const auto first = registry.insert(700, QStringLiteral("first"));
  const auto second = registry.insert(700, QStringLiteral("second"));
  const auto third = registry.insert(701, QStringLiteral("third"));

  EXPECT_NE(first, second);
  EXPECT_TRUE(registry.isOpaqueHandle(first));
  EXPECT_FALSE(registry.isOpaqueHandle(700));
  EXPECT_EQ(registry.size(), 3u);

  auto retainedFirst = registry.take(first);
  ASSERT_TRUE(retainedFirst.has_value());
  EXPECT_EQ(retainedFirst->pid, 700);
  EXPECT_EQ(retainedFirst->payload, QStringLiteral("first"));

  auto retainedSecond = registry.take(second);
  ASSERT_TRUE(retainedSecond.has_value());
  EXPECT_EQ(retainedSecond->pid, 700);
  EXPECT_EQ(retainedSecond->payload, QStringLiteral("second"));
  EXPECT_FALSE(registry.take(second).has_value());

  auto retainedThird = registry.take(third);
  ASSERT_TRUE(retainedThird.has_value());
  EXPECT_EQ(retainedThird->pid, 701);
  EXPECT_EQ(retainedThird->payload, QStringLiteral("third"));
  EXPECT_EQ(registry.size(), 0u);

  ApplicationRunnerRegistry<QString, 64> delayedRegistry;
  std::vector<std::uintptr_t> delayedHandles;
  for (int i = 0; i < 96; ++i) {
    delayedHandles.push_back(
        delayedRegistry.insert(1000 + i, QString::number(i)));
  }
  EXPECT_EQ(delayedRegistry.size(), 96u);

  // Insertion alone does not infer process completion or evict entries.
  // Consume the handles deliberately out of order.
  for (int i = 95; i >= 4; --i) {
    auto retained = delayedRegistry.take(delayedHandles[i]);
    ASSERT_TRUE(retained.has_value()) << "missing handle at index " << i;
    EXPECT_EQ(retained->pid, 1000 + i);
    EXPECT_EQ(retained->payload, QString::number(i));
  }

  std::atomic<int> consumed = 0;
  std::thread firstWait([&]() {
    if (delayedRegistry.take(delayedHandles[2])) {
      ++consumed;
    }
  });
  std::thread secondWait([&]() {
    if (delayedRegistry.take(delayedHandles[3])) {
      ++consumed;
    }
  });
  firstWait.join();
  secondWait.join();
  EXPECT_EQ(consumed.load(), 2);

  // A handle is single-consumer even when two waiters race for it.
  consumed = 0;
  std::thread duplicateFirst([&]() {
    if (delayedRegistry.take(delayedHandles[1])) {
      ++consumed;
    }
  });
  std::thread duplicateSecond([&]() {
    if (delayedRegistry.take(delayedHandles[1])) {
      ++consumed;
    }
  });
  duplicateFirst.join();
  duplicateSecond.join();
  EXPECT_EQ(consumed.load(), 1);

  EXPECT_TRUE(delayedRegistry.take(delayedHandles[0]).has_value());
  EXPECT_EQ(delayedRegistry.size(), 0u);
}

TEST_F(ProcessLifetimeTest, FailedInsertionDoesNotInvalidateExistingHandle)
{
  ApplicationRunnerRegistry<ThrowingPayload, 1> registry;
  const auto retained = registry.insert(4100, ThrowingPayload(41));

  ThrowingPayload::throwOnMove = true;
  EXPECT_THROW(registry.insert(4200, ThrowingPayload(42)), std::runtime_error);
  ThrowingPayload::throwOnMove = false;

  EXPECT_EQ(registry.size(), 1u);
  auto entry = registry.take(retained);
  ASSERT_TRUE(entry.has_value());
  EXPECT_EQ(entry->pid, 4100);
  EXPECT_EQ(entry->payload.value, 41);
}

TEST_F(ProcessLifetimeTest,
       CompletedApplicationHandleRetentionIsBoundedWithoutEvictingActiveWork)
{
  using Completion = std::shared_ptr<ApplicationCompletion>;
  using Registry = ApplicationRunnerRegistry<Completion, 4>;
  Registry registry;
  const auto canRetire = [](const Registry::Entry& entry) {
    return entry.payload && entry.payload->canRetire();
  };
  const auto neverExpired = [](const Registry::Entry&) { return false; };

  std::vector<std::uintptr_t> activeHandles;
  for (int i = 0; i < 3; ++i) {
    auto active = std::make_shared<ApplicationCompletion>(
        5000 + i, true, QStringLiteral("Default"));
    activeHandles.push_back(registry.insert(5000 + i, std::move(active)));
  }

  auto cleanupPending = std::make_shared<ApplicationCompletion>(
      5100, true, QStringLiteral("Default"));
  cleanupPending->finishLifetime(ApplicationCompletion::Result::Completed, 0);
  const auto cleanupPendingHandle =
      registry.insert(5100, std::move(cleanupPending));

  std::vector<std::uintptr_t> completedHandles;
  for (int i = 0; i < 12; ++i) {
    auto completed = std::make_shared<ApplicationCompletion>(
        5200 + i, false, QStringLiteral("Default"));
    completed->finishLifetime(ApplicationCompletion::Result::Completed, 0);
    const auto retained = completed;
    completedHandles.push_back(
        registry.insert(5200 + i, std::move(completed)));
    retained->onCleanupFinished(
        [&]() { registry.prune(canRetire, neverExpired); });
    retained->finishCleanup();
  }

  // Three running entries, one cleanup-pending terminal entry, and the four
  // newest cleanup-finished entries remain. Active work does not consume the
  // completed-cache capacity and is never selected for eviction.
  EXPECT_EQ(registry.size(), 8u);
  for (const auto handle : activeHandles) {
    EXPECT_TRUE(registry.take(handle).has_value());
  }
  EXPECT_TRUE(registry.take(cleanupPendingHandle).has_value());
  for (int i = 0; i < 8; ++i) {
    EXPECT_FALSE(registry.take(completedHandles[i]).has_value());
  }
  for (int i = 8; i < 12; ++i) {
    EXPECT_TRUE(registry.take(completedHandles[i]).has_value());
  }
  EXPECT_EQ(registry.size(), 0u);
}

TEST_F(ProcessLifetimeTest, ApplicationHandleReleaseIsSingleConsumer)
{
  ApplicationRunnerRegistry<QString> registry;
  const auto handle = registry.insert(5300, QStringLiteral("running"));

  EXPECT_TRUE(registry.release(handle));
  EXPECT_FALSE(registry.release(handle));
  EXPECT_FALSE(registry.take(handle).has_value());

  for (int iteration = 0; iteration < 128; ++iteration) {
    const auto raced =
        registry.insert(5400 + iteration, QString::number(iteration));
    std::atomic<bool> start = false;
    std::atomic<int> consumers = 0;
    std::thread releaser([&]() {
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      if (registry.release(raced)) {
        ++consumers;
      }
    });
    std::thread waiter([&]() {
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      if (registry.take(raced)) {
        ++consumers;
      }
    });
    start.store(true, std::memory_order_release);
    releaser.join();
    waiter.join();
    EXPECT_EQ(consumers.load(), 1) << "iteration " << iteration;
  }
}

TEST_F(ProcessLifetimeTest, ApplicationCompletionCachesTerminalStateAndControl)
{
  ApplicationCompletion completion(4321, true, QStringLiteral("alternate"));
  completion.updateProcess(4322, QStringLiteral("openmw"));
  completion.requestControl(ApplicationCompletion::Control::Cancel);
  completion.requestControl(ApplicationCompletion::Control::ForceUnlock);
  completion.requestControl(ApplicationCompletion::Control::Cancel);
  completion.finishLifetime(ApplicationCompletion::Result::Completed, 47);
  completion.finishCleanup();

  const auto snapshot = completion.snapshot();
  EXPECT_EQ(completion.rootPid(), 4321);
  EXPECT_TRUE(completion.ownsVfs());
  EXPECT_EQ(completion.profileName(), QStringLiteral("alternate"));
  EXPECT_EQ(completion.requestedControl(),
            ApplicationCompletion::Control::ForceUnlock);
  EXPECT_EQ(snapshot.displayPid, 4322);
  EXPECT_EQ(snapshot.displayName, QStringLiteral("openmw"));
  EXPECT_EQ(snapshot.result, ApplicationCompletion::Result::Completed);
  EXPECT_EQ(snapshot.exitCode, 47u);
  EXPECT_TRUE(snapshot.cleanupFinished);
}

TEST_F(ProcessLifetimeTest,
       ApplicationCompletionRetriesRemainNonterminalAndCleanupNotifiesOnce)
{
  ApplicationCompletion completion(5400, true, QStringLiteral("Default"));
  std::atomic<int> cleanupCallbacks = 0;
  completion.onCleanupFinished([&]() { ++cleanupCallbacks; });

  completion.noteObservationFailure();
  completion.noteObservationFailure();
  auto snapshot = completion.snapshot();
  EXPECT_EQ(snapshot.result, ApplicationCompletion::Result::Running);
  EXPECT_EQ(snapshot.observationFailures, 2u);
  EXPECT_FALSE(snapshot.cleanupFinished);
  EXPECT_FALSE(completion.canRetire());

  completion.finishLifetime(ApplicationCompletion::Result::Completed, 9);
  completion.finishCleanup();
  completion.finishCleanup();
  completion.onCleanupFinished([&]() { ++cleanupCallbacks; });
  snapshot = completion.snapshot();
  EXPECT_EQ(snapshot.result, ApplicationCompletion::Result::Completed);
  EXPECT_EQ(snapshot.exitCode, 9u);
  EXPECT_TRUE(snapshot.cleanupFinished);
  EXPECT_TRUE(completion.canRetire());
  EXPECT_TRUE(completion.retentionExpired(0ms));
  EXPECT_EQ(cleanupCallbacks.load(), 2);
}

TEST_F(ProcessLifetimeTest,
       ApplicationCompletionClaimsRefreshExactlyOnceAcrossRace) {
  for (int iteration = 0; iteration < 256; ++iteration) {
    ApplicationCompletion completion(5000 + iteration, false,
                                     QStringLiteral("Default"));
    completion.requestRefresh();
    completion.finishLifetime(ApplicationCompletion::Result::Completed, 0);

    std::atomic<bool> start = false;
    std::atomic<int> claims = 0;
    std::thread integrated([&]() {
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      if (completion.claimRefreshForCleanup()) {
        ++claims;
      }
    });
    std::thread late([&]() {
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      completion.finishCleanup();
      if (completion.claimRefreshAfterCleanup()) {
        ++claims;
      }
    });

    start.store(true, std::memory_order_release);
    integrated.join();
    late.join();
    EXPECT_EQ(claims.load(), 1) << "iteration " << iteration;
  }
}

TEST_F(ProcessLifetimeTest,
       ApplicationCompletionDistinguishesRefreshFailureFromSuccess) {
  ApplicationCompletion failed(5700, false, QStringLiteral("Default"));
  failed.requestRefresh();
  failed.failRefresh();
  failed.finishRefresh();
  auto snapshot = failed.snapshot();
  EXPECT_TRUE(snapshot.refreshFinished);
  EXPECT_TRUE(snapshot.refreshFailed);

  ApplicationCompletion succeeded(5701, false, QStringLiteral("Default"));
  succeeded.requestRefresh();
  succeeded.finishRefresh();
  succeeded.failRefresh();
  snapshot = succeeded.snapshot();
  EXPECT_TRUE(snapshot.refreshFinished);
  EXPECT_FALSE(snapshot.refreshFailed);
}

TEST_F(ProcessLifetimeTest, ApplicationCompletionCleanupPolicyIsResourceSafe) {
  using Result = ApplicationCompletion::Result;
  EXPECT_TRUE(
      ApplicationCompletion::requiresLaunchCleanup(Result::Completed, false));
  EXPECT_TRUE(ApplicationCompletion::requiresLaunchCleanup(Result::Completed,
                                                            true));
  EXPECT_FALSE(ApplicationCompletion::requiresLaunchCleanup(Result::Cancelled,
                                                             false));
  EXPECT_TRUE(ApplicationCompletion::requiresLaunchCleanup(Result::Cancelled,
                                                            true));
  EXPECT_FALSE(ApplicationCompletion::requiresLaunchCleanup(
      Result::ForceUnlocked, false));
  EXPECT_TRUE(ApplicationCompletion::requiresLaunchCleanup(Result::ForceUnlocked,
                                                            true));
  EXPECT_FALSE(ApplicationCompletion::requiresLaunchCleanup(Result::Running,
                                                             true));
  EXPECT_FALSE(ApplicationCompletion::requiresLaunchCleanup(Result::Error,
                                                             true));

  ApplicationCompletion failed(6000, false, QStringLiteral("Default"));
  failed.requestRefresh();
  failed.finishLifetime(Result::Error, static_cast<std::uint32_t>(-1));
  failed.finishCleanup();
  EXPECT_FALSE(failed.claimRefreshAfterCleanup());
}

TEST_F(ProcessLifetimeTest, PostRunContextTracksConcurrentLaunchOwnership)
{
  ProcessLaunchContextTracker context;
  EXPECT_TRUE(context.reserve(QStringLiteral("native-a"),
                              QStringLiteral("profile-a"), false));
  EXPECT_TRUE(context.reserve(QStringLiteral("native-b"),
                              QStringLiteral("profile-b"), false));
  EXPECT_EQ(context.activeCount(), 2);

  // Non-VFS launches may overlap and finish in either order, but each cleanup
  // can be claimed exactly once. A claim remains active until cleanup ends.
  EXPECT_TRUE(context.claimCompletion(QStringLiteral("native-b"),
                                      QStringLiteral("profile-b"), false));
  EXPECT_FALSE(context.claimCompletion(QStringLiteral("native-b"),
                                       QStringLiteral("profile-b"), false));
  EXPECT_TRUE(context.contains(QStringLiteral("native-b")));
  context.finishCompletion(QStringLiteral("native-b"));
  EXPECT_FALSE(context.contains(QStringLiteral("native-b")));

  EXPECT_FALSE(context.claimCompletion(QStringLiteral("native-a"),
                                       QStringLiteral("wrong-profile"), false));
  EXPECT_FALSE(context.claimCompletion(QStringLiteral("native-a"),
                                       QStringLiteral("profile-a"), true));
  EXPECT_TRUE(context.claimCompletion(QStringLiteral("native-a"),
                                      QStringLiteral("profile-a"), false));
  context.finishCompletion(QStringLiteral("native-a"));
  EXPECT_EQ(context.activeCount(), 0);

  EXPECT_TRUE(context.reserve(QStringLiteral("vfs-a"),
                              QStringLiteral("profile-a"), true));
  EXPECT_FALSE(context.reserve(QStringLiteral("vfs-b"),
                               QStringLiteral("profile-b"), true));
  EXPECT_TRUE(context.claimCompletion(QStringLiteral("vfs-a"),
                                      QStringLiteral("profile-a"), true));
  EXPECT_FALSE(context.reserve(QStringLiteral("vfs-b"),
                               QStringLiteral("profile-b"), true));
  context.finishCompletion(QStringLiteral("vfs-a"));
  EXPECT_TRUE(context.reserve(QStringLiteral("vfs-b"),
                              QStringLiteral("profile-b"), true));
  context.abandon(QStringLiteral("vfs-b"));
  EXPECT_EQ(context.activeCount(), 0);
}

TEST_F(ProcessLifetimeTest, VfsReservationCanBeHandedToSynchronousNestedLaunch)
{
  ProcessLaunchContextTracker context;
  const auto outer = QStringLiteral("outer");
  const auto nested = QStringLiteral("nested");
  const auto profile = QStringLiteral("Default");

  ASSERT_TRUE(context.reserve(outer, profile, true));
  context.abandon(outer);
  ASSERT_TRUE(context.reserve(nested, profile, true));
  ASSERT_TRUE(context.claimCompletion(nested, profile, true));
  context.finishCompletion(nested);

  // A synchronously waited helper has relinquished ownership, so the outer
  // launch can reclaim its exact token before doing any VFS preparation.
  EXPECT_TRUE(context.reserve(outer, profile, true));
  EXPECT_TRUE(context.claimCompletion(outer, profile, true));
  context.finishCompletion(outer);

  ASSERT_TRUE(context.reserve(outer, profile, true));
  context.abandon(outer);
  ASSERT_TRUE(context.reserve(nested, profile, true));

  // An asynchronous nested launch still owns the VFS; the outer launch must
  // abort instead of creating two simultaneous owners.
  EXPECT_FALSE(context.reserve(outer, profile, true));
  EXPECT_FALSE(context.contains(outer));
  EXPECT_TRUE(context.contains(nested));
  context.abandon(nested);
}

} // namespace
