#include "prefixsymlinks.h"
#include "gamedetection.h"
#include "steamappinfo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <uibase/log.h>
#include <unistd.h>

namespace {

constexpr auto kSkyrimSpecialEditionDirectory = "Skyrim Special Edition";
constexpr auto kLegacyLinkDirectory = ".fluorine/legacy-links";
constexpr auto kLegacyLinkName = "skyrim-special-edition-appdata.link";

static const char* SKIP_DIRS[] = {
    "Temp", "Microsoft", "wine", "Public", "root",
    "Application Data", "Cookies", "Local Settings",
    "NetHood", "PrintHood", "Recent", "SendTo",
    "Start Menu", "Templates", "My Documents", "My Music",
    "My Pictures", "My Videos", "Desktop", "Downloads",
    "Favorites", "Links", "Searches",
    "Contacts", "3D Objects",
};

// Folders under AppData that are written by launchers, anti-cheat, GPU
// drivers and Wine itself rather than by one game. Every Steam prefix has its
// own copy, so linking the first one found would route data from all other
// games into an unrelated game's compatdata (which Steam deletes on uninstall).
static const char* SHARED_APPDATA_DIRS[] = {
    "CrashReportClient", "dxvk", "dxvk-cache", "vkd3d-proton", "Epic Games",
    "EpicGamesLauncher", "NVIDIA", "NVIDIA Corporation", "UnrealEngine",
    "EasyAntiCheat", "EasyAntiCheat_EOS", "BattlEye", "D3DSCache", "CrashDumps",
    "Programs", "Packages", "Steam", "Ubisoft Game Launcher", "Battle.net",
    "Google", "Mozilla", "Temp", "cache", "Cache", "mesa_shader_cache",
    "AMD", "Intel", "Unity", "UnityLogs", "Overwolf", "discord",
    "mod.io", "IO Interactive",
};

bool shouldSkip(const QString& name)
{
  for (const char* s : SKIP_DIRS) {
    if (name.compare(QLatin1String(s), Qt::CaseInsensitive) == 0)
      return true;
  }
  return false;
}

bool isSharedAppDataDir(const QString& name)
{
  for (const char* s : SHARED_APPDATA_DIRS) {
    if (name.compare(QLatin1String(s), Qt::CaseInsensitive) == 0)
      return true;
  }
  return false;
}

bool isPathEntryPresent(const QString& path)
{
  const QFileInfo info(path);
  // QFileInfo::exists() is false for a dangling link, but a dangling link is
  // still an entry that must not be overwritten or discarded.
  return info.exists() || info.isSymLink();
}

QString nextLegacyLinkPath(const QString& prefixPath)
{
  const QDir backupDir(QDir(prefixPath).filePath(kLegacyLinkDirectory));
  const QString base = backupDir.filePath(kLegacyLinkName);
  if (!isPathEntryPresent(base) &&
      !isPathEntryPresent(base + QStringLiteral(".target")))
    return base;

  for (int index = 1; index < 10000; ++index) {
    const QString candidate =
        backupDir.filePath(QStringLiteral("%1.%2").arg(kLegacyLinkName).arg(index));
    if (!isPathEntryPresent(candidate) &&
        !isPathEntryPresent(candidate + QStringLiteral(".target")))
      return candidate;
  }

  return {};
}

/// Read the literal link target. QFileInfo::symLinkTarget() may normalize a
/// relative target; retaining the literal is needed if a failed migration has
/// to restore the link at its original parent directory.
QString readSymlinkTargetLiteral(const QString& path)
{
  const QByteArray encodedPath = QFile::encodeName(path);
  QByteArray buffer(256, '\0');

  for (;;) {
    const ssize_t length = ::readlink(encodedPath.constData(), buffer.data(),
                                      static_cast<size_t>(buffer.size()));
    if (length < 0)
      return {};
    if (length < buffer.size()) {
      buffer.truncate(static_cast<int>(length));
      return QFile::decodeName(buffer);
    }
    if (buffer.size() >= 1024 * 1024)
      return {};
    buffer.resize(buffer.size() * 2);
  }
}

/// True when any ancestor is a symlink that resolves outside `prefixRoot`.
/// The prefix root itself may be a symlink (umu creates `pfx -> <prefix>`, and
/// users often relocate prefixes with one); only links *below* it that leave
/// the prefix could redirect the migration onto files Fluorine does not own.
bool hasEscapingSymlinkAncestor(const QString& prefixRoot,
                                const QStringList& ancestorsBelowRoot)
{
  const QString canonicalRoot = QFileInfo(prefixRoot).canonicalFilePath();
  if (canonicalRoot.isEmpty())
    return true;
  const QString rootWithSeparator =
      canonicalRoot.endsWith(QLatin1Char('/')) ? canonicalRoot
                                               : canonicalRoot + QLatin1Char('/');

  for (const QString& ancestor : ancestorsBelowRoot) {
    const QFileInfo info(ancestor);
    if (!info.isSymLink())
      continue;
    const QString resolved = info.canonicalFilePath();
    if (resolved.isEmpty())
      return true;  // dangling link
    if (resolved != canonicalRoot && !resolved.startsWith(rootWithSeparator))
      return true;
  }
  return false;
}

bool writeLinkMetadata(const QString& path, const QString& target)
{
  QFile metadata(path);
  if (!metadata.open(QIODevice::WriteOnly | QIODevice::NewOnly | QIODevice::Text))
    return false;
  const QByteArray encodedTarget = target.toUtf8();
  bool complete = metadata.write(encodedTarget) == encodedTarget.size();
  if (complete)
    complete = metadata.flush();
  if (metadata.error() != QFileDevice::NoError)
    complete = false;
  metadata.close();
  // NewOnly guarantees that this migration created the sidecar. Remove a
  // partial file so a later setup can retry instead of treating it as a
  // reserved rollback slot.
  if (!complete)
    QFile::remove(path);
  return complete;
}

/// Find the username directory inside drive_c/users/.
QString findPrefixUsername(const QString& usersDir)
{
  QDir dir(usersDir);
  for (const QString& entry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    if (entry != QStringLiteral("Public") && entry != QStringLiteral("root"))
      return entry;
  }
  return QStringLiteral("steamuser");
}

/// Create a symlink. If a stale symlink already points elsewhere and
/// `replaceStale` is true, replace it. Never clobbers a real directory.
bool createSymlinkIfNeeded(const QString& linkPath, const QString& target,
                           bool replaceStale)
{
  QFileInfo fi(linkPath);
  if (fi.isSymLink()) {
    if (fi.symLinkTarget() == target)
      return true;
    if (!replaceStale)
      return false;
    QFile::remove(linkPath);
  } else if (fi.exists()) {
    return false;  // real directory — don't clobber
  }

  QDir().mkpath(QFileInfo(linkPath).absolutePath());
  if (symlink(target.toUtf8().constData(), linkPath.toUtf8().constData()) != 0) {
    MOBase::log::warn("Failed to create symlink {} -> {}", linkPath, target);
    return false;
  }
  return true;
}

/// Remove links created by earlier versions that point a shared (non-game)
/// AppData folder into a Steam compatdata prefix. Only symlinks are removed;
/// their targets are never touched.
void removeSharedAppDataLinks(const QString& nakBase)
{
  const QDir dir(nakBase);
  for (const QFileInfo& entry :
       dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System)) {
    if (!entry.isSymLink() || !isSharedAppDataDir(entry.fileName()))
      continue;
    const QString target = readSymlinkTargetLiteral(entry.absoluteFilePath());
    if (!target.contains(QLatin1String("/compatdata/")))
      continue;
    if (QFile::remove(entry.absoluteFilePath()))
      MOBase::log::info("Removed shared AppData link {} -> {}",
                        entry.absoluteFilePath(), target);
  }
}

/// Scan all subdirectories in gameBase and symlink them into nakBase.
/// `replaceStale` — when true, stale symlinks (e.g. left from a previous
/// run pointing to a lower-priority prefix) get overwritten.
int scanAndLinkAll(const QString& nakBase, const QString& gameBase,
                   const QString& label, const QString& gameName,
                   bool skipMyGames = false, bool replaceStale = false)
{
  QDir dir(gameBase);
  if (!dir.exists())
    return 0;

  int count = 0;
  for (const QString& folder : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    if (shouldSkip(folder))
      continue;
    // Skyrim's AppData/Local catalog is tied to the game runtime/content
    // version. Sharing it between Steam and Fluorine prefixes can make
    // Skyrim 1.6.1170 parse a newer CSV2 catalog and throw invalid stoull.
    // Documents/My Games remains linked so the existing save/profile flow is
    // unchanged; this exclusion is deliberately limited to AppData/Local.
    if (label.compare(QStringLiteral("AppData/Local"), Qt::CaseInsensitive) == 0 &&
        folder.compare(QLatin1String(kSkyrimSpecialEditionDirectory),
                       Qt::CaseInsensitive) == 0) {
      MOBase::log::info("Not linking AppData/Local/{}; keeping it private per "
                        "prefix", folder);
      continue;
    }
    if (skipMyGames && folder == QStringLiteral("My Games"))
      continue;
    if (label.startsWith(QLatin1String("AppData/"), Qt::CaseInsensitive) &&
        isSharedAppDataDir(folder))
      continue;

    const QString linkPath = nakBase + "/" + folder;
    const QString target   = gameBase + "/" + folder;

    if (createSymlinkIfNeeded(linkPath, target, replaceStale)) {
      MOBase::log::info("Linked {}/{} -> {} ({})", label, folder, target, gameName);
      ++count;
    }
  }
  return count;
}

}  // namespace

void ensureTempDirectory(const QString& prefixPath)
{
  const QString usersDir = prefixPath + "/drive_c/users";
  const QString username = findPrefixUsername(usersDir);
  const QString tempDir  = usersDir + "/" + username + "/AppData/Local/Temp";

  if (QDir().mkpath(tempDir))
    MOBase::log::info("Ensured AppData/Local/Temp directory exists");
  else
    MOBase::log::warn("Failed to create Temp directory at {}", tempDir);
}

bool ensureSkyrimSpecialEditionAppDataPrivate(const QString& prefixPath)
{
  const QString prefixRoot = QDir(prefixPath).absolutePath();
  const QString driveC = QDir(prefixRoot).filePath("drive_c");
  const QString usersDir = QDir(driveC).filePath("users");
  const QString username = findPrefixUsername(usersDir);
  const QString userDir = QDir(usersDir).filePath(username);
  const QString appDataDir = QDir(userDir).filePath("AppData");
  const QString appDataLocal =
      QDir(appDataDir).filePath("Local");
  const QString skyrimPath =
      QDir(appDataLocal).filePath(QLatin1String(kSkyrimSpecialEditionDirectory));

  // A symlink in any ancestor that leaves the prefix would make the migration
  // operate outside the managed prefix. Refuse before moving the
  // game-directory link; the caller can repair the prefix layout explicitly
  // without risking external files. Links that stay inside the prefix (and a
  // symlinked prefix root, as umu uses) are harmless.
  if (hasEscapingSymlinkAncestor(prefixRoot, {driveC, usersDir, userDir,
                                              appDataDir, appDataLocal})) {
    MOBase::log::error("Refusing Skyrim AppData migration because an ancestor "
                       "is a symlink leaving the prefix (path '{}')", skyrimPath);
    return false;
  }

  const QFileInfo existing(skyrimPath);
  if (existing.isSymLink()) {
    const QString backupPath = nextLegacyLinkPath(prefixPath);
    if (backupPath.isEmpty()) {
      MOBase::log::error("Unable to reserve a rollback path for legacy Skyrim "
                         "AppData link '{}'", skyrimPath);
      return false;
    }

    if (!QDir().mkpath(QFileInfo(backupPath).absolutePath())) {
      MOBase::log::error("Unable to create rollback directory for legacy Skyrim "
                         "AppData link '{}'", backupPath);
      return false;
    }

    const QString originalTarget = readSymlinkTargetLiteral(skyrimPath);
    if (originalTarget.isEmpty()) {
      MOBase::log::error("Unable to read legacy Skyrim AppData link '{}'; "
                         "leaving it unchanged", skyrimPath);
      return false;
    }

    // A relative link changes meaning if it is merely renamed into the
    // rollback directory. Recreate the backup with an absolute equivalent and
    // retain the literal target in a sidecar for exact rollback.
    const QString backupTarget = QDir::isAbsolutePath(originalTarget)
        ? originalTarget
        : QDir(QFileInfo(skyrimPath).absolutePath()).absoluteFilePath(originalTarget);
    if (!QFile::link(backupTarget, backupPath)) {
      MOBase::log::error("Unable to preserve legacy Skyrim AppData link '{}' "
                         "as '{}'", skyrimPath, backupPath);
      return false;
    }

    const QString metadataPath = backupPath + QStringLiteral(".target");
    if (!writeLinkMetadata(metadataPath, originalTarget)) {
      QFile::remove(backupPath);
      MOBase::log::error("Unable to record legacy Skyrim AppData link target; "
                         "leaving '{}' unchanged", skyrimPath);
      return false;
    }

    // Remove the link itself, never the directory it targets. This preserves
    // the Steam catalog and every other target file byte-for-byte.
    if (!QFile::remove(skyrimPath)) {
      QFile::remove(metadataPath);
      QFile::remove(backupPath);
      MOBase::log::error("Unable to preserve legacy Skyrim AppData link '{}' "
                         "as '{}'", skyrimPath, backupPath);
      return false;
    }

    if (!QDir().mkpath(skyrimPath)) {
      // Roll back while the destination is still absent. If rollback itself
      // fails, the original link remains safely stored at backupPath and the
      // target is still untouched.
      if (!isPathEntryPresent(skyrimPath) &&
          QFile::link(originalTarget, skyrimPath)) {
        QFile::remove(metadataPath);
        QFile::remove(backupPath);
        MOBase::log::error("Unable to create private Skyrim AppData directory; "
                           "restored legacy link '{}'", skyrimPath);
      } else {
        MOBase::log::error("Unable to create private Skyrim AppData directory; "
                           "legacy link preserved at '{}'", backupPath);
      }
      return false;
    }

    MOBase::log::info("Migrated shared Skyrim AppData link '{}' to private "
                      "directory; rollback link saved at '{}'", skyrimPath,
                      backupPath);
    return true;
  }

  if (existing.exists()) {
    if (existing.isDir()) {
      // A real directory is user-owned prefix data. Leave it and all of its
      // contents intact, including any catalog it may already contain.
      MOBase::log::debug("Skyrim AppData path '{}' is already a private "
                         "directory", skyrimPath);
      return true;
    }

    MOBase::log::error("Skyrim AppData path '{}' exists but is not a directory; "
                       "refusing to overwrite it", skyrimPath);
    return false;
  }

  if (!QDir().mkpath(skyrimPath)) {
    MOBase::log::error("Unable to create private Skyrim AppData directory '{}'",
                       skyrimPath);
    return false;
  }

  MOBase::log::info("Created private Skyrim AppData directory '{}'", skyrimPath);
  return true;
}

void createGameSymlinksAuto(const QString& prefixPath, bool linkSaveData)
{
  GameScanResult result;
  if (linkSaveData)
    result = detectAllGames();
  else
    MOBase::log::info("Skipping save data links to Steam prefixes (disabled)");

  const QString usersDir = prefixPath + "/drive_c/users";
  const QString username = findPrefixUsername(usersDir);
  const QString userDir  = usersDir + "/" + username;
  const QString documents    = userDir + "/Documents";
  const QString myGames      = documents + "/My Games";
  const QString appdataLocal = userDir + "/AppData/Local";
  const QString appdataRoaming = userDir + "/AppData/Roaming";

  QDir().mkpath(myGames);
  QDir().mkpath(appdataLocal);
  QDir().mkpath(appdataRoaming);

  // Sort detected games so that actual Games win over Tools/Editors when
  // two prefixes share a folder name (e.g. Skyrim SE vs Creation Kit both
  // have "My Games/Skyrim Special Edition"). Without this, scanAndLinkAll
  // would pick whichever appeared first and shadow the real game.
  // Locate Steam install (mirrors the path list in findSteamInstallations()).
  QString steamPath;
  {
    const QString home = QDir::homePath();
    static const char* PATHS[] = {
        ".local/share/Steam", ".steam/debian-installation", ".steam/steam",
        ".var/app/com.valvesoftware.Steam/data/Steam",
        ".var/app/com.valvesoftware.Steam/.local/share/Steam",
        "snap/steam/common/.local/share/Steam",
    };
    for (const char* rel : PATHS) {
      const QString full = QDir(home).filePath(QString::fromLatin1(rel));
      if (QFileInfo::exists(full + "/appcache/appinfo.vdf")) {
        steamPath = full;
        break;
      }
    }
  }
  const QHash<quint32, SteamAppInfo>& appInfo =
      steamPath.isEmpty() ? QHash<quint32, SteamAppInfo>{}
                          : loadSteamAppInfo(steamPath);

  auto appType = [&appInfo](const QString& appIdStr) -> QString {
    bool ok = false;
    const quint32 id = appIdStr.toUInt(&ok);
    if (!ok)
      return {};
    const auto it = appInfo.constFind(id);
    return it == appInfo.constEnd() ? QString{} : it->type;
  };

  std::vector<DetectedGame> ranked(result.games.begin(), result.games.end());
  std::stable_sort(ranked.begin(), ranked.end(),
                   [&](const DetectedGame& a, const DetectedGame& b) {
                     return steamAppTypeRank(appType(a.app_id))
                            < steamAppTypeRank(appType(b.app_id));
                   });

  removeSharedAppDataLinks(appdataLocal);
  removeSharedAppDataLinks(appdataRoaming);

  int linked = 0;
  bool first = true;
  for (const DetectedGame& game : ranked) {
    if (game.prefix_path.isEmpty())
      continue;

    const QString gameUsersDir = game.prefix_path + "/drive_c/users";
    const QString gameUsername = findPrefixUsername(gameUsersDir);
    const QString gameUserDir  = gameUsersDir + "/" + gameUsername;

    // Only the highest-ranked candidate is allowed to replace stale symlinks
    // from previous runs. Lower-ranked tools/demos must not overwrite real
    // games' links even if they happen to share a folder name.
    const bool replaceStale = first;
    first = false;

    linked += scanAndLinkAll(myGames, gameUserDir + "/Documents/My Games",
                             QStringLiteral("Documents/My Games"), game.name,
                             false, replaceStale);
    linked += scanAndLinkAll(documents, gameUserDir + "/Documents",
                             QStringLiteral("Documents"), game.name, true,
                             replaceStale);
    linked += scanAndLinkAll(appdataLocal, gameUserDir + "/AppData/Local",
                             QStringLiteral("AppData/Local"), game.name,
                             false, replaceStale);
    linked += scanAndLinkAll(appdataRoaming, gameUserDir + "/AppData/Roaming",
                             QStringLiteral("AppData/Roaming"), game.name,
                             false, replaceStale);
  }

  if (linked > 0)
    MOBase::log::info("Created {} symlinks to game prefixes", linked);

  // "My Documents" compat symlink.
  const QString myDocs = userDir + "/My Documents";
  if (!QFileInfo::exists(myDocs) && !QFileInfo(myDocs).isSymLink() &&
      symlink("Documents", myDocs.toUtf8().constData()) != 0) {
    MOBase::log::warn("Failed to create symlink {} -> Documents", myDocs);
  }
}
