#include "gamegamebryo.h"

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

#include <QtDebug>
#include <QtGlobal>

#include <Knownfolders.h>
#include <Shlobj.h>
#include <Windows.h>
#include <winreg.h>
#include <winver.h>

#include <optional>
#include <string>
#include <vector>

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
  return gameDirectory().absoluteFilePath("data");
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
}

QFileInfo GameGamebryo::findInGameFolder(const QString& relativePath) const
{
  return QFileInfo(m_GamePath + "/" + relativePath);
}

QString GameGamebryo::identifyGamePath() const
{
  QString path = "Software\\Bethesda Softworks\\" + gameShortName();
  return findInRegistry(HKEY_LOCAL_MACHINE, path.toStdWString().c_str(),
                        L"Installed Path");
}

bool GameGamebryo::prepareIni(const QString& exec)
{
  MOBase::IProfile* profile = m_Organizer->profile();

  QString basePath = profile->localSettingsEnabled()
                         ? profile->absolutePath()
                         : documentsDirectory().absolutePath();

  if (!iniFiles().isEmpty()) {

    QString profileIni = basePath + "/" + iniFiles()[0];

    WCHAR setting[512];
    if (!GetPrivateProfileStringW(L"Launcher", L"bEnableFileSelection", L"0", setting,
                                  512, profileIni.toStdWString().c_str()) ||
        wcstol(setting, nullptr, 10) != 1) {
      MOBase::WriteRegistryValue(L"Launcher", L"bEnableFileSelection", L"1",
                                 profileIni.toStdWString().c_str());
    }
  }

  return true;
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
  return findInRegistry(HKEY_LOCAL_MACHINE, L"Software\\LOOT", L"Installed Path") +
         "/Loot.exe";
}

QString GameGamebryo::localAppFolder()
{
  QString result = getKnownFolderPath(FOLDERID_LocalAppData, false);
  if (result.isEmpty()) {
    // fallback: try the registry
    result = getSpecialPath("Local AppData");
  }
  return result;
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
  QString filePath = destinationDirectory.absoluteFilePath(destinationFileName);
  if (!QFileInfo(filePath).exists()) {
    if (!MOBase::shellCopy(sourcePath + "/" + sourceFileName, filePath)) {
      // if copy file fails, create the file empty
      QFile(filePath).open(QIODevice::WriteOnly);
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

std::unique_ptr<BYTE[]> GameGamebryo::getRegValue(HKEY key, LPCWSTR path, LPCWSTR value,
                                                  DWORD flags, LPDWORD type = nullptr)
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

  return QString::fromUtf16(reinterpret_cast<const ushort*>(buffer.get()));
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

  return {};
}

QString GameGamebryo::parseEpicGamesLocation(const QStringList& manifests)
{
  // Use the registry entry to find the EGL Data dir first, just in case something
  // changes
  QString manifestDir = findInRegistry(
      HKEY_LOCAL_MACHINE, L"Software\\Epic Games\\EpicGamesLauncher", L"AppDataPath");
  if (manifestDir.isEmpty())
    manifestDir = getKnownFolderPath(FOLDERID_ProgramData, false) +
                  "\\Epic\\EpicGamesLauncher\\Data\\";
  manifestDir += "Manifests";
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
  QString path = "Software\\Valve\\Steam";
  QString steamLocation =
      findInRegistry(HKEY_CURRENT_USER, path.toStdWString().c_str(), L"SteamPath");
  if (!steamLocation.isEmpty()) {
    QString steamLibraryLocation;
    QString steamLibraries(steamLocation + "\\" + "config" + "\\" +
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
      QString gameLocation = steamLibraryLocation + "\\" + "steamapps" + "\\" +
                             "common" + "\\" + directoryName;
      if (QDir(gameLocation).exists())
        return gameLocation;
    }
  }
  return "";
}

void GameGamebryo::registerFeature(std::shared_ptr<MOBase::GameFeature> feature)
{
  // priority does not matter, this is a game plugin so will get lowest priority in MO2
  m_Organizer->gameFeatures()->registerFeature(this, feature, 0, true);
}
