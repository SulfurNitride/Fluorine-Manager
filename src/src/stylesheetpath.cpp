#include "stylesheetpath.h"

#include "shared/appconfig.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

#include <algorithm>

namespace StyleSheetPath
{
namespace
{
QString styleDirectory(const QString& root)
{
  return QDir(root).filePath(
      QString::fromStdWString(AppConfig::stylesheetsPath()));
}

bool isContainedPath(const QString& canonicalPath,
                     const QString& canonicalDirectory)
{
  if (canonicalPath.isEmpty() || canonicalDirectory.isEmpty()) {
    return false;
  }

  const QString relative =
      QDir(canonicalDirectory).relativeFilePath(canonicalPath);
  return relative != QStringLiteral("..") &&
         !relative.startsWith(QStringLiteral("../")) &&
         !QDir::isAbsolutePath(relative);
}

QString authorizedStyleDirectory(const QString& root)
{
  const QString canonicalRoot = QDir(root).canonicalPath();
  const QString candidate      = styleDirectory(root);
  const QString canonicalStyle = QDir(candidate).canonicalPath();
  if (canonicalStyle == canonicalRoot || !QFileInfo(canonicalStyle).isDir() ||
      !isContainedPath(canonicalStyle, canonicalRoot)) {
    return {};
  }
  return canonicalStyle;
}

bool isContainedFile(const QString& candidate, const QString& directory)
{
  const QString canonicalDirectory = QDir(directory).canonicalPath();
  const QString canonicalFile      = QFileInfo(candidate).canonicalFilePath();
  return isContainedPath(canonicalFile, canonicalDirectory) &&
         QFileInfo(canonicalFile).isFile();
}
}  // namespace

QStringList searchDirectories(const QString& applicationDirectory,
                              const QString& instanceDirectory)
{
  QStringList result;
  const QString installed = authorizedStyleDirectory(applicationDirectory);
  if (!installed.isEmpty()) {
    result.append(installed);
  }
  if (!instanceDirectory.isEmpty()) {
    const QString instanceStyles = authorizedStyleDirectory(instanceDirectory);
    if (!instanceStyles.isEmpty() && !result.contains(instanceStyles)) {
      result.append(instanceStyles);
    }
  }
  return result;
}

QString resolve(const QString& styleName, const QStringList& directories)
{
  if (styleName.isEmpty() || QFileInfo(styleName).fileName() != styleName) {
    return {};
  }

  for (const QString& directory : directories) {
    const QString candidate = QDir(directory).filePath(styleName);
    if (isContainedFile(candidate, directory)) {
      return QFileInfo(candidate).canonicalFilePath();
    }
  }
  return {};
}

QStringList available(const QStringList& directories)
{
  QStringList result;
  QSet<QString> seen;
  for (const QString& directory : directories) {
    QStringList names;
    QDirIterator iterator(directory, QStringList{QStringLiteral("*.qss")},
                          QDir::Files);
    while (iterator.hasNext()) {
      iterator.next();
      const QString name = iterator.fileName();
      if (isContainedFile(iterator.filePath(), directory)) {
        names.append(name);
      }
    }
    std::sort(names.begin(), names.end(),
              [](const QString& lhs, const QString& rhs) {
                return lhs.compare(rhs, Qt::CaseInsensitive) < 0;
              });
    for (const QString& name : names) {
      if (!seen.contains(name)) {
        seen.insert(name);
        result.append(name);
      }
    }
  }
  return result;
}

QString resolveAsset(const QString& url, const QString& stylesheetDirectory)
{
  const QString trimmed = url.trimmed();
  const QUrl parsed(trimmed);
  if (trimmed.isEmpty() || trimmed.startsWith(':') ||
      QDir::isAbsolutePath(trimmed) || !parsed.scheme().isEmpty()) {
    return trimmed;
  }

  const QString cleaned = QDir::cleanPath(trimmed);
  if (cleaned == QStringLiteral("..") ||
      cleaned.startsWith(QStringLiteral("../")) ||
      QDir::isAbsolutePath(cleaned)) {
    return {};
  }

  const QString canonicalRoot = QDir(stylesheetDirectory).canonicalPath();
  if (canonicalRoot.isEmpty()) {
    return {};
  }

  QString current = canonicalRoot;
  const QStringList components = cleaned.split('/', Qt::SkipEmptyParts);
  for (const QString& component : components) {
    const QString exact = QDir(current).filePath(component);
    if (QFileInfo::exists(exact)) {
      current = exact;
      continue;
    }

    const QStringList matches = QDir(current).entryList(
        QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    QStringList caseMatches;
    for (const QString& name : matches) {
      if (name.compare(component, Qt::CaseInsensitive) == 0) {
        caseMatches.append(name);
      }
    }
    if (caseMatches.size() != 1) {
      return QDir(canonicalRoot).absoluteFilePath(cleaned);
    }
    current = QDir(current).filePath(caseMatches.front());
  }

  const QString canonicalFile = QFileInfo(current).canonicalFilePath();
  if (!isContainedPath(canonicalFile, canonicalRoot) ||
      !QFileInfo(canonicalFile).isFile()) {
    return {};
  }
  return canonicalFile;
}

QString resolveAssets(const QString& stylesheet,
                      const QString& stylesheetDirectory)
{
  static const QRegularExpression urlRe(
      QStringLiteral(R"(url\(\s*(['"]?)([^'")]+)\1\s*\))"),
      QRegularExpression::CaseInsensitiveOption);

  QString result;
  qsizetype last = 0;
  auto matches   = urlRe.globalMatch(stylesheet);
  while (matches.hasNext()) {
    const auto match = matches.next();
    result += stylesheet.mid(last, match.capturedStart() - last);

    QString resolved = resolveAsset(match.captured(2), stylesheetDirectory);
    if (resolved.isEmpty()) {
      result += QStringLiteral("url()");
    } else {
      resolved.replace('\\', QStringLiteral("\\\\"));
      resolved.replace('"', QStringLiteral("\\\""));
      result += QStringLiteral("url(\"%1\")").arg(resolved);
    }
    last = match.capturedEnd();
  }
  result += stylesheet.mid(last);
  return result;
}

}  // namespace StyleSheetPath
