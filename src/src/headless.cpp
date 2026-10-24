#include "headless.h"
#include <QObject>
#include <atomic>
#include <mutex>
#include <uibase/log.h>
#include <uibase/report.h>

namespace headless
{

namespace
{
  std::atomic<bool> g_active{false};
  std::mutex g_mutex;
  Notifier g_notifier;
  QString g_subject;
  std::optional<QString> g_firstFailure;

  // Records and notifies without logging; used for prompts that uibase has
  // already logged.
  void notifyAndRecord(const QString& reason)
  {
    Notifier notifier;
    QString summary;
    {
      std::lock_guard lock(g_mutex);
      if (!g_firstFailure) {
        g_firstFailure = reason;
      }
      notifier = g_notifier;
      summary  = g_subject.isEmpty() ? QObject::tr("Fluorine couldn't start the game")
                                     : QObject::tr("Couldn't start %1").arg(g_subject);
    }

    if (notifier) {
      notifier(summary, reason);
    }
  }
}  // namespace

void activate(Notifier notifier)
{
  {
    std::lock_guard lock(g_mutex);
    g_notifier = std::move(notifier);
  }
  g_active = true;
  MOBase::setHeadlessReportHandler([](const QString& m) {
    notifyAndRecord(m);
  });
}

void deactivateForTesting()
{
  g_active = false;
  {
    std::lock_guard lock(g_mutex);
    g_notifier = nullptr;
    g_subject.clear();
    g_firstFailure.reset();
  }
  MOBase::setHeadlessReportHandler({});
}

bool active()
{
  return g_active;
}

void setSubject(const QString& exeTitle)
{
  std::lock_guard lock(g_mutex);
  g_subject = exeTitle;
}

void fail(const QString& reason)
{
  MOBase::log::error("{}", reason);
  notifyAndRecord(reason);
}

std::optional<QString> firstFailure()
{
  std::lock_guard lock(g_mutex);
  return g_firstFailure;
}

int runExitCode(RunOutcome outcome, int gameExitCode)
{
  switch (outcome) {
  case RunOutcome::Stopped:
    return StoppedExitCode;
  case RunOutcome::Failed:
    return LaunchFailedExitCode;
  case RunOutcome::Completed:
    break;
  }

  if (gameExitCode < 0) {
    return LaunchFailedExitCode;
  }
  // Windows exit codes can exceed what a Linux process can return.
  if (gameExitCode > 255) {
    return 1;
  }
  return gameExitCode;
}

}  // namespace headless
