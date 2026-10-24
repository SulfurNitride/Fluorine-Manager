#include "headlessstop.h"

#include <atomic>
#include <csignal>

namespace headless
{

namespace
{
  // Lock-free so the handler can touch them.
  std::atomic<bool> g_waitStarted{false};
  std::atomic<bool> g_stopRequested{false};
  std::atomic<SignalFallback> g_fallback{nullptr};

  void handleStopSignal(int sig)
  {
    if (g_waitStarted.load() && !g_stopRequested.load()) {
      g_stopRequested.store(true);
      return;
    }

    if (SignalFallback fallback = g_fallback.load()) {
      fallback(sig);
    }
  }
}  // namespace

void installStopHandlers(SignalFallback fallback)
{
  g_fallback.store(fallback);

  struct sigaction action{};
  action.sa_handler = handleStopSignal;
  sigemptyset(&action.sa_mask);
  // The process survives the signal now, so I/O interrupted in other threads
  // has to restart.
  action.sa_flags = SA_RESTART;

  for (int sig : {SIGTERM, SIGINT, SIGHUP}) {
    sigaction(sig, &action, nullptr);
  }
}

void setWaitActive(bool active)
{
  if (active) {
    g_waitStarted.store(true);
  }
}

bool stopRequested()
{
  return g_stopRequested.load();
}

env::ProcessWaitAction waitAction()
{
  return stopRequested() ? env::ProcessWaitAction::Unlock
                         : env::ProcessWaitAction::Continue;
}

void resetStopForTesting()
{
  g_waitStarted.store(false);
  g_stopRequested.store(false);
}

}  // namespace headless
