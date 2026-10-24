#include "steamcloudsync.h"
#include "steamcef.h"
#include "vdfparser.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>
#include <QUuid>
#include <QWebSocket>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QSet>
#include <stdexcept>
#include <unistd.h>

namespace {
constexpr auto leaf = "CD Projekt Red/Cyberpunk 2077";
enum AppCloudStatus {
  CloudSynchronized = 3,
  CloudSyncFailed = 8,
  CloudConflict = 9,
  CloudPendingElsewhere = 10,
};
[[noreturn]] void fail(const QString& message)
{
  throw std::runtime_error(message.toStdString());
}
void pause(int milliseconds)
{
  QEventLoop loop;
  QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
  loop.exec(QEventLoop::AllEvents);
}
QByteArray read(const QString& path)
{
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
QString findRoot()
{
  QStringList candidates;
  for (const auto* relative : {"/.local/share/Steam", "/.steam/steam", "/.steam/debian-installation"}) {
    const QString root = QFileInfo(QDir::homePath() + relative).canonicalFilePath();
    if (!root.isEmpty() && !root.contains("/.var/app/") && !root.contains("/snap/")
        && QFileInfo::exists(root + "/steamapps") && !candidates.contains(root))
      candidates << root;
  }
  if (candidates.size() != 1)
    fail(QObject::tr("Automatic cloud sync currently requires one native Steam installation. "
                     "Flatpak/Snap Steam and ambiguous installations are not supported yet."));
  return candidates.first();
}
bool steamRunning(const QString& root)
{
  // Modern native Steam does not always publish steam.pid.
  for (const auto& pid : QDir("/proc").entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    bool ok;
    pid.toLongLong(&ok);
    if (!ok || QFileInfo("/proc/" + pid).ownerId() != getuid()) continue;
    const QString executable = QFileInfo("/proc/" + pid + "/exe").symLinkTarget();
    if (executable.startsWith(root + '/') && QFileInfo(executable).fileName() == "steam") return true;
  }
  return false;
}
bool steamGameRunning()
{
  for (const QString& pid : QDir("/proc").entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    bool ok;
    pid.toInt(&ok);
    if (!ok || pid.toLongLong() == QCoreApplication::applicationPid()) continue;
    const QFileInfo proc("/proc/" + pid);
    if (proc.ownerId() != getuid()) continue;
    const QString executable = QFileInfo(proc.filePath() + "/exe").symLinkTarget();
    const QString name = QFileInfo(executable).fileName();
    if (name.startsWith("steam") || name.contains("ModOrganizer") || name == "fluorine-manager") continue;
    const auto environment = read(proc.filePath() + "/environ").split('\0');
    for (const auto& value : environment) {
      if (value.startsWith("SteamAppId=") && value.mid(11).toULongLong() != 0)
        return true;
    }
  }
  return false;
}

QMap<QString, QByteArray> saveHashes(const QString& folder)
{
  QMap<QString, QByteArray> hashes;
  if (!QFileInfo(folder).isDir()) return hashes;
  QDirIterator iterator(folder, QDir::Files | QDir::Hidden | QDir::System,
                        QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    iterator.next();
    const auto info = iterator.fileInfo();
    if (info.fileName() == "steam_autocloud.vdf") continue;
    if (info.isSymLink() || !info.isFile())
      fail(QObject::tr("A save collection contains a link or special file."));
    QFile file(info.absoluteFilePath());
    QCryptographicHash hash(QCryptographicHash::Sha1);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file))
      fail(QObject::tr("A Cyberpunk save file could not be read."));
    hashes.insert(QDir(folder).relativeFilePath(info.absoluteFilePath()), hash.result());
  }
  return hashes;
}

void copyTree(const QString& source, const QString& destination)
{
  QDirIterator iterator(source, QDir::AllEntries | QDir::Hidden | QDir::System
                        | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    iterator.next();
    const auto info = iterator.fileInfo();
    if (info.fileName() == "steam_autocloud.vdf") continue;
    const QString output = QDir(destination).filePath(QDir(source).relativeFilePath(info.absoluteFilePath()));
    if (info.isSymLink() || (!info.isDir() && !info.isFile()))
      fail(QObject::tr("A save collection contains a link or special file."));
    if (info.isDir()) {
      if (!QDir().mkpath(output)) fail(QObject::tr("Could not copy the selected save collection."));
    } else if (!QDir().mkpath(QFileInfo(output).absolutePath()) || !QFile::copy(info.absoluteFilePath(), output)) {
      fail(QObject::tr("Could not copy the selected save collection."));
    }
  }
}
}

bool SteamCloud::supported(const QString& appId, const QString& executable)
{
  return appId.trimmed() == "1091500"
         && QFileInfo(executable).fileName().compare("Cyberpunk2077.exe", Qt::CaseInsensitive) == 0;
}

bool SteamCloud::validAccount(const QString& account)
{
  const auto numeric = account.toULongLong();
  return QRegularExpression("^[0-9]{17}$").match(account).hasMatch()
         && numeric > 76561197960265728ULL && numeric < 76561197960265728ULL + 4294967296ULL;
}

QProcessEnvironment SteamCloud::clientEnvironment(QProcessEnvironment environment)
{
  for (const auto* name : {"LD_LIBRARY_PATH", "LD_PRELOAD", "QT_PLUGIN_PATH", "PATH", "XDG_DATA_DIRS"}) {
    const QString original = "FLUORINE_ORIG_" + QString(name);
    if (environment.contains(original)) {
      const auto value = environment.value(original);
      if (value.isEmpty()) environment.remove(name);
      else environment.insert(name, value);
      environment.remove(original);
    } else if (QString(name) != "PATH" && QString(name) != "XDG_DATA_DIRS") {
      environment.remove(name);
    }
  }
  for (const auto* name : {"QT_QPA_PLATFORM_PLUGIN_PATH", "QT_QPA_PLATFORM", "PYTHONHOME", "PYTHONPATH",
                           "WINEPREFIX", "WINEDLLOVERRIDES", "SteamAppId", "SteamGameId"})
    environment.remove(name);
  return environment;
}

void SteamCloud::JobLog::consume(const QByteArray& data)
{
  m_buffer += data;
  for (int newline; (newline = m_buffer.indexOf('\n')) >= 0;) {
    const auto line = m_buffer.left(newline);
    m_buffer.remove(0, newline + 1);
    if (!line.contains("[AppID 1091500]")) continue;
    if (line.contains("Starting sync")
        && line.contains(m_upload ? "AC Exit" : "AC Launch")) m_started = true;
    if (!m_started) continue;
    const auto lower = line.toLower();
    if (lower.contains("conflict") || lower.contains("failed") || lower.contains("result fail")
        || lower.contains("error") || lower.contains("not running autocloud"))
      m_error = QObject::tr("Steam reported a cloud error or conflict. Resolve it in Steam, then retry. "
                            "Local saves have been retained.");
    if (m_upload) {
      if (line.contains("Upload complete, result OK") || line.contains("Upload complete in build list"))
        m_complete = true;
    } else if (line.contains("AutoCloud done. Watching")) {
      m_complete = true;
    }
  }
  if (m_buffer.size() > 1024 * 1024)
    m_error = QObject::tr("Steam returned an oversized cloud log record.");
}

QString SteamCloud::mapSaveFolder(const QString& source, const QString& target)
{
  const QFileInfo src(source), dst(target);
  if (!src.isDir() || src.fileName() != "Cyberpunk 2077"
      || dst.fileName() != "Cyberpunk 2077" || !QDir::isAbsolutePath(source)
      || !QDir::isAbsolutePath(target))
    fail(QObject::tr("The Cyberpunk save-folder mapping is invalid."));
  if (src.canonicalFilePath() == dst.canonicalFilePath()) return {};
  if (dst.isSymLink())
    fail(QObject::tr("Steam's save folder is already linked to a different location. "
                     "Resolve that mapping before enabling automatic sync."));
  if (source.startsWith(target + '/') || target.startsWith(source + '/'))
    fail(QObject::tr("Cloud save folders must not contain each other."));
  if (dst.exists() && !dst.isDir()) fail(QObject::tr("Steam's save path is not a directory."));
  if (!QDir().mkpath(dst.absolutePath())) fail(QObject::tr("Cannot create Steam's save-folder parent."));
  QString backup;
  if (dst.exists()) {
    backup = target + ".fluorine-backup-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!QDir().rename(target, backup)) fail(QObject::tr("Cannot back up Steam's existing save folder."));
  }
  if (!QFile::link(src.canonicalFilePath(), target)) {
    if (!backup.isEmpty() && !QDir().rename(backup, target))
      fail(QObject::tr("Could not create the save link or restore its backup: %1").arg(backup));
    fail(QObject::tr("Could not create the shared Steam save-folder link."));
  }
  return backup;
}

bool SteamCloud::matchingJournal(const QJsonObject& journal, const QString& account,
                                const QString& source)
{
  return journal.isEmpty() || (journal.value("account").toString() == account
                              && journal.value("source").toString() == source);
}

QString SteamCloud::snapshotSaves(const QString& source, const QString& backupRoot)
{
  if (!QFileInfo(source).isDir() || !QDir::isAbsolutePath(backupRoot)
      || backupRoot.startsWith(source + '/'))
    fail(QObject::tr("Invalid save backup location."));
  if (!QDir().mkpath(backupRoot)) fail(QObject::tr("Cannot create the save backup directory."));
  QStringList files;
  qint64 bytes = 0;
  QDirIterator iterator(source, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    iterator.next();
    const auto file = iterator.fileInfo();
    if (file.isSymLink() || (!file.isDir() && !file.isFile()))
      fail(QObject::tr("The save folder contains a link or special file. Review it before cloud sync."));
    if (file.isFile()) { files << file.absoluteFilePath(); bytes += file.size(); }
  }
  if (QStorageInfo(backupRoot).bytesAvailable() < bytes + 16 * 1024 * 1024)
    fail(QObject::tr("There is not enough disk space to back up saves before downloading."));
  const QString destination = backupRoot + '/' + QDateTime::currentDateTimeUtc().toString("yyyyMMdd-hhmmss")
                              + '-' + QUuid::createUuid().toString(QUuid::WithoutBraces);
  if (!QDir().mkpath(destination)) fail(QObject::tr("Cannot create a save snapshot."));
  for (const auto& path : files) {
    const QString output = destination + '/' + QDir(source).relativeFilePath(path);
    if (!QDir().mkpath(QFileInfo(output).absolutePath()) || !QFile::copy(path, output))
      fail(QObject::tr("Could not complete the save snapshot. Cloud download was not requested."));
  }
  return destination;
}

bool SteamCloud::cacheMatchesSaves(const QString& source, const QByteArray& cache)
{
  const auto root = parseVdf(QString::fromUtf8(cache));
  const auto* app = root.get("1091500");
  if (!app || !app->isObject() || !QFileInfo(source).isDir()) return false;
  QSet<QString> localFiles;
  QDirIterator iterator(source, QDir::Files | QDir::Hidden | QDir::System,
                        QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    iterator.next();
    const auto info = iterator.fileInfo();
    if (info.fileName() == "steam_autocloud.vdf") continue;
    if (info.isSymLink() || !info.isFile()) return false;
    const QString name = QString(leaf) + '/' + QDir(source).relativeFilePath(info.absoluteFilePath());
    localFiles.insert(name);
    const auto* record = app->get(name);
    if (!record || record->getString("root") != "9" || record->getString("syncstate") != "1"
        || record->getString("persiststate") != "0"
        || record->getString("size").toLongLong() != info.size()) return false;
    QFile file(info.absoluteFilePath());
    QCryptographicHash hash(QCryptographicHash::Sha1);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)
        || QString::fromLatin1(hash.result().toHex()) != record->getString("sha")) return false;
  }
  for (auto it = app->asObject().constBegin(); it != app->asObject().constEnd(); ++it) {
    if (!it.key().startsWith(QString(leaf) + '/') || !it.value().isObject()) continue;
    if (it.value().getString("root") == "9" && it.value().getString("persiststate") == "0"
        && it.value().getString("syncstate") == "1" && !localFiles.contains(it.key()))
      return false;
  }
  return true;
}

bool SteamCloud::saveCollectionsMatch(const QString& first, const QString& second)
{
  return saveHashes(first) == saveHashes(second);
}

QString SteamCloud::replaceSaveCollection(const QString& source, const QString& destination)
{
  const QFileInfo src(source), dst(destination);
  if (!src.isDir() || !dst.isDir() || src.fileName() != "Cyberpunk 2077"
      || dst.fileName() != "Cyberpunk 2077" || src.canonicalFilePath() == dst.canonicalFilePath())
    fail(QObject::tr("The selected save collections are invalid."));
  saveHashes(source); // Validate the entire source before moving anything.
  const QString backup = destination + ".fluorine-replaced-"
                         + QUuid::createUuid().toString(QUuid::WithoutBraces);
  if (!QDir().rename(destination, backup))
    fail(QObject::tr("Could not preserve the replaced save collection."));
  if (!QDir().mkpath(destination)) {
    QDir().rename(backup, destination);
    fail(QObject::tr("Could not recreate the active save folder."));
  }
  try {
    copyTree(source, destination);
  } catch (...) {
    QDir(destination).removeRecursively();
    QDir().rename(backup, destination);
    throw;
  }
  return backup;
}

SteamCloudSync::SteamCloudSync(QWidget* parent, QString settingsFile, QString gameDirectory,
                               QString saveDirectory)
    : m_parent(parent), m_settingsFile(std::move(settingsFile)),
      m_gameDirectory(std::move(gameDirectory)), m_source(std::move(saveDirectory))
{}
SteamCloudSync::~SteamCloudSync() = default;

bool SteamCloudSync::optIn(QWidget* parent, const QString& settingsFile)
{
  QSettings settings(settingsFile, QSettings::IniFormat);
  if (!settings.contains(SteamCloud::setting)) {
    const auto answer = QMessageBox::question(parent, QObject::tr("Steam Cloud for Cyberpunk"),
        QObject::tr("Enable automatic Steam Cloud sync before and after playing Cyberpunk?\n\n"
                    "This links Steam's save folder to this prefix. Existing Steam saves are backed up. "
                    "Steam may need to restart with its local debugging interface enabled. "
                    "Sync can update your local and cloud saves.\n\n"
                    "You can change this in the Saves tab's right-click menu."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    settings.setValue(SteamCloud::setting, answer == QMessageBox::Yes);
    settings.sync();
  }
  return settings.value(SteamCloud::setting, false).toBool();
}

QJsonObject SteamCloudSync::evaluate(const QString& expression)
{
  try {
    return SteamCef::Client().evaluate(expression).toObject();
  } catch (const SteamCef::Error& e) {
    switch (e.kind()) {
    case SteamCef::Error::Kind::Unavailable:
      fail(QObject::tr("Steam's local debugging connection is unavailable."));
    case SteamCef::Error::Kind::NoContext:
      fail(QObject::tr("Steam did not expose a trusted local UI context."));
    case SteamCef::Error::Kind::NoResponse:
    case SteamCef::Error::Kind::ScriptFailed:
      break;
    }
    fail(QObject::tr("Steam's internal cloud interface did not respond successfully."));
  }
}

void SteamCloudSync::ensureClient()
{
  try {
    if (steamRunning(m_root) && evaluate("(()=>({ready:!!SteamClient.Console?.ExecCommand}))()").value("ready").toBool()) return;
  } catch (const std::exception&) {}
  if (steamGameRunning())
    fail(QObject::tr("Close running Steam games before enabling the cloud connection. Steam has not been restarted."));
  const QString steam = QStandardPaths::findExecutable("steam");
  if (steam.isEmpty()) fail(QObject::tr("The native Steam launcher was not found."));
  if (steamRunning(m_root)) {
    if (QMessageBox::question(m_parent, QObject::tr("Restart Steam for cloud sync"),
        QObject::tr("Restart Steam with its local debugging interface enabled? "
                    "Downloads may pause briefly. Do not start a game during this operation. "
                    "The interface stays enabled until Steam is restarted normally."),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
      fail(QObject::tr("Steam restart cancelled. The game was not launched."));
    QProcess shutdown;
    shutdown.setProcessEnvironment(SteamCloud::clientEnvironment(QProcessEnvironment::systemEnvironment()));
    shutdown.start(steam, {"-shutdown"});
    if (!shutdown.waitForStarted(3000)) fail(QObject::tr("Could not request Steam shutdown."));
    shutdown.waitForFinished(3000);
    QElapsedTimer timer;
    timer.start();
    while (steamRunning(m_root) && timer.elapsed() < 30000) pause(250);
    if (steamRunning(m_root)) fail(QObject::tr("Steam did not exit. Close it manually and retry; no process was killed."));
  }
  QProcess launch;
  launch.setProgram(steam);
  launch.setArguments({"-cef-enable-debugging"});
  launch.setProcessEnvironment(SteamCloud::clientEnvironment(QProcessEnvironment::systemEnvironment()));
  launch.setWorkingDirectory(QDir::homePath());
  launch.setStandardInputFile(QProcess::nullDevice());
  launch.setStandardOutputFile(QProcess::nullDevice());
  launch.setStandardErrorFile(QProcess::nullDevice());
  if (!launch.startDetached()) fail(QObject::tr("Could not start Steam."));
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 60000) {
    try {
      if (steamRunning(m_root) && evaluate("(()=>({ready:!!SteamClient.Console?.ExecCommand}))()").value("ready").toBool()) {
        currentAccount(); // The UI briefly reports a placeholder user during startup.
        return;
      }
    } catch (const std::exception&) {}
    pause(500);
  }
  fail(QObject::tr("Steam is not ready. Sign in to Steam and retry."));
}

QString SteamCloudSync::currentAccount()
{
  const auto user = evaluate(R"JS(new Promise((resolve,reject)=>{
    let h; const t=setTimeout(()=>{h?.unregister();reject(new Error('timeout'));},5000);
    h=SteamClient.User.RegisterForCurrentUserChanges(u=>{
      clearTimeout(t);setTimeout(()=>h?.unregister(),0);
      resolve({account:u.strSteamID,offline:u.bIsOfflineMode});
    });
  }))JS");
  const QString account = user.value("account").toString();
  if (!SteamCloud::validAccount(account) || user.value("offline").toBool())
    fail(QObject::tr("Steam must be signed in and online before cloud sync."));
  return account;
}

QJsonObject SteamCloudSync::cloudState()
{
  return evaluate(R"JS(new Promise((resolve,reject)=>{
      let h; const t=setTimeout(()=>{h?.unregister();reject(new Error('timeout'));},5000);
      h=SteamClient.Apps.RegisterForAppDetails(1091500,d=>{
        clearTimeout(t);setTimeout(()=>h?.unregister(),0);
        resolve({available:d.bCloudAvailable,accountEnabled:d.bCloudEnabledForAccount,
                 appEnabled:d.bCloudEnabledForApp,status:d.eCloudStatus});
      });
    }))JS");
}

void SteamCloudSync::checkAccountAndCloud(bool requireSynchronized)
{
  QElapsedTimer ready;
  ready.start();
  while (true) {
    if (currentAccount() != m_account)
      fail(QObject::tr("The Steam account changed. Cloud sync was stopped to protect both accounts' saves."));
    const auto state = cloudState();
    if (state.value("available").toBool() && state.value("accountEnabled").toBool()
        && state.value("appEnabled").toBool()) {
      const int status = state.value("status").toInt();
      if (status == CloudConflict)
        fail(QObject::tr("Steam reports a Cyberpunk Cloud conflict. Open Cyberpunk in your Steam Library, "
                         "select the local or cloud collection in Steam's conflict dialog, then retry. "
                         "Fluorine has not renamed or overwritten either collection."));
      if (status == CloudSyncFailed)
        fail(QObject::tr("Steam reports that Cyberpunk Cloud synchronization failed. Retry it in Steam, then launch again."));
      if (status == CloudPendingElsewhere)
        fail(QObject::tr("Steam reports a pending Cyberpunk Cloud operation on another device. Finish that operation, then retry."));
      if (!requireSynchronized || status == CloudSynchronized) return;
      if (ready.elapsed() < 10000) { pause(250); continue; }
      fail(QObject::tr("Steam finished the file operation but did not report a synchronized cloud state."));
    }
    // These flags initially arrive false while the restarted client loads app data.
    if (ready.elapsed() < 30000) { pause(500); continue; }
    if (!state.value("available").toBool())
      fail(QObject::tr("Steam has not loaded Cyberpunk's cloud information, or cloud support is unavailable. Please retry once Steam is ready."));
    fail(QObject::tr("Enable Steam Cloud for both your Steam account and Cyberpunk, then retry."));
  }
}

QString SteamCloudSync::cachePath() const
{
  const auto accountId = m_account.toULongLong() - 76561197960265728ULL;
  return m_root + "/userdata/" + QString::number(accountId) + "/1091500/remotecache.vdf";
}

QJsonObject SteamCloudSync::journal() const
{
  if (!QFileInfo::exists(m_journalPath)) return {};
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(read(m_journalPath), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject() || document.object().isEmpty())
    fail(QObject::tr("The Steam Cloud recovery record is unreadable. No synchronization was attempted."));
  return document.object();
}

void SteamCloudSync::saveJournal(bool pending, const QString& status)
{
  QSaveFile file(m_journalPath);
  if (!file.open(QIODevice::WriteOnly)) fail(QObject::tr("Cannot save the cloud recovery record."));
  const auto bytes = QJsonDocument(QJsonObject{{"account", m_account}, {"source", m_source},
      {"pending", pending}, {"status", status},
      {"updated", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}}).toJson();
  if (file.write(bytes) != bytes.size() || !file.commit())
    fail(QObject::tr("Cannot commit the cloud recovery record."));
  QSettings settings(m_settingsFile, QSettings::IniFormat);
  settings.setValue("fluorine/steam_cloud_status", status);
  settings.sync();
  qInfo().noquote() << "Steam Cloud (Cyberpunk):" << status;
}

void SteamCloudSync::sync(bool upload, QProgressDialog* progress, qint64 logStart)
{
  checkAccountAndCloud();
  QFile log(m_root + "/logs/cloud_log.txt");
  if (!log.open(QIODevice::ReadOnly))
    fail(QObject::tr("Cannot monitor Steam's cloud log; sync completion cannot be verified."));
  const qint64 initialSize = log.size();
  if (logStart < 0 || logStart > initialSize) logStart = initialSize;
  if (!log.seek(logStart))
    fail(QObject::tr("Cannot monitor Steam's cloud log; sync completion cannot be verified."));
  SteamCloud::JobLog job(upload);
  job.consume(log.readAll());

  auto uploadVerified = [&] {
    if (!upload) return true;
    for (int attempt = 0; attempt < 20; ++attempt) {
      if (SteamCloud::cacheMatchesSaves(m_source, read(cachePath()))) return true;
      pause(250);
    }
    return false;
  };

  // Steam itself may finish the normal AC Exit upload while Fluorine is still
  // unmounting the VFS. Accept that exact session's job instead of requesting a
  // duplicate and then waiting forever for a second log record.
  if (job.complete() && uploadVerified()) {
    checkAccountAndCloud(true);
    return;
  }
  if (!job.started()) {
    // A synchronized cache that already matches every local file is sufficient
    // for a no-op upload; there is no new transfer for Steam to log.
    if (upload && SteamCloud::cacheMatchesSaves(m_source, read(cachePath()))) {
      checkAccountAndCloud(true);
      return;
    }
    evaluate(QString("(()=>{SteamClient.Console.ExecCommand('cloud_sync_%1 1091500');return {requested:true};})()")
                 .arg(upload ? "up" : "down"));
  }
  QElapsedTimer timer;
  timer.start();
  qint64 nextStateCheck = 2000;
  while (timer.elapsed() < 60000) {
    if (progress && progress->wasCanceled())
      fail(QObject::tr("Waiting for Steam Cloud was cancelled. Steam may still finish in the background; "
                       "the upload remains pending until Fluorine verifies it."));
    if (log.size() < log.pos()) fail(QObject::tr("Steam rotated its cloud log during synchronization. Please retry."));
    job.consume(log.readAll());
    if (!job.error().isEmpty()) fail(job.error());
    if (job.complete()) {
      if (!uploadVerified()) fail(QObject::tr("Steam finished a sync job, but its synced-file records do not "
                                              "match the complete local save collection. Upload remains pending."));
      checkAccountAndCloud(true);
      return;
    }
    if (timer.elapsed() >= nextStateCheck) {
      checkAccountAndCloud(); // Detect Steam's conflict/failure state promptly.
      nextStateCheck += 2000;
    }
    pause(250);
  }
  fail(QObject::tr("Steam cloud synchronization timed out. Completion is unverified; check Steam before retrying."));
}

bool SteamCloudSync::prepare()
{
  try {
    QProgressDialog progress(QObject::tr("Preparing Steam Cloud…"), QObject::tr("Cancel"), 0, 0, m_parent);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    progress.show();
    m_root = findRoot();
    if (!QDir::isAbsolutePath(m_source)
        || !QDir::cleanPath(m_source).endsWith("/drive_c/users/steamuser/Saved Games/" + QString(leaf)))
      fail(QObject::tr("The game plugin did not resolve a supported Cyberpunk Wine save folder."));
    const QString stateDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/fluorine/steam-cloud";
    if (!QDir().mkpath(stateDir)) fail(QObject::tr("Cannot create cloud recovery storage."));
    m_journalPath = stateDir + "/1091500.json";
    m_lock = std::make_unique<QLockFile>(stateDir + "/1091500.lock");
    m_lock->setStaleLockTime(0); // A game session can legitimately last hours.
    if (!m_lock->tryLock()) fail(QObject::tr("Another Fluorine Cyberpunk cloud session is active."));
    if (!QDir().mkpath(m_source)) fail(QObject::tr("Cannot create the active save folder."));
    m_source = QFileInfo(m_source).canonicalFilePath();
    const QString steamapps = QFileInfo(QDir(m_gameDirectory).filePath("../..")).canonicalFilePath();
    const auto manifest = AppManifest::fromVdf(QString::fromUtf8(read(steamapps + "/appmanifest_1091500.acf")));
    if (manifest.app_id != "1091500" || manifest.install_dir.isEmpty()
        || QFileInfo(steamapps + "/common/" + manifest.install_dir).canonicalFilePath()
            != QFileInfo(m_gameDirectory).canonicalFilePath())
      fail(QObject::tr("This game folder does not match a Steam Cyberpunk installation. Automatic cloud sync is unavailable."));
    const QString prefix = steamapps + "/compatdata/1091500/pfx";
    if (!QFileInfo::exists(prefix + "/drive_c/users/steamuser"))
      fail(QObject::tr("Steam's Cyberpunk prefix is missing. Automatic save-root resolution is not available for this installation yet."));
    m_target = prefix + "/drive_c/users/steamuser/Saved Games/" + leaf;
    ensureClient();
    if (steamGameRunning()) fail(QObject::tr("Close running Steam games before starting a cloud-synced launch."));
    m_account = currentAccount();
    checkAccountAndCloud();
    const auto previous = journal();
    if (!SteamCloud::matchingJournal(previous, m_account, m_source))
      fail(QObject::tr("This Steam account or save prefix differs from the recorded cloud session. "
                       "Automatic sync is blocked to avoid mixing saves. Disable it in the Saves menu until the mapping is reviewed."));
    const bool firstSession = previous.isEmpty()
                              || (QFileInfo(m_target).isDir() && !QFileInfo(m_target).isSymLink());
    // Bind ownership even if mapping or the first download fails afterward.
    if (firstSession) saveJournal(false, QObject::tr("Preparing first cloud session"));

    bool uploadInitialLocal = false;
    if (firstSession && QFileInfo(m_target).isDir() && !QFileInfo(m_target).isSymLink()) {
      // Establish Steam's freshest view while it still owns its original save
      // directory. Both pre-sync collections are retained before Steam can act.
      const QString localSnapshot = SteamCloud::snapshotSaves(m_source, stateDir + "/backups/1091500");
      const QString steamSnapshot = SteamCloud::snapshotSaves(m_target, stateDir + "/backups/1091500");
      QSettings settings(m_settingsFile, QSettings::IniFormat);
      settings.setValue("fluorine/steam_cloud_backup", localSnapshot);
      settings.setValue("fluorine/steam_cloud_steam_backup", steamSnapshot);
      settings.sync();
      progress.setLabelText(QObject::tr("Checking the existing Cyberpunk Steam Cloud collection…"));
      sync(false, &progress);

      const auto localFiles = saveHashes(m_source);
      const auto steamFiles = saveHashes(m_target);
      if (localFiles.isEmpty() && !steamFiles.isEmpty()) {
        const QString replaced = SteamCloud::replaceSaveCollection(m_target, m_source);
        qInfo().noquote() << "Empty Fluorine save folder preserved at:" << replaced;
      } else if (!localFiles.isEmpty() && !steamFiles.isEmpty() && localFiles != steamFiles) {
        progress.hide();
        QMessageBox choice(QMessageBox::Question, QObject::tr("Choose Cyberpunk save collection"),
            QObject::tr("Steam Cloud and this Fluorine prefix contain different Cyberpunk save collections. "
                        "Cyberpunk must keep its original slot names, so Fluorine will not merge or renumber them.\n\n"
                        "Both current collections have been backed up. Which complete collection should become active?"),
            QMessageBox::NoButton, m_parent);
        auto* useSteam = choice.addButton(QObject::tr("Use Steam Cloud saves"), QMessageBox::AcceptRole);
        auto* useLocal = choice.addButton(QObject::tr("Use Fluorine saves"), QMessageBox::DestructiveRole);
        choice.addButton(QMessageBox::Cancel);
        choice.setDefaultButton(qobject_cast<QPushButton*>(useSteam));
        choice.exec();
        if (choice.clickedButton() == useSteam) {
          const QString replaced = SteamCloud::replaceSaveCollection(m_target, m_source);
          qInfo().noquote() << "Previous Fluorine save collection preserved at:" << replaced;
        } else if (choice.clickedButton() == useLocal) {
          uploadInitialLocal = true;
        } else {
          fail(QObject::tr("Save collection selection cancelled. Neither collection was replaced."));
        }
        progress.show();
      }
    }
    const QString backup = SteamCloud::mapSaveFolder(m_source, m_target);
    if (!backup.isEmpty()) {
      qInfo().noquote() << "Steam Cloud save-folder backup:" << backup;
      QMessageBox::information(m_parent, QObject::tr("Steam save folder linked"),
          QObject::tr("Steam now uses Fluorine's save folder. Its previous folder was preserved at:\n%1").arg(backup));
    }
    if (uploadInitialLocal) {
      progress.setLabelText(QObject::tr("Uploading the selected Fluorine save collection…"));
      sync(true, &progress);
      saveJournal(false, QObject::tr("Selected Fluorine save collection uploaded"));
    }
    bool localChanges = !SteamCloud::cacheMatchesSaves(m_source, read(cachePath()));
    if (localChanges && !previous.value("pending").toBool())
      saveJournal(true, QObject::tr("Local save changes await upload decision"));
    if (previous.value("pending").toBool() || localChanges) {
      // Ask Steam to refresh remote state before offering an upload. If another
      // device changed the cloud too, Steam enters its conflict state and this
      // operation stops without selecting a side.
      progress.setLabelText(QObject::tr("Checking Steam Cloud for save conflicts…"));
      sync(false, &progress);
      localChanges = !SteamCloud::cacheMatchesSaves(m_source, read(cachePath()));
    }
    if (localChanges) {
      if (QMessageBox::question(m_parent, QObject::tr("Unsynced local saves"),
          QObject::tr("Steam has checked for remote changes and the local Cyberpunk collection still differs. "
                      "Upload the complete local collection now? Cyberpunk's slot names will be left unchanged."),
          QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        fail(QObject::tr("Pending upload retained. The game was not launched."));
      progress.setLabelText(QObject::tr("Uploading pending Cyberpunk saves…"));
      sync(true, &progress);
      saveJournal(false, QObject::tr("Pending upload completed"));
    } else if (previous.value("pending").toBool()) {
      saveJournal(false, QObject::tr("Steam Cloud already contains the pending local saves"));
    }
    progress.setLabelText(QObject::tr("Downloading Cyberpunk Steam Cloud saves…"));
    const QString snapshot = SteamCloud::snapshotSaves(m_source, stateDir + "/backups/1091500");
    QSettings settings(m_settingsFile, QSettings::IniFormat);
    settings.setValue("fluorine/steam_cloud_backup", snapshot);
    settings.sync();
    sync(false, &progress);
    saveJournal(false, QObject::tr("Pre-launch Steam Cloud sync completed"));
    return true;
  } catch (const std::exception& error) {
    displayFailure(QString::fromUtf8(error.what()));
    return false;
  }
}

bool SteamCloudSync::markLaunching()
{
  try {
    QFile log(m_root + "/logs/cloud_log.txt");
    m_launchLogOffset = log.open(QIODevice::ReadOnly) ? log.size() : -1;
    saveJournal(true, QObject::tr("Game launching; upload pending"));
    m_launched = true;
    return true;
  } catch (const std::exception& error) {
    displayFailure(QString::fromUtf8(error.what()));
    return false;
  }
}

bool SteamCloudSync::finish(bool cleanExit)
{
  if (!m_launched) return false;
  try {
    if (!cleanExit) fail(QObject::tr("The game ended abnormally or process tracking failed. "
                                    "Automatic upload was not requested; local saves remain pending."));
    QProgressDialog progress(QObject::tr("Uploading Cyberpunk Steam Cloud saves…"), QObject::tr("Cancel"), 0, 0, m_parent);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    progress.show();
    if (QFileInfo(m_target).canonicalFilePath() != m_source)
      fail(QObject::tr("The save-folder mapping changed during play. Upload was not requested."));
    sync(true, &progress, m_launchLogOffset);
    saveJournal(false, QObject::tr("Post-game Steam Cloud sync completed"));
  } catch (const std::exception& error) {
    displayFailure(QString::fromUtf8(error.what()));
    m_launched = false;
    return false;
  }
  m_launched = false;
  return true;
}

void SteamCloudSync::displayFailure(const QString& message)
{
  qWarning().noquote() << "Steam Cloud:" << message;
  QSettings settings(m_settingsFile, QSettings::IniFormat);
  settings.setValue("fluorine/steam_cloud_status", QObject::tr("Not synced: %1").arg(message));
  settings.sync();
  QMessageBox::warning(m_parent, QObject::tr("Steam Cloud — synchronization not confirmed"), message);
}
