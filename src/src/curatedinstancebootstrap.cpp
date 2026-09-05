#include "curatedinstancebootstrap.h"

#include <QDir>
#include <QSettings>
#include <QVector>

namespace
{
QString wineZPath(const QString& path)
{
  QString result = QStringLiteral("Z:") + QDir::fromNativeSeparators(path);
  result.replace('/', '\\');
  return result;
}

QString replacePathRoot(QString value, const QString& source,
                        const QString& destination)
{
  if (source.isEmpty()) return value;

  qsizetype offset = 0;
  while ((offset = value.indexOf(source, offset, Qt::CaseInsensitive)) >= 0) {
    const qsizetype after = offset + source.size();
    if (after < value.size() && value.at(after) != '/'
        && value.at(after) != '\\' && value.at(after) != '"'
        && !value.at(after).isSpace()) {
      offset = after;
      continue;
    }
    value.replace(offset, source.size(), destination);
    offset += destination.size();
  }
  return value;
}

QString rebaseExecutableValue(QString value, const QString& sourceGamePath,
                              const QString& stockGamePath)
{
  const QString source = QDir::cleanPath(sourceGamePath);
  const QString stock = QDir::cleanPath(stockGamePath);
  value = replacePathRoot(value, wineZPath(source), wineZPath(stock));
  value = replacePathRoot(value, QDir::fromNativeSeparators(source),
                          QDir::fromNativeSeparators(stock));
  return value;
}
}  // namespace

QString curatedGamePluginId(const QString& gamePlugin)
{
  if (gamePlugin.compare(QStringLiteral("Fallout New Vegas"),
                         Qt::CaseInsensitive) == 0)
    return QStringLiteral("New Vegas");
  if (gamePlugin.compare(QStringLiteral("Tale of Two Wastelands"),
                         Qt::CaseInsensitive) == 0)
    return QStringLiteral("TTW");
  return gamePlugin;
}

int rebaseCustomExecutableGamePaths(QSettings& ini,
                                    const QString& sourceGamePath,
                                    const QString& stockGamePath)
{
  if (sourceGamePath.trimmed().isEmpty() || stockGamePath.trimmed().isEmpty())
    return 0;

  struct Update
  {
    int index;
    QString key;
    QString value;
  };
  QVector<Update> updates;
  const QStringList pathKeys{QStringLiteral("binary"),
                             QStringLiteral("workingDirectory"),
                             QStringLiteral("arguments")};

  const int count = ini.beginReadArray(QStringLiteral("customExecutables"));
  for (int index = 0; index < count; ++index) {
    ini.setArrayIndex(index);
    for (const QString& key : pathKeys) {
      if (!ini.contains(key)) continue;
      const QString current = ini.value(key).toString();
      const QString rebased = rebaseExecutableValue(
          current, sourceGamePath, stockGamePath);
      if (rebased != current) updates.push_back({index, key, rebased});
    }
  }
  ini.endArray();

  if (!updates.isEmpty()) {
    ini.beginWriteArray(QStringLiteral("customExecutables"), count);
    for (const auto& update : updates) {
      ini.setArrayIndex(update.index);
      ini.setValue(update.key, update.value);
    }
    ini.endArray();
  }
  return updates.size();
}

QPair<bool, QString> bootstrapCuratedInstance(const QString& instancePath,
                                              const QString& gamePlugin,
                                              const QString& gamePath,
                                              const QString& downloadsPath)
{
  if (instancePath.trimmed().isEmpty() || gamePlugin.trimmed().isEmpty()
      || gamePath.trimmed().isEmpty() || downloadsPath.trimmed().isEmpty()) {
    return {false, "Cannot initialise a curated instance with empty paths or game metadata."};
  }

  const QStringList directories = {
      instancePath,
      QDir(instancePath).filePath("mods"),
      QDir(instancePath).filePath("profiles"),
      QDir(instancePath).filePath("overwrite"),
      downloadsPath,
  };
  for (const auto& directory : directories) {
    if (!QDir().mkpath(directory))
      return {false, QString("Cannot create curated instance folder: %1").arg(directory)};
  }

  QSettings ini(QDir(instancePath).filePath("ModOrganizer.ini"), QSettings::IniFormat);
  // MO's [General] section is QSettings' root section. Writing a group named
  // "General" produces [%General], which Instance deliberately does not read.
  // Remove that legacy escaped group and write the keys at the INI root.
  ini.remove("General");
  ini.setValue("gameName", curatedGamePluginId(gamePlugin));
  ini.setValue("gamePath", QDir::cleanPath(gamePath));
  ini.setValue("Settings/base_directory", QDir::cleanPath(instancePath));
  ini.setValue("Settings/download_directory", QDir::cleanPath(downloadsPath));
  ini.setValue("Settings/mod_directory", QDir(instancePath).filePath("mods"));
  ini.setValue("Settings/profiles_directory", QDir(instancePath).filePath("profiles"));
  ini.setValue("Settings/overwrite_directory", QDir(instancePath).filePath("overwrite"));
  ini.setValue("fluorine/vfs_root_builder", false);
  ini.sync();
  if (ini.status() != QSettings::NoError)
    return {false, QString("Cannot write curated instance configuration: %1")
                       .arg(ini.fileName())};

  return {true, {}};
}
