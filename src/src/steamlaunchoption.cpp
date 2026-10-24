#include "steamlaunchoption.h"

#include "desktopshortcutpolicy.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace steamlaunch
{

QString installedLauncherPath(const QProcessEnvironment& env)
{
  QString dataHome = env.value(QStringLiteral("XDG_DATA_HOME"));
  if (dataHome.isEmpty() || !QDir::isAbsolutePath(dataHome)) {
    dataHome = env.value(QStringLiteral("HOME")) + QStringLiteral("/.local/share");
  }
  return dataHome + QStringLiteral("/fluorine/bin/fluorine-manager");
}

QString launcherForLine(const QProcessEnvironment& env, const QString& runningLauncher)
{
  const QString installed = installedLauncherPath(env);
  return QFile::exists(installed) ? installed : runningLauncher;
}

QString instanceArgument(bool portable, const QString& directory,
                         const QString& displayName)
{
  return portable ? QFileInfo(directory).absoluteFilePath() : displayName;
}

// Steam substitutes %command% as text before the shell runs, so a literal
// %command% inside a quoted value would be replaced too. Splitting the token
// with an empty quote pair keeps the shell's result identical.
static QString quoteValue(const QString& value)
{
  QString quoted = env::desktopshortcut::shellQuote(value);
  quoted.replace(QStringLiteral("%command%"), QStringLiteral("%comm''and%"));
  return quoted;
}

QString launchOptionLine(const QString& launcher, const QString& instanceId,
                         const QString& profileOrEmpty, const QString& exeTitle)
{
  QString line = quoteValue(launcher) + QStringLiteral(" -i ") + quoteValue(instanceId);
  if (!profileOrEmpty.isEmpty()) {
    line += QStringLiteral(" -p ") + quoteValue(profileOrEmpty);
  }
  line += QStringLiteral(" run --headless -e ") + quoteValue(exeTitle) +
          QStringLiteral(" # %command%");
  return line;
}

std::optional<QString> steamAppIdFor(const QString& exeAppId, const QString& gameAppId)
{
  const QString exe = exeAppId.trimmed();
  const QString id  = exe.isEmpty() ? gameAppId.trimmed() : exe;
  if (id.isEmpty()) {
    return std::nullopt;
  }
  for (const QChar c : id) {
    if (c < QLatin1Char('0') || c > QLatin1Char('9')) {
      return std::nullopt;
    }
  }
  return id;
}

}  // namespace steamlaunch
