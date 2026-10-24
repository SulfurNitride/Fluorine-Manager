#ifndef STEAMLAUNCHOPTION_H
#define STEAMLAUNCHOPTION_H

#include <QProcessEnvironment>
#include <QString>
#include <optional>

namespace steamlaunch
{

// Mirrors the installer's FLUORINE_DATA_HOME rule. Takes the environment so a
// test can supply one.
QString installedLauncherPath(const QProcessEnvironment& env);

// The installed launcher if it exists, else the launcher of the running binary.
QString launcherForLine(const QProcessEnvironment& env, const QString& runningLauncher);

// The value for -i: the absolute directory for a portable instance, else its
// name.
QString instanceArgument(bool portable, const QString& directory,
                         const QString& displayName);

// '<launcher>' -i '<instance>'[ -p '<profile>'] run --headless -e '<title>' # %command%
QString launchOptionLine(const QString& launcher, const QString& instanceId,
                         const QString& profileOrEmpty, const QString& exeTitle);

// The executable's own Steam app id wins when set (and must then be digits),
// otherwise the game's. Matches ProcessRunner.
std::optional<QString> steamAppIdFor(const QString& exeAppId, const QString& gameAppId);

}  // namespace steamlaunch

#endif
