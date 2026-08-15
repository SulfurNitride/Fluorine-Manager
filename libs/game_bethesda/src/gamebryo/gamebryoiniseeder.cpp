#include "gamebryoiniseeder.h"

#include <uibase/transactionalwritefile.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

namespace GamebryoIniSeeder
{
namespace
{
Result failure(const QString& error) { return {.error = error}; }

bool isLeafName(const QString& name)
{
  return !name.isEmpty() && name != "." && name != ".." && !name.contains('/') &&
         !name.contains('\\') && !name.contains(QChar::Null);
}

QStringList caseFamily(const QDir& directory, const QString& canonicalName)
{
  QStringList result;
  const QFileInfoList entries = directory.entryInfoList(
      QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
  for (const QFileInfo& entry : entries)
  {
    if (entry.fileName().compare(canonicalName, Qt::CaseInsensitive) == 0)
    {
      result.append(entry.absoluteFilePath());
    }
  }
  return result;
}

bool sameFile(const QString& first, const QString& second)
{
#ifdef Q_OS_UNIX
  struct stat firstStat{};
  struct stat secondStat{};
  if (::stat(QFile::encodeName(first).constData(), &firstStat) != 0 ||
      ::stat(QFile::encodeName(second).constData(), &secondStat) != 0)
  {
    return false;
  }
  return firstStat.st_dev == secondStat.st_dev && firstStat.st_ino == secondStat.st_ino;
#else
  const QString firstCanonical = QFileInfo(first).canonicalFilePath();
  const QString secondCanonical = QFileInfo(second).canonicalFilePath();
  return !firstCanonical.isEmpty() && firstCanonical == secondCanonical;
#endif
}

Result validateOrCreateAlias(const QDir& directory, const QString& aliasName,
                             const QString& authoritativePath)
{
  const QString aliasPath = directory.absoluteFilePath(aliasName);
  if (QFileInfo(aliasPath).fileName() == QFileInfo(authoritativePath).fileName())
  {
    return {.success = true, .authoritativePath = authoritativePath};
  }

  QFileInfo alias(aliasPath);
  if (alias.isSymLink() || alias.exists())
  {
    // A case-insensitive filesystem can resolve the alternate spelling to the
    // same ordinary directory entry without exposing a symlink.
    if (sameFile(aliasPath, authoritativePath))
    {
      return {.success = true, .authoritativePath = authoritativePath};
    }
    return failure(QStringLiteral("INI alias '%1' is not an owned link to '%2'")
                       .arg(aliasPath, authoritativePath));
  }

  // Use a relative target so moving the containing profile keeps the alias valid.
  if (!QFile::link(QFileInfo(authoritativePath).fileName(), aliasPath))
  {
    alias.refresh();
    if (!alias.isSymLink() || !sameFile(aliasPath, authoritativePath))
    {
      return failure(QStringLiteral("Could not create INI alias '%1' for '%2'")
                         .arg(aliasPath, authoritativePath));
    }
  }

  return {.success = true, .authoritativePath = authoritativePath};
}

Result seedMissing(const QString& targetPath, const QString& sourcePath)
{
  MOBase::TransactionalWriteFile target(targetPath);
  QByteArray targetContents;
  bool targetPresent = false;
  if (!target.readOriginal(targetContents, targetPresent))
  {
    return failure(target.errorString());
  }
  if (targetPresent)
  {
    return {.success = true, .authoritativePath = targetPath};
  }

  if (sourcePath.isEmpty())
  {
    return {.success = true};
  }

  MOBase::TransactionalWriteFile source(sourcePath);
  QByteArray sourceContents;
  bool sourcePresent = false;
  if (!source.readOriginal(sourceContents, sourcePresent))
  {
    return failure(source.errorString());
  }
  if (!sourcePresent)
  {
    return {.success = true};
  }
  if (sourceContents.isEmpty())
  {
    return failure(QStringLiteral("Default INI '%1' is empty").arg(sourcePath));
  }

  QFileDevice::Permissions permissions;
  if (!source.readPermissions(permissions))
  {
    return failure(source.errorString());
  }
  permissions |= QFileDevice::ReadOwner | QFileDevice::WriteOwner;
  if (!target.setPermissions(permissions) || !target.replaceWith(sourceContents))
  {
    return failure(target.errorString());
  }

  return {.success = true, .seeded = true, .authoritativePath = targetPath};
}
}  // namespace

Result ensure(const QString& basePath, const QString& canonicalName,
              const QString& sourcePath)
{
  if (!isLeafName(canonicalName))
  {
    return failure(QStringLiteral("Invalid INI leaf name '%1'").arg(canonicalName));
  }

  QDir directory(basePath);
  if (!directory.exists() && !directory.mkpath("."))
  {
    return failure(QStringLiteral("Could not create INI directory '%1'").arg(basePath));
  }

  const QStringList family = caseFamily(directory, canonicalName);
  QStringList regularFiles;
  QStringList links;
  for (const QString& path : family)
  {
    const QFileInfo info(path);
    if (info.isSymLink())
    {
      links.append(path);
    }
    else if (info.isFile())
    {
      regularFiles.append(path);
    }
    else
    {
      return failure(QStringLiteral("Unsafe INI family leaf '%1'").arg(path));
    }
  }

  if (regularFiles.size() > 1)
  {
    return failure(QStringLiteral("Ambiguous INI case family for '%1': %2")
                       .arg(canonicalName, regularFiles.join(", ")));
  }

  QString authoritativePath;
  bool seeded = false;
  if (regularFiles.isEmpty())
  {
    if (!links.isEmpty())
    {
      return failure(QStringLiteral("INI family for '%1' contains only unsafe or "
                                    "dangling links")
                         .arg(canonicalName));
    }

    const QString targetPath = directory.absoluteFilePath(canonicalName);
    const Result seed = seedMissing(targetPath, sourcePath);
    if (!seed.success || seed.authoritativePath.isEmpty())
    {
      return seed;
    }
    authoritativePath = seed.authoritativePath;
    seeded = seed.seeded;
  }
  else
  {
    authoritativePath = regularFiles.constFirst();
  }

  for (const QString& link : links)
  {
    if (!sameFile(link, authoritativePath))
    {
      return failure(QStringLiteral("INI link '%1' does not resolve to '%2'")
                         .arg(link, authoritativePath));
    }
  }

#ifndef _WIN32
  QStringList aliases{canonicalName};
  const QString lower = canonicalName.toLower();
  if (!aliases.contains(lower))
  {
    aliases.append(lower);
  }
  for (const QString& alias : aliases)
  {
    const Result aliasResult = validateOrCreateAlias(directory, alias, authoritativePath);
    if (!aliasResult.success)
    {
      return aliasResult;
    }
  }
#endif

  return {.success = true, .seeded = seeded, .authoritativePath = authoritativePath};
}

}  // namespace GamebryoIniSeeder
