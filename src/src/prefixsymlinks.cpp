#include "prefixsymlinks.h"
#include "gamedetection.h"
#include "prefixsymlinktransaction.h"
#include "steamappinfo.h"

#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <uibase/log.h>

void ensureTempDirectory(const QString &prefixPath) {
  QString error;
  if (!ensureTempDirectoryChecked(prefixPath, &error)) {
    MOBase::log::warn("Failed to create the prefix Temp directory: {}", error);
  }
}

bool ensureTempDirectoryChecked(const QString &prefixPath, QString *error) {
  QString detail;
  const bool success =
      PrefixSymlinkTransaction::ensureTempDirectory(prefixPath, detail);
  if (error != nullptr) {
    *error = detail;
  }
  if (success) {
    MOBase::log::info("Ensured AppData/Local/Temp directory exists");
  }
  return success;
}

bool createGameSymlinksAutoChecked(const QString &prefixPath, QString *error) {
  GameScanResult result = detectAllGames();

  // Sort detected games so that actual Games win over Tools/Editors when
  // two prefixes share a folder name (e.g. Skyrim SE vs Creation Kit both
  // have "My Games/Skyrim Special Edition"). Without this, scanAndLinkAll
  // would pick whichever appeared first and shadow the real game.
  // Locate Steam install (mirrors the path list in findSteamInstallations()).
  QString steamPath;
  {
    const QString home = QDir::homePath();
    static const char *PATHS[] = {
        ".local/share/Steam",
        ".steam/debian-installation",
        ".steam/steam",
        ".var/app/com.valvesoftware.Steam/data/Steam",
        ".var/app/com.valvesoftware.Steam/.local/share/Steam",
        "snap/steam/common/.local/share/Steam",
    };
    for (const char *rel : PATHS) {
      const QString full = QDir(home).filePath(QString::fromLatin1(rel));
      if (QFileInfo::exists(full + "/appcache/appinfo.vdf")) {
        steamPath = full;
        break;
      }
    }
  }
  const QHash<quint32, SteamAppInfo> &appInfo =
      steamPath.isEmpty() ? QHash<quint32, SteamAppInfo>{}
                          : loadSteamAppInfo(steamPath);

  auto appType = [&appInfo](const QString &appIdStr) -> QString {
    bool ok = false;
    const quint32 id = appIdStr.toUInt(&ok);
    if (!ok)
      return {};
    const auto it = appInfo.constFind(id);
    return it == appInfo.constEnd() ? QString{} : it->type;
  };

  std::vector<DetectedGame> ranked(result.games.begin(), result.games.end());
  std::stable_sort(ranked.begin(), ranked.end(),
                   [&](const DetectedGame &a, const DetectedGame &b) {
                     const int aRank = steamAppTypeRank(appType(a.app_id));
                     const int bRank = steamAppTypeRank(appType(b.app_id));
                     return aRank < bRank;
                   });

  QVector<PrefixSymlinkTransaction::Candidate> candidates;
  candidates.reserve(static_cast<qsizetype>(ranked.size()));
  for (const DetectedGame &game : ranked) {
    candidates.push_back({game.prefix_path, game.name});
  }

  const PrefixSymlinkTransaction::Result applied =
      PrefixSymlinkTransaction::apply(prefixPath, candidates);
  if (error != nullptr) {
    *error = applied.error;
  }
  if (!applied.success) {
    return false;
  }

  if (applied.created > 0) {
    MOBase::log::info("Created {} symlinks to game prefixes", applied.created);
  }
  MOBase::log::info("Adopted {} existing links and preserved {} real leaves",
                    applied.adopted, applied.preserved);
  return true;
}

void createGameSymlinksAuto(const QString &prefixPath) {
  QString error;
  if (!createGameSymlinksAutoChecked(prefixPath, &error)) {
    MOBase::log::warn("Failed to create game-prefix links: {}", error);
  }
}
