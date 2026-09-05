#include "wabbajackpostinstall.h"
#include "clf3installutils.h"

#include "curatedinstancebootstrap.h"
#include "fluorinepaths.h"
#include "pefileutils.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <utility>

namespace
{
QString pluginName(const QString& gameId)
{
  static const QHash<QString, QString> names{
      {QStringLiteral("falloutnewvegas"), QStringLiteral("Fallout New Vegas")},
      {QStringLiteral("fallout3"), QStringLiteral("Fallout 3")},
      {QStringLiteral("fallout4"), QStringLiteral("Fallout 4")},
      {QStringLiteral("skyrim"), QStringLiteral("Skyrim")},
      {QStringLiteral("skyrimspecialedition"),
       QStringLiteral("Skyrim Special Edition")},
      {QStringLiteral("skyrimvr"), QStringLiteral("Skyrim VR")},
      {QStringLiteral("oblivion"), QStringLiteral("Oblivion")},
      {QStringLiteral("morrowind"), QStringLiteral("Morrowind")},
  };
  return names.value(gameId.toLower(), gameId);
}

bool copyTree(const QString& source, const QString& destination, QString* error)
{
  const QDir sourceRoot(source);
  if (!sourceRoot.exists()) {
    if (error) *error = QString("Source folder is missing: %1").arg(source);
    return false;
  }
  QDirIterator iterator(source, QDir::Files | QDir::NoSymLinks,
                        QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    const QString sourceFile = iterator.next();
    QString relative = sourceRoot.relativeFilePath(sourceFile);
    relative.replace('\\', '/');
    if (relative.isEmpty() || relative == QStringLiteral("..")
        || relative.startsWith(QStringLiteral("../"))) {
      if (error) *error = QString("Unsafe post-install path: %1").arg(relative);
      return false;
    }
    const QString destinationFile = QDir(destination).filePath(relative);
    if (!QDir().mkpath(QFileInfo(destinationFile).absolutePath())) {
      if (error) *error = QString("Cannot create destination for %1").arg(relative);
      return false;
    }
    QFile input(sourceFile);
    if (!input.open(QIODevice::ReadOnly)) {
      if (error) *error = QString("Cannot read %1").arg(sourceFile);
      return false;
    }
    QSaveFile output(destinationFile);
    if (!output.open(QIODevice::WriteOnly)) {
      if (error) *error = QString("Cannot write %1").arg(destinationFile);
      return false;
    }
    while (!input.atEnd()) {
      const QByteArray data = input.read(1024 * 1024);
      if (data.isEmpty() && input.error() != QFileDevice::NoError) {
        if (error) *error = QString("Cannot read %1").arg(sourceFile);
        return false;
      }
      if (output.write(data) != data.size()) {
        if (error) *error = QString("Cannot write %1").arg(destinationFile);
        return false;
      }
    }
    if (!output.commit()) {
      if (error) *error = QString("Cannot finish writing %1").arg(destinationFile);
      return false;
    }
    QFile::setPermissions(destinationFile, QFileInfo(sourceFile).permissions());
  }
  if (error) error->clear();
  return true;
}

QJsonObject compatibilityProfile(const QString& machineName)
{
  QFile catalog(QStringLiteral(":/fluorine/wabbajack-compatibility/catalog.json"));
  if (!catalog.open(QIODevice::ReadOnly)) return {};
  const auto root = QJsonDocument::fromJson(catalog.readAll()).object();
  for (const auto& value : root.value(QStringLiteral("profiles")).toArray()) {
    const auto profile = value.toObject();
    if (profile.value(QStringLiteral("machineName")).toString().compare(
            machineName, Qt::CaseInsensitive) == 0)
      return profile;
  }
  return {};
}

QJsonObject compatibilityTool(const QString& id)
{
  QFile catalog(QStringLiteral(":/fluorine/wabbajack-compatibility/catalog.json"));
  if (!catalog.open(QIODevice::ReadOnly)) return {};
  const auto root = QJsonDocument::fromJson(catalog.readAll()).object();
  for (const auto& value : root.value(QStringLiteral("tools")).toArray()) {
    const auto tool = value.toObject();
    if (tool.value(QStringLiteral("id")).toString() == id) return tool;
  }
  return {};
}

QString find7z()
{
  const QStringList bundled{
      QDir(fluorineDataDir()).filePath(QStringLiteral("bin/7zz")),
      QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("7zz")),
  };
  for (const QString& path : bundled)
    if (QFileInfo(path).isExecutable()) return path;
  for (const QString& name : {QStringLiteral("7zz"), QStringLiteral("7z"),
                              QStringLiteral("7za")}) {
    const QString path = QStandardPaths::findExecutable(name);
    if (!path.isEmpty()) return path;
  }
  return {};
}

bool isNativeExecutable(const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return false;
  const QByteArray header = file.read(4);
  return header.startsWith(QByteArrayLiteral("\x7f" "ELF"))
         || header.startsWith(QByteArrayLiteral("#!"));
}

}

WabbajackPostInstall::WabbajackPostInstall(QObject* parent)
    : QObject(parent), m_nexus(this)
{
  connect(&m_nativePatcher,
          qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [this](int code, QProcess::ExitStatus status) {
            nativePatcherFinished(code, status);
          });

  connect(&m_nexus, &NexusArtifactDownloader::authorizationRequired, this,
          &WabbajackPostInstall::nexusAuthorizationRequired);
  connect(&m_nexus, &NexusArtifactDownloader::progress, this,
          [this](const QString& id, qint64 received, qint64 total) {
            emit toolProgress(id, received, total);
          });
  connect(&m_nexus, &NexusArtifactDownloader::failed, this,
          [this](const QString&, const QString& reason) { fail(reason); });
  connect(&m_nexus, &NexusArtifactDownloader::completed, this,
          [this](const QString& id, const QString& path, const QJsonObject& metadata) {
            emit logLine(tr("Using Nexus file %1 (%2).")
                             .arg(metadata.value(QStringLiteral("file_name")).toString(),
                                  metadata.value(QStringLiteral("version")).toString()));
            if (id == QStringLiteral("fnv-4gb-patcher-linux")
                || id == QStringLiteral("oblivion-4gb-patcher-linux"))
              extractNativePatcher(path);
            else
              fail(tr("Downloaded an unexpected Nexus setup tool: %1").arg(id));
          });

  for (auto* process : {&m_extractor, &m_nativePatcher}) {
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
      if (m_active && error == QProcess::FailedToStart)
        fail(tr("Could not start compatibility tool: %1").arg(process->errorString()));
    });
  }

  connect(&m_extractor,
          qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [this](int code, QProcess::ExitStatus status) {
            if (!m_active) return;
            if (status != QProcess::NormalExit || code != 0)
              return fail(tr("The Linux 4 GB patcher archive could not be extracted."));
            launchExtractedNativePatcher();
          });
}

bool WabbajackPostInstall::hasAdapter(const QString& machineName)
{
  return compatibilityProfile(machineName).value(QStringLiteral("implemented")).toBool();
}

QString WabbajackPostInstall::detectedStore(const QString& gamePath)
{
  const QString normalized = QDir::fromNativeSeparators(gamePath).toLower();
  if (normalized.contains(QStringLiteral("/steamapps/common/")))
    return QStringLiteral("Steam");
  const QDir directory(gamePath);
  if (!directory.entryList({QStringLiteral("goggame-*.info")}, QDir::Files).isEmpty())
    return QStringLiteral("GOG");
  if (directory.exists(QStringLiteral(".egstore"))
      || normalized.contains(QStringLiteral("/heroic/")))
    return QStringLiteral("Epic Games");
  return {};
}

void WabbajackPostInstall::start(const WabbajackPostInstallConfig& config)
{
  m_config = config;
  m_adjustments.clear();
  m_warnings.clear();
  m_profile = compatibilityProfile(m_config.machineName);
  m_gameRootPath.clear();
  m_manualRootPath.clear();
  m_patcherExtractPath.clear();
  m_gameExecutable.clear();
  m_nativePatcherToolId.clear();
  m_nativePatcherTargets.clear();
  m_requiredPatcherBackups.clear();
  m_active = true;
  ensureInstance();
}

void WabbajackPostInstall::ensureInstance()
{
  const QString ini = QDir(m_config.installPath).filePath(QStringLiteral("ModOrganizer.ini"));
  if (!QFileInfo::exists(ini)) {
    emit stepStarted(QStringLiteral("instance-bootstrap"),
                     tr("Create Fluorine instance configuration"));
    const auto result = bootstrapCuratedInstance(
        m_config.installPath, pluginName(m_config.gameId), m_config.sourceGamePath,
        m_config.downloadsPath);
    if (!result.first) return fail(result.second);
    m_adjustments.push_back(tr("Created ModOrganizer.ini and portable instance folders."));
    emit stepFinished(QStringLiteral("instance-bootstrap"));
  }

  if (hasAdapter(m_config.machineName)) beginAdapter();
  else finish();
}

void WabbajackPostInstall::beginAdapter()
{
  m_gameExecutable = m_profile.value(QStringLiteral("executable")).toString();
  if (m_gameExecutable.isEmpty())
    return fail(tr("The compatibility profile has no game executable."));
  if (!QFileInfo::exists(QDir(m_config.sourceGamePath).filePath(
          m_gameExecutable))) {
    return fail(tr("%1 automatic setup requires the original %2 game folder.")
                    .arg(m_config.machineName, pluginName(m_config.gameId)));
  }
  const QString gameRootMode =
      m_profile.value(QStringLiteral("gameRoot")).toString();
  if (gameRootMode == QStringLiteral("original")) {
    m_gameRootPath = QDir::cleanPath(m_config.sourceGamePath);
  } else if (gameRootMode == QStringLiteral("authored-stock-game")) {
    const QString stockFolder =
        m_profile.value(QStringLiteral("stockGameFolder")).toString();
    if (stockFolder.isEmpty())
      return fail(tr("The compatibility profile does not name its authored Stock Game folder."));
    m_gameRootPath = QDir(m_config.installPath).filePath(stockFolder);
    if (!QFileInfo::exists(QDir(m_gameRootPath).filePath(m_gameExecutable))) {
      return fail(tr("The modlist expects an authored Stock Game folder, but it was not installed."));
    }
  } else {
    return fail(tr("The compatibility profile has an unknown game-root policy."));
  }
  for (const auto& value :
       m_profile.value(QStringLiteral("manualRootFolders")).toArray()) {
    const QString name = value.toString();
    const QString candidate = QDir(m_config.installPath).filePath(name);
    if (QFileInfo(candidate).isDir()) {
      m_manualRootPath = candidate;
      break;
    }
  }
  gameRootReady();
}

void WabbajackPostInstall::gameRootReady()
{
  deployRootFiles();
}

void WabbajackPostInstall::deployRootFiles()
{
  if (m_manualRootPath.isEmpty()) {
    if (m_profile.value(QStringLiteral("manualRootRequired")).toBool())
      return fail(tr("The modlist did not contain its expected manual game-root folder."));
    return runProfilePatcher();
  }
  const QString checkpoint = QDir(m_config.installPath).filePath(QStringLiteral(".fluorine-root-deployment.ini"));
  if (Clf3InstallUtils::rootDeploymentMatches(checkpoint, m_config.jobId,
                                            m_manualRootPath, m_gameRootPath)) {
    emit logLine(tr("Game-root files were already deployed for this setup attempt; preserving patched files."));
    return runProfilePatcher();
  }
  emit stepStarted(QStringLiteral("root-files"), tr("Deploy game-root files"));
  QString error;
  if (!copyTree(m_manualRootPath, m_gameRootPath, &error)) return fail(error);
  if (!Clf3InstallUtils::saveRootDeployment(checkpoint, m_config.jobId,
                                          m_manualRootPath, m_gameRootPath))
    return fail(tr("Cannot save the game-root deployment checkpoint. Patching has not started."));
  m_adjustments.push_back(tr("Deployed the modlist's files into its documented game root."));
  emit stepFinished(QStringLiteral("root-files"));
  runProfilePatcher();
}

void WabbajackPostInstall::runProfilePatcher()
{
  const QString patcher =
      m_profile.value(QStringLiteral("patcher")).toString(QStringLiteral("none"));
  if (patcher == QStringLiteral("fnv-nexus-linux")) return runVnvPatcher();
  if (patcher == QStringLiteral("oblivion-nexus-linux"))
    return runOblivionPatcher();
  if (patcher == QStringLiteral("fallout3-anniversary-manual")) {
    const QString backup =
        QDir(m_gameRootPath).filePath(QStringLiteral("Fallout3_backup.exe"));
    if (!QFileInfo::exists(backup)) {
      m_warnings.push_back(tr("The Fallout Anniversary Patcher still needs to be run "
                              "from the game root/Patcher.exe."));
    }
    return configureInstance();
  }
  configureInstance();
}

void WabbajackPostInstall::runVnvPatcher()
{
  const QString game =
      QDir(m_gameRootPath).filePath(QStringLiteral("FalloutNV.exe"));
  const QString backup =
      QDir(m_gameRootPath).filePath(QStringLiteral("FalloutNV_backup.exe"));
  if (QFileInfo::exists(backup) && peLargeAddressAware(game))
    return configureInstance();

  m_nativePatcherTargets = {game};
  m_requiredPatcherBackups = {backup};
  emit stepStarted(QStringLiteral("vnv-patcher"), tr("Patch FalloutNV.exe"));
  acquireNativePatcher(QStringLiteral("fnv-4gb-patcher-linux"));
}

void WabbajackPostInstall::runOblivionPatcher()
{
  const QString game =
      QDir(m_gameRootPath).filePath(QStringLiteral("Oblivion.exe"));
  const QString launcher =
      QDir(m_gameRootPath).filePath(QStringLiteral("OblivionLauncher.exe"));
  if (!QFileInfo::exists(game))
    return fail(tr("Stock Game is missing Oblivion.exe."));

  m_nativePatcherTargets = {game};
  if (QFileInfo::exists(launcher)) {
    m_nativePatcherTargets.push_back(launcher);
  } else {
    m_warnings.push_back(tr("OblivionLauncher.exe was not present to patch."));
  }

  m_requiredPatcherBackups.clear();
  for (const QString& target : std::as_const(m_nativePatcherTargets)) {
    if (!peLargeAddressAware(target))
      m_requiredPatcherBackups.push_back(target + QStringLiteral(".Backup"));
  }
  if (m_requiredPatcherBackups.isEmpty()) {
    m_adjustments.push_back(
        tr("Verified 4 GB support for the Oblivion executables."));
    return configureInstance();
  }

  emit stepStarted(QStringLiteral("oblivion-4gb"),
                   tr("Enable 4 GB support for Oblivion"));
  acquireNativePatcher(QStringLiteral("oblivion-4gb-patcher-linux"));
}

void WabbajackPostInstall::acquireNativePatcher(const QString& toolId)
{
  const QJsonObject tool = compatibilityTool(toolId);
  if (tool.isEmpty() || tool.value(QStringLiteral("source")).toString()
                            != QStringLiteral("nexus"))
    return fail(tr("The reviewed Linux 4 GB patcher definition is missing."));
  m_nativePatcherToolId = toolId;
  emit statusChanged(tr("Resolving the Linux 4 GB patcher from Nexus…"));
  m_nexus.start({tool.value(QStringLiteral("id")).toString(),
                 tool.value(QStringLiteral("name")).toString(),
                 tool.value(QStringLiteral("domain")).toString(),
                 tool.value(QStringLiteral("modId")).toInt(),
                 tool.value(QStringLiteral("fileNameContains")).toString(),
                 tool.value(QStringLiteral("minimumVersion")).toString()});
}

void WabbajackPostInstall::extractNativePatcher(const QString& archivePath)
{
  const QString sevenZip = find7z();
  if (sevenZip.isEmpty())
    return fail(tr("A 7z executable is required to unpack the Nexus Linux patcher."));
  m_patcherExtractPath =
      QFileInfo(archivePath).absolutePath() + QStringLiteral("/extracted-")
      + m_nativePatcherToolId;
  QDir(m_patcherExtractPath).removeRecursively();
  if (!QDir().mkpath(m_patcherExtractPath))
    return fail(tr("Cannot create the Linux patcher extraction folder."));
  emit statusChanged(tr("Unpacking the Linux 4 GB patcher…"));
  m_extractor.start(sevenZip,
                    {QStringLiteral("x"), QStringLiteral("-y"),
                     QStringLiteral("-o") + m_patcherExtractPath, archivePath});
}

void WabbajackPostInstall::launchExtractedNativePatcher()
{
  QString patcher;
  const QJsonObject tool = compatibilityTool(m_nativePatcherToolId);
  const QString expectedName = tool.value(QStringLiteral("executable")).toString();
  QDirIterator iterator(m_patcherExtractPath, QDir::Files | QDir::NoSymLinks,
                        QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    const QString candidate = iterator.next();
    const QString name = QFileInfo(candidate).fileName();
    const bool nameMatches =
        expectedName.isEmpty()
            ? (name.contains(QStringLiteral("patch"), Qt::CaseInsensitive)
               || name.contains(QStringLiteral("fnv"), Qt::CaseInsensitive))
            : name.compare(expectedName, Qt::CaseInsensitive) == 0;
    if (nameMatches && isNativeExecutable(candidate)) {
      patcher = candidate;
      break;
    }
  }
  if (patcher.isEmpty())
    return fail(tr("The selected Nexus release did not contain a native Linux patcher."));
  QFile file(patcher);
  file.setPermissions(file.permissions() | QFileDevice::ExeOwner
                      | QFileDevice::ExeUser | QFileDevice::ExeGroup);
  emit statusChanged(tr("Applying the Linux 4 GB patch to the documented game root…"));
  m_nativePatcher.setWorkingDirectory(m_gameRootPath);
  QStringList arguments;
  if (m_nativePatcherToolId == QStringLiteral("oblivion-4gb-patcher-linux")) {
    for (const QString& target : std::as_const(m_nativePatcherTargets))
      arguments.push_back(QFileInfo(target).fileName());
  }
  m_nativePatcher.start(patcher, arguments);
}

void WabbajackPostInstall::nativePatcherFinished(
    int code, QProcess::ExitStatus status)
{
  if (!m_active) return;
  if (status != QProcess::NormalExit || code != 0)
    return fail(tr("The native Linux 4 GB patcher exited unsuccessfully."));
  for (const QString& target : std::as_const(m_nativePatcherTargets)) {
    if (!peLargeAddressAware(target))
      return fail(tr("The native Linux patcher did not enable 4 GB support for %1.")
                      .arg(QFileInfo(target).fileName()));
  }
  for (const QString& backup : std::as_const(m_requiredPatcherBackups)) {
    if (!QFileInfo::exists(backup))
      return fail(tr("The native Linux patcher did not create its expected backup: %1")
                      .arg(QFileInfo(backup).fileName()));
  }

  if (m_nativePatcherToolId == QStringLiteral("fnv-4gb-patcher-linux")) {
    m_adjustments.push_back(
        tr("Patched FalloutNV.exe with the Nexus Linux patcher."));
    emit stepFinished(QStringLiteral("vnv-patcher"));
  } else if (m_nativePatcherToolId
             == QStringLiteral("oblivion-4gb-patcher-linux")) {
    m_adjustments.push_back(
        tr("Patched and verified the Oblivion executables with Nexus mod 56555."));
    emit stepFinished(QStringLiteral("oblivion-4gb"));
  } else {
    return fail(tr("An unknown native Linux patcher completed."));
  }
  configureInstance();
}

void WabbajackPostInstall::configureInstance()
{
  emit stepStarted(QStringLiteral("instance-config"),
                   tr("Configure the modlist for Fluorine"));
  QSettings ini(QDir(m_config.installPath).filePath(QStringLiteral("ModOrganizer.ini")),
                QSettings::IniFormat);
  ini.setValue(QStringLiteral("gameName"),
               curatedGamePluginId(pluginName(m_config.gameId)));
  ini.setValue(QStringLiteral("Settings/base_directory"),
               QDir::cleanPath(m_config.installPath));
  ini.setValue(QStringLiteral("fluorine/vfs_root_builder"), false);
  int rebasedExecutables = 0;
  if (QDir::cleanPath(m_gameRootPath) != QDir::cleanPath(m_config.sourceGamePath)) {
    ini.setValue(QStringLiteral("gamePath"), QDir::cleanPath(m_gameRootPath));
    rebasedExecutables = rebaseCustomExecutableGamePaths(
        ini, m_config.sourceGamePath, m_gameRootPath);
  }
  ini.sync();
  if (ini.status() != QSettings::NoError)
    return fail(tr("Cannot update the modlist's ModOrganizer.ini."));
  if (rebasedExecutables > 0) {
    m_adjustments.push_back(
        tr("Rebased %n executable path(s) to the modlist's authored Stock Game.", "",
           rebasedExecutables));
  }
  emit stepFinished(QStringLiteral("instance-config"));
  finish();
}

void WabbajackPostInstall::finish()
{
  if (!m_active) return;
  m_active = false;
  emit statusChanged(m_warnings.isEmpty()
                         ? tr("Fluorine compatibility setup complete.")
                         : tr("Compatibility setup completed with warnings."));
  emit completed(m_adjustments, m_warnings);
}

void WabbajackPostInstall::fail(const QString& message)
{
  if (!m_active) return;
  m_active = false;
  emit failed(message);
}

void WabbajackPostInstall::provideNexusAuthorization(const QString& requestId,
                                                     const QString& url)
{
  m_nexus.provideAuthorization(requestId, url);
}

void WabbajackPostInstall::rejectNexusAuthorization(const QString& requestId,
                                                    const QString& reason)
{
  m_nexus.rejectAuthorization(requestId, reason);
}

void WabbajackPostInstall::cancel()
{
  m_active = false;
  m_nexus.cancel();
  if (m_extractor.state() != QProcess::NotRunning) m_extractor.kill();
  if (m_nativePatcher.state() != QProcess::NotRunning) m_nativePatcher.kill();
}
