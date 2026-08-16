#include "fluorineupdater.h"

#include <fluorine_build_info.h>
#include <uibase/log.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>

#include <array>

namespace
{
// Hardcoded for now — if the project ever moves, these become CMake-configured
// constants like the version info. Keeping them here avoids wiring a second
// generated header just for two strings.
constexpr const char* kRepoOwner   = "SulfurNitride";
constexpr const char* kRepoProject = "Fluorine-Manager";

QString buildApiUrl(FluorineUpdater::Channel c)
{
  const QString base =
      QStringLiteral("https://api.github.com/repos/%1/%2/releases")
          .arg(kRepoOwner, kRepoProject);
  return c == FluorineUpdater::Channel::Stable
             ? base + QStringLiteral("/latest")
             : base + QStringLiteral("/tags/nightly");
}

// Returns {major, minor, patch}; components that fail to parse stay at -1.
std::array<int, 3> parseSemver(const QString& tagOrVersion)
{
  QString s = tagOrVersion.trimmed();
  if (s.startsWith('v') || s.startsWith('V')) {
    s.remove(0, 1);
  }

  std::array<int, 3> out{-1, -1, -1};
  const QStringList parts = s.split('.', Qt::SkipEmptyParts);
  for (int i = 0; i < 3 && i < parts.size(); ++i) {
    bool ok = false;
    // Drop any prerelease suffix like "0.1.5-beta.2" — we only care about
    // the numeric triple for the coarse newer/older decision.
    QString num = parts[i];
    const int dashIdx = num.indexOf(QRegularExpression("[^0-9]"));
    if (dashIdx >= 0) {
      num = num.left(dashIdx);
    }
    const int v = num.toInt(&ok);
    if (ok) {
      out[i] = v;
    }
  }
  return out;
}

bool isStableNewerThan(const QString& releaseTag, const QString& currentSemver)
{
  const auto a = parseSemver(releaseTag);
  const auto b = parseSemver(currentSemver);
  for (int i = 0; i < 3; ++i) {
    if (a[i] != b[i]) {
      return a[i] > b[i];
    }
  }
  return false;
}
}  // namespace

FluorineUpdater::FluorineUpdater(QObject* parent)
    : QObject(parent), m_net(new QNetworkAccessManager(this))
{}

FluorineUpdater::~FluorineUpdater()
{
  cancel();
}

FluorineUpdater::Channel FluorineUpdater::buildChannel()
{
#if FLUORINE_IS_NIGHTLY_BUILD
  return Channel::Nightly;
#else
  return Channel::Stable;
#endif
}

QString FluorineUpdater::channelToString(Channel c)
{
  return c == Channel::Nightly ? QStringLiteral("nightly")
                               : QStringLiteral("stable");
}

FluorineUpdater::Channel FluorineUpdater::channelFromString(const QString& s,
                                                            Channel fallback)
{
  if (s.compare(QStringLiteral("nightly"), Qt::CaseInsensitive) == 0 ||
      s.compare(QStringLiteral("beta"), Qt::CaseInsensitive) == 0) {
    return Channel::Nightly;
  }
  if (s.compare(QStringLiteral("stable"), Qt::CaseInsensitive) == 0) {
    return Channel::Stable;
  }
  return fallback;
}

void FluorineUpdater::checkForUpdates(Channel channel)
{
  cancel();

  QNetworkRequest req{QUrl(buildApiUrl(channel))};
  req.setRawHeader("User-Agent", "Fluorine-Manager/updater");
  req.setRawHeader("Accept", "application/vnd.github+json");
  req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::NoLessSafeRedirectPolicy);
  req.setTransferTimeout(30000);

  QNetworkReply* reply = createRequest(req);
  if (reply == nullptr) {
    emit checkFailed(tr("Unable to start the update check"));
    return;
  }

  m_reply = reply;
  const QPointer<QNetworkReply> guardedReply(reply);
  connect(reply, &QNetworkReply::finished, this,
          [this, guardedReply, channel]() {
            if (guardedReply) {
              onReplyFinished(guardedReply.data(), channel);
            }
          });

  // A custom or cached backend may already be complete before the connection
  // above is installed. Authenticate and consume that completion immediately.
  if (reply->isFinished()) {
    onReplyFinished(reply, channel);
  }
}

void FluorineUpdater::cancel()
{
  const QPointer<QNetworkReply> previous = m_reply;
  m_reply.clear();
  if (!previous) {
    return;
  }

  // Clear ownership and disconnect before abort(): QNetworkReply is allowed to
  // emit finished synchronously from abort(). Any already-queued callback is
  // still harmless because onReplyFinished authenticates the exact reply.
  QObject::disconnect(previous, nullptr, this, nullptr);
  previous->abort();
  if (previous) {
    previous->deleteLater();
  }
  // This manager is updater-private. Retiring its connection immediately
  // avoids leaving the completed GitHub TLS socket in Qt's idle connection
  // pool, where Qt 6.11 can warn when the peer closes it at the timeout.
  m_net->clearConnectionCache();
}

QNetworkReply*
FluorineUpdater::createRequest(const QNetworkRequest& request)
{
  return m_net->get(request);
}

void FluorineUpdater::onReplyFinished(QNetworkReply* reply, Channel channel)
{
  if (reply == nullptr || reply != m_reply.data()) {
    if (reply != nullptr) {
      reply->deleteLater();
    }
    return;
  }

  const auto error        = reply->error();
  const QString errorText = reply->errorString();
  const QByteArray raw = error == QNetworkReply::NoError ? reply->readAll()
                                                          : QByteArray();

  m_reply.clear();
  // Schedule retirement before notifying observers, and never touch the reply
  // after a terminal signal: a receiver may synchronously destroy this updater.
  reply->deleteLater();
  // The updater owns its QNetworkAccessManager exclusively, so this cannot
  // disrupt Nexus, download-manager, or plugin traffic.
  m_net->clearConnectionCache();

  if (error != QNetworkReply::NoError) {
    MOBase::log::warn("update check failed ({}): {}",
                      channelToString(channel), errorText);
    emit checkFailed(errorText);
    return;
  }

  const QJsonDocument doc = QJsonDocument::fromJson(raw);
  if (!doc.isObject()) {
    emit checkFailed(tr("GitHub returned an unexpected response"));
    return;
  }

  ReleaseInfo info;
  info.channel = channel;
  const QJsonObject obj = doc.object();

  bool ok = false;
  if (channel == Channel::Nightly) {
    ok = parseNightlyRelease(obj, info);
  } else {
    ok = parseStableRelease(obj, info);
  }

  if (!ok) {
    emit checkFailed(tr("Unable to parse release metadata"));
    return;
  }

  if (channel == Channel::Nightly) {
    const QString currentBuild   = QStringLiteral(FLUORINE_BUILD_NUMBER);
    const QString currentTs      = QStringLiteral(FLUORINE_BUILD_TIMESTAMP);
    const QString currentCommit  = QStringLiteral(FLUORINE_BUILD_COMMIT);

    bool remoteBuildOk = false;
    bool currentBuildOk = false;
    const qulonglong remoteBuild = info.buildNumber.toULongLong(&remoteBuildOk);
    const qulonglong installedBuild =
        currentBuild.toULongLong(&currentBuildOk);

    if (!remoteBuildOk && info.timestamp.isEmpty()) {
      emit checkFailed(
          tr("Nightly release is missing build metadata; cannot compare"));
      return;
    }

    const bool sameCommit = !currentCommit.isEmpty() && !info.commit.isEmpty() &&
                            currentCommit == info.commit;
    if (sameCommit) {
      emit upToDate(info);
    } else if (remoteBuildOk && currentBuildOk) {
      if (remoteBuild > installedBuild) {
        emit updateAvailable(info);
      } else {
        emit upToDate(info);
      }
    } else if (!currentTs.isEmpty() && info.timestamp > currentTs) {
      emit updateAvailable(info);
    } else if (currentBuild.isEmpty() && currentTs.isEmpty()) {
      // A stable/dev build explicitly checking Nightly has no rolling build
      // identity, so offer the current Nightly.
      emit updateAvailable(info);
    } else {
      emit upToDate(info);
    }
  } else {
    const QString currentVersion = QStringLiteral(FLUORINE_VERSION_STRING);
    if (isStableNewerThan(info.versionString, currentVersion)) {
      emit updateAvailable(info);
    } else {
      emit upToDate(info);
    }
  }
}

bool FluorineUpdater::parseStableRelease(const QJsonObject& obj,
                                         ReleaseInfo& out)
{
  out.tagName = obj.value(QStringLiteral("tag_name")).toString();
  out.name    = obj.value(QStringLiteral("name")).toString();
  out.releaseNotes = obj.value(QStringLiteral("body")).toString();
  out.htmlUrl = obj.value(QStringLiteral("html_url")).toString();

  if (out.tagName.isEmpty()) {
    return false;
  }

  out.versionString = out.tagName;
  if (out.versionString.startsWith('v') || out.versionString.startsWith('V')) {
    out.versionString.remove(0, 1);
  }

  // Pick a tar.gz asset if available, otherwise a zip.
  const QJsonArray assets = obj.value(QStringLiteral("assets")).toArray();
  QString zipFallback;
  for (const QJsonValue& v : assets) {
    const QJsonObject a = v.toObject();
    const QString name  = a.value(QStringLiteral("name")).toString();
    const QString url   =
        a.value(QStringLiteral("browser_download_url")).toString();
    if (name.endsWith(QStringLiteral(".tar.gz"), Qt::CaseInsensitive) ||
        name.endsWith(QStringLiteral(".tgz"), Qt::CaseInsensitive)) {
      out.downloadUrl = url;
      break;
    }
    if (name.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive) &&
        zipFallback.isEmpty()) {
      zipFallback = url;
    }
  }
  if (out.downloadUrl.isEmpty()) {
    out.downloadUrl = zipFallback;
  }
  return true;
}

bool FluorineUpdater::parseNightlyRelease(const QJsonObject& obj,
                                          ReleaseInfo& out)
{
  out.tagName = obj.value(QStringLiteral("tag_name")).toString();
  out.name    = obj.value(QStringLiteral("name")).toString();
  out.releaseNotes = obj.value(QStringLiteral("body")).toString();
  out.htmlUrl = obj.value(QStringLiteral("html_url")).toString();

  // Extract fluorine-meta block from release body. Format emitted by the CI
  // workflow:
  //   <!-- fluorine-meta
  //   channel=nightly
  //   build_number=<monotonic GitHub Actions run number>
  //   timestamp=YYYYMMDDHHMM
  //   commit=<full sha>
  //   short=<7-char sha>
  //   -->
  const QString body = obj.value(QStringLiteral("body")).toString();
  const int metaStart = body.indexOf(QStringLiteral("<!-- fluorine-meta"));
  if (metaStart < 0) {
    MOBase::log::debug("nightly release body missing fluorine-meta block");
    return true;  // partially parsed — let caller surface checkFailed
  }
  const int metaEnd = body.indexOf(QStringLiteral("-->"), metaStart);
  const QString block =
      metaEnd > metaStart ? body.mid(metaStart, metaEnd - metaStart) : QString();

  const QStringList lines =
      block.split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                  Qt::SkipEmptyParts);
  for (const QString& line : lines) {
    const int eq = line.indexOf('=');
    if (eq <= 0) {
      continue;
    }
    const QString key   = line.left(eq).trimmed();
    const QString value = line.mid(eq + 1).trimmed();
    if (key == QStringLiteral("build_number")) {
      out.buildNumber = value;
    } else if (key == QStringLiteral("timestamp")) {
      out.timestamp = value;
    } else if (key == QStringLiteral("commit")) {
      out.commit = value;
    }
  }

  // Prefer a .tar.gz asset with "nightly" in the name, else any .tar.gz,
  // else fall back to a .zip. GitHub Releases serves whatever we upload,
  // but some CI flows produce zip artifacts; accepting both lets users
  // swap formats without breaking the in-app updater.
  const QJsonArray assets = obj.value(QStringLiteral("assets")).toArray();
  QString tarFallback;
  QString zipFallback;
  auto isArchive = [](const QString& name) {
    return name.endsWith(QStringLiteral(".tar.gz"), Qt::CaseInsensitive) ||
           name.endsWith(QStringLiteral(".tgz"), Qt::CaseInsensitive) ||
           name.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive);
  };
  auto isZip = [](const QString& name) {
    return name.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive);
  };
  for (const QJsonValue& v : assets) {
    const QJsonObject a = v.toObject();
    const QString name  = a.value(QStringLiteral("name")).toString();
    if (!isArchive(name)) {
      continue;
    }
    const QString url =
        a.value(QStringLiteral("browser_download_url")).toString();
    if (name.contains(QStringLiteral("nightly"), Qt::CaseInsensitive) &&
        !isZip(name)) {
      out.downloadUrl = url;
      break;
    }
    if (isZip(name)) {
      if (zipFallback.isEmpty()) zipFallback = url;
    } else if (tarFallback.isEmpty()) {
      tarFallback = url;
    }
  }
  if (out.downloadUrl.isEmpty()) {
    out.downloadUrl = !tarFallback.isEmpty() ? tarFallback : zipFallback;
  }
  return true;
}
