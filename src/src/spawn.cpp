/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "spawn.h"

#include "env.h"
#include "envmodule.h"
#include "fluorineconfig.h"
#include "protonlauncher.h"
#include "settings.h"
#include "shared/appconfig.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QtDebug>

#include <uibase/errorcodes.h>
#include <uibase/log.h>
#include <uibase/report.h>
#include <uibase/utility.h>

#include <cerrno>
#include <cstring>
#include <csignal>
#include <sys/types.h>

class QMessageBox;

using namespace MOBase;

namespace spawn::dialogs
{

QString makeDetails(const SpawnParameters& sp, int code, const QString& more = {})
{
  const bool cwdExists =
      (sp.currentDirectory.isEmpty() ? true : sp.currentDirectory.exists());

  return QString("Error %1%2: %3\n"
                 " . binary: '%4'\n"
                 " . arguments: '%5'\n"
                 " . cwd: '%6'%7\n"
                 " . stdout: %8, stderr: %9, hooked: %10")
      .arg(code)
      .arg(more.isEmpty() ? more : ", " + more)
      .arg(QString::fromUtf8(strerror(code)))
      .arg(sp.binary.absoluteFilePath())
      .arg(sp.arguments)
      .arg(sp.currentDirectory.absolutePath())
      .arg(cwdExists ? "" : " (not found)")
      .arg(sp.stdOut == -1 ? "no" : "yes")
      .arg(sp.stdErr == -1 ? "no" : "yes")
      .arg(sp.hooked ? "yes" : "no");
}

QString makeContent(const SpawnParameters& sp, int code)
{
  if (code == ENOENT) {
    return QObject::tr("The file '%1' does not exist.")
        .arg(sp.binary.absoluteFilePath());
  } else if (code == EACCES) {
    return QObject::tr("Permission denied when trying to start '%1'. "
                       "Check that the file is executable.")
        .arg(sp.binary.absoluteFilePath());
  }

  if (!sp.currentDirectory.exists()) {
    return QObject::tr("The working directory '%1' does not exist.")
        .arg(sp.currentDirectory.absolutePath());
  }

  return QString::fromUtf8(strerror(code));
}

void spawnFailed(QWidget* parent, const SpawnParameters& sp, int code)
{
  const auto details = makeDetails(sp, code);
  log::error("{}", details);

  const auto title    = QObject::tr("Cannot launch program");
  const auto mainText = QObject::tr("Cannot start %1").arg(sp.binary.fileName());

  MOBase::TaskDialog(parent, title)
      .main(mainText)
      .content(makeContent(sp, code))
      .details(details)
      .icon(QMessageBox::Critical)
      .exec();
}

QMessageBox::StandardButton confirmStartSteam(QWidget* parent, const SpawnParameters& sp,
                                              const QString& details)
{
  const auto title    = QObject::tr("Launch Steam");
  const auto mainText = QObject::tr("This program requires Steam");
  const auto content =
      QObject::tr("Mod Organizer has detected that this "
                  "program likely requires Steam to be "
                  "running to function properly.");

  return MOBase::TaskDialog(parent, title)
      .main(mainText)
      .content(content)
      .details(details)
      .icon(QMessageBox::Question)
      .button({QObject::tr("Start Steam"), QMessageBox::Yes})
      .button({QObject::tr("Continue without starting Steam"),
               QObject::tr("The program might fail to run."), QMessageBox::No})
      .button({QObject::tr("Cancel"), QMessageBox::Cancel})
      .remember("steamQuery", sp.binary.fileName())
      .exec();
}

QMessageBox::StandardButton confirmBlacklisted(QWidget* parent,
                                               const SpawnParameters& sp,
                                               Settings& settings)
{
  const auto title = QObject::tr("Blacklisted program");
  const auto mainText =
      QObject::tr("The program %1 is blacklisted").arg(sp.binary.fileName());
  const auto content = QObject::tr(
      "The program you are attempting to launch is blacklisted in the virtual "
      "filesystem. This will likely prevent it from seeing any mods, INI files "
      "or any other virtualized files.");

  const auto details = "Executable: " + sp.binary.fileName() +
                       "\nCurrent blacklist: " + settings.executablesBlacklist();

  return MOBase::TaskDialog(parent, title)
      .main(mainText)
      .content(content)
      .details(details)
      .icon(QMessageBox::Question)
      .remember("blacklistedExecutable", sp.binary.fileName())
      .button({QObject::tr("Continue"),
               QObject::tr("Your mods might not work."), QMessageBox::Yes})
      .button({QObject::tr("Cancel"), QMessageBox::Cancel})
      .exec();
}

}  // namespace spawn::dialogs

namespace spawn
{

void logSpawning(const SpawnParameters& sp, const QString& realCmd)
{
  log::debug("spawning binary:\n"
             " . exe: '{}'\n"
             " . args: '{}'\n"
             " . cwd: '{}'\n"
             " . steam id: '{}'\n"
             " . hooked: {}\n"
             " . stdout: {}\n"
             " . stderr: {}\n"
             " . real cmd: '{}'",
             sp.binary.absoluteFilePath(), sp.arguments,
             sp.currentDirectory.absolutePath(), sp.steamAppID, sp.hooked,
             (sp.stdOut == -1 ? "no" : "yes"), (sp.stdErr == -1 ? "no" : "yes"),
             realCmd);
}

uint32_t parseSteamAppId(const QString& steamAppId)
{
  bool ok      = false;
  const auto n = steamAppId.toUInt(&ok);
  return (ok ? n : 0u);
}

QString firstExistingSetting(const QSettings& settings, const QStringList& keys)
{
  for (const QString& key : keys) {
    const QString value = settings.value(key).toString().trimmed();
    if (!value.isEmpty()) {
      return value;
    }
  }

  return {};
}

// Strip "<letter>:"="..." entries (other than C:/Z:) from the
// [Software\\Wine\\Drives] section of system.reg.  Without this, Wine
// recreates pruned dosdevices symlinks at the next prefix start from the
// registry, so the more-specific drive (e.g. X:\Games) keeps winning over
// Z:\home\user\Games during path canonicalisation.
static QStringList pruneDriveRegistry(const QString& prefixPath)
{
  const QString regPath = QDir(prefixPath).filePath("system.reg");
  QFile file(regPath);
  if (!file.exists()) {
    return {};
  }
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    MOBase::log::warn("pruneDriveRegistry: cannot open '{}'", regPath.toStdString());
    return {};
  }

  static const QRegularExpression sectionRe(
      QStringLiteral(R"(^\[Software\\\\Wine\\\\Drives\])"),
      QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression driveRe(QStringLiteral(R"(^"([A-Za-z]):"\s*=)"));

  QStringList lines;
  QStringList removed;
  bool inDrives = false;

  QTextStream in(&file);
  while (!in.atEnd()) {
    const QString line    = in.readLine();
    const QString trimmed = line.trimmed();

    if (trimmed.startsWith('[')) {
      inDrives = sectionRe.match(trimmed).hasMatch();
      lines.append(line);
      continue;
    }

    if (inDrives) {
      const auto m = driveRe.match(trimmed);
      if (m.hasMatch()) {
        const QChar letter = m.captured(1).at(0).toLower();
        if (letter != QLatin1Char('c') && letter != QLatin1Char('z')) {
          removed << QString(letter.toUpper()) + QLatin1Char(':');
          continue;
        }
      }
    }

    lines.append(line);
  }
  file.close();

  if (removed.isEmpty()) {
    return {};
  }

  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
    MOBase::log::warn("pruneDriveRegistry: cannot rewrite '{}'", regPath.toStdString());
    return {};
  }
  QTextStream out(&file);
  for (const QString& l : lines) {
    out << l << "\n";
  }
  return removed;
}

// Remove dosdevices/<letter>: symlinks for any drive other than C: and Z:,
// and strip the matching [Software\\Wine\\Drives] entries from system.reg
// so Wine doesn't recreate them at the next prefix start.  External tooling
// (Faugus, manual edits, modlist installers) can re-add drives like X: that
// map subtrees of the host filesystem; Wine then prefers the more specific
// drive when canonicalising paths, which mangles binaries we passed in as
// Z:\home\user\... into X:\....  Keeping the allowed list minimal means MO2
// can rely on Z: being the only host-mapped drive.
void pruneExtraDrives(const QString& prefixPath)
{
  static const QSet<QString> kept = {QStringLiteral("c:"), QStringLiteral("z:")};

  const QString dosdevices = QDir(prefixPath).filePath("dosdevices");
  QDir dir(dosdevices);
  if (!dir.exists()) {
    return;
  }

  QStringList removed;
  for (const QString& entry :
       dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries)) {
    const QString lower = entry.toLower();
    if (lower.length() != 2 || !lower.endsWith(':') || !lower.at(0).isLetter()) {
      continue;
    }
    if (kept.contains(lower)) {
      continue;
    }
    if (QFile::remove(dir.filePath(entry))) {
      removed << entry.toUpper();
    }
  }

  const QStringList regRemoved = pruneDriveRegistry(prefixPath);

  QSet<QString> all;
  for (const QString& d : removed) {
    all.insert(d);
  }
  for (const QString& d : regRemoved) {
    all.insert(d);
  }
  if (!all.isEmpty()) {
    QStringList sorted(all.begin(), all.end());
    sorted.sort();
    MOBase::log::info(
        "Pruned stale drive letters from prefix '{}': {} (symlinks: {}, "
        "registry: {})",
        prefixPath, sorted.join(QStringLiteral(", ")).toStdString(),
        removed.join(QStringLiteral(", ")).toStdString(),
        regRemoved.join(QStringLiteral(", ")).toStdString());
  }
}

QString resolvePrefixPath()
{
  // The Fluorine config is authoritative: it's the prefix the user
  // explicitly created through Settings > Proton and that Fluorine itself
  // initialises with wineboot/DLL installs. Always prefer it.
  if (auto cfg = FluorineConfig::load(); cfg.has_value() && cfg->prefixExists()) {
    MOBase::log::debug("resolvePrefixPath: using Fluorine config prefix '{}'",
                       cfg->prefix_path);
    return cfg->prefix_path.trimmed();
  }

  const Settings* settings = Settings::maybeInstance();
  if (settings == nullptr) {
    return {};
  }

  // Fallbacks, in priority order. `fluorine/prefix_path` is set only by
  // explicit user action (CLI `--prefix` or the instance creation wizard),
  // so we trust it above the `Settings/*` keys that game-detection can
  // populate automatically with an external manager's prefix (Heroic,
  // Bottles, Lutris). Those external prefixes are fine as discovery hints
  // but must not silently override the user's chosen Fluorine prefix —
  // see issue #52.
  const QSettings instanceSettings(settings->filename(), QSettings::IniFormat);
  const QString explicitPath =
      instanceSettings.value("fluorine/prefix_path").toString().trimmed();
  if (!explicitPath.isEmpty()) {
    MOBase::log::debug("resolvePrefixPath: using explicit fluorine/prefix_path '{}'",
                       explicitPath);
    return explicitPath;
  }

  const QString fallback = firstExistingSetting(
      instanceSettings, {"Settings/proton_prefix_path", "Settings/prefix_path",
                         "Proton/prefix_path"});
  if (!fallback.isEmpty()) {
    MOBase::log::warn(
        "resolvePrefixPath: falling back to auto-detected prefix '{}' — this "
        "may point at an external manager's prefix (Heroic/Bottles). Create a "
        "Fluorine prefix in Settings > Proton to override.",
        fallback);
  }
  return fallback;
}

QString resolveProtonPath()
{
  if (auto cfg = FluorineConfig::load(); cfg.has_value()) {
    const QString protonPath = cfg->proton_path.trimmed();
    if (!protonPath.isEmpty()) {
      return protonPath;
    }
  }

  const Settings* settings = Settings::maybeInstance();
  if (settings == nullptr) {
    return {};
  }

  const QSettings instanceSettings(settings->filename(), QSettings::IniFormat);
  return firstExistingSetting(
      instanceSettings,
      {"Settings/proton_path", "Proton/path", "fluorine/proton_path"});
}

int spawn(const SpawnParameters& sp, pid_t& processId)
{
  const QString bin = MOBase::normalizePathForHost(sp.binary.absoluteFilePath());
  QString cwd       = MOBase::normalizePathForHost(sp.currentDirectory.absolutePath());

  QStringList argList;
  if (!sp.arguments.isEmpty()) {
    argList = QProcess::splitCommand(sp.arguments);
  }

  if (cwd.isEmpty()) {
    cwd = QFileInfo(bin).absolutePath();
  }

  logSpawning(sp, bin + " " + sp.arguments);

  ProtonLauncher launcher;
  launcher.setBinary(bin)
      .setArguments(argList)
      .setWorkingDir(cwd)
      .setSteamAppId(parseSteamAppId(sp.steamAppID));

  if (sp.useProton) {
    // Read per-instance settings from the instance INI (not the global QSettings).
    const Settings* instanceForLaunch = Settings::maybeInstance();
    bool useSteamDrm                  = true;
    bool useSLR                       = true;
    bool useSteamOverlay              = false;
    QString storeVariant;
    if (instanceForLaunch) {
      const QSettings instanceIni(instanceForLaunch->filename(), QSettings::IniFormat);
      useSteamDrm     = instanceIni.value("fluorine/steam_drm", true).toBool();
      useSLR          = instanceIni.value("fluorine/use_slr", true).toBool();
      useSteamOverlay = instanceIni.value("fluorine/steam_overlay", false).toBool();
      storeVariant    = instanceIni.value("game_edition").toString().trimmed();
    }

    launcher.setSteamDrm(useSteamDrm)
        .setSteamOverlay(useSteamOverlay)
        .setStoreVariant(storeVariant)
        .setUseSLR(useSLR);

    const QString prefixPath = resolvePrefixPath();
    if (prefixPath.isEmpty()) {
      MOBase::log::error("No Wine prefix configured - cannot launch game. "
                         "Configure a prefix in Settings > Proton.");
      return ENOENT;
    } else if (!QDir(QDir(prefixPath).filePath("drive_c")).exists()) {
      MOBase::log::error("Wine prefix '{}' does not contain drive_c/ - prefix is invalid",
                         prefixPath);
      return ENOENT;
    } else {
      MOBase::log::info("Using Wine prefix: {}", prefixPath);
      pruneExtraDrives(prefixPath);
      launcher.setPrefix(prefixPath);
    }

    const QString protonPath = resolveProtonPath();
    if (!protonPath.isEmpty()) {
      launcher.setProtonPath(protonPath);
    }

    const QString wrapper =
        QSettings().value("fluorine/launch_wrapper").toString().trimmed();
    if (!wrapper.isEmpty()) {
      launcher.setWrapper(wrapper);
    }

    if (!sp.saveBindMountSource.isEmpty() && !sp.saveBindMountTarget.isEmpty()) {
      launcher.setSavesBindMount(sp.saveBindMountSource, sp.saveBindMountTarget);
    }
  } else {
    MOBase::log::info("Launching executable directly without Proton");
  }

  launcher.setUseTerminal(sp.useTerminal);

  const auto [ok, pid] = launcher.launch();
  if (!ok) {
    return (errno != 0 ? errno : EIO);
  }

  processId = static_cast<pid_t>(pid);
  return 0;
}

struct SteamStatus
{
  bool running    = false;
  bool accessible = false;
};

SteamStatus getSteamStatus()
{
  SteamStatus ss;

  QProcess pgrep;
  pgrep.start("pgrep", QStringList() << "-x" << "steam");
  pgrep.waitForFinished(3000);

  if (pgrep.exitCode() == 0) {
    ss.running    = true;
    ss.accessible = true;
    log::debug("steam is running");
  }

  return ss;
}

QString makeSteamArguments(const QString& username, const QString& password)
{
  QString args;

  if (username != "") {
    args += "-login " + username;

    if (password != "") {
      args += " " + password;
    }
  }

  return args;
}

bool startSteam(QWidget* parent)
{
  QString steamPath;

  // Prefer ~/.steam/root/steam.sh, fall back to PATH.
  const QString homeDir = QDir::homePath();
  const QString steamSh = homeDir + "/.steam/root/steam.sh";
  if (QFileInfo::exists(steamSh)) {
    steamPath = steamSh;
  } else {
    steamPath = QStandardPaths::findExecutable("steam");
  }

  if (steamPath.isEmpty()) {
    log::error("could not find steam installation");

    const auto title = QObject::tr("Cannot start Steam");
    MOBase::TaskDialog(parent, title)
        .main(title)
        .content(QObject::tr("The Steam executable could not be found. "
                             "Make sure Steam is installed."))
        .icon(QMessageBox::Critical)
        .button({QObject::tr("Continue without starting Steam"),
                 QObject::tr("The program may fail to launch."), QMessageBox::Yes})
        .button({QObject::tr("Cancel"), QMessageBox::Cancel})
        .exec();

    return false;
  }

  SpawnParameters sp;
  sp.binary = QFileInfo(steamPath);

  pid_t pid    = -1;
  const auto e = spawn(sp, pid);

  if (e != 0) {
    log::error("failed to start steam");
    return false;
  }

  QMessageBox::information(
      parent, QObject::tr("Waiting"),
      QObject::tr("Please press OK once you're logged into steam."));

  return true;
}

bool checkSteam(QWidget* parent, const SpawnParameters& sp, const QDir& gameDirectory,
                const QString& steamAppID, const Settings& settings)
{
  static const std::vector<QString> steamFiles = {"libsteam_api.so"};

  log::debug("checking steam");

  if (!steamAppID.isEmpty()) {
    env::set("SteamAPPId", steamAppID);
  } else {
    env::set("SteamAPPId", settings.steam().appID());
  }

  bool steamRequired = false;
  QString details;

  for (const auto& file : steamFiles) {
    const QFileInfo fi(gameDirectory.absoluteFilePath(file));
    if (fi.exists()) {
      details = QString("managed game is located at '%1' and file '%2' exists")
                    .arg(gameDirectory.absolutePath())
                    .arg(fi.absoluteFilePath());

      log::debug("{}", details);
      steamRequired = true;

      break;
    }
  }

  if (!steamRequired) {
    log::debug("program doesn't seem to require steam");
    return true;
  }

  auto ss = getSteamStatus();

  if (!ss.running) {
    log::debug("steam isn't running, asking to start steam");

    const auto c = dialogs::confirmStartSteam(parent, sp, details);

    if (c == QMessageBox::Yes) {
      log::debug("user wants to start steam");

      if (!startSteam(parent)) {
        return false;
      }

      ss = getSteamStatus();
      if (!ss.running) {
        log::error("steam is still not running, hoping for the best");
        return true;
      }
    } else if (c == QMessageBox::No) {
      log::debug("user declined to start steam");
      return true;
    } else {
      log::debug("user cancelled");
      return false;
    }
  }

  return true;
}

bool checkBlacklist(QWidget* parent, const SpawnParameters& sp, Settings& settings)
{
  for (;;) {
    if (!settings.isExecutableBlacklisted(sp.binary.fileName())) {
      return true;
    }

    const auto r = dialogs::confirmBlacklisted(parent, sp, settings);

    if (r != QMessageBox::Retry) {
      return (r == QMessageBox::Yes);
    }
  }
}

pid_t startBinary(QWidget* parent, const SpawnParameters& sp)
{
  pid_t pid    = -1;
  const auto e = spawn::spawn(sp, pid);

  if (e != 0) {
    if (e == ENOENT && sp.useProton && !FluorineConfig::isSetup()) {
      QMessageBox::critical(
          parent, QObject::tr("No Wine Prefix"),
          QObject::tr("No Wine prefix has been configured for this instance.\n\n"
                      "A Wine prefix is required to run Windows games through "
                      "Proton.\n\n"
                      "To create one, go to:\n"
                      "  Settings → Wine/Proton\n\n"
                      "Set the prefix location, then click \"Create Prefix\" "
                      "to generate a new prefix."));
    } else {
      dialogs::spawnFailed(parent, sp, e);
    }
    return -1;
  }

  return pid;
}

QString findJavaInstallation(const QString& jarFile)
{
  Q_UNUSED(jarFile);

  const auto javaPath = QStandardPaths::findExecutable("java");
  if (!javaPath.isEmpty()) {
    return javaPath;
  }

  return {};
}

bool isBatchFile(const QFileInfo& target)
{
  const auto batchExtensions = {"sh"};

  const QString extension = target.suffix();
  for (auto&& e : batchExtensions) {
    if (extension.compare(e, Qt::CaseInsensitive) == 0) {
      return true;
    }
  }

  return false;
}

bool isExeFile(const QFileInfo& target)
{
  return target.isExecutable() && target.isFile();
}

bool isJavaFile(const QFileInfo& target)
{
  return (target.suffix().compare("jar", Qt::CaseInsensitive) == 0);
}

QFileInfo getCmdPath()
{
  const auto p = env::get("SHELL");
  if (!p.isEmpty()) {
    return QFileInfo(p);
  }

  return QFileInfo("/bin/bash");
}

FileExecutionTypes getFileExecutionType(const QFileInfo& target)
{
  if (isExeFile(target) || isBatchFile(target) || isJavaFile(target)) {
    return FileExecutionTypes::Executable;
  }

  return FileExecutionTypes::Other;
}

FileExecutionContext getFileExecutionContext(QWidget* parent, const QFileInfo& target)
{
  if (isExeFile(target)) {
    return {.binary=target, .arguments="", .type=FileExecutionTypes::Executable};
  }

  if (isBatchFile(target)) {
    return {.binary=getCmdPath(), .arguments=QString("\"%1\"").arg(target.absoluteFilePath()),
            .type=FileExecutionTypes::Executable};
  }

  if (isJavaFile(target)) {
    auto java = findJavaInstallation(target.absoluteFilePath());

    if (java.isEmpty()) {
      java = QFileDialog::getOpenFileName(parent, QObject::tr("Select binary"),
                                          QString(), QObject::tr("Binary") + " (*)");
    }

    if (!java.isEmpty()) {
      return {.binary=QFileInfo(java),
              .arguments=QString("-jar \"%1\"")
                  .arg(QDir::toNativeSeparators(target.absoluteFilePath())),
              .type=FileExecutionTypes::Executable};
    }
  }

  return {.binary={}, .arguments={}, .type=FileExecutionTypes::Other};
}

}  // namespace spawn
