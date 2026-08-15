#include "fluorineconfig.h"
#include "fluorinepaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QUuid>
#include <uibase/log.h>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <sys/types.h>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
constexpr auto PrefixOwnershipMarker = ".fluorine-managed-prefix";
constexpr char PrefixOwnershipMarkerBytes[] = "Fluorine Manager managed prefix\n";

enum class MarkerStatus
{
  Missing,
  Exact,
  Invalid,
};

#ifdef Q_OS_UNIX
bool sameIdentity(const struct stat& left, const struct stat& right)
{
  return left.st_dev == right.st_dev && left.st_ino == right.st_ino;
}

int openDirectoryNoFollow(const QString& path, bool createMissing)
{
  const QString absolute = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
  if (absolute.isEmpty() || absolute == QStringLiteral("/")) {
    errno = EINVAL;
    return -1;
  }

  int current = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (current < 0) {
    return -1;
  }
  const QStringList components = absolute.split('/', Qt::SkipEmptyParts);
  for (const QString& component : components) {
    const QByteArray encoded = QFile::encodeName(component);
    int next =
        ::openat(current, encoded.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (next < 0 && errno == ENOENT && createMissing) {
      if (::mkdirat(current, encoded.constData(), 0700) != 0 && errno != EEXIST) {
        ::close(current);
        return -1;
      }
      next =
          ::openat(current, encoded.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    }
    if (next < 0) {
      ::close(current);
      return -1;
    }
    ::close(current);
    current = next;
  }
  return current;
}

bool liveDirectoryMatches(int retained, const QString& path)
{
  const int live = openDirectoryNoFollow(path, false);
  if (live < 0) {
    return false;
  }
  struct stat retainedStatus{};
  struct stat liveStatus{};
  const bool matches = ::fstat(retained, &retainedStatus) == 0 && ::fstat(live, &liveStatus) == 0 &&
                       sameIdentity(retainedStatus, liveStatus);
  ::close(live);
  return matches;
}

MarkerStatus markerStatusAt(int directory)
{
  const int marker =
      ::openat(directory, PrefixOwnershipMarker, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  if (marker < 0) {
    return errno == ENOENT ? MarkerStatus::Missing : MarkerStatus::Invalid;
  }

  struct stat status{};
  const qsizetype expectedSize = static_cast<qsizetype>(sizeof(PrefixOwnershipMarkerBytes) - 1);
  if (::fstat(marker, &status) != 0 || !S_ISREG(status.st_mode) || status.st_uid != ::geteuid() ||
      status.st_size != expectedSize) {
    ::close(marker);
    return MarkerStatus::Invalid;
  }

  QByteArray contents(expectedSize, Qt::Uninitialized);
  qsizetype offset = 0;
  while (offset < contents.size()) {
    const ssize_t count = ::read(marker, contents.data() + offset, contents.size() - offset);
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count <= 0) {
      ::close(marker);
      return MarkerStatus::Invalid;
    }
    offset += count;
  }
  char extra             = 0;
  const ssize_t trailing = ::read(marker, &extra, 1);
  ::close(marker);
  const bool exact =
      trailing == 0 && contents == QByteArray(PrefixOwnershipMarkerBytes, expectedSize);
  return exact ? MarkerStatus::Exact : MarkerStatus::Invalid;
}

bool writeExactMarkerAt(int directory)
{
  const QString temporaryName = QStringLiteral(".fluorine-managed-prefix.tmp.%1")
                                    .arg(QUuid::createUuid().toString(QUuid::Id128));
  const QByteArray encodedTemporary = QFile::encodeName(temporaryName);
  const int marker                  = ::openat(directory, encodedTemporary.constData(),
                                               O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (marker < 0) {
    return false;
  }

  const char* contents = PrefixOwnershipMarkerBytes;
  const qsizetype size = sizeof(PrefixOwnershipMarkerBytes) - 1;
  qsizetype offset     = 0;
  while (offset < size) {
    const ssize_t count = ::write(marker, contents + offset, size - offset);
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count <= 0) {
      ::close(marker);
      ::unlinkat(directory, encodedTemporary.constData(), 0);
      return false;
    }
    offset += count;
  }
  const bool fileDurable = ::fsync(marker) == 0;
  const bool closed      = ::close(marker) == 0;
  if (!fileDurable || !closed) {
    ::unlinkat(directory, encodedTemporary.constData(), 0);
    return false;
  }

  if (::linkat(directory, encodedTemporary.constData(), directory, PrefixOwnershipMarker, 0) != 0) {
    const int failureCode = errno;
    ::unlinkat(directory, encodedTemporary.constData(), 0);
    return failureCode == EEXIST && markerStatusAt(directory) == MarkerStatus::Exact &&
           ::fsync(directory) == 0;
  }
  const bool temporaryRemoved = ::unlinkat(directory, encodedTemporary.constData(), 0) == 0;
  const bool directoryDurable = ::fsync(directory) == 0;
  return temporaryRemoved && directoryDurable && markerStatusAt(directory) == MarkerStatus::Exact;
}
#else
MarkerStatus markerStatusAtPath(const QString& markerPath)
{
  const QFileInfo markerInfo(markerPath);
  if (!markerInfo.exists() && !markerInfo.isSymLink()) {
    return MarkerStatus::Missing;
  }
  if (!markerInfo.isFile() || markerInfo.isSymLink()) {
    return MarkerStatus::Invalid;
  }
  QFile marker(markerPath);
  if (!marker.open(QIODevice::ReadOnly)) {
    return MarkerStatus::Invalid;
  }
  return marker.readAll() == QByteArray(PrefixOwnershipMarkerBytes) ? MarkerStatus::Exact
                                                                    : MarkerStatus::Invalid;
}
#endif

QString fluorineConfigPath()
{
  QString configRoot = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
  if (configRoot.isEmpty()) {
    configRoot = QDir::homePath() + "/.config";
  }

  return QDir(configRoot).filePath("fluorine/config.json");
}
}  // namespace

QString FluorineConfig::configFilePath()
{
  return fluorineConfigPath();
}

std::optional<FluorineConfig> FluorineConfig::load()
{
  const QString path = configFilePath();
  QFile f(path);
  if (!f.exists()) {
    return std::nullopt;
  }

  if (!f.open(QIODevice::ReadOnly)) {
    return std::nullopt;
  }

  const auto json = QJsonDocument::fromJson(f.readAll());
  f.close();

  if (!json.isObject()) {
    return std::nullopt;
  }

  const QJsonObject obj = json.object();

  FluorineConfig cfg;
  cfg.app_id      = static_cast<uint32_t>(obj.value("app_id").toInteger());
  cfg.prefix_path = obj.value("prefix_path").toString();
  cfg.proton_name = obj.value("proton_name").toString();
  cfg.proton_path = obj.value("proton_path").toString();
  cfg.created     = obj.value("created").toString();

  return cfg;
}

bool FluorineConfig::save() const
{
  const QString path = configFilePath();
  const QFileInfo fi(path);

  if (!QDir().mkpath(fi.dir().absolutePath())) {
    return false;
  }

  QJsonObject obj;
  obj.insert("app_id", static_cast<qint64>(app_id));
  obj.insert("prefix_path", prefix_path);
  obj.insert("proton_name", proton_name);
  obj.insert("proton_path", proton_path);
  obj.insert("created", created);

  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) {
    return false;
  }

  const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Indented);
  return f.write(payload) == payload.size() && f.commit();
}

void FluorineConfig::deleteConfig() 
{
  const QString path = configFilePath();
  if (QFile::exists(path)) {
    QFile::remove(path);
  }
}

bool FluorineConfig::prefixExists() const
{
  if (prefix_path.isEmpty()) {
    return false;
  }

  // prefix_path may point to the compatdata dir (containing pfx/) or
  // directly to the pfx dir (containing drive_c).
  const QDir dir(prefix_path);
  return dir.exists("drive_c") || dir.exists("pfx/drive_c");
}

QString FluorineConfig::compatDataPath() const
{
  if (prefix_path.isEmpty()) {
    return {};
  }

  QDir prefixDir(QDir::cleanPath(prefix_path));
  if (prefixDir.dirName() == "pfx") {
    prefixDir.cdUp();
    return QDir::cleanPath(prefixDir.absolutePath());
  }

  // Some older/imported configurations point at the compatdata root itself
  // instead of its pfx child. In that case the deletion boundary is the
  // configured directory, never its parent.
  return QDir::cleanPath(prefixDir.absolutePath());
}

bool FluorineConfig::markPrefixOwned() const
{
  const QString compatData = compatDataPath();
  if (compatData.isEmpty()) {
    return false;
  }

#ifdef Q_OS_UNIX
  const int directory = openDirectoryNoFollow(compatData, true);
  if (directory < 0) {
    return false;
  }
  const MarkerStatus status = markerStatusAt(directory);
  const bool marked         = status == MarkerStatus::Exact
                                  ? ::fsync(directory) == 0
                                  : status == MarkerStatus::Missing && writeExactMarkerAt(directory);
  const bool live           = liveDirectoryMatches(directory, compatData);
  ::close(directory);
  return marked && live;
#else
  const QFileInfo compatInfo(compatData);
  if (compatInfo.isSymLink() || (!compatInfo.exists() && !QDir().mkpath(compatData)) ||
      QFileInfo(compatData).canonicalFilePath() !=
          QDir::cleanPath(QFileInfo(compatData).absoluteFilePath())) {
    return false;
  }
  const QString markerPath  = QDir(compatData).filePath(QString::fromLatin1(PrefixOwnershipMarker));
  const MarkerStatus status = markerStatusAtPath(markerPath);
  if (status == MarkerStatus::Exact) {
    return true;
  }
  if (status != MarkerStatus::Missing) {
    return false;
  }
  QSaveFile marker(markerPath);
  marker.setDirectWriteFallback(false);
  const QByteArray contents(PrefixOwnershipMarkerBytes);
  return marker.open(QIODevice::WriteOnly) &&
         marker.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                               QFileDevice::ReadUser | QFileDevice::WriteUser) &&
         marker.write(contents) == contents.size() && marker.flush() && marker.commit() &&
         markerStatusAtPath(markerPath) == MarkerStatus::Exact;
#endif
}

bool FluorineConfig::canDestroyPrefix() const
{
  const QString compatData = compatDataPath();
  if (compatData.isEmpty() || QFileInfo(prefix_path).isSymLink()) {
    return false;
  }

#ifdef Q_OS_UNIX
  const int directory = openDirectoryNoFollow(compatData, false);
  if (directory < 0) {
    return false;
  }
  const MarkerStatus marker = markerStatusAt(directory);
  const bool live           = liveDirectoryMatches(directory, compatData);
  ::close(directory);
  if (!live || marker == MarkerStatus::Invalid) {
    return false;
  }
#else
  const QFileInfo compatInfo(compatData);
  if (!compatInfo.isDir() || compatInfo.isSymLink() ||
      compatInfo.canonicalFilePath() != QDir::cleanPath(compatInfo.absoluteFilePath())) {
    return false;
  }
  const MarkerStatus marker =
      markerStatusAtPath(QDir(compatData).filePath(QString::fromLatin1(PrefixOwnershipMarker)));
  if (marker == MarkerStatus::Invalid) {
    return false;
  }
#endif
  if (marker == MarkerStatus::Exact) {
    return true;
  }

  // Prefixes created before ownership markers were introduced are safe only
  // at Fluorine's historical default location. Custom legacy locations must
  // be removed manually rather than risking unrelated or externally managed
  // data.
  const QString legacyDefault =
      QDir(fluorineDataDir()).filePath(QStringLiteral("Prefix"));
  return QDir::cleanPath(compatData) == QDir::cleanPath(legacyDefault);
}

bool FluorineConfig::resetPrefixForRecreation() const
{
  if (!canDestroyPrefix()) {
    return false;
  }

  QDir prefixDir(prefix_path);
  if (prefixDir.exists() && !prefixDir.removeRecursively()) {
    return false;
  }

  // A direct-root configuration removes its marker along with the prefix.
  // Re-establish ownership before setup so later deletion remains guarded.
  return markPrefixOwned();
}

bool FluorineConfig::destroyPrefix() const
{
  const QString compatData = compatDataPath();
  if (compatData.isEmpty()) {
    deleteConfig();
    return true;
  }

  if (!canDestroyPrefix()) {
    MOBase::log::error(
        "Refusing to delete unowned Wine prefix root '{}'. Remove it manually "
        "if it is no longer needed.",
        compatData);
    return false;
  }

  // Kill any wine processes still bound to this prefix. Otherwise they hold
  // file handles that keep the files around (still on disk even after unlink
  // until the last fd is closed) and can prevent directory removal on some
  // filesystems.
  const QString cleanCompat = QDir::cleanPath(compatData);
  const QString cleanPrefix = QDir::cleanPath(compatData + "/pfx");
  QDir const procDir("/proc");
  const QStringList pids =
      procDir.entryList({QStringLiteral("[0-9]*")}, QDir::Dirs);
  QList<qint64> victims;
  for (const QString& pid : pids) {
    QFile envF("/proc/" + pid + "/environ");
    if (!envF.open(QIODevice::ReadOnly))
      continue;
    const QByteArray environ = envF.readAll();
    for (const QByteArray& kv : environ.split('\0')) {
      QString val;
      if (kv.startsWith("WINEPREFIX="))
        val = QString::fromUtf8(kv.mid(11));
      else if (kv.startsWith("STEAM_COMPAT_DATA_PATH="))
        val = QString::fromUtf8(kv.mid(23));
      else
        continue;
      const QString clean = QDir::cleanPath(val);
      if (clean == cleanCompat || clean == cleanPrefix) {
        bool ok = false;
        const qint64 p = pid.toLongLong(&ok);
        if (ok)
          victims.append(p);
        break;
      }
    }
  }

  for (qint64 const p : victims)
    ::kill(static_cast<pid_t>(p), SIGKILL);
  if (!victims.isEmpty())
    QThread::msleep(200);

  QDir dir(compatData);
  if (dir.exists()) {
    if (!dir.removeRecursively()) {
      MOBase::log::warn("destroyPrefix: failed to remove '{}' — files may be "
                        "locked by lingering processes",
                        compatData.toStdString());
      return false;
    }
  }

  deleteConfig();
  return true;
}

bool FluorineConfig::isSetup()
{
  auto cfg = load();
  return cfg.has_value() && cfg->prefixExists();
}

std::optional<QString> FluorineConfig::prefixPath()
{
  auto cfg = load();
  if (cfg.has_value() && cfg->prefixExists()) {
    return cfg->prefix_path;
  }

  return std::nullopt;
}
