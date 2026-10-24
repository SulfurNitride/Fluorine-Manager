#ifndef APPLICATIONEXIT_H
#define APPLICATIONEXIT_H

#include <climits>

#include <QCoreApplication>
#include <QSignalBlocker>

const int RestartExitCode  = INT_MAX;
const int ReselectExitCode = INT_MAX - 1;

namespace MOShared
{

// Quits every running event loop on the main thread with `code`. A restart
// re-enters exec() on the same application object, so it must not look like a
// quit to listeners.
inline void exitEventLoop(QCoreApplication* app, int code)
{
  if (code != RestartExitCode) {
    app->exit(code);
    return;
  }

  // exit() emits aboutToQuit. Qt WebEngine deletes its shared GL context from
  // that signal and never recreates it, so a second restart would delete it
  // again. exec() re-arms the signal for the real quit.
  const QSignalBlocker blocker(app);
  app->exit(code);
}

}  // namespace MOShared

#endif  // APPLICATIONEXIT_H
