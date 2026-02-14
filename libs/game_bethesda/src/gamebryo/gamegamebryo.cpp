#include "gamegamebryo.h"

#include <uibase/filesystemutilities.h>

#include "bsainvalidation.h"
#include "dataarchives.h"
#include "gamebryomoddatacontent.h"
#include "gamebryosavegame.h"
#include "gameplugins.h"
#include "iprofile.h"
#include "log.h"
#include "registry.h"
#include "savegameinfo.h"
#include "scopeguard.h"
#include "scriptextender.h"
#include "utility.h"
#include "vdf_parser.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

#include <QtDebug>
#include <QtGlobal>

#ifdef _WIN32
#include <Knownfolders.h>
#include <Shlobj.h>
#include <Windows.h>
#include <winreg.h>
#include <winver.h>
#endif

#ifdef HAS_NAK_FFI
#include <nak_ffi.h>
#endif

#include <optional>
#include <string>
#include <vector>

// Local resolveFileCaseInsensitive moved to MOBase::resolveFileCaseInsensitive
using MOBase::resolveFileCaseInsensitive;

GameGamebryo::GameGamebryo() {}

void GameGamebryo::detectGame()
{
  m_GamePath    = identifyGamePath();
  m_MyGamesPath = determineMyGamesPath(gameName());
}

bool GameGamebryo::init(MOBase::IOrganizer* moInfo)
{
  m_Organizer = moInfo;
  m_Organizer->onAboutToRun([this](const auto& binary) {
    return prepareIni(binary);
  });
  return true;
}

bool GameGamebryo::isInstalled() const
{
  return !m_GamePath.isEmpty();
}

QIcon GameGamebryo::gameIcon() const
{
  return MOBase::iconForExecutable(gameDirectory().absoluteFilePath(binaryName()));
}

QDir GameGamebryo::gameDirectory() const
{
  return QDir(m_GamePath);
}

QDir GameGamebryo::dataDirectory() const
{
  return gameDirectory().absoluteFilePath("Data");
}

void GameGamebryo::setGamePath(const QString& path)
{
  m_GamePath = path;
}

QDir GameGamebryo::documentsDirectory() const
{
  return m_MyGamesPath;
}

QDir GameGamebryo::savesDirectory() const
{
  return QDir(myGamesPath() + "/Saves");
}

std::vector<std::shared_ptr<const MOBase::ISaveGame>>
GameGamebryo::listSaves(QDir folder) const
{
  QStringList filters;
  filters << QString("*.") + savegameExtension();

  std::vector<std::shared_ptr<const MOBase::ISaveGame>> saves;
  for (auto info : folder.entryInfoList(filters, QDir::Files)) {
    try {
      saves.push_back(makeSaveGame(info.filePath()));
    } catch (std::exception& e) {
      MOBase::log::error("{}", e.what());
      continue;
    }
  }

  return saves;
}

void GameGamebryo::setGameVariant(const QString& variant)
{
  m_GameVariant = variant;
}

QString GameGamebryo::binaryName() const
{
  return gameShortName() + ".exe";
}

MOBase::IPluginGame::LoadOrderMechanism GameGamebryo::loadOrderMechanism() const
{
  return LoadOrderMechanism::FileTime;
}

MOBase::IPluginGame::SortMechanism GameGamebryo::sortMechanism() const
{
  return SortMechanism::LOOT;
}

bool GameGamebryo::looksValid(QDir const& path) const
{
  // Check for <prog>.exe for now.
  return path.exists(binaryName());
}

QString GameGamebryo::gameVersion() const
{
  // We try the file version, but if it looks invalid (starts with the fallback
  // version), we look the product version instead. If the product version is
  // not empty, we use it.
  QString binaryAbsPath = gameDirectory().absoluteFilePath(binaryName());
  QString version       = MOBase::getFileVersion(binaryAbsPath);
  if (version.startsWith(FALLBACK_GAME_VERSION)) {
    QString pversion = MOBase::getProductVersion(binaryAbsPath);
    if (!pversion.isEmpty()) {
      version = pversion;
    }
  }
  return version;
}

QString GameGamebryo::getLauncherName() const
{
  return gameShortName() + "Launcher.exe";
}

WORD GameGamebryo::getArch(QString const& program) const
{
#ifdef _WIN32
  WORD arch = 0;
  // This *really* needs to be factored out
  std::wstring app_name =
      L"\\\\?\\" +
      QDir::toNativeSeparators(this->gameDirectory().absoluteFilePath(program))
          .toStdWString();

  WIN32_FIND_DATAW FindFileData;
  HANDLE hFind = ::FindFirstFileW(app_name.c_str(), &FindFileData);

  // exit if the binary was not found
  if (hFind == INVALID_HANDLE_VALUE)
    return arch;

  HANDLE hFile            = INVALID_HANDLE_VALUE;
  HANDLE hMapping         = INVALID_HANDLE_VALUE;
  LPVOID addrHeader       = nullptr;
  PIMAGE_NT_HEADERS peHdr = nullptr;

  hFile = CreateFileW(app_name.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                      OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, NULL);
  if (hFile == INVALID_HANDLE_VALUE)
    goto cleanup;

  hMapping = CreateFileMappingW(hFile, NULL, PAGE_READONLY | SEC_IMAGE, 0, 0,
                                program.toStdWString().c_str());
  if (hMapping == INVALID_HANDLE_VALUE)
    goto cleanup;

  addrHeader = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
  if (addrHeader == NULL)
    goto cleanup;  // couldn't memory map the file

  peHdr = ImageNtHeader(addrHeader);
  if (peHdr == NULL)
    goto cleanup;  // couldn't read the header

  arch = peHdr->FileHeader.Machine;

cleanup:  // release all of our handles
  FindClose(hFind);
  if (hFile != INVALID_HANDLE_VALUE)
    CloseHandle(hFile);
  if (hMapping != INVALID_HANDLE_VALUE)
    CloseHandle(hMapping);
  return arch;
#else
  Q_UNUSED(program);
  return 0;
#endif
}

QFileInfo GameGamebryo::findInGameFolder(const QString& relativePath) const
{
  return QFileInfo(m_GamePath + "/" + relativePath);
}

QString GameGamebryo::identifyGamePath() const
{
#ifdef HAS_NAK_FFI
  // Use NaK to detect game installations on Linux
  NakGameList gameList = nak_detect_all_games();
  ON_BLOCK_EXIT([&]() {
    nak_game_list_free(gameList);
  });

  QString shortName = gameShortName();
  QString fullName  = gameName();

  auto tokensMatch = [](const QString& detected, const QString& candidate) {
    const QString detectedLower = detected.toLower();
    const QStringList tokens =
        candidate.toLower().split(QRegularExpression("[^a-z0-9]+"),
                                  Qt::SkipEmptyParts);
    bool anyToken = false;
    for (const QString& token : tokens) {
      if (token.size() < 3) {
        continue;
      }
      anyToken = true;
      if (!detectedLower.contains(token)) {
        return false;
      }
    }
    return anyToken;
  };

  for (size_t i = 0; i < gameList.count; ++i) {
    const NakGame& game = gameList.games[i];
    QString detectedName = QString::fromUtf8(game.name);
    QString detectedPath = QString::fromUtf8(game.install_path);
    if (detectedName.compare(fullName, Qt::CaseInsensitive) == 0 ||
        detectedName.compare(shortName, Qt::CaseInsensitive) == 0 ||
        detectedName.contains(fullName, Qt::CaseInsensitive) ||
        detectedName.contains(shortName, Qt::CaseInsensitive) ||
        tokensMatch(detectedName, fullName) || tokensMatch(detectedName, shortName)) {
      if (looksValid(QDir(detectedPath))) {
        return detectedPath;
      }
    }
  }
  return {};
#elif defined(_WIN32)
  QString path = "Software\\Bethesda Softworks\\" + gameShortName();
  return findInRegistry(HKEY_LOCAL_MACHINE, path.toStdWString().c_str(),
                        L"Installed Path");
#else
  return {};
#endif
}

bool GameGamebryo::prepareIni(const QString&)
{
  const auto profile = m_Organizer->profile();

  QString basePath = profile->localSettingsEnabled()
                         ? profile->absolutePath()
                         : documentsDirectory().absolutePath();

  // Ensure all INI files exist with adequate content before writing settings.
  // On Linux, the game launcher often doesn't work properly and can't create
  // the INI files. If an INI is missing or is a stub (< 200 bytes, meaning
  // only MO2-written keys exist), seed it from the game's default INI template.
  ensureIniFilesExist(basePath);

  if (!iniFiles().isEmpty()) {
    // Resolve case-insensitively (e.g., fallout.ini vs Fallout.ini on Linux)
    QString profileIni =
        resolveFileCaseInsensitive(basePath + "/" + iniFiles()[0]);

    QString setting = readIniValue(profileIni, "Launcher", "bEnableFileSelection", "0");
    if (setting.toLong() != 1) {
      MOBase::WriteRegistryValue("Launcher", "bEnableFileSelection", "1", profileIni);
    }
  }

  return true;
}

void GameGamebryo::ensureIniFilesExist(const QString& basePath)
{
  // Make sure the target directory exists
  QDir baseDir(basePath);
  if (!baseDir.exists()) {
    baseDir.mkpath(".");
  }

  for (const QString& iniFile : iniFiles()) {
    QString targetPath = basePath + "/" + iniFile;
    QFileInfo targetInfo(targetPath);

    // Check if the target file already has adequate content
    // (> 200 bytes = more than just MO2's BSA invalidation stub)
    if (targetInfo.exists() && targetInfo.size() > 200) {
      continue;
    }

#ifndef _WIN32
    // On Linux, search for a differently-cased version of this INI file
    // (e.g., FalloutPrefs.ini when we expect falloutprefs.ini).
    // We can't use resolveFileCaseInsensitive here because the exact-case
    // file might exist as an empty stub while the real one has different case.
    QString caseMismatchPath;
    {
      const QString target = targetInfo.fileName();
      const QStringList entries =
          baseDir.entryList(QDir::Files | QDir::Hidden | QDir::System);
      for (const QString& entry : entries) {
        if (entry != target &&
            entry.compare(target, Qt::CaseInsensitive) == 0) {
          caseMismatchPath = baseDir.absoluteFilePath(entry);
          break;
        }
      }
    }

    // If a differently-cased version exists with adequate content,
    // replace the stub (if any) with a symlink to the real file.
    if (!caseMismatchPath.isEmpty()) {
      QFileInfo altInfo(caseMismatchPath);
      if (altInfo.exists() && altInfo.size() > 200) {
        if (targetInfo.exists()) {
          QFile::remove(targetPath);
        }
        QFile::link(altInfo.fileName(), targetPath);
        MOBase::log::info("Created case-matching symlink '{}' -> '{}'",
                          iniFile, altInfo.fileName());
        continue;
      }
    }
#endif

    // The INI doesn't exist or is a stub, and no adequate differently-cased
    // version was found. Try to seed from the game's default INI template
    // (e.g., fallout_default.ini, oblivion_default.ini).
    QString baseName = QFileInfo(iniFile).completeBaseName();  // "fallout"
    QString defaultIniName = baseName + "_default.ini";
    QString defaultIniPath =
        resolveFileCaseInsensitive(gameDirectory().absoluteFilePath(defaultIniName));

    if (QFileInfo::exists(defaultIniPath)) {
      // Remove the stub file if it exists so we can replace it
      if (targetInfo.exists()) {
        QFile::remove(targetPath);
      }
      if (QFile::copy(defaultIniPath, targetPath)) {
        // Make the copy writable
        QFile::setPermissions(
            targetPath,
            QFile::permissions(targetPath) | QFile::WriteUser | QFile::WriteOwner);
        MOBase::log::info("Seeded '{}' from default INI '{}'", iniFile, defaultIniPath);
      } else {
        MOBase::log::warn("Failed to copy default INI '{}' -> '{}'",
                          defaultIniPath, targetPath);
      }
    }
  }
}

QString GameGamebryo::readIniValue(const QString& iniFile, const QString& section,
                                   const QString& key, const QString& defaultValue)
{
  // Read INI values directly without QSettings to avoid QSettings
  // misinterpreting backslashes as line continuations.
  QFile file(iniFile);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return defaultValue;
  }

  QString sectionHeader = "[" + section + "]";
  QTextStream in(&file);
  bool inSection = false;

  while (!in.atEnd()) {
    QString line = in.readLine().trimmed();

    if (line.startsWith('[') && line.endsWith(']')) {
      if (inSection) {
        break;  // Left the target section without finding key
      }
      if (line.compare(sectionHeader, Qt::CaseInsensitive) == 0) {
        inSection = true;
      }
      continue;
    }

    if (inSection && !line.isEmpty() && !line.startsWith(';') && !line.startsWith('#')) {
      int eqPos = line.indexOf('=');
      if (eqPos > 0) {
        QString existingKey = line.left(eqPos).trimmed();
        if (existingKey.compare(key, Qt::CaseInsensitive) == 0) {
          return line.mid(eqPos + 1).trimmed();
        }
      }
    }
  }

  return defaultValue;
}

QString GameGamebryo::selectedVariant() const
{
  return m_GameVariant;
}

QString GameGamebryo::myGamesPath() const
{
  return m_MyGamesPath;
}

/*static*/ QString GameGamebryo::getLootPath()
{
#ifdef _WIN32
  return findInRegistry(HKEY_LOCAL_MACHINE, L"Software\\LOOT", L"Installed Path") +
         "/Loot.exe";
#else
  // On Linux, look for loot in common locations
  QString flatpakPath = QDir::homePath() + "/.var/app/io.github.loot.loot/data/LOOT/Loot";
  if (QFileInfo(flatpakPath).exists())
    return flatpakPath;
  // Try system PATH
  QString systemLoot = QStandardPaths::findExecutable("loot");
  if (!systemLoot.isEmpty())
    return systemLoot;
  return "loot";
#endif
}

QString GameGamebryo::localAppFolder()
{
#ifdef _WIN32
  QString result = getKnownFolderPath(FOLDERID_LocalAppData, false);
  if (result.isEmpty()) {
    // fallback: try the registry
    result = getSpecialPath("Local AppData");
  }
  return result;
#else
  return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
#endif
}

void GameGamebryo::copyToProfile(QString const& sourcePath,
                                 QDir const& destinationDirectory,
                                 QString const& sourceFileName)
{
  copyToProfile(sourcePath, destinationDirectory, sourceFileName, sourceFileName);
}

void GameGamebryo::copyToProfile(QString const& sourcePath,
                                 QDir const& destinationDirectory,
                                 QString const& sourceFileName,
                                 QString const& destinationFileName)
{
  // Use case-insensitive check so we don't create duplicates on Linux
  // (e.g. a profile with "SkyrimPrefs.ini" from Windows is not overwritten
  // by a new "skyrimprefs.ini" copy).
  QString filePath = resolveFileCaseInsensitive(
      destinationDirectory.absoluteFilePath(destinationFileName));
  if (!QFileInfo(filePath).exists()) {
    QString sourceFile = sourcePath + "/" + sourceFileName;
    // On Linux, try case-insensitive match if the exact source doesn't exist
    sourceFile = resolveFileCaseInsensitive(sourceFile);
    if (!MOBase::shellCopy(sourceFile, filePath)) {
      // if copy file fails, create the file empty
      QFile outputFile(filePath);
      if (!outputFile.open(QIODevice::WriteOnly)) {
        MOBase::log::warn("Failed to create fallback file '{}': {}", filePath,
                          outputFile.errorString());
      }
    }
  }
}

MappingType GameGamebryo::mappings() const
{
  MappingType result;

  for (const QString& profileFile : {"plugins.txt", "loadorder.txt"}) {
    result.push_back({m_Organizer->profilePath() + "/" + profileFile,
                      localAppFolder() + "/" + gameShortName() + "/" + profileFile,
                      false});
  }

  return result;
}

#ifdef _WIN32
std::unique_ptr<BYTE[]> GameGamebryo::getRegValue(HKEY key, LPCWSTR path, LPCWSTR value,
                                                  DWORD flags, LPDWORD type)
{
  DWORD size = 0;
  HKEY subKey;
  LONG res = ::RegOpenKeyExW(key, path, 0, KEY_QUERY_VALUE | KEY_WOW64_32KEY, &subKey);
  if (res != ERROR_SUCCESS) {
    res = ::RegOpenKeyExW(key, path, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &subKey);
    if (res != ERROR_SUCCESS)
      return std::unique_ptr<BYTE[]>();
  }
  res = ::RegGetValueW(subKey, L"", value, flags, type, nullptr, &size);
  if (res == ERROR_FILE_NOT_FOUND || res == ERROR_UNSUPPORTED_TYPE) {
    return std::unique_ptr<BYTE[]>();
  }
  if (res != ERROR_SUCCESS && res != ERROR_MORE_DATA) {
    throw MOBase::MyException(
        QObject::tr("failed to query registry path (preflight): %1").arg(res, 0, 16));
  }

  std::unique_ptr<BYTE[]> result(new BYTE[size]);
  res = ::RegGetValueW(subKey, L"", value, flags, type, result.get(), &size);

  if (res != ERROR_SUCCESS) {
    throw MOBase::MyException(
        QObject::tr("failed to query registry path (read): %1").arg(res, 0, 16));
  }

  return result;
}

QString GameGamebryo::findInRegistry(HKEY baseKey, LPCWSTR path, LPCWSTR value)
{
  std::unique_ptr<BYTE[]> buffer =
      getRegValue(baseKey, path, value, RRF_RT_REG_SZ | RRF_NOEXPAND);

  return QString::fromUtf16(reinterpret_cast<const char16_t*>(buffer.get()));
}

QString GameGamebryo::getKnownFolderPath(REFKNOWNFOLDERID folderId, bool useDefault)
{
  PWSTR path = nullptr;
  ON_BLOCK_EXIT([&]() {
    if (path != nullptr)
      ::CoTaskMemFree(path);
  });

  if (::SHGetKnownFolderPath(folderId, useDefault ? KF_FLAG_DEFAULT_PATH : 0, NULL,
                             &path) == S_OK) {
    return QDir::fromNativeSeparators(QString::fromWCharArray(path));
  } else {
    return QString();
  }
}

QString GameGamebryo::getSpecialPath(const QString& name)
{
  QString base = findInRegistry(
      HKEY_CURRENT_USER,
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders",
      name.toStdWString().c_str());

  WCHAR temp[MAX_PATH];
  if (::ExpandEnvironmentStringsW(base.toStdWString().c_str(), temp, MAX_PATH) != 0) {
    return QString::fromWCharArray(temp);
  } else {
    return base;
  }
}
#endif  // _WIN32

QString GameGamebryo::determineMyGamesPath(const QString& gameName)
{
  const QString pattern = "%1/My Games/" + gameName;

  auto tryDir = [&](const QString& dir) -> std::optional<QString> {
    if (dir.isEmpty()) {
      return {};
    }

    const auto path = pattern.arg(dir);
    if (!QFileInfo(path).exists()) {
      return {};
    }

    return path;
  };

#ifdef _WIN32
  // a) this is the way it should work. get the configured My Documents directory
  if (auto d = tryDir(getKnownFolderPath(FOLDERID_Documents, false))) {
    return *d;
  }

  // b) if there is no <game> directory there, look in the default directory
  if (auto d = tryDir(getKnownFolderPath(FOLDERID_Documents, true))) {
    return *d;
  }

  // c) finally, look in the registry. This is discouraged
  if (auto d = tryDir(getSpecialPath("Personal"))) {
    return *d;
  }
#else
  // On Linux, My Games is inside the Wine prefix's Documents folder.
  // Check common Wine prefix locations for steamuser Documents.
  QStringList prefixDocPaths;

  // First check the configured prefix from MO2 settings (most reliable).
  const QString configuredPrefix =
      QSettings().value("fluorine/prefix_path").toString().trimmed();
  if (!configuredPrefix.isEmpty()) {
    const QString configuredDocs =
        QDir(configuredPrefix).filePath("drive_c/users/steamuser/Documents");
    prefixDocPaths.append(configuredDocs);
  }

  // Standard Steam Proton prefix paths
  QString steamRoot = QDir::homePath() + "/.steam/steam/steamapps/compatdata";
  QDir compatDir(steamRoot);
  if (compatDir.exists()) {
    QStringList appIds = compatDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& appId : appIds) {
      prefixDocPaths.append(steamRoot + "/" + appId +
                            "/pfx/drive_c/users/steamuser/Documents");
    }
  }

  // Also check XDG Documents (for native games or manual setups)
  prefixDocPaths.append(
      QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));

  for (const QString& docPath : prefixDocPaths) {
    if (auto d = tryDir(docPath)) {
      MOBase::log::debug("determineMyGamesPath: found '{}' for game '{}'", *d, gameName);
      return *d;
    }
  }

  // If no existing directory was found, try to create it in the configured prefix
  // so that the game launcher can populate it on first run.
  if (!configuredPrefix.isEmpty()) {
    const QString configuredDocs =
        QDir(configuredPrefix).filePath("drive_c/users/steamuser/Documents");
    const QString newPath = pattern.arg(configuredDocs);
    if (QDir().mkpath(newPath)) {
      MOBase::log::info("determineMyGamesPath: created '{}' for game '{}'", newPath,
                        gameName);
      return newPath;
    }
  }

  MOBase::log::warn("determineMyGamesPath: could not find My Games path for '{}'",
                    gameName);
#endif

  return {};
}

QString GameGamebryo::parseEpicGamesLocation(const QStringList& manifests)
{
#ifdef _WIN32
  // Use the registry entry to find the EGL Data dir first, just in case something
  // changes
  QString manifestDir = findInRegistry(
      HKEY_LOCAL_MACHINE, L"Software\\Epic Games\\EpicGamesLauncher", L"AppDataPath");
  if (manifestDir.isEmpty())
    manifestDir = getKnownFolderPath(FOLDERID_ProgramData, false) +
                  "\\Epic\\EpicGamesLauncher\\Data\\";
  manifestDir += "Manifests";
#else
  // Epic Games on Linux (via Heroic or Legendary)
  QString manifestDir = QDir::homePath() +
                        "/.config/heroic/store_cache/egl_manifests";
  if (!QDir(manifestDir).exists()) {
    // Try Legendary
    manifestDir = QDir::homePath() + "/.config/legendary/installed.json";
    // Legendary uses a different format - skip for now
    return "";
  }
#endif

  QDir epicManifests(manifestDir, "*.item",
                     QDir::SortFlags(QDir::Name | QDir::IgnoreCase), QDir::Files);
  if (epicManifests.exists()) {
    QDirIterator it(epicManifests);
    while (it.hasNext()) {
      QString manifestFile = it.next();
      QFile manifest(manifestFile);

      if (!manifest.open(QIODevice::ReadOnly)) {
        qWarning("Couldn't open Epic Games manifest file.");
        continue;
      }

      QByteArray manifestData = manifest.readAll();

      QJsonDocument manifestJson(QJsonDocument::fromJson(manifestData));

      if (manifests.contains(manifestJson["AppName"].toString())) {
        return manifestJson["InstallLocation"].toString();
      }
    }
  }
  return "";
}

QString GameGamebryo::parseSteamLocation(const QString& appid,
                                         const QString& directoryName)
{
#ifdef HAS_NAK_FFI
  // Use NaK to find Steam path
  char* steamPathC = nak_find_steam_path();
  QString steamLocation;
  if (steamPathC) {
    steamLocation = QString::fromUtf8(steamPathC);
    nak_string_free(steamPathC);
  }
#elif defined(_WIN32)
  QString path = "Software\\Valve\\Steam";
  QString steamLocation =
      findInRegistry(HKEY_CURRENT_USER, path.toStdWString().c_str(), L"SteamPath");
#else
  // Fallback: common Steam locations on Linux
  QString steamLocation = QDir::homePath() + "/.steam/steam";
  if (!QDir(steamLocation).exists()) {
    steamLocation = QDir::homePath() + "/.local/share/Steam";
  }
#endif

  if (!steamLocation.isEmpty()) {
    QString steamLibraryLocation;
    QString steamLibraries(steamLocation + "/" + "config" + "/" +
                           "libraryfolders.vdf");
    if (QFile(steamLibraries).exists()) {
      std::ifstream file(steamLibraries.toStdString());
      auto root = tyti::vdf::read(file);
      for (auto child : root.childs) {
        tyti::vdf::object* library = child.second.get();
        auto apps                  = library->childs["apps"];
        if (apps->attribs.contains(appid.toStdString())) {
          steamLibraryLocation = QString::fromStdString(library->attribs["path"]);
          break;
        }
      }
    }
    if (!steamLibraryLocation.isEmpty()) {
      if (!directoryName.isEmpty()) {
        QString gameLocation = steamLibraryLocation + "/" + "steamapps" + "/" +
                               "common" + "/" + directoryName;
        if (QDir(gameLocation).exists()) {
          return gameLocation;
        }
      }

      // Fallback: resolve install dir from appmanifest_<appid>.acf.
      QString manifestPath = steamLibraryLocation + "/steamapps/appmanifest_" + appid +
                             ".acf";
      if (QFile::exists(manifestPath)) {
        std::ifstream manifestFile(manifestPath.toStdString());
        auto manifest = tyti::vdf::read(manifestFile);
        auto appStateIt = manifest.childs.find("AppState");
        if (appStateIt != manifest.childs.end()) {
          const auto& attrs = appStateIt->second->attribs;
          auto installdirIt = attrs.find("installdir");
          if (installdirIt != attrs.end()) {
            QString installdir = QString::fromStdString(installdirIt->second);
            QString gameLocation = steamLibraryLocation + "/steamapps/common/" + installdir;
            if (QDir(gameLocation).exists()) {
              return gameLocation;
            }
          }
        }
      }
    }
  }
  return "";
}

void GameGamebryo::registerFeature(std::shared_ptr<MOBase::GameFeature> feature)
{
  // priority does not matter, this is a game plugin so will get lowest priority in MO2
  m_Organizer->gameFeatures()->registerFeature(this, feature, 0, true);
}
