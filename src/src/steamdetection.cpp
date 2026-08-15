#include "steamdetection.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>
#include <uibase/log.h>

#include "vdfparser.h"

// ============================================================================
// SteamProtonInfo
// ============================================================================

QString SteamProtonInfo::wineBinary() const
{
  for (const char* sub : {"files/bin/wine", "dist/bin/wine"}) {
    const QString p = QDir(path).filePath(QString::fromLatin1(sub));
    if (QFileInfo::exists(p))
      return p;
  }
  return {};
}

QString SteamProtonInfo::wineserverBinary() const
{
  for (const char* sub : {"files/bin/wineserver", "dist/bin/wineserver"}) {
    const QString p = QDir(path).filePath(QString::fromLatin1(sub));
    if (QFileInfo::exists(p))
      return p;
  }
  return {};
}

QString SteamProtonInfo::binDir() const
{
  const QString wine = wineBinary();
  return wine.isEmpty() ? QString() : QFileInfo(wine).absolutePath();
}

// ============================================================================
// Steam Path Detection
// ============================================================================

QString findSteamPath()
{
  const QString home = QDir::homePath();
  const QStringList candidates = {
      home + "/.steam/steam",
      home + "/.local/share/Steam",
      home + "/.var/app/com.valvesoftware.Steam/.steam/steam",
      home + "/snap/steam/common/.steam/steam",
  };
  for (const QString& p : candidates) {
    if (QFileInfo::exists(p))
      return p;
  }
  return {};
}

QStringList findSteamLibraryPaths()
{
  QStringList libraries;

  const QString steamPath = findSteamPath();
  if (steamPath.isEmpty())
    return libraries;

  auto addLibrary = [&](const QString& path) {
    const QString cleanPath = QDir::cleanPath(path);
    if (cleanPath.isEmpty()) {
      return;
    }

    const QFileInfo info(cleanPath);
    const QString canonicalPath = info.canonicalFilePath();
    const QString libraryPath =
        canonicalPath.isEmpty() ? info.absoluteFilePath() : canonicalPath;
    const QString normalizedLibraryPath = QDir::cleanPath(libraryPath);

    if (normalizedLibraryPath.isEmpty() || libraries.contains(normalizedLibraryPath)) {
      return;
    }
    if (QFileInfo::exists(QDir(normalizedLibraryPath).filePath(QStringLiteral("steamapps")))) {
      libraries.append(normalizedLibraryPath);
    }
  };

  addLibrary(steamPath);

  QFile libraryFolders(QDir(steamPath).filePath(
      QStringLiteral("steamapps/libraryfolders.vdf")));
  if (!libraryFolders.open(QIODevice::ReadOnly | QIODevice::Text)) {
    MOBase::log::debug("Steam libraryfolders.vdf not found at '{}'",
                       libraryFolders.fileName());
    return libraries;
  }

  const QString content = QString::fromUtf8(libraryFolders.readAll());
  for (const QString& path : parseLibraryFolders(content)) {
    addLibrary(path);
  }

  return libraries;
}

// ============================================================================
// Proton Detection
// ============================================================================

namespace {

/// Check if a Proton version is 10 or newer.
bool isProton10OrNewer(const SteamProtonInfo& p)
{
  if (p.is_experimental || p.name.contains(QStringLiteral("Experimental")))
    return true;

  if (p.name.contains(QStringLiteral("CachyOS")))
    return true;

  if (p.name == QStringLiteral("LegacyRuntime") ||
      p.name.contains(QStringLiteral("Runtime")))
    return false;

  auto extractMajor = [](const QString& s, QChar sep) -> int {
    bool ok = false;
    int v   = s.section(sep, 0, 0).toInt(&ok);
    return ok ? v : -1;
  };

  if (p.name.startsWith(QStringLiteral("GE-Proton"))) {
    int v = extractMajor(p.name.mid(9), '-');
    return v < 0 || v >= 10;
  }
  if (p.name.startsWith(QStringLiteral("Proton "))) {
    int v = extractMajor(p.name.mid(7), '.');
    return v < 0 || v >= 10;
  }
  if (p.name.startsWith(QStringLiteral("EM-"))) {
    int v = extractMajor(p.name.mid(3), '.');
    return v < 0 || v >= 10;
  }

  return true;  // unknown format — allow
}

/// Scan a directory for Proton installations.
void scanProtonDir(const QString& dir, bool isSteamBuiltin,
                   QVector<SteamProtonInfo>& out)
{
  QDir d(dir);
  if (!d.exists())
    return;

  const QStringList entries = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QString& name : entries) {
    const QString path = d.absoluteFilePath(name);

    bool hasProton = QFileInfo::exists(path + "/proton");
    bool hasVdf    = QFileInfo::exists(path + "/compatibilitytool.vdf");

    if (!hasProton && !hasVdf)
      continue;

    SteamProtonInfo info;
    info.name       = name;
    info.path       = path;
    info.is_steam_proton  = isSteamBuiltin;
    info.is_experimental  = name.contains(QStringLiteral("Experimental"));

    if (isSteamBuiltin) {
      if (info.is_experimental) {
        info.config_name = QStringLiteral("proton_experimental");
      } else {
        const QString version = QString(name).remove(QStringLiteral("Proton "));
        const QString major   = version.section('.', 0, 0);
        info.config_name      = QStringLiteral("proton_%1").arg(major);
      }
    } else {
      info.config_name = name;
    }

    out.append(std::move(info));
  }
}

}  // namespace

QVector<SteamProtonInfo>
findProtonsForPaths(const QStringList& steamLibraries,
                    const QStringList& compatibilityToolDirs)
{
  QVector<SteamProtonInfo> protons;

  // 1. Steam's built-in Protons across every Steam library. Steam may install
  // tools like "Proton 10.0" under a secondary library's steamapps/common.
  for (const QString& library : steamLibraries) {
    QVector<SteamProtonInfo> builtin;
    scanProtonDir(QDir(library).filePath(QStringLiteral("steamapps/common")), true,
                  builtin);
    for (auto& p : builtin) {
      if (p.name.startsWith(QStringLiteral("Proton"))) {
        protons.append(std::move(p));
      }
    }
  }

  // 2. Custom Protons in user's compatibilitytools.d.
  for (const QString& library : steamLibraries) {
    scanProtonDir(QDir(library).filePath(QStringLiteral("compatibilitytools.d")),
                  false, protons);
  }

  // 3. Other supported compatibility-tool directories (system and Heroic).
  for (const QString& directory : compatibilityToolDirs) {
    scanProtonDir(directory, false, protons);
  }

  // Filter to Proton 10+.
  protons.erase(
      std::remove_if(protons.begin(), protons.end(),
                     [](const SteamProtonInfo& p) { return !isProton10OrNewer(p); }),
      protons.end());

  // Filter to Protons with valid wine binaries.
  protons.erase(
      std::remove_if(protons.begin(), protons.end(),
                     [](const SteamProtonInfo& p) {
                       bool ok = !p.wineBinary().isEmpty();
                       if (!ok) {
                         MOBase::log::warn("Skipping Proton '{}': wine binary not found",
                                           p.name);
                       }
                       return !ok;
                     }),
      protons.end());

  QSet<QString> seenPaths;
  QVector<SteamProtonInfo> uniqueProtons;
  uniqueProtons.reserve(protons.size());

  for (SteamProtonInfo& proton : protons) {
    const QFileInfo pathInfo(proton.path);
    const QString canonicalPath = pathInfo.canonicalFilePath();
    const QString pathKey =
        QDir::cleanPath(canonicalPath.isEmpty() ? proton.path : canonicalPath);
    if (!pathKey.isEmpty() && seenPaths.contains(pathKey)) {
      MOBase::log::debug("Skipping duplicate Proton '{}' at '{}'",
                         proton.name, proton.path);
      continue;
    }

    if (!pathKey.isEmpty()) {
      seenPaths.insert(pathKey);
    }
    uniqueProtons.append(std::move(proton));
  }

  protons = std::move(uniqueProtons);

  // Sort: Experimental first, then by name descending (newest first).
  std::sort(protons.begin(), protons.end(),
            [](const SteamProtonInfo& a, const SteamProtonInfo& b) {
              if (a.is_experimental != b.is_experimental)
                return a.is_experimental > b.is_experimental;
              return a.name > b.name;
            });

  return protons;
}

QVector<SteamProtonInfo> findSteamProtons()
{
  const QString home = QDir::homePath();
  const QStringList compatibilityToolDirs = {
      QStringLiteral("/usr/share/steam/compatibilitytools.d"),
      QDir(home).filePath(QStringLiteral(".config/heroic/tools/proton")),
      QDir(home).filePath(QStringLiteral(
          ".var/app/com.heroicgameslauncher.hgl/config/heroic/tools/proton")),
  };

  // Do not require Steam to be installed: Heroic and system Proton runners
  // are valid inputs for Fluorine's direct Proton launcher.
  return findProtonsForPaths(findSteamLibraryPaths(), compatibilityToolDirs);
}
