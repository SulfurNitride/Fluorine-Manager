#ifndef HEADLESSSTOP_H
#define HEADLESSSTOP_H

#include "processlifetime.h"

// Steam stops a game by sending SIGTERM to the launch command. Headless mode
// turns that signal into an Unlock of the process wait so the normal cleanup
// (unmounting the VFS, syncing saves) still runs.
namespace headless
{

using SignalFallback = void (*)(int);

// Handles SIGTERM, SIGINT and SIGHUP. Before the wait has started, or on a
// second signal, the handler calls fallback(sig) instead, so a stuck run can
// still be killed. Handlers are installed without SA_RESETHAND.
void installStopHandlers(SignalFallback fallback);

// Marks the process wait as started. A first signal from then on, including
// during cleanup after the wait ended, requests a stop instead of escalating.
void setWaitActive(bool active);

bool stopRequested();

// Unlock once a stop was requested, otherwise Continue.
env::ProcessWaitAction waitAction();

// Clears the stop and wait-started flags.
void resetStopForTesting();

}  // namespace headless

#endif
