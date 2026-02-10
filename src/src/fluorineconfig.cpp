#include "fluorineconfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace
{
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

  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }

  const qint64 written = f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
  f.close();

  return written >= 0;
}

void FluorineConfig::deleteConfig() const
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

  return QDir(QDir(prefix_path).filePath("drive_c")).exists();
}

QString FluorineConfig::compatDataPath() const
{
  if (prefix_path.isEmpty()) {
    return QString();
  }

  QDir prefixDir(prefix_path);
  if (prefixDir.dirName() == "pfx") {
    prefixDir.cdUp();
    return QDir::cleanPath(prefixDir.absolutePath());
  }

  return QDir::cleanPath(QFileInfo(prefix_path).dir().absolutePath());
}

void FluorineConfig::destroyPrefix() const
{
  const QString compatData = compatDataPath();
  if (!compatData.isEmpty()) {
    QDir dir(compatData);
    if (dir.exists()) {
      dir.removeRecursively();
    }
  }

  deleteConfig();
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
