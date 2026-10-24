#ifndef HEADLESS_H
#define HEADLESS_H

#include <QString>
#include <functional>
#include <optional>

// Headless mode: Fluorine runs a game for Steam without showing any window.
// Anything that would normally pop a dialog becomes a desktop notification.
namespace headless
{

// Exit code when Fluorine itself couldn't launch the game.
inline constexpr int LaunchFailedExitCode = 125;
// Exit code when Steam asked for the game to stop.
inline constexpr int StoppedExitCode = 143;

enum class RunOutcome
{
  Completed,
  Stopped,
  Failed
};

using Notifier = std::function<void(const QString& summary, const QString& body)>;

// Turns headless mode on and routes uibase error prompts to fail(). Does not
// log: it runs before logging is set up.
void activate(Notifier notifier);
void deactivateForTesting();
bool active();

// Names the program being launched, used in the notification summary.
void setSubject(const QString& exeTitle);

// Logs the reason, notifies, and remembers it if it is the first one.
void fail(const QString& reason);
std::optional<QString> firstFailure();

// Maps how a run ended to the process exit code. gameExitCode is only
// consulted for Completed; a negative value means the game was never waited on.
int runExitCode(RunOutcome outcome, int gameExitCode);

}  // namespace headless

#endif  // HEADLESS_H
