#include "afterrunrefreshqueue.h"
#include "downloadreplylifetime.h"
#include "processlaunchcontext.h"
#include "nexuscredentialstate.h"
#include "nexusvalidationlifecycle.h"
#include "restarttransaction.h"
#include "settingswritebarrier.h"
#include "shared/exitstate.h"
#include "updaterrestartpolicy.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace
{

TEST(SettingsWriteBarrierTest, SuppressionBlocksExplicitDiskSync)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath(QStringLiteral("instance.ini"));

  SettingsWriteBarrier barrier;
  barrier.suppress();
  {
    QSettings settings(path, QSettings::IniFormat);
    EXPECT_FALSE(barrier.runIfAllowed([&] {
      settings.setValue(QStringLiteral("PluginPersistance/probe/value"), 42);
      settings.sync();
    }));
  }

  QSettings verification(path, QSettings::IniFormat);
  EXPECT_FALSE(
      verification.contains(QStringLiteral("PluginPersistance/probe/value")));
}

TEST(SettingsWriteBarrierTest, RetainedBackendIsNotDestroyedByOwnerTeardown)
{
  struct FlushOnDestruction
  {
    bool* flushed;
    ~FlushOnDestruction() { *flushed = true; }
  };

  bool flushed = false;
  auto* backend = new FlushOnDestruction{&flushed};
  auto* retained = fail_stop::retainBackendForProcessLifetime(backend);

  EXPECT_EQ(backend, nullptr);
  EXPECT_FALSE(flushed);

  // A production fail-stop intentionally leaves this to _Exit. Reclaim it in
  // the test only so the test process itself does not leak.
  delete retained;
  EXPECT_TRUE(flushed);
}

TEST(SettingsWriteBarrierTest, SuppressionClosesNewAdmission)
{
  SettingsWriteBarrier barrier;
  std::mutex mutex;
  std::condition_variable condition;
  bool mutationEntered = false;
  bool releaseMutation = false;

  std::thread mutation([&] {
    EXPECT_TRUE(barrier.runIfAllowed([&] {
      std::unique_lock lock(mutex);
      mutationEntered = true;
      condition.notify_all();
      condition.wait(lock, [&] { return releaseMutation; });
    }));
  });

  {
    std::unique_lock lock(mutex);
    condition.wait(lock, [&] { return mutationEntered; });
  }

  barrier.suppress();
  EXPECT_TRUE(barrier.suppressed());
  EXPECT_FALSE(barrier.runIfAllowed([] {}));

  {
    const std::lock_guard lock(mutex);
    releaseMutation = true;
  }
  condition.notify_all();
  mutation.join();

  EXPECT_FALSE(barrier.runIfAllowed([] {}));
}

TEST(SettingsWriteBarrierTest, SerializedSinkDoesNotOverlapMutations)
{
  SettingsWriteBarrier barrier(SettingsWriteBarrier::Concurrency::Serialized);
  std::mutex mutex;
  std::condition_variable condition;
  bool firstEntered  = false;
  bool releaseFirst  = false;
  bool secondStarted = false;
  bool secondEntered = false;

  std::thread first([&] {
    EXPECT_TRUE(barrier.runIfAllowed([&] {
      std::unique_lock lock(mutex);
      firstEntered = true;
      condition.notify_all();
      condition.wait(lock, [&] { return releaseFirst; });
    }));
  });
  {
    std::unique_lock lock(mutex);
    condition.wait(lock, [&] { return firstEntered; });
  }

  std::thread second([&] {
    {
      const std::lock_guard lock(mutex);
      secondStarted = true;
      condition.notify_all();
    }
    EXPECT_TRUE(barrier.runIfAllowed([&] {
      const std::lock_guard lock(mutex);
      secondEntered = true;
      condition.notify_all();
    }));
  });

  {
    std::unique_lock lock(mutex);
    condition.wait(lock, [&] { return secondStarted; });
    EXPECT_FALSE(secondEntered);
  }

  {
    const std::lock_guard lock(mutex);
    releaseFirst = true;
  }
  condition.notify_all();

  first.join();
  second.join();
  EXPECT_TRUE(secondEntered);
}

TEST(SettingsWriteBarrierTest, ActiveMutationRejectsNestedTerminalFlow)
{
  SettingsWriteBarrier barrier;
  bool terminalFlowEntered = false;

  EXPECT_FALSE(SettingsWriteBarrier::currentThreadHasMutation());
  ASSERT_TRUE(barrier.runIfAllowed([&] {
    EXPECT_TRUE(SettingsWriteBarrier::currentThreadHasMutation());
    if (!SettingsWriteBarrier::currentThreadHasMutation()) {
      terminalFlowEntered = true;
    }

    // Ordinary recursive mutations remain part of their already admitted
    // transaction even after top-level admission closes.
    barrier.suppress();
    ASSERT_TRUE(barrier.runIfAllowed([&] {
      EXPECT_TRUE(SettingsWriteBarrier::currentThreadHasMutation());
    }));
  }));

  EXPECT_FALSE(terminalFlowEntered);
  EXPECT_FALSE(SettingsWriteBarrier::currentThreadHasMutation());
  EXPECT_FALSE(barrier.runIfAllowed([] {}));
}

TEST(DownloadReplyRetirementTest, CompletedReplyIsRetiredBeforeDeferredDeletion)
{
  QPointer<QObject> trackedReply = new QObject;
  QObject* deferredDelete = download_reply::retire(trackedReply);

  ASSERT_NE(deferredDelete, nullptr);
  EXPECT_TRUE(trackedReply.isNull());

  // Simulate the event loop delivering deleteLater. The owner no longer
  // exposes a pointer that a later callback could dereference.
  delete deferredDelete;
  EXPECT_TRUE(trackedReply.isNull());
}

TEST(AfterRunRefreshQueueTest, FailStopRetiresAssignedAndPendingWaiters)
{
  AfterRunRefreshQueue queue;
  int assigned = 0;
  int pending = 0;

  queue.assign(41, {[&assigned]() { ++assigned; }});
  queue.enqueue({QStringLiteral("Default"), [&pending]() { ++pending; }});

  auto completions = queue.takeAllCompletionsForFailStop();
  ASSERT_EQ(completions.size(), 2u);
  for (auto& complete : completions) {
    complete();
  }

  EXPECT_EQ(assigned, 1);
  EXPECT_EQ(pending, 1);
  EXPECT_FALSE(queue.hasPending());
  EXPECT_TRUE(queue.takeAssigned(41).empty());
  EXPECT_TRUE(queue.takeAllCompletionsForFailStop().empty());
}

TEST(NexusValidationLifecycleTest, TerminalCallbackMayDestroyItsOwner)
{
  struct Owner
  {
    std::function<void()> callback;
  };

  auto owner = std::make_unique<Owner>();
  owner->callback = [&] { owner.reset(); };

  nexus_validation::invokeTerminalCallback(owner->callback);
  EXPECT_FALSE(owner);
}

TEST(NexusValidationLifecycleTest, OutcomeSurvivesReentrantAttemptDestruction)
{
  struct Attempt
  {
    int value;
    QString text;

    int result() const { return value; }
    const QString& message() const { return text; }
  };

  auto attempt = std::make_unique<Attempt>(
      Attempt{7, QStringLiteral("hard validation failure")});
  const auto outcome = nexus_validation::snapshotOutcome(*attempt);
  attempt.reset();

  EXPECT_EQ(outcome.first, 7);
  EXPECT_EQ(outcome.second, QStringLiteral("hard validation failure"));
}

TEST(ProcessLaunchContextTrackerTest, NativeLaunchBlocksShutdownThroughCleanup)
{
  ProcessLaunchContextTracker context;
  EXPECT_TRUE(context.activeLaunches().empty());

  ASSERT_TRUE(context.reserve(QStringLiteral("native"),
                              QStringLiteral("Default"), false));
  auto active = context.activeLaunches();
  EXPECT_EQ(active.total, 1);
  EXPECT_EQ(active.vfs, 0);
  EXPECT_EQ(active.native, 1);
  EXPECT_FALSE(active.empty());

  // Claiming completion starts afterRun; it does not make destruction safe.
  ASSERT_TRUE(context.claimCompletion(QStringLiteral("native"),
                                      QStringLiteral("Default"), false));
  EXPECT_FALSE(context.activeLaunches().empty());

  context.finishCompletion(QStringLiteral("native"));
  EXPECT_TRUE(context.activeLaunches().empty());
}

TEST(ProcessLaunchContextTrackerTest, ConfigurationLeaseBlocksOnlyNewLaunches)
{
  ProcessLaunchContextTracker context;

  {
    auto configuration = context.tryAcquireConfigurationLease();
    ASSERT_TRUE(configuration);
    EXPECT_FALSE(context.tryAcquireConfigurationLease());
    EXPECT_FALSE(context.reserve(QStringLiteral("during-settings"),
                                 QStringLiteral("Default"), false));

    auto moved = std::move(configuration);
    EXPECT_FALSE(configuration);
    EXPECT_TRUE(moved);
    EXPECT_FALSE(context.reserve(QStringLiteral("after-move"),
                                 QStringLiteral("Default"), true));
  }

  ASSERT_TRUE(context.reserve(QStringLiteral("running"),
                              QStringLiteral("Default"), false));
  auto configuration = context.tryAcquireConfigurationLease();
  ASSERT_TRUE(configuration);
  EXPECT_FALSE(context.reserve(QStringLiteral("new-during-settings"),
                               QStringLiteral("Default"), false));
  EXPECT_EQ(context.activeCount(), 1);
  context.abandon(QStringLiteral("running"));
  configuration = {};
  EXPECT_TRUE(context.tryAcquireConfigurationLease());
}

TEST(ProcessLaunchContextTrackerTest, FailStopRejectsConfigurationLease)
{
  ProcessLaunchContextTracker context;
  context.suppressNewReservations();
  EXPECT_FALSE(context.tryAcquireConfigurationLease());
}

TEST(ProcessLaunchContextTrackerTest, FailStopRejectsNewLaunchesButAllowsCleanup)
{
  ProcessLaunchContextTracker context;
  ASSERT_TRUE(context.reserve(QStringLiteral("existing"),
                              QStringLiteral("Default"), false));

  context.suppressNewReservations();
  EXPECT_FALSE(context.reserve(QStringLiteral("queued-plugin-launch"),
                               QStringLiteral("Default"), false));
  EXPECT_EQ(context.activeLaunches().total, 1);

  ASSERT_TRUE(context.claimCompletion(QStringLiteral("existing"),
                                      QStringLiteral("Default"), false));
  context.finishCompletion(QStringLiteral("existing"));
  EXPECT_TRUE(context.activeLaunches().empty());
}

TEST(ProcessLaunchContextTrackerTest, VfsLaunchBlocksShutdownThroughCleanup)
{
  ProcessLaunchContextTracker context;
  ASSERT_TRUE(context.reserve(QStringLiteral("vfs"),
                              QStringLiteral("Default"), true));

  auto active = context.activeLaunches();
  EXPECT_EQ(active.total, 1);
  EXPECT_EQ(active.vfs, 1);
  EXPECT_EQ(active.native, 0);
  EXPECT_FALSE(active.empty());

  ASSERT_TRUE(context.claimCompletion(QStringLiteral("vfs"),
                                      QStringLiteral("Default"), true));
  EXPECT_FALSE(context.activeLaunches().empty());

  ASSERT_TRUE(context.releaseCompletedVfsReservation(QStringLiteral("vfs")));
  EXPECT_TRUE(context.reserve(QStringLiteral("successor"),
                              QStringLiteral("Default"), true));
  // Both the afterRun invocation and its successor are tracked for shutdown.
  EXPECT_EQ(context.activeLaunches().total, 2);

  context.finishCompletion(QStringLiteral("vfs"));
  EXPECT_FALSE(context.activeLaunches().empty());
  context.abandon(QStringLiteral("successor"));
  EXPECT_TRUE(context.activeLaunches().empty());
}

TEST(ProcessLaunchContextTrackerTest, VfsHandoffNeverHidesOuterLaunch)
{
  ProcessLaunchContextTracker context;
  ASSERT_TRUE(context.reserve(QStringLiteral("outer"),
                              QStringLiteral("Default"), true));
  ASSERT_TRUE(context.handoffVfsReservation(QStringLiteral("outer")));
  EXPECT_FALSE(context.activeLaunches().empty());

  ASSERT_TRUE(context.reserve(QStringLiteral("nested"),
                              QStringLiteral("Default"), true));
  EXPECT_FALSE(context.reclaimVfsReservation(QStringLiteral("outer")));
  context.abandon(QStringLiteral("nested"));

  EXPECT_TRUE(context.reclaimVfsReservation(QStringLiteral("outer")));
  EXPECT_FALSE(context.activeLaunches().empty());
  context.abandon(QStringLiteral("outer"));
  EXPECT_TRUE(context.activeLaunches().empty());
}

TEST(ProcessLaunchContextTrackerTest, PreviewRejectsOccupiedVfsWithoutForeignClaim)
{
  ProcessLaunchContextTracker context;
  ASSERT_TRUE(context.reserve(QStringLiteral("application"),
                              QStringLiteral("Default"), true));

  ScopedProcessLaunchReservation preview(
      context, QStringLiteral("preview"), QStringLiteral("Default"), true);
  EXPECT_FALSE(preview);

  // A rejected preview has no context it can claim and therefore no authority
  // to unmount the application's VFS session.
  EXPECT_FALSE(context.claimCompletion(QStringLiteral("preview"),
                                       QStringLiteral("Default"), true));
  EXPECT_TRUE(context.contains(QStringLiteral("application")));
  context.abandon(QStringLiteral("application"));
}

TEST(ProcessLaunchContextTrackerTest, PreviewCloseRequiresExactTokenAndProfile)
{
  ProcessLaunchContextTracker context;
  ScopedProcessLaunchReservation preview(
      context, QStringLiteral("preview"), QStringLiteral("Default"), true);
  ASSERT_TRUE(preview);
  preview.retain();

  EXPECT_FALSE(context.claimCompletion(QStringLiteral("foreign"),
                                       QStringLiteral("Default"), true));
  EXPECT_FALSE(context.claimCompletion(QStringLiteral("preview"),
                                       QStringLiteral("Other"), true));
  EXPECT_TRUE(context.contains(QStringLiteral("preview")));

  ASSERT_TRUE(context.claimCompletion(QStringLiteral("preview"),
                                      QStringLiteral("Default"), true));
  ASSERT_TRUE(
      context.releaseCompletedVfsReservation(QStringLiteral("preview")));
  context.finishCompletion(QStringLiteral("preview"));
  EXPECT_TRUE(context.activeLaunches().empty());
}

TEST(ProcessLaunchContextTrackerTest, FailedPreviewPreparationAbandonsReservation)
{
  ProcessLaunchContextTracker context;
  try {
    ScopedProcessLaunchReservation preview(
        context, QStringLiteral("preview"), QStringLiteral("Default"), true);
    ASSERT_TRUE(preview);
    throw std::runtime_error("preparation failed");
  } catch (const std::runtime_error&) {
  }

  EXPECT_TRUE(context.activeLaunches().empty());
  EXPECT_TRUE(context.reserve(QStringLiteral("application"),
                              QStringLiteral("Default"), true));
  context.abandon(QStringLiteral("application"));
}

TEST(ProcessShutdownPolicyTest, ForceNeverOverridesTrackedLaunches)
{
  ProcessLaunchContextTracker context;
  ASSERT_TRUE(context.reserve(QStringLiteral("application"),
                              QStringLiteral("Default"), false));

  EXPECT_FALSE(ProcessShutdownPolicy::allowsCoreDestruction(
      context.activeLaunches()));
  EXPECT_FALSE(ProcessShutdownPolicy::checkDownloadPrompts(/*force=*/true));

  context.abandon(QStringLiteral("application"));
  EXPECT_TRUE(ProcessShutdownPolicy::allowsCoreDestruction(
      context.activeLaunches()));
  EXPECT_FALSE(ProcessShutdownPolicy::checkDownloadPrompts(/*force=*/true));
  EXPECT_FALSE(ProcessShutdownPolicy::showActiveLaunchFeedback(
      /*silentActiveLaunch=*/true));
  EXPECT_TRUE(ProcessShutdownPolicy::checkDownloadPrompts(/*force=*/false));
}

TEST(UpdaterRestartPolicyTest, HelperNeverLaunchesWhileOldProcessIsAlive)
{
  EXPECT_FALSE(
      updater_restart::shouldRetryExit(ExitRequestResult::Authorized));
  EXPECT_TRUE(updater_restart::shouldRetryExit(ExitRequestResult::Refused));
  EXPECT_TRUE(updater_restart::shouldRetryExit(ExitRequestResult::InProgress));

  EXPECT_FALSE(updater_restart::helperMayLaunch(1234, 1234, 'S'));
  EXPECT_TRUE(updater_restart::helperMayLaunch(5678, 1234, 'S'));
  EXPECT_TRUE(updater_restart::helperMayLaunch(std::nullopt, 1234, 'S'));
  EXPECT_TRUE(updater_restart::helperMayLaunch(1234, 1234, 'Z'));

  const QByteArray script = updater_restart::helperScript();
  EXPECT_TRUE(script.contains("OLD_START=\"$2\""));
  EXPECT_TRUE(script.contains("START=\"${20:-}\""));
  EXPECT_TRUE(script.contains("$STATE\" != Z"));
  EXPECT_FALSE(script.contains("kill -0"));
  EXPECT_FALSE(script.contains("for _ in"));
  EXPECT_FALSE(script.contains("seq 1"));
  EXPECT_TRUE(script.contains("CLEANUP_DIR=\"${5:-}\""));
  EXPECT_TRUE(script.contains("--fluorine-clean-update=$CLEANUP_DIR"));
  EXPECT_TRUE(script.contains("--fluorine-wait-publish=30"));
  EXPECT_LT(script.indexOf("while old_process_is_same_generation"),
            script.indexOf("exec \"$NEW_LAUNCHER\""));
}

QByteArray fakeProcStat(char state, quint64 startTime)
{
  QByteArray stat = "4242 (fluorine ) helper) ";
  stat += state;
  // /proc/<pid>/stat fields 4..21 precede field 22 (starttime).
  for (int i = 0; i < 18; ++i) {
    stat += " 0";
  }
  stat += " " + QByteArray::number(startTime) + "\n";
  return stat;
}

void writeFile(const QString& path, const QByteArray& contents)
{
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  ASSERT_EQ(file.write(contents), contents.size());
  file.close();
}

TEST(UpdaterRestartPolicyTest, HelperStopsWaitingForZombieGeneration)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString procRoot = temporary.filePath(QStringLiteral("proc"));
  ASSERT_TRUE(QDir().mkpath(procRoot + QStringLiteral("/4242")));
  const QString statPath = procRoot + QStringLiteral("/4242/stat");
  writeFile(statPath, fakeProcStat('S', 1234));

  const QString helperPath = temporary.filePath(QStringLiteral("install.sh"));
  writeFile(helperPath, updater_restart::helperScript());

  QProcess helper;
  helper.start(QStringLiteral("bash"),
               {helperPath, QStringLiteral("4242"), QStringLiteral("1234"),
                QStringLiteral("/bin/true"), procRoot});
  ASSERT_TRUE(helper.waitForStarted());
  EXPECT_FALSE(helper.waitForFinished(250));

  writeFile(statPath, fakeProcStat('Z', 1234));
  EXPECT_TRUE(helper.waitForFinished(2000));
  EXPECT_EQ(helper.exitStatus(), QProcess::NormalExit);
  EXPECT_EQ(helper.exitCode(), 0);
}

TEST(UpdaterRestartPolicyTest, HelperDoesNotWaitForReusedPid)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString procRoot = temporary.filePath(QStringLiteral("proc"));
  ASSERT_TRUE(QDir().mkpath(procRoot + QStringLiteral("/4242")));
  writeFile(procRoot + QStringLiteral("/4242/stat"), fakeProcStat('S', 9999));
  const QString helperPath = temporary.filePath(QStringLiteral("install.sh"));
  writeFile(helperPath, updater_restart::helperScript());

  QProcess helper;
  helper.start(QStringLiteral("bash"),
               {helperPath, QStringLiteral("4242"), QStringLiteral("1234"),
                QStringLiteral("/bin/true"), procRoot});
  ASSERT_TRUE(helper.waitForStarted());
  EXPECT_TRUE(helper.waitForFinished(2000));
  EXPECT_EQ(helper.exitCode(), 0);
}

TEST(RestartTransactionTest, RefusalDoesNotPersistOrAcceptSelection)
{
  int authorizations = 0;
  int commits = 0;
  const auto result = restart_transaction::authorizeThenCommit(
      true,
      [&] {
        ++authorizations;
        return ExitRequestResult::Refused;
      },
      [&] { ++commits; });

  EXPECT_EQ(result, ExitRequestResult::Refused);
  EXPECT_EQ(authorizations, 1);
  EXPECT_EQ(commits, 0);
}

TEST(RestartTransactionTest, AuthorizationPrecedesPersistence)
{
  std::vector<int> order;
  const auto result = restart_transaction::authorizeThenCommit(
      true,
      [&] {
        order.push_back(1);
        return ExitRequestResult::Authorized;
      },
      [&] { order.push_back(2); });

  EXPECT_EQ(result, ExitRequestResult::Authorized);
  EXPECT_EQ(order, (std::vector<int>{1, 2}));
}

TEST(RestartTransactionTest, InProgressAuthorizationNeverCommits)
{
  int commits = 0;
  const auto result = restart_transaction::authorizeThenCommit(
      true, [] { return ExitRequestResult::InProgress; }, [&] { ++commits; });

  EXPECT_EQ(result, ExitRequestResult::InProgress);
  EXPECT_EQ(commits, 0);
}

TEST(RestartTransactionTest, NoRestartCommitsWithoutAuthorization)
{
  int authorizations = 0;
  int commits = 0;
  const auto result = restart_transaction::authorizeThenCommit(
      false,
      [&] {
        ++authorizations;
        return ExitRequestResult::Refused;
      },
      [&] { ++commits; });

  EXPECT_EQ(result, ExitRequestResult::Authorized);
  EXPECT_EQ(authorizations, 0);
  EXPECT_EQ(commits, 1);
}

TEST(RestartTransactionTest, RefusedSettingsRollbackRestoresCacheAndEventsInOrder)
{
  std::vector<int> order;
  const auto result = restart_transaction::restoreAfterRefusal(
      [&] {
        order.push_back(1);  // persistent QSettings snapshot
        return true;
      },
      [&] {
        order.push_back(2);  // cached plugin maps and blacklist
        return true;
      },
      [&] {
        order.push_back(3);  // plugin enablement and UI signals
        return true;
      },
      [&] {
        order.push_back(4);  // final disk verification
        return true;
      });

  EXPECT_EQ(result, restart_transaction::RollbackResult::Complete);
  EXPECT_EQ(order, (std::vector<int>{1, 2, 3, 4}));
}

TEST(RestartTransactionTest, RollbackCacheFailureStopsEventsAndVerification)
{
  int eventReversals = 0;
  int verifications = 0;
  const auto result = restart_transaction::restoreAfterRefusal(
      [] { return true; }, [] { return false; },
      [&] {
        ++eventReversals;
        return true;
      },
      [&] {
        ++verifications;
        return true;
      });

  EXPECT_EQ(result, restart_transaction::RollbackResult::CacheFailed);
  EXPECT_EQ(eventReversals, 0);
  EXPECT_EQ(verifications, 0);
}

TEST(RestartTransactionTest, ReversalNotifiesEachActuallyChangedStateOnce)
{
  const std::vector<int> states{10, 20, 30};
  const std::vector<bool> changed{true, false, true};
  std::vector<int> notifications;

  EXPECT_TRUE(restart_transaction::notifyChangedStatesOnce(
      states, changed,
      [&](int state) { notifications.push_back(state); }));
  EXPECT_EQ(notifications, (std::vector<int>{10, 30}));

  notifications.clear();
  EXPECT_FALSE(restart_transaction::notifyChangedStatesOnce(
      states, std::vector<bool>{true},
      [&](int state) { notifications.push_back(state); }));
  EXPECT_TRUE(notifications.empty());
}

NexusOAuthTokens nexusTokens(const QString& suffix, const QString& apiKey)
{
  NexusOAuthTokens tokens;
  tokens.accessToken  = QStringLiteral("access-") + suffix;
  tokens.refreshToken = QStringLiteral("refresh-") + suffix;
  tokens.scope        = QStringLiteral("openid profile");
  tokens.tokenType    = QStringLiteral("Bearer");
  tokens.expiresAt = QDateTime::fromString(QStringLiteral("2035-04-05T06:07:08Z"),
                                           Qt::ISODate);
  tokens.apiKey = apiKey;
  return tokens;
}

APIUserAccount nexusAccount(const QString& suffix, const NexusOAuthTokens& tokens)
{
  APILimits limits;
  limits.maxDailyRequests        = 2500;
  limits.remainingDailyRequests  = 2400;
  limits.maxHourlyRequests       = 100;
  limits.remainingHourlyRequests = 90;

  return APIUserAccount()
      .accessToken(tokens.accessToken)
      .apiKey(tokens.apiKey)
      .id(QStringLiteral("id-") + suffix)
      .name(QStringLiteral("user-") + suffix)
      .type(APIUserAccountTypes::Premium)
      .limits(limits);
}

struct FakeNexusEditState
{
  NexusCredentialStoreSnapshot store;
  NexusLiveCredentialSnapshot live;

  bool operator==(const FakeNexusEditState&) const = default;
};

restart_transaction::RollbackResult rollbackNexusEdit(
    FakeNexusEditState& current, const FakeNexusEditState& original,
    int& accountNotifications, bool failStoreRestore = false,
    bool failVerification = false)
{
  return restart_transaction::restoreAfterRefusal(
      [&] {
        if (failStoreRestore) {
          return false;
        }
        current.store = original.store;
        return true;
      },
      [] { return true; },
      [&] {
        current.live = original.live;
        ++accountNotifications;
        return true;
      },
      [&] { return !failVerification && current == original; });
}

TEST(NexusSettingsRollbackTest, ConnectThenCancelRestoresCredentialAbsence)
{
  const FakeNexusEditState original;
  const auto connectedTokens = nexusTokens(QStringLiteral("connected"),
                                           QStringLiteral("connected-api"));

  FakeNexusEditState current;
  current.store.apiKey         = connectedTokens.apiKey;
  current.store.oauthTokens    = connectedTokens;
  current.live.tokens          = connectedTokens;
  current.live.validationState = NexusValidationState::Valid;
  current.live.account =
      nexusAccount(QStringLiteral("connected"), connectedTokens);

  int notifications = 0;
  EXPECT_EQ(rollbackNexusEdit(current, original, notifications),
            restart_transaction::RollbackResult::Complete);
  EXPECT_EQ(current, original);
  EXPECT_FALSE(current.store.apiKey);
  EXPECT_FALSE(current.store.oauthTokens);
  EXPECT_EQ(notifications, 1);
}

TEST(NexusSettingsRollbackTest, DisconnectThenCancelRestoresCredentialsAndAccount)
{
  const auto tokens =
      nexusTokens(QStringLiteral("original"), QStringLiteral("original-api"));
  FakeNexusEditState original;
  original.store.apiKey         = tokens.apiKey;
  original.store.oauthTokens    = tokens;
  original.live.tokens          = tokens;
  original.live.validationState = NexusValidationState::Valid;
  original.live.account         = nexusAccount(QStringLiteral("original"), tokens);

  FakeNexusEditState current;
  int notifications = 0;
  EXPECT_EQ(rollbackNexusEdit(current, original, notifications),
            restart_transaction::RollbackResult::Complete);
  EXPECT_EQ(current, original);
  EXPECT_TRUE(current.live.account.isValid());
  EXPECT_EQ(notifications, 1);
}

TEST(NexusSettingsRollbackTest, ManualApiKeyThenCancelRestoresExactOldValues)
{
  const auto oldTokens =
      nexusTokens(QStringLiteral("old"), QStringLiteral("old-api"));
  const auto newTokens =
      nexusTokens(QStringLiteral("old"), QStringLiteral("replacement-api"));

  FakeNexusEditState original;
  original.store.apiKey         = oldTokens.apiKey;
  original.store.oauthTokens    = oldTokens;
  original.live.tokens          = oldTokens;
  original.live.validationState = NexusValidationState::Valid;
  original.live.account         = nexusAccount(QStringLiteral("old"), oldTokens);

  FakeNexusEditState current = original;
  current.store.apiKey       = newTokens.apiKey;
  current.store.oauthTokens  = newTokens;
  current.live.tokens        = newTokens;
  current.live.account       = nexusAccount(QStringLiteral("old"), newTokens);

  int notifications = 0;
  EXPECT_EQ(rollbackNexusEdit(current, original, notifications),
            restart_transaction::RollbackResult::Complete);
  EXPECT_EQ(current, original);
  EXPECT_EQ(current.store.apiKey, oldTokens.apiKey);
  EXPECT_EQ(notifications, 1);
}

TEST(NexusSettingsRollbackTest, RestartRefusalUsesSameVerifiedRollback)
{
  const auto oldTokens =
      nexusTokens(QStringLiteral("before"), QStringLiteral("before-api"));
  const auto newTokens =
      nexusTokens(QStringLiteral("after"), QStringLiteral("after-api"));

  FakeNexusEditState original;
  original.store.apiKey         = oldTokens.apiKey;
  original.store.oauthTokens    = oldTokens;
  original.live.tokens          = oldTokens;
  original.live.validationState   = NexusValidationState::NotChecked;
  original.live.validationWaiting = true;
  original.live.account         = nexusAccount(QStringLiteral("before"), oldTokens);

  FakeNexusEditState current;
  current.store.apiKey         = newTokens.apiKey;
  current.store.oauthTokens    = newTokens;
  current.live.tokens          = newTokens;
  current.live.validationState = NexusValidationState::Valid;
  current.live.account         = nexusAccount(QStringLiteral("after"), newTokens);

  int notifications = 0;
  EXPECT_EQ(rollbackNexusEdit(current, original, notifications),
            restart_transaction::RollbackResult::Complete);
  EXPECT_EQ(current, original);
  EXPECT_EQ(current.live.validationState, NexusValidationState::NotChecked);
  EXPECT_TRUE(current.live.validationWaiting);
  EXPECT_EQ(notifications, 1);
}

TEST(NexusSettingsRollbackTest, StoreFailureIsSurfacedBeforeLiveStateChanges)
{
  const auto tokens =
      nexusTokens(QStringLiteral("old"), QStringLiteral("old-api"));
  FakeNexusEditState original;
  original.store.apiKey      = tokens.apiKey;
  original.store.oauthTokens = tokens;
  original.live.tokens       = tokens;

  FakeNexusEditState current;
  int notifications = 0;
  EXPECT_EQ(rollbackNexusEdit(current, original, notifications,
                              /*failStoreRestore=*/true),
            restart_transaction::RollbackResult::PersistenceFailed);
  EXPECT_EQ(notifications, 0);
  EXPECT_NE(current, original);
}

TEST(NexusSettingsRollbackTest, FinalMismatchIsSurfaced)
{
  FakeNexusEditState current;
  const FakeNexusEditState original;
  int notifications = 0;
  EXPECT_EQ(rollbackNexusEdit(current, original, notifications,
                              /*failStoreRestore=*/false,
                              /*failVerification=*/true),
            restart_transaction::RollbackResult::VerificationFailed);
  EXPECT_EQ(notifications, 1);
}

TEST(NexusSettingsRollbackTest, OAuthOperationIsPartOfTheLiveSnapshot)
{
  NexusLiveCredentialSnapshot refreshing;
  refreshing.oauthOperation      = NexusOAuthOperation::Refresh;
  refreshing.oauthFlowGeneration = 41;
  refreshing.oauthAttemptGeneration = 7;

  auto authorization = refreshing;
  authorization.oauthOperation = NexusOAuthOperation::Authorization;
  EXPECT_NE(refreshing, authorization);

  auto replacementFlow = refreshing;
  replacementFlow.oauthFlowGeneration = 42;
  EXPECT_NE(refreshing, replacementFlow);

  auto replacementAttempt = refreshing;
  replacementAttempt.oauthAttemptGeneration = 8;
  EXPECT_NE(refreshing, replacementAttempt);
}

TEST(NexusSettingsRollbackTest, EveryIncompleteRollbackRequiresFailStop)
{
  using restart_transaction::RollbackResult;
  EXPECT_FALSE(restart_transaction::requiresFailStop(RollbackResult::Complete));
  EXPECT_TRUE(
      restart_transaction::requiresFailStop(RollbackResult::PersistenceFailed));
  EXPECT_TRUE(restart_transaction::requiresFailStop(RollbackResult::CacheFailed));
  EXPECT_TRUE(
      restart_transaction::requiresFailStop(RollbackResult::EventReversalFailed));
  EXPECT_TRUE(restart_transaction::requiresFailStop(
      RollbackResult::VerificationFailed));
}

TEST(ExitStateTest, InProcessRestartClearsPriorCloseAuthorization)
{
  ExitState state;
  ASSERT_EQ(state.requestAuthorization([] { return true; }),
            ExitRequestResult::Authorized);
  ASSERT_TRUE(state.canClose());

  state.resetForRestart();
  EXPECT_FALSE(state.canClose());
  EXPECT_FALSE(state.exiting());
  EXPECT_EQ(state.requestAuthorization([] { return true; }),
            ExitRequestResult::Authorized);
}

TEST(ExitStateTest, ReentrantAttemptIsNotAuthorizedWhenOuterLaterRefuses)
{
  ExitState state;
  ExitRequestResult innerResult = ExitRequestResult::Authorized;
  int innerAuthorizationCalls = 0;

  const auto outerResult = state.requestAuthorization([&] {
    innerResult = state.requestAuthorization([&] {
      ++innerAuthorizationCalls;
      return true;
    });
    EXPECT_TRUE(state.exiting());
    return false;
  });

  EXPECT_EQ(innerResult, ExitRequestResult::InProgress);
  EXPECT_EQ(innerAuthorizationCalls, 0);
  EXPECT_EQ(outerResult, ExitRequestResult::Refused);
  EXPECT_FALSE(state.canClose());
  EXPECT_FALSE(state.exiting());
  EXPECT_TRUE(updater_restart::shouldRetryExit(innerResult));
}

TEST(ExitStateTest, ReentrantTransactionCannotCommitDuringOuterAttempt)
{
  ExitState state;
  ExitRequestResult transactionResult = ExitRequestResult::Authorized;
  int commits = 0;

  const auto outerResult = state.requestAuthorization([&] {
    transactionResult = restart_transaction::authorizeThenCommit(
        true, [&] { return state.requestAuthorization([] { return true; }); },
        [&] { ++commits; });
    return false;
  });

  EXPECT_EQ(outerResult, ExitRequestResult::Refused);
  EXPECT_EQ(transactionResult, ExitRequestResult::InProgress);
  EXPECT_EQ(commits, 0);
  EXPECT_FALSE(state.canClose());
  EXPECT_TRUE(updater_restart::shouldRetryExit(transactionResult));
  EXPECT_TRUE(updater_restart::shouldRetryExit(outerResult));
}

TEST(ExitStateTest, ExceptionEndsAttemptWithoutAuthorizingClose)
{
  ExitState state;

  EXPECT_THROW(
      state.requestAuthorization([]() -> bool {
        throw std::runtime_error("authorization failed");
      }),
      std::runtime_error);
  EXPECT_FALSE(state.exiting());
  EXPECT_FALSE(state.canClose());
  EXPECT_EQ(state.requestAuthorization([] { return false; }),
            ExitRequestResult::Refused);
}

TEST(ProcessCoordinationTest, WaitRemainsBlockedThroughQueuedNextGeneration)
{
  int activeGeneration = 1;
  int wakes = 0;

  const bool completed = process_coordination::waitUntilIdle(
      [&]() { return activeGeneration != 0; },
      [&]() {
        ++wakes;
        if (activeGeneration == 1) {
          // Generation N+1 starts before generation N releases its waiter.
          activeGeneration = 2;
        } else {
          activeGeneration = 0;
        }
        return true;
      });

  EXPECT_TRUE(completed);
  EXPECT_EQ(wakes, 2);
}

TEST(ProcessCoordinationTest, WaitStopsWhenOwnerIsTornDown)
{
  int waits = 0;
  const bool completed = process_coordination::waitUntilIdle(
      []() { return true; },
      [&]() {
        ++waits;
        return false;
      });

  EXPECT_FALSE(completed);
  EXPECT_EQ(waits, 1);
}

}  // namespace
