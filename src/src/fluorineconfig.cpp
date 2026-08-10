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
#include <uibase/log.h>

#include <csignal>
#include <sys/types.h>

namespace
{
constexpr auto PrefixOwnershipMarker = ".fluorine-managed-prefix";

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
  if (compatData.isEmpty() || QFileInfo(compatData).isSymLink() ||
      !QDir().mkpath(compatData)) {
    return false;
  }

  QFile marker(QDir(compatData).filePath(QString::fromLatin1(PrefixOwnershipMarker)));
  if (!marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  return marker.write("Fluorine Manager managed prefix\n") >= 0;
}

bool FluorineConfig::canDestroyPrefix() const
{
  const QString compatData = compatDataPath();
  if (compatData.isEmpty() || QFileInfo(compatData).isSymLink() ||
      QFileInfo(prefix_path).isSymLink()) {
    return false;
  }

  const QFileInfo marker(
      QDir(compatData).filePath(QString::fromLatin1(PrefixOwnershipMarker)));
  if (marker.isFile() && !marker.isSymLink()) {
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
