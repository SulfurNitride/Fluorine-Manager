#include "bethinipie.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

#include <uibase/imoinfo.h>
#include <uibase/iplugingame.h>
#include <uibase/iprofile.h>
#include <uibase/log.h>

using namespace MOBase;

static const char* GITHUB_REPO  = "SulfurNitride/Fluorine-Bethini-Pie-Performance-INI-Editor";
static const char* RELEASE_TAG  = "latest";
static const char* ASSET_NAME   = "BethiniPie-linux.tar.gz";

// Maps Fluorine managedGame()->gameName() to the BethINI Pie app name.
// MO2 game plugins sometimes return a short name (e.g. "New Vegas") that
// differs from BethINI Pie's app folder name ("Fallout New Vegas").
static const QMap<QString, QString> s_MO2ToBethiniName = {
    {"Skyrim Special Edition", "Skyrim Special Edition"},
    {"Fallout 4", "Fallout 4"},
    {"Fallout New Vegas", "Fallout New Vegas"},
    {"New Vegas", "Fallout New Vegas"},
    {"TTW", "Fallout New Vegas"},
    {"Starfield", "Starfield"},
};

// Maps the BethINI Pie app name to its Bethini.json INI directory key.
static const QMap<QString, QString> s_GameIniKeys = {
    {"Skyrim Special Edition", "sSkyrim Special EditionINIPath"},
    {"Fallout 4", "sFallout 4INIPath"},
    {"Fallout New Vegas", "sFallout New VegasINIPath"},
    {"Starfield", "sStarfieldINIPath"},
};

BethiniPie::BethiniPie() : m_MOInfo(nullptr), m_Network(nullptr) {}

bool BethiniPie::init(IOrganizer* moInfo)
{
  m_MOInfo = moInfo;
  return true;
}

QString BethiniPie::name() const
{
  return "BethINI Pie";
}

QString BethiniPie::localizedName() const
{
  return tr("BethINI Pie");
}

QString BethiniPie::author() const
{
  return "Fluorine";
}

QString BethiniPie::description() const
{
  return tr("Performance INI editor for Bethesda games");
}

VersionInfo BethiniPie::version() const
{
  return VersionInfo(1, 0, 0, VersionInfo::RELEASE_FINAL);
}

QList<PluginSetting> BethiniPie::settings() const
{
  return {};
}

QString BethiniPie::displayName() const
{
  return tr("BethINI Pie");
}

QString BethiniPie::tooltip() const
{
  return tr("Open BethINI Pie to optimize INI settings for the current game");
}

QIcon BethiniPie::icon() const
{
  return QIcon(":/bethinipie/icon");
}

QString BethiniPie::toolDir() const
{
  QString dataDir =
      QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
  return dataDir + "/fluorine/tools/bethini-pie";
}

QString BethiniPie::executablePath() const
{
  return toolDir() + "/BethiniPie/Bethini";
}

QString BethiniPie::localSha() const
{
  QFile f(toolDir() + "/.sha");
  if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return QString::fromUtf8(f.readAll()).trimmed();
  }
  return {};
}

void BethiniPie::saveLocalSha(const QString& sha) const
{
  QDir().mkpath(toolDir());
  QFile f(toolDir() + "/.sha");
  if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
    f.write(sha.toUtf8());
  }
}

QString BethiniPie::bethiniGameName() const
{
  if (!m_MOInfo || !m_MOInfo->managedGame())
    return {};
  const QString mo2Name = m_MOInfo->managedGame()->gameName();
  return s_MO2ToBethiniName.value(mo2Name);
}

QString BethiniPie::iniDirectoryKey() const
{
  QString game = bethiniGameName();
  return s_GameIniKeys.value(game);
}

void BethiniPie::writeBethiniConfig(const QString& bethiniDir,
                                    const QString& iniPath) const
{
  QString game    = bethiniGameName();
  QString iniKey  = iniDirectoryKey();
  if (game.isEmpty() || iniKey.isEmpty())
    return;

  // Write a Bethini.ini that pre-selects the game and points at the INI directory
  QString configPath = bethiniDir + "/Bethini.ini";
  QFile f(configPath);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
    log::error("BethINI Pie: failed to write config: {}", configPath);
    return;
  }

  QTextStream out(&f);
  out << "[General]\n";
  out << "sAppName=" << game << "\n";
  out << "bAlwaysSelectGame=0\n";
  out << "\n";
  out << "[Directories]\n";
  out << iniKey << "=" << iniPath << "\n";

  // Starfield has a second directory key for base game INIs
  if (game == "Starfield" && m_MOInfo->managedGame()) {
    QString gameDir = m_MOInfo->managedGame()->gameDirectory().absolutePath();
    out << "sStarfieldPath=" << gameDir << "\n";
  }

  out << "\n";
  f.close();

  log::info("BethINI Pie: wrote config for {} with INI path: {}", game, iniPath);
}

bool BethiniPie::ensureUpToDate() const
{
  if (!m_Network)
    m_Network = new QNetworkAccessManager();

  // Query the latest release from GitHub API
  QString apiUrl = QString("https://api.github.com/repos/%1/releases/tags/%2")
                       .arg(GITHUB_REPO, RELEASE_TAG);

  QNetworkRequest req{QUrl(apiUrl)};
  req.setHeader(QNetworkRequest::UserAgentHeader, "Fluorine-Manager");
  req.setRawHeader("Accept", "application/vnd.github+json");

  QEventLoop loop;
  QNetworkReply* reply = m_Network->get(req);
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

  // Timeout after 10 seconds
  QTimer timeout;
  timeout.setSingleShot(true);
  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  timeout.start(10000);
  loop.exec();

  if (!reply->isFinished() || reply->error() != QNetworkReply::NoError) {
    // Network error -- if we have a cached version, use it
    QString err = reply->isFinished()
                      ? QStringLiteral("network error %1").arg(reply->error())
                      : QStringLiteral("request timed out");
    log::warn("BethINI Pie: failed to check for updates: {}", err);
    reply->deleteLater();

    if (QFile::exists(executablePath())) {
      log::info("BethINI Pie: using cached version");
      return true;
    }
    return false;
  }

  QJsonDocument doc  = QJsonDocument::fromJson(reply->readAll());
  reply->deleteLater();
  QJsonObject release = doc.object();

  // Extract the commit SHA from the release body
  // The workflow puts "**Commit:** `<sha>`" in the body
  QString body      = release["body"].toString();
  QString remoteSha;
  int commitIdx = body.indexOf("`", body.indexOf("**Commit:**"));
  if (commitIdx >= 0) {
    int endIdx  = body.indexOf("`", commitIdx + 1);
    remoteSha   = body.mid(commitIdx + 1, endIdx - commitIdx - 1).trimmed();
  }

  if (remoteSha.isEmpty()) {
    log::warn("BethINI Pie: could not parse commit SHA from release");
    if (QFile::exists(executablePath()))
      return true;
    // Try to download anyway -- fall through
  }

  // Check if we're up to date
  QString currentSha = localSha();
  if (!remoteSha.isEmpty() && remoteSha == currentSha &&
      QFile::exists(executablePath())) {
    log::info("BethINI Pie: up to date ({})", remoteSha.left(8));
    return true;
  }

  // Find the download URL for the Linux tarball
  QString downloadUrl;
  QJsonArray assets = release["assets"].toArray();
  for (const QJsonValue& asset : assets) {
    if (asset.toObject()["name"].toString() == ASSET_NAME) {
      downloadUrl = asset.toObject()["browser_download_url"].toString();
      break;
    }
  }

  if (downloadUrl.isEmpty()) {
    log::error("BethINI Pie: no Linux asset found in release");
    return QFile::exists(executablePath());
  }

  // Download with progress dialog
  log::info("BethINI Pie: downloading update from {}",
            log::safeUrlForLog(downloadUrl));

  QProgressDialog progress(tr("Downloading BethINI Pie..."), tr("Cancel"), 0, 100,
                           parentWidget());
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(0);
  progress.setValue(0);

  QNetworkRequest dlReq{QUrl(downloadUrl)};
  dlReq.setHeader(QNetworkRequest::UserAgentHeader, "Fluorine-Manager");
  dlReq.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

  QNetworkReply* dlReply = m_Network->get(dlReq);

  QObject::connect(dlReply, &QNetworkReply::downloadProgress,
                   [&progress](qint64 received, qint64 total) {
                     if (total > 0) {
                       progress.setMaximum(static_cast<int>(total));
                       progress.setValue(static_cast<int>(received));
                     }
                   });

  QEventLoop dlLoop;
  QObject::connect(dlReply, &QNetworkReply::finished, &dlLoop, &QEventLoop::quit);
  QObject::connect(&progress, &QProgressDialog::canceled, dlReply, &QNetworkReply::abort);
  dlLoop.exec();

  if (dlReply->error() != QNetworkReply::NoError) {
    log::error("BethINI Pie: download failed with network error {}",
               dlReply->error());
    dlReply->deleteLater();
    progress.close();
    return QFile::exists(executablePath());
  }

  QByteArray data = dlReply->readAll();
  dlReply->deleteLater();
  progress.close();

  // Save tarball to temp file and extract
  QString dir = toolDir();
  QDir().mkpath(dir);

  QString tarPath = dir + "/BethiniPie-linux.tar.gz";
  {
    QFile tarFile(tarPath);
    if (!tarFile.open(QIODevice::WriteOnly)) {
      log::error("BethINI Pie: failed to write tarball");
      return false;
    }
    tarFile.write(data);
  }

  // Remove old installation
  QDir oldDir(dir + "/BethiniPie");
  if (oldDir.exists())
    oldDir.removeRecursively();

  // Extract
  QProcess tar;
  tar.setWorkingDirectory(dir);
  tar.start("tar", {"xzf", tarPath});
  tar.waitForFinished(30000);

  // Clean up tarball
  QFile::remove(tarPath);

  if (tar.exitCode() != 0) {
    log::error("BethINI Pie: extraction failed: {}", tar.readAllStandardError());
    return false;
  }

  // Make executable
  QFile::setPermissions(executablePath(),
                        QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                            QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                            QFileDevice::ExeGroup);

  // Fix Tcl module path -- PyInstaller bundles hardcode /usr/share/tcltk/... in tm.tcl
  // but the actual .tm files are in _internal/_tcl_data/tcl8/
  {
    QString tmTclPath = dir + "/BethiniPie/_internal/_tcl_data/tm.tcl";
    QFile tmTcl(tmTclPath);
    if (tmTcl.open(QIODevice::ReadWrite | QIODevice::Text)) {
      QString content = QString::fromUtf8(tmTcl.readAll());
      // Replace the hardcoded system path with a relative reference that Tcl
      // will resolve at runtime via [info library]
      content.replace(
          "variable paths {/usr/share/tcltk/tcl8.6/tcl8}",
          "variable paths [list [file join [info library] tcl8]]");
      tmTcl.seek(0);
      tmTcl.resize(0);
      tmTcl.write(content.toUtf8());
      tmTcl.close();
      log::info("BethINI Pie: patched tm.tcl module path");
    }
  }

  // PyInstaller puts datas into _internal/ but BethIni Pie expects apps/, icons/,
  // fonts/ in the executable's parent directory. Create symlinks.
  {
    QString bethiniDir = dir + "/BethiniPie";
    QString internalDir = bethiniDir + "/_internal";
    for (const QString& subdir : {"apps", "icons", "fonts"}) {
      QString target = internalDir + "/" + subdir;
      QString link   = bethiniDir + "/" + subdir;
      if (QFileInfo::exists(target) && !QFileInfo::exists(link)) {
        QFile::link(target, link);
        log::info("BethINI Pie: symlinked {} -> {}", link, target);
      }
    }
  }

  // Save the SHA
  if (!remoteSha.isEmpty())
    saveLocalSha(remoteSha);

  log::info("BethINI Pie: updated to {}", remoteSha.left(8));
  return true;
}

void BethiniPie::display() const
{
  if (!m_MOInfo) {
    QMessageBox::critical(parentWidget(), tr("Error"),
                          tr("Plugin not initialized."));
    return;
  }

  // Check if the current game is supported
  QString game = bethiniGameName();
  if (game.isEmpty()) {
    QString currentGame =
        m_MOInfo->managedGame() ? m_MOInfo->managedGame()->gameName() : "unknown";
    QMessageBox::warning(
        parentWidget(), tr("Unsupported Game"),
        tr("BethINI Pie does not support %1.\n\nSupported games: Skyrim Special "
           "Edition, Fallout 4, Fallout New Vegas, Starfield.")
            .arg(currentGame));
    return;
  }

  // Download / update BethINI Pie
  if (!ensureUpToDate()) {
    QMessageBox::critical(
        parentWidget(), tr("Download Failed"),
        tr("Failed to download BethINI Pie. Check your internet connection and "
           "try again."));
    return;
  }

  // Determine the INI directory to point BethINI Pie at
  // Use the first INI file's directory from the current profile
  QStringList iniFiles = m_MOInfo->managedGame()->iniFiles();
  QString iniPath;
  if (!iniFiles.isEmpty()) {
    QString firstIni = m_MOInfo->profile()->absoluteIniFilePath(iniFiles[0]);
    iniPath          = QFileInfo(firstIni).absolutePath();
  }

  if (iniPath.isEmpty()) {
    QMessageBox::warning(parentWidget(), tr("No INI Files"),
                         tr("Could not determine INI file location for the "
                            "current profile."));
    return;
  }

  // Write the Bethini.ini config
  QString bethiniDir = toolDir() + "/BethiniPie";
  writeBethiniConfig(bethiniDir, iniPath);

  // Launch BethINI Pie
  QString exe = executablePath();
  log::info("BethINI Pie: launching {} for {} (INIs at {})", exe, game, iniPath);

  QProcess* proc = new QProcess();
  proc->setWorkingDirectory(bethiniDir);
  proc->setProgram(exe);

  // Set TCL/TK library paths so the bundled Tcl finds its data files
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.insert("TCL_LIBRARY", bethiniDir + "/_internal/_tcl_data");
  env.insert("TK_LIBRARY", bethiniDir + "/_internal/_tk_data");
  proc->setProcessEnvironment(env);

  // Clean up QProcess when it finishes
  QObject::connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                   proc, &QProcess::deleteLater);

  proc->start();

  if (!proc->waitForStarted(5000)) {
    log::error("BethINI Pie: failed to start: {}", proc->errorString());
    QMessageBox::critical(parentWidget(), tr("Launch Failed"),
                          tr("Failed to launch BethINI Pie:\n%1")
                              .arg(proc->errorString()));
    delete proc;
  }
}
