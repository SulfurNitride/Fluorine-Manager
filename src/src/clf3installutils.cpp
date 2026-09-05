#include "clf3installutils.h"

#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>
#include <limits>

namespace Clf3InstallUtils
{
std::unique_ptr<QSettings> openSettings(const QString& configRoot)
{
  QString root = configRoot;
  if (root.isEmpty()) root = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
  if (root.isEmpty()) root = QDir::homePath() + QStringLiteral("/.config");
  auto settings = std::make_unique<QSettings>(
      QDir(root).filePath(QStringLiteral("fluorine/wabbajack.ini")), QSettings::IniFormat);
  settings->setFallbacksEnabled(false);

  // Earlier builds used default QSettings with an empty organization name.
  // Qt can still write its fallback file while reporting AccessError. Read that
  // file explicitly, importing only our keys and never reviving a cleared job.
  if (settings->status() == QSettings::NoError
      && !settings->value(QStringLiteral("clf3/legacyImported"), false).toBool()) {
    QSettings legacy(QDir(root).filePath(QStringLiteral("Unknown Organization/ModOrganizer.conf")),
                     QSettings::IniFormat);
    legacy.setFallbacksEnabled(false);
    for (const auto& key : legacy.allKeys()) {
      if (key.startsWith(QStringLiteral("clf3/")) && !settings->contains(key))
        settings->setValue(key, legacy.value(key));
    }
    settings->setValue(QStringLiteral("clf3/legacyImported"), true);
    settings->sync();
  }
  return settings;
}

QVector<SpaceRequirement> combineSpaceRequirements(
    const QVector<SpaceRequirement>& requirements)
{
  QVector<SpaceRequirement> result;
  for (const auto& requirement : requirements) {
    auto found = std::find_if(result.begin(), result.end(), [&](const auto& item) {
      return item.volume == requirement.volume;
    });
    if (found == result.end()) {
      result.push_back(requirement);
      result.last().required = qMax(qint64(0), requirement.required);
    } else {
      const qint64 extra = qMax(qint64(0), requirement.required);
      found->required += qMin(extra, std::numeric_limits<qint64>::max() - found->required);
      found->available = qMin(found->available, requirement.available);
      found->purpose += QStringLiteral(" + ") + requirement.purpose;
    }
  }
  return result;
}

qint64 temporarySpaceEstimate(qint64 archives, qint64 installed)
{
  // A planning allowance, not a guarantee of the pipeline's peak extraction use.
  return qMax(2LL * 1024 * 1024 * 1024, qMax(archives, installed) / 5);
}

QString prepareWritableDirectory(const QString& path)
{
  if (path.trimmed().isEmpty() || !QDir::isAbsolutePath(path))
    return QObject::tr("Choose an absolute folder path: %1").arg(path);
  if (!QDir().mkpath(path) || !QFileInfo(path).isDir())
    return QObject::tr("Cannot create or open folder: %1").arg(path);
  // Test actual writes, including ACLs, read-only mounts, and full filesystems.
  QTemporaryFile probe(QDir(path).filePath(QStringLiteral(".fluorine-write-XXXXXX")));
  if (!probe.open() || probe.write("check", 5) != 5 || !probe.flush())
    return QObject::tr("Cannot write to %1: %2").arg(path, probe.errorString());
  return {};
}

bool pathsOverlap(const QString& first, const QString& second)
{
  if (first.isEmpty() || second.isEmpty()) return false;
  auto resolve = [](const QString& path) {
    QFileInfo info(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
    QStringList suffix;
    while (!info.exists() && !info.isSymLink()) {
      suffix.prepend(info.fileName());
      const QString parent = info.absolutePath();
      if (parent == info.absoluteFilePath()) break;
      info.setFile(parent);
    }
    QString base = info.canonicalFilePath();
    if (base.isEmpty()) base = info.absoluteFilePath();
    for (const auto& part : suffix) base = QDir(base).filePath(part);
    return QDir::cleanPath(base);
  };
  const auto a = resolve(first);
  const auto b = resolve(second);
  auto contains = [](const QString& parent, const QString& child) {
    return parent == child || child.startsWith(parent.endsWith('/') ? parent : parent + '/');
  };
  return contains(a, b) || contains(b, a);
}

QString redactLog(QString text)
{
  // Download links may contain signed credentials; remove all queries/fragments
  // and URL user-info, including NXM keys and CDN signatures.
  static const QRegularExpression urls(QStringLiteral(R"((?:https?|nxm)://[^\s<>"']+)"),
                                       QRegularExpression::CaseInsensitiveOption);
  auto matches = urls.globalMatch(text);
  QVector<QRegularExpressionMatch> found;
  while (matches.hasNext()) found.push_back(matches.next());
  for (auto it = found.crbegin(); it != found.crend(); ++it) {
    QUrl url(it->captured());
    url.setUserInfo({});
    url.setQuery(QString());
    url.setFragment({});
    text.replace(it->capturedStart(), it->capturedLength(), url.toString());
  }
  // Redact the rest of any line containing a credential assignment. This also
  // covers JSON protocol diagnostics and unquoted Authorization headers.
  static const QRegularExpression credentials(
      QStringLiteral(R"((\b(?:authorization|proxy-authorization|api[_-]?key|access[_-]?token|refresh[_-]?token|token|password|secret|key)\b["']?\s*[:=]\s*)[^\r\n]*)"),
      QRegularExpression::CaseInsensitiveOption);
  text.replace(credentials, QStringLiteral("\\1[REDACTED]"));
  static const QRegularExpression bearer(QStringLiteral(R"(\bBearer\s+[^\s"']+)"),
                                         QRegularExpression::CaseInsensitiveOption);
  text.replace(bearer, QStringLiteral("Bearer [REDACTED]"));
  return text;
}

bool rootDeploymentMatches(const QString& checkpoint, const QString& jobId,
                           const QString& source, const QString& destination)
{
  if (jobId.isEmpty() || !QFileInfo(source).isDir() || !QFileInfo(destination).isDir()) return false;
  QSettings settings(checkpoint, QSettings::IniFormat);
  return settings.value("jobId").toString() == jobId
         && settings.value("source").toString() == QFileInfo(source).canonicalFilePath()
         && settings.value("destination").toString() == QFileInfo(destination).canonicalFilePath();
}

bool saveRootDeployment(const QString& checkpoint, const QString& jobId,
                        const QString& source, const QString& destination)
{
  // Older callers without a persisted job ID cannot opt into skipping deployment.
  if (jobId.isEmpty()) return true;
  QSettings settings(checkpoint, QSettings::IniFormat);
  settings.setValue("jobId", jobId);
  settings.setValue("source", QFileInfo(source).canonicalFilePath());
  settings.setValue("destination", QFileInfo(destination).canonicalFilePath());
  settings.sync();
  return settings.status() == QSettings::NoError;
}
}
