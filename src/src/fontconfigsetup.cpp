#include "fontconfigsetup.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

namespace FontconfigSetup
{
namespace
{
constexpr auto DISABLE_ENV = "FLUORINE_DISABLE_FONTCONFIG_FIX";
constexpr auto FILE_ENV = "FONTCONFIG_FILE";
constexpr auto PATH_ENV = "FONTCONFIG_PATH";
constexpr auto ORIG_FILE_ENV = "FLUORINE_ORIG_FONTCONFIG_FILE";
constexpr auto ORIG_PATH_ENV = "FLUORINE_ORIG_FONTCONFIG_PATH";

bool writeEmbeddedConfig(const QString& configPath)
{
  QFile embedded(QStringLiteral(":/fluorine/fontconfig/fonts.conf"));
  if (!embedded.open(QIODevice::ReadOnly)) {
    return false;
  }

  QSaveFile output(configPath);
  if (!output.open(QIODevice::WriteOnly)) {
    return false;
  }
  if (output.write(embedded.readAll()) < 0) {
    output.cancelWriting();
    return false;
  }
  return output.commit();
}

void activate(const QString& fontDir, const QString& configPath)
{
  if (!qEnvironmentVariableIsSet(ORIG_FILE_ENV)) {
    qputenv(ORIG_FILE_ENV, qgetenv(FILE_ENV));
  }
  if (!qEnvironmentVariableIsSet(ORIG_PATH_ENV)) {
    qputenv(ORIG_PATH_ENV, qgetenv(PATH_ENV));
  }
  qputenv(FILE_ENV, QFile::encodeName(configPath));
  qputenv(PATH_ENV, QFile::encodeName(fontDir));
}
}

QString applicationDirectory(const QString& argv0)
{
  QString executablePath =
      QFileInfo(QStringLiteral("/proc/self/exe")).canonicalFilePath();

  if (executablePath.isEmpty() && !argv0.isEmpty()) {
    const QFileInfo supplied(argv0);
    if (supplied.exists()) {
      executablePath = supplied.canonicalFilePath();
      if (executablePath.isEmpty()) {
        executablePath = supplied.absoluteFilePath();
      }
    } else {
      executablePath = QStandardPaths::findExecutable(argv0);
    }
  }

  if (executablePath.isEmpty()) {
    return QDir::currentPath();
  }
  return QFileInfo(executablePath).absoluteDir().absolutePath();
}

Result configure(const QString& appDir)
{
  if (qEnvironmentVariableIsSet(DISABLE_ENV)) {
    return {State::Disabled, {}, false};
  }

  const QString fontDir = QDir(appDir).filePath(QStringLiteral("etc/fonts"));
  const QString configPath =
      QDir(fontDir).filePath(QStringLiteral("fonts.conf"));

  if (QFileInfo(configPath).isFile()) {
    activate(fontDir, configPath);
    return {State::Active, configPath, false};
  }

  bool generated = false;
  if (QDir().mkpath(fontDir)) {
    generated = writeEmbeddedConfig(configPath);
  }
  if (!generated || !QFileInfo(configPath).isFile()) {
    return {State::Unavailable, configPath, false};
  }

  activate(fontDir, configPath);
  return {State::Active, configPath, true};
}

void restoreCallerEnvironment(QProcessEnvironment& environment)
{
  auto restore =
      [&environment](const QString& name, const QString& originalName) {
        if (!environment.contains(originalName)) {
          return;
        }

        const QString original = environment.value(originalName);
        if (original.isEmpty()) {
          environment.remove(name);
        } else {
          environment.insert(name, original);
        }
        environment.remove(originalName);
      };

  restore(QString::fromLatin1(FILE_ENV), QString::fromLatin1(ORIG_FILE_ENV));
  restore(QString::fromLatin1(PATH_ENV), QString::fromLatin1(ORIG_PATH_ENV));
}

}  // namespace FontconfigSetup
