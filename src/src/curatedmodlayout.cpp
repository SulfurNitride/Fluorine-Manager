#include "curatedmodlayout.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSet>

namespace
{
QString topLevelDataDirectory(const QString& root)
{
  const QDir directory(root);
  for (const auto& entry : directory.entryInfoList(
           QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
    if (entry.fileName().compare(QStringLiteral("Data"),
                                 Qt::CaseInsensitive) == 0)
      return entry.absoluteFilePath();
  }
  return {};
}

bool hasGameDataAtRoot(const QString& root)
{
  static const QSet<QString> dataDirectories = {
      QStringLiteral("config"),     QStringLiteral("interface"),
      QStringLiteral("lodsettings"), QStringLiteral("menus"),
      QStringLiteral("meshes"),     QStringLiteral("music"),
      QStringLiteral("nvse"),       QStringLiteral("scripts"),
      QStringLiteral("shaders"),    QStringLiteral("sound"),
      QStringLiteral("textures"),   QStringLiteral("uio"),
      QStringLiteral("video"),
  };
  static const QSet<QString> dataFileSuffixes = {
      QStringLiteral("bsa"), QStringLiteral("ba2"), QStringLiteral("dll"),
      QStringLiteral("esm"), QStringLiteral("esl"), QStringLiteral("esp"),
  };
  const QDir directory(root);
  for (const auto& entry : directory.entryInfoList(
           QDir::AllEntries | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
    if (entry.isDir()
        && dataDirectories.contains(entry.fileName().toCaseFolded()))
      return true;
    if (entry.isFile()
        && dataFileSuffixes.contains(entry.suffix().toCaseFolded()))
      return true;
  }
  return false;
}

QString discoverModPayloadRoot(const QString& root, int depth)
{
  if (depth > 8 || !QFileInfo(root).isDir()) return root;
  const QString data = topLevelDataDirectory(root);
  if (!data.isEmpty()) return data;
  if (hasGameDataAtRoot(root)) return root;

  const auto directories = QDir(root).entryInfoList(
      QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
  if (directories.size() != 1) return root;
  const QString child = directories.front().absoluteFilePath();
  const QString discovered = discoverModPayloadRoot(child, depth + 1);
  return hasGameDataAtRoot(discovered) || discovered != child ? discovered : root;
}
}

QString curatedModPayloadRoot(const QString& extractedRoot)
{
  return discoverModPayloadRoot(extractedRoot, 0);
}

QString curatedRootPayloadRoot(const QString& extractedRoot)
{
  const QDir directory(extractedRoot);
  const auto entries = directory.entryInfoList(
      QDir::AllEntries | QDir::NoDotAndDotDot | QDir::NoSymLinks);
  if (entries.size() == 1 && entries.front().isDir())
    return entries.front().absoluteFilePath();
  return extractedRoot;
}

QString curatedNestedFomodArchive(const QString& extractedRoot, QString* error)
{
  QStringList matches;
  QDirIterator iterator(extractedRoot, QDir::Files | QDir::NoSymLinks,
                        QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    const QString path = iterator.next();
    if (QFileInfo(path).suffix().compare(QStringLiteral("fomod"),
                                         Qt::CaseInsensitive) == 0)
      matches.push_back(path);
  }
  if (matches.size() > 1) {
    if (error)
      *error = QString("Multiple nested .fomod archives were found below %1")
                   .arg(extractedRoot);
    return {};
  }
  if (error) error->clear();
  return matches.value(0);
}

bool validateCuratedModLayout(const QString& modRoot, QString* error)
{
  if (!QFileInfo(modRoot).isDir()) {
    if (error) *error = QString("Installed mod folder is missing: %1").arg(modRoot);
    return false;
  }
  const QString nestedData = topLevelDataDirectory(modRoot);
  if (!nestedData.isEmpty()) {
    if (error)
      *error = QString("Installed mod has an extra top-level Data folder: %1")
                   .arg(nestedData);
    return false;
  }
  QString fomodError;
  const QString nestedFomod = curatedNestedFomodArchive(modRoot, &fomodError);
  if (!fomodError.isEmpty()) {
    if (error) *error = fomodError;
    return false;
  }
  if (!nestedFomod.isEmpty()) {
    if (error)
      *error = QString("Installed mod still contains an unexpanded .fomod archive: %1")
                   .arg(nestedFomod);
    return false;
  }
  const QString payload = curatedModPayloadRoot(modRoot);
  if (QDir::cleanPath(payload) != QDir::cleanPath(modRoot)) {
    if (error)
      *error = QString("Installed mod has an extra archive wrapper directory: %1")
                   .arg(payload);
    return false;
  }
  if (error) error->clear();
  return true;
}
