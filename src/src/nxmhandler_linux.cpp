#include "nxmhandler_linux.h"
#include <log.h>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTextStream>

using namespace MOBase;

namespace
{
const QString NxmDesktopFile = QStringLiteral("com.fluorine.manager.nxm-handler.desktop");
const QString LegacyNxmDesktopFile = QStringLiteral("mo2-nxm-handler.desktop");
const QStringList UrlSchemes = {
    QStringLiteral("x-scheme-handler/nxm"),
    QStringLiteral("x-scheme-handler/modl"),
};

QString ensureDir(const QString& path)
{
  QDir const dir(path);
  if (!dir.exists() && !QDir().mkpath(path)) {
    return {};
  }
  return path;
}

bool writeTextFile(const QString& path, const QString& content)
{
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
    return false;
  }

  QTextStream stream(&file);
  stream << content;
  return stream.status() == QTextStream::Ok;
}

QStringList desktopFilesFromEntry(const QString& line)
{
  const int equals = line.indexOf('=');
  if (equals < 0) {
    return {};
  }

  QStringList result;
  for (const auto& desktopFile : line.mid(equals + 1).split(';', Qt::SkipEmptyParts)) {
    const auto trimmed = desktopFile.trimmed();
    if (!trimmed.isEmpty() && !result.contains(trimmed)) {
      result.append(trimmed);
    }
  }
  return result;
}

QString desktopFilesEntry(const QString& mimeType, const QStringList& desktopFiles)
{
  return mimeType + "=" + desktopFiles.join(';') + ";";
}

QStringList readMimeAppsList(const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return {};
  }

  auto lines = QString::fromUtf8(file.readAll()).split('\n');
  while (!lines.isEmpty() && lines.back().isEmpty()) {
    lines.removeLast();
  }
  return lines;
}

int findSection(const QStringList& lines, const QString& sectionName)
{
  for (int i = 0; i < lines.size(); ++i) {
    if (lines.at(i).trimmed() == sectionName) {
      return i;
    }
  }
  return -1;
}

int sectionEnd(const QStringList& lines, int sectionStart)
{
  for (int i = sectionStart + 1; i < lines.size(); ++i) {
    if (lines.at(i).trimmed().startsWith('[')) {
      return i;
    }
  }
  return lines.size();
}

template <typename Update>
void updateMimeSection(QStringList& lines, const QString& sectionName,
                       const QString& mimeType, Update update, bool createSection)
{
  int sectionStart = findSection(lines, sectionName);
  if (sectionStart < 0) {
    if (!createSection) {
      return;
    }

    if (!lines.isEmpty() && !lines.back().isEmpty()) {
      lines.append(QString());
    }
    sectionStart = lines.size();
    lines.append(sectionName);
  }

  const int end = sectionEnd(lines, sectionStart);
  for (int i = sectionStart + 1; i < end; ++i) {
    const QString item = lines.at(i).trimmed();
    if (!item.startsWith(mimeType + "=")) {
      continue;
    }

    const QStringList updated = update(desktopFilesFromEntry(item));
    if (updated.isEmpty()) {
      lines.removeAt(i);
    } else {
      lines[i] = desktopFilesEntry(mimeType, updated);
    }
    return;
  }

  const QStringList updated = update(QStringList{});
  if (!updated.isEmpty()) {
    lines.insert(sectionStart + 1, desktopFilesEntry(mimeType, updated));
  }
}

void prependDesktopFile(QStringList& desktopFiles, const QString& desktopFile)
{
  desktopFiles.removeAll(desktopFile);
  desktopFiles.prepend(desktopFile);
}

void removeDesktopFile(QStringList& desktopFiles, const QString& desktopFile)
{
  desktopFiles.removeAll(desktopFile);
}

void runDesktopCommand(const QString& program, const QStringList& arguments)
{
  QProcess proc;
  proc.setProgram(program);
  proc.setArguments(arguments);
  proc.setProcessChannelMode(QProcess::ForwardedChannels);

  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  auto restoreOrStrip = [&env](const QString& var, const QString& origVar) {
    if (env.contains(origVar)) {
      const QString orig = env.value(origVar);
      if (orig.isEmpty()) {
        env.remove(var);
      } else {
        env.insert(var, orig);
      }
      env.remove(origVar);
    }
  };

  restoreOrStrip(QStringLiteral("LD_LIBRARY_PATH"),
                 QStringLiteral("FLUORINE_ORIG_LD_LIBRARY_PATH"));
  restoreOrStrip(QStringLiteral("LD_PRELOAD"),
                 QStringLiteral("FLUORINE_ORIG_LD_PRELOAD"));
  restoreOrStrip(QStringLiteral("PATH"), QStringLiteral("FLUORINE_ORIG_PATH"));
  restoreOrStrip(QStringLiteral("XDG_DATA_DIRS"),
                 QStringLiteral("FLUORINE_ORIG_XDG_DATA_DIRS"));
  restoreOrStrip(QStringLiteral("QT_PLUGIN_PATH"),
                 QStringLiteral("FLUORINE_ORIG_QT_PLUGIN_PATH"));

  QStringList toolDirs;
  auto addToolDir = [&toolDirs](const QString& path) {
    if (!path.isEmpty() && QDir(path).exists() && !toolDirs.contains(path)) {
      toolDirs.append(path);
    }
  };

  const QString baseDir = env.value(QStringLiteral("MO2_BASE_DIR"));
  addToolDir(env.value(QStringLiteral("MO2_LIBS_DIR")));
  if (!baseDir.isEmpty()) {
    addToolDir(QDir(baseDir).filePath(QStringLiteral("lib")));
    addToolDir(baseDir);
  }
  const QString appDir = QCoreApplication::applicationDirPath();
  addToolDir(QDir(appDir).filePath(QStringLiteral("lib")));
  addToolDir(appDir);
  if (!toolDirs.isEmpty()) {
    const QString path = env.value(QStringLiteral("PATH"));
    env.insert(QStringLiteral("PATH"),
               toolDirs.join(QLatin1Char(':')) +
                   (path.isEmpty() ? QString() : QStringLiteral(":") + path));
  }

  env.remove(QStringLiteral("QT_QPA_PLATFORM_PLUGIN_PATH"));
  proc.setProcessEnvironment(env);

  proc.start();
  if (!proc.waitForStarted()) {
    log::debug("{} is not available", program);
    return;
  }

  proc.waitForFinished();
  const int result = proc.exitCode();
  if (result == -2) {
    log::debug("{} is not available", program);
  } else if (result != 0) {
    log::warn("{} exited with code {}", program, result);
  }
}

void refreshDesktopAssociationCaches(const QString& appsDir)
{
  runDesktopCommand(QStringLiteral("update-desktop-database"), QStringList{appsDir});
  runDesktopCommand(QStringLiteral("xdg-desktop-menu"),
                    QStringList{QStringLiteral("forceupdate")});
}

// Older releases also wrote user preferences beside the desktop files. That
// mimeapps.list location is deprecated, but clean up our entries when the file
// already exists so upgrading users are not left with conflicting defaults.
// Never create the deprecated file as part of cleanup.
void cleanLegacyMimeAppsList(const QString& path)
{
  if (!QFileInfo::exists(path)) {
    return;
  }

  QStringList lines = readMimeAppsList(path);
  const QStringList sections = {
      QStringLiteral("[Default Applications]"),
      QStringLiteral("[Added Associations]"),
      QStringLiteral("[Removed Associations]"),
  };

  for (const auto& scheme : UrlSchemes) {
    for (const auto& section : sections) {
      updateMimeSection(
          lines, section, scheme,
          [](QStringList desktopFiles) {
            removeDesktopFile(desktopFiles, NxmDesktopFile);
            removeDesktopFile(desktopFiles, LegacyNxmDesktopFile);
            return desktopFiles;
          },
          false);
    }
  }

  writeTextFile(path, lines.join('\n') + "\n");
}

// xdg-desktop-portal remembers chooser picks in its permission store. An
// earlier build registered both com.fluorine.manager.desktop and
// mo2-nxm-handler.desktop for nxm:// — anyone who picked Fluorine Manager
// from the chooser had it persisted as their always-use app, which kept
// routing nxm:// to the wrong handler (full MO2 launch with no URL) even
// after the bad MimeType was removed. Strip known stale app IDs so existing
// users self-heal on next launch or when they re-associate links.
void clearStalePortalChoice(const QString& mimeType)
{
  const QStringList staleAppIds = {
      QStringLiteral("com.fluorine.manager"),
      QStringLiteral("mo2-nxm-handler"),
      QStringLiteral("ModOrganizer"),
      QStringLiteral("modorganizer"),
      QStringLiteral("vortex"),
      QStringLiteral("Vortex"),
      QStringLiteral("com.nexusmods.vortex"),
      QStringLiteral("nexusmods-vortex"),
  };

  for (const auto& appId : staleAppIds) {
    QDBusMessage msg = QDBusMessage::createMethodCall(
        "org.freedesktop.impl.portal.PermissionStore",
        "/org/freedesktop/impl/portal/PermissionStore",
        "org.freedesktop.impl.portal.PermissionStore", "DeletePermission");
    msg << QStringLiteral("desktop-used-apps") << mimeType << appId;

    // Fire-and-forget on the session bus. The reply is uninteresting: a missing
    // entry returns an error and we don't want to log on every clean startup.
    QDBusConnection::sessionBus().call(msg, QDBus::NoBlock);
  }
}

void updateMimeAppsList(const QString& path, const QString& mimeType,
                        const QString& desktopFile)
{
  QStringList lines = readMimeAppsList(path);

  updateMimeSection(
      lines, QStringLiteral("[Default Applications]"), mimeType,
      [&](QStringList) {
        return QStringList{desktopFile};
      },
      true);

  updateMimeSection(
      lines, QStringLiteral("[Added Associations]"), mimeType,
      [&](QStringList desktopFiles) {
        removeDesktopFile(desktopFiles, LegacyNxmDesktopFile);
        prependDesktopFile(desktopFiles, desktopFile);
        return desktopFiles;
      },
      true);

  updateMimeSection(
      lines, QStringLiteral("[Removed Associations]"), mimeType,
      [&](QStringList desktopFiles) {
        removeDesktopFile(desktopFiles, desktopFile);
        removeDesktopFile(desktopFiles, LegacyNxmDesktopFile);
        return desktopFiles;
      },
      false);

  writeTextFile(path, lines.join('\n') + "\n");
}

void removeMimeAppsAssociation(const QString& path, const QString& mimeType,
                               const QString& desktopFile)
{
  QStringList lines = readMimeAppsList(path);

  updateMimeSection(
      lines, QStringLiteral("[Default Applications]"), mimeType,
      [&](QStringList desktopFiles) {
        removeDesktopFile(desktopFiles, desktopFile);
        removeDesktopFile(desktopFiles, LegacyNxmDesktopFile);
        return desktopFiles;
      },
      false);

  updateMimeSection(
      lines, QStringLiteral("[Added Associations]"), mimeType,
      [&](QStringList desktopFiles) {
        removeDesktopFile(desktopFiles, desktopFile);
        removeDesktopFile(desktopFiles, LegacyNxmDesktopFile);
        return desktopFiles;
      },
      false);

  updateMimeSection(
      lines, QStringLiteral("[Removed Associations]"), mimeType,
      [&](QStringList desktopFiles) {
        prependDesktopFile(desktopFiles, desktopFile);
        prependDesktopFile(desktopFiles, LegacyNxmDesktopFile);
        return desktopFiles;
      },
      true);

  writeTextFile(path, lines.join('\n') + "\n");
}
}  // namespace

void NxmHandlerLinux::registerHandler()
{
  const QString home = QDir::homePath();
  if (home.isEmpty()) {
    log::error("cannot register nxm handler: home path is empty");
    return;
  }

  const QString appsDir    = ensureDir(home + "/.local/share/applications");
  const QString configDir  = ensureDir(home + "/.config");

  if (appsDir.isEmpty() || configDir.isEmpty()) {
    log::error("cannot register nxm handler: failed to create required directories");
    return;
  }

  // Create a wrapper script and point the desktop file at it
  const QString localBin = ensureDir(home + "/.local/bin");
  if (localBin.isEmpty()) {
    log::error("cannot register nxm handler: failed to create ~/.local/bin");
    return;
  }

  const QString wrapperPath = localBin + "/mo2-nxm-handler";

  // Always prefer the packaged launcher over the bare core executable. The
  // launcher configures the bundled Qt platform/plugin and library paths;
  // desktop portals start handlers with a clean environment, so invoking
  // ModOrganizer-core directly makes cold NXM launches fail before main().
  const QString appDir = QCoreApplication::applicationDirPath();
  const QFileInfo launcherInfo(QDir(appDir).filePath("fluorine-manager"));
  const QString executable =
      launcherInfo.exists() && launcherInfo.isExecutable()
          ? launcherInfo.absoluteFilePath()
          : QCoreApplication::applicationFilePath();

  // Prefer the lightweight authenticated primary-process handoff when
  // Fluorine is already running. If no primary accepts it, start Fluorine
  // normally with the URL; normal startup selects the last-used instance and
  // CommandLine begins the download once that instance is ready.
  const QString wrapper =
      QString("#!/bin/sh\n"
              "url=$1\n"
              "[ -n \"$url\" ] || exit 2\n"
              "if \"%1\" nxm-handle \"$url\"; then\n"
              "  exit 0\n"
              "fi\n"
              "exec \"%1\" \"$url\"\n")
          .arg(executable);

  if (!writeTextFile(wrapperPath, wrapper)) {
    log::error("failed to write nxm wrapper script '{}'", wrapperPath);
    return;
  }

  QFile::setPermissions(wrapperPath,
                        QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                            QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                            QFileDevice::ExeGroup | QFileDevice::ReadOther |
                            QFileDevice::ExeOther);

  // Use the absolute path — ~/.local/bin is often not in PATH when the
  // browser or desktop environment invokes the URL scheme handler.
  const QString execLine = wrapperPath + " %u";

  const QString desktopPath = appsDir + "/" + NxmDesktopFile;
  const QString desktop = QString("[Desktop Entry]\n"
                                  "Type=Application\n"
                                  "Name=Fluorine Manager NXM Handler\n"
                                  "Exec=%1\n"
                                  "MimeType=x-scheme-handler/nxm;x-scheme-handler/modl;\n"
                                  "NoDisplay=true\n").arg(execLine);

  if (!writeTextFile(desktopPath, desktop)) {
    log::error("failed to write nxm desktop entry '{}'", desktopPath);
    return;
  }

  QFile::remove(appsDir + "/" + LegacyNxmDesktopFile);

  cleanLegacyMimeAppsList(appsDir + "/mimeapps.list");

  for (const auto& scheme : UrlSchemes) {
    updateMimeAppsList(configDir + "/mimeapps.list", scheme, NxmDesktopFile);
  }

  refreshDesktopAssociationCaches(appsDir);

  for (const auto& scheme : UrlSchemes) {
    clearStalePortalChoice(scheme);
  }
}

void NxmHandlerLinux::unregisterHandler()
{
  const QString home = QDir::homePath();
  if (home.isEmpty()) {
    log::error("cannot remove nxm handler: home path is empty");
    return;
  }

  const QString appsDir   = ensureDir(home + "/.local/share/applications");
  const QString configDir = ensureDir(home + "/.config");

  if (appsDir.isEmpty() || configDir.isEmpty()) {
    log::error("cannot remove nxm handler: failed to create required directories");
    return;
  }

  for (const auto& scheme : UrlSchemes) {
    removeMimeAppsAssociation(configDir + "/mimeapps.list", scheme, NxmDesktopFile);
  }

  cleanLegacyMimeAppsList(appsDir + "/mimeapps.list");

  QFile::remove(appsDir + "/" + NxmDesktopFile);
  QFile::remove(appsDir + "/" + LegacyNxmDesktopFile);

  refreshDesktopAssociationCaches(appsDir);

  for (const auto& scheme : UrlSchemes) {
    clearStalePortalChoice(scheme);
  }
}
