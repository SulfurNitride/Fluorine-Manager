#include "curatedguideinstaller.h"

#include "curatedguidenxmbroker.h"
#include "curatedgamemanifest.h"
#include "curatedfomod.h"
#include "curatedinstancebootstrap.h"
#include "curatedmodlayout.h"
#include "fluorineconfig.h"
#include "fluorinepaths.h"
#include "instancemanager.h"
#include "nexusinterface.h"
#include "nxmaccessmanager.h"
#include "protonlauncher.h"
#include "settings.h"
#include "vfsbackend.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>
#include <QUrlQuery>
#include <QUuid>
#include <QVersionNumber>
#include <QtConcurrent>
#include <nxmurl.h>

#include <filesystem>
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <utility>
#include <unistd.h>

namespace
{
QString nowUtc()
{
  return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

bool copyFile(const QString& source, const QString& destination, QString* error)
{
  QDir().mkpath(QFileInfo(destination).absolutePath());
  if (QFileInfo::exists(destination) && !QFile::remove(destination)) {
    if (error) *error = QString("Cannot replace %1").arg(destination);
    return false;
  }
  if (!QFile::copy(source, destination)) {
    if (error) *error = QString("Cannot copy %1 to %2").arg(source, destination);
    return false;
  }
  return true;
}

QStringList jsonStrings(const QJsonValue& value)
{
  QStringList result;
  for (const auto& item : value.toArray()) result.push_back(item.toString());
  return result;
}

QString findRecursively(const QString& root, const QString& filename)
{
  if (root.isEmpty() || filename.isEmpty()) return {};
  QDirIterator it(root, {filename}, QDir::Files, QDirIterator::Subdirectories);
  return it.hasNext() ? it.next() : QString{};
}

bool nexusCategoryMatches(const QJsonObject& file, const QString& guideCategory)
{
  if (guideCategory.isEmpty()) return true;
  const QString category = guideCategory.toLower();
  const QString nexusName = file.value("category_name").toString().toLower();
  const int nexusId = file.value("category_id").toInt(-1);
  if (category == "main files")
    return nexusId == 1 || nexusName == "main" || nexusName == "main files";
  if (category == "updates")
    return nexusId == 2 || nexusName == "update" || nexusName == "updates";
  if (category == "optional files")
    return nexusId == 3 || nexusName == "optional" || nexusName == "optional files";
  if (category == "miscellaneous files")
    return nexusId == 5 || nexusName == "miscellaneous"
           || nexusName == "miscellaneous files" || nexusName == "misc";
  return false;
}

QString procEnvironmentValue(qint64 pid, const QByteArray& key)
{
  QFile file(QString("/proc/%1/environ").arg(pid));
  if (!file.open(QIODevice::ReadOnly)) return {};
  const QByteArray prefix = key + '=';
  for (const auto& entry : file.readAll().split('\0')) {
    if (entry.startsWith(prefix)) return QString::fromUtf8(entry.mid(prefix.size()));
  }
  return {};
}

QString comparablePath(const QString& path)
{
  const QString canonical = QFileInfo(path).canonicalFilePath();
  return QDir::cleanPath(canonical.isEmpty() ? QFileInfo(path).absoluteFilePath()
                                              : canonical);
}

QString normalizedNexusLabel(QString value)
{
  value = value.toLower();
  value.replace("xnvse", "nvse");
  value.remove(QRegularExpression("[^a-z0-9]+"));
  return value;
}

bool processAlive(qint64 pid)
{
  return pid > 0 && (::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM);
}

qint64 wineserverForPrefix(const QString& prefixPath)
{
  if (prefixPath.isEmpty()) return 0;
  const QString expectedPrefix = comparablePath(prefixPath);
  const QDir proc("/proc");
  for (const auto& entry : proc.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    bool numeric = false;
    const qint64 candidate = entry.fileName().toLongLong(&numeric);
    if (!numeric || entry.ownerId() != static_cast<uint>(::getuid())) continue;
    QFile comm(entry.absoluteFilePath() + "/comm");
    if (!comm.open(QIODevice::ReadOnly)
        || QString::fromUtf8(comm.readAll()).trimmed() != "wineserver")
      continue;
    const QString prefix = procEnvironmentValue(candidate, "WINEPREFIX");
    if (!prefix.isEmpty() && comparablePath(prefix) == expectedPrefix) return candidate;
  }
  return 0;
}
}

CuratedGuideInstaller::CuratedGuideInstaller(QObject* parent) : QObject(parent)
{
  auto* nexusManager = NexusInterface::instance().getAccessManager();
  connect(nexusManager, &NXMAccessManager::credentialsReceived, this,
          [this](const APIUserAccount& account) {
            if (!m_nexusAccountCheckStarted) return;
            m_nexusAccountCheckStarted = false;
            if (m_cancelled) return;
            if (account.type() == APIUserAccountTypes::None) {
              failAction("Nexus account validation did not return an authenticated account.");
              return;
            }
            emit log(QString("Nexus account ready: %1 (%2).")
                         .arg(account.name(), localizedUserAccountType(account.type())));
            QTimer::singleShot(0, this, &CuratedGuideInstaller::drive);
          });
  connect(nexusManager, &NXMAccessManager::validateFailed, this,
          [this](const QString& reason) {
            if (!m_nexusAccountCheckStarted) return;
            m_nexusAccountCheckStarted = false;
            if (!m_cancelled)
              failAction(QString("Cannot validate the saved Nexus account: %1").arg(reason));
          });
  connect(nexusManager, &NXMAccessManager::updateOAuthState, this,
          [this](NXMAccessManager::OAuthState state, const QString& reason) {
            if (!m_nexusAccountCheckStarted
                || state != NXMAccessManager::OAuthState::Error)
              return;
            m_nexusAccountCheckStarted = false;
            if (!m_cancelled)
              failAction(QString("Cannot refresh the saved Nexus account: %1").arg(reason));
          });
  connect(&CuratedGuideNxmBroker::instance(), &CuratedGuideNxmBroker::accepted,
          this, [this](const QString& artifactId, const QString& url) {
            if (m_currentAction.isEmpty()) return;
            const auto* action = m_recipe.action(m_currentAction);
            const auto* artifact = action ? m_recipe.artifact(action->artifact) : nullptr;
            if (!artifact || artifact->id != artifactId) return;
            acquireNexus(*action, *artifact, url);
          });
  connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
    const QString output = QString::fromUtf8(m_process.readAllStandardOutput()).trimmed();
    if (!output.isEmpty()) emit log(output);
  });
  connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
    const QString output = QString::fromUtf8(m_process.readAllStandardError()).trimmed();
    if (!output.isEmpty()) emit log(output);
  });
}

CuratedGuideInstaller::~CuratedGuideInstaller()
{
  stopBackgroundExtractions();
  shutdownProtonSession();
}

void CuratedGuideInstaller::start(const CuratedGuideRecipe& recipe,
                                  const CuratedGuideInstallConfig& config)
{
  if (!m_currentAction.isEmpty()) return;
  m_recipe = recipe;
  m_config = config;
  QDir().mkpath(config.jobPath);
  QDir().mkpath(config.downloadsPath);
  m_statePath = QDir(config.jobPath).filePath("install-state.json");
  initialiseState();
  const auto bootstrap = bootstrapCuratedInstance(
      config.instancePath, recipe.gamePlugin,
      config.options.value("managedGamePath").toString(), config.downloadsPath);
  if (!bootstrap.first) {
    emit failed(bootstrap.second);
    return;
  }
  persist();
  drive();
}

void CuratedGuideInstaller::resume(const CuratedGuideRecipe& recipe,
                                   const QString& statePath)
{
  QString error;
  auto state = CuratedGuideInstallState::load(statePath, &error);
  if (!error.isEmpty()) {
    emit failed(error);
    return;
  }
  if (state.recipeId != recipe.id || !recipe.matchesDigest(state.recipeDigest)) {
    emit failed("The saved job belongs to a different recipe revision.");
    return;
  }
  m_recipe = recipe;
  m_state = state;
  reconcileStateWithRecipe();
  m_statePath = statePath;
  m_config = {state.instanceName, state.instancePath, state.downloadsPath,
              QFileInfo(statePath).absolutePath(), state.options};
  const auto bootstrap = bootstrapCuratedInstance(
      m_config.instancePath, recipe.gamePlugin,
      m_config.options.value("managedGamePath").toString(), m_config.downloadsPath);
  if (!bootstrap.first) {
    emit failed(bootstrap.second);
    return;
  }
  for (auto& action : m_state.actions) {
    if (action.status == CuratedActionStatus::Running
        || action.status == CuratedActionStatus::WaitingForUser
        || action.status == CuratedActionStatus::Failed) {
      action.status = CuratedActionStatus::Pending;
      action.error.clear();
    }
  }
  m_state.overallStatus = "running";
  persist();
  drive();
}

void CuratedGuideInstaller::repair(const CuratedGuideRecipe& recipe,
                                   const QString& statePath)
{
  QString error;
  auto state = CuratedGuideInstallState::load(statePath, &error);
  if (!error.isEmpty()) { emit failed(error); return; }
  m_recipe = recipe;
  m_state = state;
  reconcileStateWithRecipe();
  m_statePath = statePath;
  m_config = {state.instanceName, state.instancePath, state.downloadsPath,
              QFileInfo(statePath).absolutePath(), state.options};
  const auto bootstrap = bootstrapCuratedInstance(
      m_config.instancePath, recipe.gamePlugin,
      m_config.options.value("managedGamePath").toString(), m_config.downloadsPath);
  if (!bootstrap.first) {
    emit failed(bootstrap.second);
    return;
  }
  m_repairing = true;
  for (auto& record : m_state.actions) {
    const auto* action = recipe.action(record.id);
    if (!action) continue;
    QString validationError;
    if (record.status != CuratedActionStatus::Complete
        || !verifyActionOutput(*action, &validationError)) {
      record.status = CuratedActionStatus::Pending;
      record.error = validationError;
    }
  }
  // Profile files may have been rewritten by MO after an invalid mod layout
  // hid a plugin. Always regenerate them after repairing installation outputs.
  for (auto& record : m_state.actions) {
    const auto* action = recipe.action(record.id);
    if (!action) continue;
    if (action->type == "write_profile" || action->type == "validate_profile"
        || action->type == "first_launch") {
      record.status = CuratedActionStatus::Pending;
      record.error.clear();
    }
  }
  m_state.overallStatus = "repairing";
  persist();
  drive();
}

void CuratedGuideInstaller::cancel()
{
  m_cancelled = true;
  CuratedGuideNxmBroker::instance().clear();
  stopBackgroundExtractions();
  shutdownProtonSession();
  if (m_process.state() != QProcess::NotRunning) {
    m_process.terminate();
  } else if (m_copyWatcher.isRunning() || m_fomodWatcher.isRunning()) {
    // QFile copies are allowed to finish; the next drive step records cancellation.
  } else {
    m_currentAction.clear();
    drive();
  }
}

void CuratedGuideInstaller::initialiseState()
{
  m_state = {};
  m_state.jobId            = QUuid::createUuid().toString(QUuid::WithoutBraces);
  m_state.recipeId         = m_recipe.id;
  m_state.recipeVersion    = m_recipe.version;
  m_state.recipeDigest     = m_recipe.digest();
  m_state.guideSourceCommit = m_recipe.sourceCommit;
  m_state.instanceName     = m_config.instanceName;
  m_state.instancePath     = m_config.instancePath;
  m_state.downloadsPath    = m_config.downloadsPath;
  m_state.createdAt        = nowUtc();
  m_state.overallStatus    = "running";
  m_state.options          = m_config.options;
  for (const auto& artifact : m_recipe.artifacts) {
    m_state.artifacts.push_back({artifact.id, {}, artifact.version, {}, 0});
  }
  for (const auto& action : m_recipe.actions) {
    m_state.actions.push_back({action.id});
  }
}

void CuratedGuideInstaller::reconcileStateWithRecipe()
{
  QVector<CuratedArtifactRecord> artifacts;
  artifacts.reserve(m_recipe.artifacts.size());
  for (const auto& artifact : m_recipe.artifacts) {
    if (const auto* existing = m_state.artifact(artifact.id))
      artifacts.push_back(*existing);
    else
      artifacts.push_back({artifact.id, {}, artifact.version, {}, 0});
  }
  m_state.artifacts = std::move(artifacts);

  QVector<CuratedActionRecord> actions;
  actions.reserve(m_recipe.actions.size());
  for (const auto& action : m_recipe.actions) {
    if (const auto* existing = m_state.action(action.id))
      actions.push_back(*existing);
    else
      actions.push_back({action.id});
  }
  m_state.actions = std::move(actions);
  m_state.recipeVersion = m_recipe.version;
  m_state.recipeDigest = m_recipe.digest();
  m_state.guideSourceCommit = m_recipe.sourceCommit;
}

bool CuratedGuideInstaller::conditionMatches(const CuratedGuideAction& action) const
{
  if (action.condition.isEmpty()) return true;
  for (auto it = action.condition.begin(); it != action.condition.end(); ++it) {
    const QJsonValue actual = m_state.options.value(it.key());
    if (it.value().isArray()) {
      bool found = false;
      for (const auto& allowed : it.value().toArray()) {
        if (allowed == actual) { found = true; break; }
      }
      if (!found) return false;
    } else if (actual != it.value()) {
      return false;
    }
  }
  return true;
}

bool CuratedGuideInstaller::dependenciesComplete(const CuratedGuideAction& action) const
{
  for (const auto& dependency : action.dependsOn) {
    const auto* record = m_state.action(dependency);
    if (!record || (record->status != CuratedActionStatus::Complete
                    && record->status != CuratedActionStatus::Skipped)) return false;
  }
  return true;
}

void CuratedGuideInstaller::drive()
{
  if (m_cancelled) {
    m_state.overallStatus = "cancelled";
    persist();
    emit cancelled();
    return;
  }
  int completed = 0;
  for (const auto& record : m_state.actions) {
    if (record.status == CuratedActionStatus::Complete
        || record.status == CuratedActionStatus::Skipped) ++completed;
  }
  if (completed == m_state.actions.size()) {
    const QString instance = comparablePath(m_config.instancePath);
    const QString globalRoot = comparablePath(InstanceManager::globalInstancesRootPath());
    if (instance.startsWith(globalRoot + QDir::separator()))
      InstanceManager::unregisterPortableInstance(m_config.instancePath);
    m_state.overallStatus = "complete";
    persist();
    emit progress(completed, completed, "Complete");
    emit finished(m_state.instancePath);
    return;
  }
  if (!ensureNexusAccountReady()) return;
  scheduleBackgroundExtractions();
  if (!m_currentAction.isEmpty()) return;

  // Premium/direct downloads use a bounded asynchronous queue. Free Nexus
  // NXM handoffs and manual artifacts remain serialized so a browser click
  // can never be attributed to the wrong artifact.
  const bool premium = NexusInterface::instance().getAPIUserAccount().type()
                       == APIUserAccountTypes::Premium;
  if (premium) {
    const int maxConcurrentAcquisitions =
        qBound(1, QThread::idealThreadCount(), 16);
    for (const auto& action : m_recipe.actions) {
      if (m_activeAcquisitions.size() >= maxConcurrentAcquisitions) break;
      if (action.type != "acquire") continue;
      auto* record = m_state.action(action.id);
      if (!record || record->status == CuratedActionStatus::Complete
          || record->status == CuratedActionStatus::Skipped
          || record->status == CuratedActionStatus::Running) continue;
      const auto* artifact = m_recipe.artifact(action.artifact);
      if (!artifact || artifact->sourceType == CuratedGuideArtifact::SourceType::Manual)
        continue;
      if (!conditionMatches(action)) {
        record->status = CuratedActionStatus::Skipped;
        record->completedAt = nowUtc();
        persist();
        continue;
      }
      record->status = CuratedActionStatus::Running;
      record->startedAt = nowUtc();
      record->error.clear();
      m_activeAcquisitions.insert(action.id);
      persist();
      emit progress(completed + m_activeAcquisitions.size() - 1,
                    m_state.actions.size(), action.name);
      emit log(QString("Starting concurrent download: %1").arg(action.name));
      acquire(action);
    }
    scheduleBackgroundExtractions();
    if (!m_activeAcquisitions.isEmpty()) return;
  }

  // Finish cached/manual/free acquisitions before modifying the instance.
  for (const auto& action : m_recipe.actions) {
    if (action.type != "acquire") continue;
    auto* record = m_state.action(action.id);
    if (!record || record->status == CuratedActionStatus::Complete
        || record->status == CuratedActionStatus::Skipped) continue;
    if (!conditionMatches(action)) {
      record->status = CuratedActionStatus::Skipped;
      record->completedAt = nowUtc();
      persist();
      QTimer::singleShot(0, this, &CuratedGuideInstaller::drive);
      return;
    }
    m_currentAction = action.id;
    record->status = CuratedActionStatus::Running;
    record->startedAt = nowUtc();
    record->error.clear();
    persist();
    emit progress(completed, m_state.actions.size(), action.name);
    emit log(QString("[%1/%2] %3 (download phase)")
                 .arg(completed + 1).arg(m_state.actions.size()).arg(action.name));
    execute(action);
    return;
  }
  for (const auto& action : m_recipe.actions) {
    auto* record = m_state.action(action.id);
    if (!record || record->status == CuratedActionStatus::Complete
        || record->status == CuratedActionStatus::Skipped
        || record->status == CuratedActionStatus::Running
        || record->status == CuratedActionStatus::WaitingForUser
        || action.type == "extract") continue;
    if (!dependenciesComplete(action)) continue;
    if (!conditionMatches(action)) {
      record->status = CuratedActionStatus::Skipped;
      record->completedAt = nowUtc();
      persist();
      QTimer::singleShot(0, this, &CuratedGuideInstaller::drive);
      return;
    }
    m_currentAction = action.id;
    record->status = CuratedActionStatus::Running;
    record->startedAt = nowUtc();
    record->error.clear();
    persist();
    emit progress(completed, m_state.actions.size(), action.name);
    emit log(QString("[%1/%2] %3").arg(completed + 1).arg(m_state.actions.size())
                 .arg(action.name));
    execute(action);
    return;
  }
  if (!m_extractionProcesses.isEmpty()) return;
  failAction("No runnable action remains; the recipe dependency graph is incomplete.");
}

bool CuratedGuideInstaller::ensureNexusAccountReady()
{
  bool needsNexus = false;
  for (const auto& artifact : m_recipe.artifacts) {
    if (artifact.sourceType == CuratedGuideArtifact::SourceType::Nexus) {
      needsNexus = true;
      break;
    }
  }
  if (!needsNexus
      || NexusInterface::instance().getAPIUserAccount().type()
             != APIUserAccountTypes::None)
    return true;
  if (m_nexusAccountCheckStarted) return false;

  NexusOAuthTokens tokens;
  const bool hasOAuth = GlobalSettings::nexusOAuthTokens(tokens);
  const bool hasApiKey = GlobalSettings::nexusApiKey(tokens.apiKey);
  if (!hasOAuth && !hasApiKey) {
    failAction("Nexus authentication is required before downloading this guide.");
    return false;
  }
  if (auto* settings = Settings::maybeInstance();
      settings && settings->network().offlineMode()) {
    failAction("Disable offline mode before downloading this guide from Nexus.");
    return false;
  }

  m_nexusAccountCheckStarted = true;
  emit log("Restoring and validating the saved Nexus account...");
  NexusInterface::instance().getAccessManager()->apiCheck(tokens, true);
  return false;
}

void CuratedGuideInstaller::scheduleBackgroundExtractions()
{
  if (m_cancelled || m_state.overallStatus == "failed") return;
  const int limit = qMax(1, QThread::idealThreadCount());
  const QString sevenZip = find7z();
  if (sevenZip.isEmpty()) return;

  bool stateChanged = false;
  for (const auto& action : m_recipe.actions) {
    if (m_extractionProcesses.size() >= limit) break;
    if (action.type != "extract") continue;
    auto* actionRecord = m_state.action(action.id);
    if (!actionRecord || actionRecord->status != CuratedActionStatus::Pending
        || !dependenciesComplete(action))
      continue;
    if (!conditionMatches(action)) {
      actionRecord->status = CuratedActionStatus::Skipped;
      actionRecord->completedAt = nowUtc();
      stateChanged = true;
      continue;
    }

    const auto* artifact = m_recipe.artifact(action.artifact);
    const auto* artifactRecord = artifact ? m_state.artifact(artifact->id) : nullptr;
    if (!artifact || !artifactRecord || artifactRecord->path.isEmpty()) continue;

    const QString output = actionOutputPath(action.id);
    QDir(output).removeRecursively();
    if (!QDir().mkpath(output)) {
      finishBackgroundExtraction(action.id, output, nullptr, false,
                                 QString("Cannot create extraction folder: %1")
                                     .arg(output));
      return;
    }

    auto* process = new QProcess(this);
    m_extractionProcesses.insert(action.id, process);
    actionRecord->status = CuratedActionStatus::Running;
    actionRecord->startedAt = nowUtc();
    actionRecord->error.clear();
    stateChanged = true;
    emit log(QString("Starting background extraction (%1/%2): %3")
                 .arg(m_extractionProcesses.size())
                 .arg(limit)
                 .arg(action.name));
    connect(process, &QProcess::readyReadStandardOutput, this,
            [this, process, name = action.name] {
              const QString output =
                  QString::fromUtf8(process->readAllStandardOutput()).trimmed();
              if (!output.isEmpty()) emit log(QString("[%1] %2").arg(name, output));
            });
    connect(process, &QProcess::readyReadStandardError, this,
            [this, process, name = action.name] {
              const QString output =
                  QString::fromUtf8(process->readAllStandardError()).trimmed();
              if (!output.isEmpty()) emit log(QString("[%1] %2").arg(name, output));
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, actionId = action.id, output, process](QProcess::ProcessError error) {
              if (error != QProcess::FailedToStart
                  || m_extractionProcesses.value(actionId) != process)
                return;
              finishBackgroundExtraction(actionId, output, process, false,
                                         process->errorString());
            });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, actionId = action.id, output, process](
                int code, QProcess::ExitStatus status) {
              if (m_extractionProcesses.value(actionId) != process) return;
              const bool success = status == QProcess::NormalExit && code == 0;
              finishBackgroundExtraction(
                  actionId, output, process, success,
                  success ? QString{}
                          : QString("Archive extraction failed with code %1").arg(code));
            });
    process->start(sevenZip, {"x", "-y", "-o" + output, artifactRecord->path});
  }
  if (stateChanged) persist();
}

void CuratedGuideInstaller::finishBackgroundExtraction(
    const QString& actionId, const QString& output, QProcess* process,
    bool success, const QString& error)
{
  if (process) {
    if (m_extractionProcesses.value(actionId) != process) return;
    m_extractionProcesses.remove(actionId);
    process->deleteLater();
  }
  auto* record = m_state.action(actionId);
  if (!record) return;
  if (!success) {
    record->status = CuratedActionStatus::Failed;
    record->error = error;
    m_state.overallStatus = "failed";
    stopBackgroundExtractions();
    shutdownProtonSession();
    persist();
    emit failed(error);
    return;
  }

  record->status = CuratedActionStatus::Complete;
  record->error.clear();
  record->completedAt = nowUtc();
  record->outputDigest = curatedBlake3Tree(output);
  emit log(QString("Background extraction complete: %1")
               .arg(m_recipe.action(actionId)->name));
  persist();
  scheduleBackgroundExtractions();
  if (m_currentAction.isEmpty())
    QTimer::singleShot(0, this, &CuratedGuideInstaller::drive);
}

void CuratedGuideInstaller::stopBackgroundExtractions()
{
  const auto processes = m_extractionProcesses;
  m_extractionProcesses.clear();
  for (auto* process : processes) {
    if (!process) continue;
    process->disconnect(this);
    if (process->state() != QProcess::NotRunning) {
      process->terminate();
      if (!process->waitForFinished(500)) process->kill();
    }
    process->deleteLater();
  }
}

void CuratedGuideInstaller::execute(const CuratedGuideAction& action)
{
  if (action.type == "acquire") return acquire(action);
  if (action.type == "copy_game") return copyGame(action);
  if (action.type == "extract") return extract(action);
  if (action.type == "install_mod") return installMod(action, false);
  if (action.type == "install_root") return installMod(action, true);
  if (action.type == "run_native") return runNative(action);
  if (action.type == "run_proton") return runProton(action);
  if (action.type == "assisted_tool") return assistedTool(action);
  if (action.type == "edit_ini") return editIni(action);
  if (action.type == "write_profile") return writeProfile(action);
  if (action.type == "validate_profile" || action.type == "first_launch")
    return validateProfile(action);
  failAction(QString("Unsupported action type: %1").arg(action.type));
}

QString CuratedGuideInstaller::artifactPath(const CuratedGuideArtifact& artifact) const
{
  return QDir(m_config.downloadsPath).filePath(artifact.filename);
}

void CuratedGuideInstaller::acquire(const CuratedGuideAction& action)
{
  const auto* artifact = m_recipe.artifact(action.artifact);
  if (!artifact) return failAcquisition(action.id, "Recipe artifact is missing.");
  if (const auto* saved = m_state.artifact(artifact->id);
      saved && !saved->path.isEmpty() && QFileInfo::exists(saved->path)) {
    QString savedError;
    if (verifyArtifact(*artifact, saved->path, &savedError)) {
      emit log(QString("Using verified recorded archive %1").arg(saved->path));
      return completeAcquisition(action.id, saved->path);
    }
  }
  const QString cached = artifactPath(*artifact);
  QString error;
  if (QFileInfo::exists(cached) && verifyArtifact(*artifact, cached, &error)) {
    auto* record = m_state.artifact(artifact->id);
    record->path = cached;
    record->sha256 = curatedSha256File(cached);
    record->size = QFileInfo(cached).size();
    emit log(QString("Using verified cached archive %1").arg(artifact->filename));
    return completeAcquisition(action.id, cached);
  }
  if (QFileInfo::exists(cached)) QFile::remove(cached);
  if (artifact->sourceType == CuratedGuideArtifact::SourceType::Direct) {
    return download(action.id, artifact->id, QUrl(artifact->url));
  }
  if (artifact->sourceType == CuratedGuideArtifact::SourceType::Nexus) {
    if (artifact->fileId <= 0) return resolveNexusFile(action, *artifact);
    return acquireNexus(action, *artifact);
  }
  auto* actionRecord = m_state.action(action.id);
  actionRecord->status = CuratedActionStatus::WaitingForUser;
  persist();
  emit manualArtifactRequired(artifact->id, artifact->name, artifact->sourceUrl,
                              artifact->filename);
}

void CuratedGuideInstaller::resolveNexusFile(const CuratedGuideAction& action,
                                             const CuratedGuideArtifact& artifact)
{
  const QUrl endpoint(QString("https://api.nexusmods.com/v1/games/%1/mods/%2/files.json")
                          .arg(artifact.domain).arg(artifact.modId));
  auto* manager = NexusInterface::instance().getAccessManager();
  auto* reply = manager ? manager->makeAuthenticatedGetRequest(endpoint) : nullptr;
  if (!reply)
    return failAcquisition(action.id,
                           "Nexus authentication is required to resolve the guide's file names.");
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, action, artifact]() mutable {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError)
              return failAcquisition(action.id, QString("Cannot resolve Nexus file: %1")
                                                    .arg(reply->errorString()));
            const auto root = QJsonDocument::fromJson(reply->readAll()).object();
            QJsonArray candidates;
            QJsonArray renamedCandidates;
            QJsonArray movedCandidates;
            for (const auto& value : root.value("files").toArray()) {
              const auto file = value.toObject();
              const QString candidateVersion = file.value("version").toString();
              const bool versionAllowed = artifact.minimumVersion.isEmpty()
                  || QVersionNumber::compare(QVersionNumber::fromString(candidateVersion),
                                             QVersionNumber::fromString(
                                                 artifact.minimumVersion)) >= 0;
              const bool matches = artifact.latestCompatible
                  ? (file.value("category_id").toInt() == 1
                     || file.value("category_name").toString().compare(
                            "MAIN", Qt::CaseInsensitive) == 0)
                  : (file.value("name").toString().compare(
                         artifact.fileLabel, Qt::CaseInsensitive) == 0
                     && nexusCategoryMatches(file, artifact.fileCategory));
              if (matches && versionAllowed)
                candidates.push_back(file);
              if (!artifact.latestCompatible && versionAllowed
                  && normalizedNexusLabel(file.value("name").toString())
                         == normalizedNexusLabel(artifact.fileLabel)) {
                if (nexusCategoryMatches(file, artifact.fileCategory))
                  renamedCandidates.push_back(file);
                else
                  movedCandidates.push_back(file);
              }
            }
            QString fallback;
            if (candidates.isEmpty() && !renamedCandidates.isEmpty()) {
              candidates = renamedCandidates;
              fallback = "renamed";
            }
            if (candidates.isEmpty() && !movedCandidates.isEmpty()) {
              candidates = movedCandidates;
              fallback = "moved to a different category";
            }
            if (!candidates.isEmpty()) {
              QJsonObject newest;
              for (const auto& value : candidates) {
                const auto candidate = value.toObject();
                if (newest.isEmpty()
                    || candidate.value("file_id").toInt()
                           > newest.value("file_id").toInt()) {
                  newest = candidate;
                }
              }
              candidates = QJsonArray{newest};
            }
            if (candidates.size() != 1)
              return failAcquisition(action.id,
                  QString("Nexus file '%1' in '%2' resolved to %3 active files; the bundled recipe must be reviewed.")
                      .arg(artifact.fileLabel, artifact.fileCategory)
                      .arg(candidates.size()));
            const auto selected = candidates.first().toObject();
            if (!fallback.isEmpty())
              emit log(QString("Nexus file '%1' was %2; using the unique active match '%3' in '%4'.")
                           .arg(artifact.fileLabel, fallback,
                                selected.value("name").toString(),
                                selected.value("category_name").toString()));
            emit log(QString("Resolved %1 [%2] to Nexus file %3 (%4).")
                         .arg(artifact.fileLabel, artifact.fileCategory)
                         .arg(selected.value("file_id").toInt())
                         .arg(selected.value("version").toString()));
            for (auto& mutableArtifact : m_recipe.artifacts) {
              if (mutableArtifact.id != artifact.id) continue;
              mutableArtifact.fileId = selected.value("file_id").toInt();
              mutableArtifact.filename =
                  selected.value("file_name").toString(mutableArtifact.filename);
              mutableArtifact.version =
                  selected.value("version").toString(mutableArtifact.version);
              if (auto* stateArtifact = m_state.artifact(artifact.id))
                stateArtifact->version = mutableArtifact.version;
              persist();
              acquireNexus(action, mutableArtifact);
              return;
            }
            failAcquisition(action.id,
                            "Resolved Nexus artifact disappeared from the recipe.");
          });
}

void CuratedGuideInstaller::acquireNexus(const CuratedGuideAction& action,
                                         const CuratedGuideArtifact& artifact,
                                         const QString& nxmUrl)
{
  QString endpoint = QString("https://api.nexusmods.com/v1/games/%1/mods/%2/files/%3/download_link.json")
                         .arg(artifact.domain).arg(artifact.modId).arg(artifact.fileId);
  const bool premium = NexusInterface::instance().getAPIUserAccount().type()
                       == APIUserAccountTypes::Premium;
  if (!premium && nxmUrl.isEmpty()) {
    CuratedGuideNxmBroker::instance().expect(artifact.id, artifact.domain,
                                             artifact.modId, artifact.fileId);
    auto* record = m_state.action(action.id);
    record->status = CuratedActionStatus::WaitingForUser;
    persist();
    const QString page = QString("https://www.nexusmods.com/%1/mods/%2?tab=files&file_id=%3&nmm=1")
                             .arg(artifact.domain).arg(artifact.modId).arg(artifact.fileId);
    emit nexusDownloadRequired(artifact.id, artifact.name, page);
    return;
  }
  if (!nxmUrl.isEmpty()) {
    const NXMUrl parsed(nxmUrl);
    QUrl url(endpoint);
    QUrlQuery query;
    query.addQueryItem("key", parsed.key());
    query.addQueryItem("expires", QString::number(parsed.expires()));
    url.setQuery(query);
    endpoint = url.toString();
  }
  auto* manager = NexusInterface::instance().getAccessManager();
  auto* reply = manager ? manager->makeAuthenticatedGetRequest(QUrl(endpoint)) : nullptr;
  if (!reply) return failAcquisition(action.id, "Nexus authentication is unavailable.");
  connect(reply, &QNetworkReply::finished, this, [this, reply, action, artifact]() {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError)
      return failAcquisition(action.id,
                             QString("Nexus download URL failed: %1")
                                 .arg(reply->errorString()));
    const auto links = QJsonDocument::fromJson(reply->readAll()).array();
    if (links.isEmpty())
      return failAcquisition(action.id, "Nexus returned no download locations.");
    download(action.id, artifact.id,
             QUrl(links.first().toObject().value("URI").toString()));
  });
}

void CuratedGuideInstaller::download(const QString& actionId,
                                     const QString& artifactId, const QUrl& url)
{
  const auto* artifact = m_recipe.artifact(artifactId);
  if (!artifact || !url.isValid())
    return failAcquisition(actionId, "Invalid artifact download URL.");
  QNetworkRequest request(url);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  auto* reply = m_network.get(request);
  const QString partial = artifactPath(*artifact) + ".unfinished";
  auto* output = new QSaveFile(partial, reply);
  if (!output->open(QIODevice::WriteOnly)) {
    reply->abort();
    return failAcquisition(actionId, QString("Cannot create %1").arg(partial));
  }
  connect(reply, &QNetworkReply::readyRead, this, [reply, output] {
    output->write(reply->readAll());
  });
  connect(reply, &QNetworkReply::downloadProgress, this,
          [this, artifactId](qint64 received, qint64 total) {
            emit artifactProgress(artifactId, received, total);
          });
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, output, actionId, artifact]() {
    output->write(reply->readAll());
    const auto networkError = reply->error();
    const QString networkMessage = reply->errorString();
    reply->deleteLater();
    if (networkError != QNetworkReply::NoError || !output->commit()) {
      return failAcquisition(actionId,
                             QString("Download failed: %1").arg(networkMessage));
    }
    const QString partial = output->fileName();
    output->deleteLater();
    const QString finalPath = artifactPath(*artifact);
    QFile::remove(finalPath);
    if (!QFile::rename(partial, finalPath))
      return failAcquisition(actionId,
                             QString("Cannot promote download to %1").arg(finalPath));
    QString error;
    if (!verifyArtifact(*artifact, finalPath, &error)) {
      QFile::remove(finalPath);
      return failAcquisition(actionId, error);
    }
    auto* record = m_state.artifact(artifact->id);
    record->path = finalPath;
    record->version = artifact->version;
    record->sha256 = curatedSha256File(finalPath);
    record->size = QFileInfo(finalPath).size();
    completeAcquisition(actionId, finalPath);
  });
}

void CuratedGuideInstaller::provideManualArtifact(const QString& artifactId,
                                                  const QString& path)
{
  const auto* action = m_recipe.action(m_currentAction);
  const auto* artifact = action ? m_recipe.artifact(action->artifact) : nullptr;
  if (!artifact || artifact->id != artifactId) return;
  QString error;
  if (!verifyArtifact(*artifact, path, &error)) return failAction(error);
  const QString destination = artifactPath(*artifact);
  if (QFileInfo(path).absoluteFilePath() != QFileInfo(destination).absoluteFilePath()
      && !copyFile(path, destination, &error)) return failAction(error);
  auto* record = m_state.artifact(artifactId);
  record->path = destination;
  record->version = artifact->version;
  record->sha256 = curatedSha256File(destination);
  record->size = QFileInfo(destination).size();
  completeAcquisition(m_currentAction, destination);
}

bool CuratedGuideInstaller::verifyArtifact(const CuratedGuideArtifact& artifact,
                                           const QString& path, QString* error) const
{
  const QFileInfo info(path);
  if (!info.isFile() || info.size() <= 0) {
    if (error) *error = QString("Artifact is missing or empty: %1").arg(path);
    return false;
  }
  if (artifact.size > 0 && info.size() != artifact.size) {
    if (error) *error = QString("%1 has the wrong size").arg(artifact.name);
    return false;
  }
  if (!artifact.sha256.isEmpty()) {
    const QString actual = curatedSha256File(path, error);
    if (actual.compare(artifact.sha256, Qt::CaseInsensitive) != 0) {
      if (error) *error = QString("SHA-256 mismatch for %1").arg(artifact.name);
      return false;
    }
  }
  if (error) error->clear();
  return true;
}

QPair<bool, QString> CuratedGuideInstaller::copyDirectory(const QString& source,
                                                          const QString& destination)
{
  std::error_code ec;
  const auto src = std::filesystem::path(source.toStdString());
  const auto dst = std::filesystem::path(destination.toStdString());
  if (!std::filesystem::is_directory(src, ec)) return {false, "Source directory is missing"};
  std::filesystem::create_directories(dst, ec);
  if (ec) return {false, QString::fromStdString(ec.message())};
  std::filesystem::copy(src, dst,
                        std::filesystem::copy_options::recursive
                            | std::filesystem::copy_options::copy_symlinks
                            | std::filesystem::copy_options::overwrite_existing, ec);
  return ec ? QPair<bool, QString>{false, QString::fromStdString(ec.message())}
            : QPair<bool, QString>{true, {}};
}

void CuratedGuideInstaller::copyGame(const CuratedGuideAction& action)
{
  const QString option = action.parameters.value("sourceOption").toString();
  const QString source = m_state.options.value(option).toString();
  const QString relative = action.parameters.value("destination").toString();
  const QString destination = QDir(m_config.instancePath).filePath(relative);
  if (source.isEmpty()) return failAction(QString("No game path supplied for %1").arg(option));
  const QString partial = destination + ".partial";
  QDir(partial).removeRecursively();
  const QString storeKey = option == "fo3Source" ? "fo3Store" : "fnvStore";
  const QString store = m_state.options.value(storeKey)
                            .toString(m_state.options.value("store").toString());
  connect(&m_copyWatcher, &QFutureWatcher<CuratedVerifiedCopyResult>::finished, this,
          [this, destination, partial] {
            const auto result = m_copyWatcher.result();
            if (!result.success) {
              QDir(partial).removeRecursively();
              return failAction(result.error);
            }
            if (!result.unexpectedFiles.isEmpty()) {
              emit log(QString("Ignored %1 file(s) that are not in the store manifest, "
                               "including: %2")
                           .arg(result.unexpectedFiles.size())
                           .arg(result.unexpectedFiles.mid(0, 5).join(", ")));
            }
            QDir(destination).removeRecursively();
            if (!QDir().rename(partial, destination))
              return failAction(QString("Cannot promote stock game to %1").arg(destination));
            if (auto* record = m_state.action(m_currentAction))
              record->provenance = result.provenance;
            completeAction(destination);
          }, Qt::SingleShotConnection);
  m_copyWatcher.setFuture(QtConcurrent::run([source, partial, store] {
    const auto resolution = resolveCuratedGameManifest(source, store);
    if (!resolution.success)
      return CuratedVerifiedCopyResult{false, resolution.error, {}, {}};
    return copyCuratedGameFromManifest(source, partial, resolution.manifest);
  }));
}

QString CuratedGuideInstaller::find7z()
{
  const QString bundled = fluorineDataDir() + "/bin/7zz";
  if (QFileInfo::exists(bundled)) return bundled;
  for (const auto& name : {"7zz", "7z", "7za"}) {
    const QString found = QStandardPaths::findExecutable(name);
    if (!found.isEmpty()) return found;
  }
  return {};
}

QString CuratedGuideInstaller::actionOutputPath(const QString& actionId) const
{
  return QDir(m_config.jobPath).filePath("staging/" + actionId);
}

void CuratedGuideInstaller::extract(const CuratedGuideAction& action)
{
  const auto* artifact = m_recipe.artifact(action.artifact);
  const auto* record = artifact ? m_state.artifact(artifact->id) : nullptr;
  if (!artifact || !record || record->path.isEmpty()) return failAction("Archive was not acquired.");
  const QString output = actionOutputPath(action.id);
  QDir(output).removeRecursively();
  QDir().mkpath(output);
  const QString sevenZip = find7z();
  if (sevenZip.isEmpty()) return failAction("No 7z executable is available.");
  connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [this, output](int code, QProcess::ExitStatus status) {
            if (m_cancelled) return drive();
            if (status != QProcess::NormalExit || code != 0)
              return failAction(QString("Archive extraction failed with code %1").arg(code));
            completeAction(output);
          }, Qt::SingleShotConnection);
  m_process.start(sevenZip, {"x", "-y", "-o" + output, record->path});
}

void CuratedGuideInstaller::installMod(const CuratedGuideAction& action, bool root)
{
  const QString sourceAction = action.parameters.value("sourceAction").toString();
  QString source = actionOutputPath(sourceAction);
  const QString subdir = action.parameters.value("sourceSubdir").toString();
  if (!subdir.isEmpty()) source = QDir(source).filePath(subdir);
  if (action.parameters.value("unwrapSingleDirectory").toBool(false))
    source = curatedRootPayloadRoot(source);
  QString folder = action.parameters.value("folder").toString(action.name);
  if (!root) {
    QString nestedError;
    const QString nestedFomod = curatedNestedFomodArchive(source, &nestedError);
    if (!nestedError.isEmpty()) return failAction(nestedError);
    if (!nestedFomod.isEmpty()) {
      const QString sevenZip = find7z();
      if (sevenZip.isEmpty())
        return failAction("No 7z executable is available for the nested .fomod archive.");
      emit log(QString("Expanding nested FOMOD archive: %1")
                   .arg(QFileInfo(nestedFomod).fileName()));
      connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
              [this, action, root, nestedFomod](int code, QProcess::ExitStatus status) {
                if (m_cancelled) return drive();
                if (status != QProcess::NormalExit || code != 0)
                  return failAction(
                      QString("Nested FOMOD extraction failed with code %1").arg(code));
                if (!QFile::remove(nestedFomod))
                  return failAction(QString("Cannot remove expanded nested FOMOD: %1")
                                        .arg(nestedFomod));
                installMod(action, root);
              }, Qt::SingleShotConnection);
      m_process.start(sevenZip,
                      {"x", "-y", "-o" + QFileInfo(nestedFomod).absolutePath(),
                       nestedFomod});
      return;
    }
  }
  if (root) {
    source = curatedRootPayloadRoot(source);
    const QString destination =
        m_state.options.value("managedGamePath").toString();
    if (destination.isEmpty() || !QFileInfo(destination).isDir())
      return failAction("The isolated game folder is unavailable for a root install.");
    const auto result = copyDirectory(source, destination);
    if (!result.first) return failAction(result.second);
    const QString legacy = QDir(m_config.instancePath).filePath("mods/" + folder);
    if (QDir(legacy).exists() && !QDir(legacy).removeRecursively())
      return failAction(QString("Cannot remove obsolete Root Builder mod: %1")
                            .arg(legacy));
    QString validationError;
    if (!verifyActionOutput(action, &validationError))
      return failAction(validationError);
    emit log(QString("Installed %1 directly into the isolated game folder.")
                 .arg(folder));
    return completeAction();
  }
  const QString destination = QDir(m_config.instancePath).filePath("mods/" + folder);
  const QString partial = destination + ".partial";
  QDir(partial).removeRecursively();
  source = curatedModPayloadRoot(source);
  const auto result = copyDirectory(source, partial);
  if (!result.first) return failAction(result.second);
  QDir(destination).removeRecursively();
  if (!QDir().rename(partial, destination))
    return failAction(QString("Cannot install mod %1").arg(folder));
  QString validationError;
  if (!validateCuratedModLayout(destination, &validationError))
    return failAction(validationError);
  completeAction(destination);
}

void CuratedGuideInstaller::runNative(const CuratedGuideAction& action)
{
  QString program = action.parameters.value("program").toString();
  if (program.startsWith("artifact:")) {
    const auto* record = m_state.artifact(program.mid(9));
    program = record ? record->path : QString{};
  }
  QStringList arguments = jsonStrings(action.parameters.value("arguments"));
  for (const auto& candidate : m_recipe.actions)
    program.replace("${action:" + candidate.id + "}", actionOutputPath(candidate.id));
  program.replace("${instance}", m_config.instancePath);
  for (QString& argument : arguments) {
    argument.replace("${instance}", m_config.instancePath);
    for (const auto& artifact : m_state.artifacts)
      argument.replace("${artifact:" + artifact.id + "}", artifact.path);
    for (auto it = m_state.options.begin(); it != m_state.options.end(); ++it)
      argument.replace("${option:" + it.key() + "}", it.value().toString());
  }
  if (!QFileInfo::exists(program))
    program = findRecursively(QFileInfo(program).absolutePath(), QFileInfo(program).fileName());
  if (program.isEmpty() || !QFileInfo::exists(program))
    return failAction("Native tool executable was not found.");
  QFile::setPermissions(program, QFile::permissions(program) | QFile::ExeUser);
  connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [this, action](int code, QProcess::ExitStatus status) {
            if (m_cancelled) return drive();
            if (status != QProcess::NormalExit || code != 0)
              return failAction(QString("Tool failed with code %1").arg(code));
            QString output = action.parameters.value("output").toString();
            output.replace("${instance}", m_config.instancePath);
            for (auto it = m_state.options.begin(); it != m_state.options.end(); ++it)
              output.replace("${option:" + it.key() + "}", it.value().toString());
            if (!output.isEmpty() && QDir::isRelativePath(output))
              output = QDir(m_config.instancePath).filePath(output);
            QString error;
            if (!verifyActionOutput(action, &error)) return failAction(error);
            completeAction(output);
          }, Qt::SingleShotConnection);
  m_process.setWorkingDirectory(action.parameters.value("workingDirectory").toString(m_config.jobPath));
  m_process.start(program, arguments);
}

void CuratedGuideInstaller::runProton(const CuratedGuideAction& action)
{
  auto config = FluorineConfig::load();
  if (!config || config->proton_path.isEmpty() || config->prefix_path.isEmpty())
    return failAction("Fluorine's Proton prefix must be configured before running a patcher.");
  QString executable = action.parameters.value("executable").toString();
  for (const auto& artifact : m_state.artifacts)
    executable.replace("${artifact:" + artifact.id + "}", artifact.path);
  executable.replace("${instance}", m_config.instancePath);
  for (const auto& candidate : m_recipe.actions)
    executable.replace("${action:" + candidate.id + "}", actionOutputPath(candidate.id));
  QString workingDirectory = action.parameters.value("workingDirectory").toString(m_config.instancePath);
  workingDirectory.replace("${instance}", m_config.instancePath);
  for (auto it = m_state.options.begin(); it != m_state.options.end(); ++it)
    workingDirectory.replace("${option:" + it.key() + "}", it.value().toString());
  const QString sourceAction = action.parameters.value("stageSourceAction").toString();
  if (!sourceAction.isEmpty()) {
    const auto staged = copyDirectory(actionOutputPath(sourceAction), workingDirectory);
    if (!staged.first) return failAction(staged.second);
    executable = QDir(workingDirectory).filePath(QFileInfo(executable).fileName());
  }
  if (!QFileInfo::exists(executable))
    executable = findRecursively(workingDirectory, QFileInfo(executable).fileName());
  if (executable.isEmpty() || !QFileInfo::exists(executable))
    return failAction("Windows tool executable was not found.");

  if (!m_state.options.value("isolated").toBool(true)) {
    const QString gameExe = QDir(workingDirectory).filePath("FalloutNV.exe");
    const QString backup = QDir(m_config.jobPath).filePath("backups/FalloutNV.exe");
    if (QFileInfo::exists(gameExe) && !QFileInfo::exists(backup)) {
      QString error;
      if (!copyFile(gameExe, backup, &error)) return failAction(error);
    }
  }
  const auto [started, pid] = ProtonLauncher()
                                  .setBinary(executable)
                                  .setWorkingDir(workingDirectory)
                                  .setGameDirectory(workingDirectory)
                                  .setProtonPath(config->proton_path)
                                  .setPrefix(config->prefix_path)
                                  .setSteamAppId(config->app_id)
                                  .setSteamDrm(false)
                                  .setStoreVariant(m_state.options.value("store").toString())
                                  .setUseSLR(true)
                                  .launch();
  if (!started)
    return failAction(QString("Could not launch %1 through Fluorine's Proton runner.")
                          .arg(executable));
  m_protonPid = pid;
  emit log(QString("Windows tool launched through Proton (PID %1); waiting for verified output.")
               .arg(pid));
  pollProtonAction(action, pid, 0);
}

void CuratedGuideInstaller::pollProtonAction(const CuratedGuideAction& action,
                                             qint64 pid, int attempts)
{
  if (m_currentAction != action.id) return;
  if (m_cancelled) return drive();
  QString error;
  if (!assistedSessionRunning(pid) && verifyActionOutput(action, &error)) {
    m_protonPid = 0;
    QString output = action.parameters.value("output").toString();
    output.replace("${instance}", m_config.instancePath);
    for (auto it = m_state.options.begin(); it != m_state.options.end(); ++it)
      output.replace("${option:" + it.key() + "}", it.value().toString());
    if (!output.isEmpty() && QDir::isRelativePath(output))
      output = QDir(m_config.instancePath).filePath(output);
    return completeAction(output);
  }
  if (attempts >= 1800)
    return failAction(QString("Timed out waiting for Windows tool output: %1").arg(error));
  QTimer::singleShot(1000, this,
                     [this, action, pid, attempts] {
                       pollProtonAction(action, pid, attempts + 1);
                     });
}

void CuratedGuideInstaller::assistedTool(const CuratedGuideAction& action)
{
  QStringList reviewedSelections;
  const bool automaticFomod = action.parameters.contains("fomodSelections");
  for (const auto& value : action.parameters.value("fomodSelections").toArray())
    reviewedSelections.push_back(value.toString());
  if (automaticFomod) {
    const QString sourceAction = action.parameters.value("browseSourceAction").toString();
    const QString source = actionOutputPath(sourceAction);
    QString destination = action.parameters.value("output").toString();
    if (QDir::isRelativePath(destination))
      destination = QDir(m_config.instancePath).filePath(destination);
    const QString partial = destination + ".partial";
    QDir(partial).removeRecursively();
    emit log(reviewedSelections.isEmpty()
                 ? QString("Applying reviewed FOMOD defaults.")
                 : QString("Applying reviewed FOMOD selection: %1")
                       .arg(reviewedSelections.join(", ")));
    connect(&m_fomodWatcher, &QFutureWatcher<QPair<bool, QString>>::finished, this,
            [this, action, destination, partial] {
              const auto result = m_fomodWatcher.result();
              if (!result.first) return failAction(result.second);
              QDir(destination).removeRecursively();
              if (!QDir().rename(partial, destination))
                return failAction(QString("Cannot install reviewed FOMOD to %1")
                                      .arg(destination));
              QString error;
              if (!verifyActionOutput(action, &error)) return failAction(error);
              emit log(QString("Reviewed FOMOD installed automatically (%1 files).")
                           .arg(result.second));
              completeAction(destination);
            }, Qt::SingleShotConnection);
    m_fomodWatcher.setFuture(QtConcurrent::run(
        [source, partial, reviewedSelections] {
          return installCuratedFomod(source, partial, reviewedSelections);
        }));
    return;
  }

  auto* record = m_state.action(action.id);
  record->status = CuratedActionStatus::WaitingForUser;
  persist();
  QString executable = action.parameters.value("executable").toString();
  for (const auto& artifact : m_state.artifacts)
    executable.replace("${artifact:" + artifact.id + "}", artifact.path);
  for (const auto& candidate : m_recipe.actions)
    executable.replace("${action:" + candidate.id + "}", actionOutputPath(candidate.id));
  const QString browseSource = action.parameters.value("browseSourceAction").toString();
  if (executable.isEmpty() && !browseSource.isEmpty())
    executable = actionOutputPath(browseSource);
  if (!QFileInfo::exists(executable))
    executable = findRecursively(QFileInfo(executable).absolutePath(), QFileInfo(executable).fileName());
  QString output = action.parameters.value("output").toString();
  output.replace("${instance}", m_config.instancePath);
  if (!output.isEmpty() && QDir::isRelativePath(output))
    output = QDir(m_config.instancePath).filePath(output);
  if (!output.isEmpty()) QDir().mkpath(output);
  QString instructions = action.parameters.value("instructions").toString();
  instructions.replace("${instance}", m_config.instancePath);
  const bool windowsTool = !QFileInfo(executable).isDir()
                           && QFileInfo(executable).suffix().compare(
                                  "exe", Qt::CaseInsensitive) == 0;
  const QString displayOutput = windowsTool ? toWinePath(output) : output;
  QString gamePath;
  if (windowsTool) {
    QString nativeGamePath = m_state.options.value("managedGamePath").toString();
    const QString gamePathSubdir =
        action.parameters.value("gamePathSubdir").toString();
    if (!gamePathSubdir.isEmpty())
      nativeGamePath = QDir(nativeGamePath).filePath(gamePathSubdir);
    gamePath = toWinePath(nativeGamePath);
  }
  emit assistedActionRequired(action.id, action.name,
                              instructions,
                              action.parameters.value("sourceUrl").toString(), executable,
                              displayOutput, gamePath);
}

bool CuratedGuideInstaller::launchAssistedExecutable(const QString& executable,
                                                     qint64* pidOut, QString* error)
{
  if (pidOut) *pidOut = 0;
  if (QFileInfo(executable).isDir()) {
    const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(executable));
    if (!opened && error) *error = QString("Could not open %1").arg(executable);
    return opened;
  }
  const auto config = FluorineConfig::load();
  if (!config || config->proton_path.isEmpty() || config->prefix_path.isEmpty()) {
    if (error) *error = "Fluorine's Proton prefix must be configured before running this tool.";
    return false;
  }
  if (!QFileInfo::exists(executable)) {
    if (error) *error = QString("Tool executable was not found: %1").arg(executable);
    return false;
  }
  const auto [started, pid] = ProtonLauncher()
                                  .setBinary(executable)
                                  .setWorkingDir(QFileInfo(executable).absolutePath())
                                  // Assisted installers may read the stock game and
                                  // write to a sibling mods folder. Expose the whole
                                  // curated instance to the Steam Linux Runtime.
                                  .setGameDirectory(m_config.instancePath)
                                  .setProtonPath(config->proton_path)
                                  .setPrefix(config->prefix_path)
                                  .setSteamAppId(config->app_id)
                                  .setSteamDrm(false)
                                  .setStoreVariant(m_state.options.value("store").toString())
                                  .setUseSLR(true)
                                  .launch();
  if (!started) {
    if (error) *error = QString("Could not launch %1 through Proton.").arg(executable);
    return false;
  }
  m_protonPid = pid;
  if (pidOut) *pidOut = pid;
  emit log(QString("Assisted tool launched through Proton (PID %1).").arg(pid));
  if (error) error->clear();
  return true;
}

bool CuratedGuideInstaller::assistedSessionRunning(qint64 pid) const
{
  if (processAlive(pid)) return true;
  const auto config = FluorineConfig::load();
  if (!config || config->prefix_path.isEmpty()) return false;
  return wineserverForPrefix(config->prefix_path) > 0;
}

bool CuratedGuideInstaller::finishAssistedAction(QString* error)
{
  const auto* action = m_recipe.action(m_currentAction);
  if (!action) {
    if (error) *error = "Assisted action is no longer active.";
    return false;
  }

  // The Proton wrapper can remain alive solely because wineserver is still
  // servicing the prefix after the tool window closes. Clicking Finish is the
  // user's explicit confirmation that the tool is done, so terminate only the
  // exact launched session/prefix before checking its output.
  shutdownProtonSession();
  QString validationError;
  if (!verifyActionOutput(*action, &validationError)) {
    if (error) *error = validationError;
    return false;
  }

  QString output = action->parameters.value("output").toString();
  output.replace("${instance}", m_config.instancePath);
  if (!output.isEmpty() && QDir::isRelativePath(output))
    output = QDir(m_config.instancePath).filePath(output);
  if (error) error->clear();
  completeAction(output);
  return true;
}

void CuratedGuideInstaller::editIni(const CuratedGuideAction& action)
{
  const QString relativePath = action.parameters.value("path").toString();
  const QString path = QDir(m_config.instancePath).filePath(relativePath);
  QFile input(path);
  if (!input.open(QIODevice::ReadOnly | QIODevice::Text))
    return failAction(QString("Cannot open guide INI for editing: %1").arg(path));
  QString contents = QString::fromUtf8(input.readAll());
  input.close();

  const auto values = action.parameters.value("values").toObject();
  for (auto it = values.begin(); it != values.end(); ++it) {
    const QRegularExpression expression(
        QStringLiteral("(^[\\t ]*%1[\\t ]*=[\\t ]*).*$")
            .arg(QRegularExpression::escape(it.key())),
        QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
    if (!contents.contains(expression))
      return failAction(QString("Guide setting %1 was not found in %2")
                            .arg(it.key(), path));
    contents.replace(expression, QStringLiteral("\\1") + it.value().toVariant().toString());
  }

  QSaveFile output(path);
  if (!output.open(QIODevice::WriteOnly | QIODevice::Text)
      || output.write(contents.toUtf8()) < 0 || !output.commit())
    return failAction(QString("Cannot save guide INI edits to %1").arg(path));
  emit log(QString("Applied reviewed guide settings to %1").arg(relativePath));
  completeAction(path);
}

void CuratedGuideInstaller::writeProfile(const CuratedGuideAction& action)
{
  const QString selectedProfile =
      action.parameters.value("profileName").toString("Default");
  QDir().mkpath(QDir(m_config.instancePath).filePath("mods"));
  QDir().mkpath(QDir(m_config.instancePath).filePath("overwrite"));

  auto writeText = [this](const QString& path, const QStringList& lines) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream stream(&file);
    for (const auto& line : lines) stream << line << "\r\n";
    return file.commit();
  };
  const QString legacySeparator =
      QDir(m_config.instancePath).filePath("mods/Create separator_separator");
  if (QDir(legacySeparator).exists()
      && QDir(legacySeparator).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty())
    QDir().rmdir(legacySeparator);

  QJsonArray profileDefinitions = m_recipe.profile.value("profiles").toArray();
  if (profileDefinitions.isEmpty()) {
    QJsonObject definition = m_recipe.profile;
    definition["name"] = selectedProfile;
    profileDefinitions.push_back(definition);
  }

  const QString managedGame = m_state.options.value("managedGamePath").toString();
  const QString defaultIni = QDir(managedGame).filePath("Fallout_default.ini");
  const QStringList resolution = m_state.options.value("resolution").toString().split('x');
  const int width = resolution.value(0).toInt();
  const int height = resolution.value(1).toInt();
  bool selectedProfileWritten = false;
  for (const auto& value : profileDefinitions) {
    const QJsonObject definition = value.toObject();
    const QString profileName = definition.value("name").toString();
    const QString profilePath =
        QDir(m_config.instancePath).filePath("profiles/" + profileName);
    if (!QDir().mkpath(QDir(profilePath).filePath("saves")))
      return failAction(QString("Cannot create profile folder: %1").arg(profilePath));
    selectedProfileWritten = selectedProfileWritten || profileName == selectedProfile;

    QStringList modlist;
    for (const auto& line : jsonStrings(definition.value("modlist"))) {
      const QString entry =
          (!line.isEmpty() && (line.front() == '+' || line.front() == '-'
                               || line.front() == '*'))
              ? line.mid(1).trimmed()
              : line.trimmed();
      if (entry.compare(QStringLiteral("overwrite"), Qt::CaseInsensitive) != 0)
        modlist.push_back(line);
    }
    for (const auto& line : modlist) {
      if ((line.startsWith('+') || line.startsWith('-'))
          && line.endsWith("_separator")) {
        QDir().mkpath(QDir(m_config.instancePath).filePath("mods/" + line.mid(1)));
      }
    }
    if (!writeText(QDir(profilePath).filePath("modlist.txt"), modlist)
        || !writeText(QDir(profilePath).filePath("plugins.txt"),
                      jsonStrings(definition.value("plugins")))
        || !writeText(QDir(profilePath).filePath("loadorder.txt"),
                      jsonStrings(definition.value("loadorder"))))
      return failAction(QString("Cannot write profile lists for %1").arg(profileName));

    if (QFileInfo::exists(defaultIni)) {
      QString error;
      if (!copyFile(defaultIni, QDir(profilePath).filePath("Fallout.ini"), &error))
        return failAction(error);
    }

    QSaveFile customIni(QDir(profilePath).filePath("FalloutCustom.ini"));
    if (!customIni.open(QIODevice::WriteOnly | QIODevice::Text))
      return failAction(QString("Cannot write FalloutCustom.ini for %1").arg(profileName));
    QString customText = definition.value("falloutCustomIni").toString();
    const QString fov = m_state.options.value("worldFov").toString("75");
    customText.replace(QRegularExpression("fDefaultWorldFOV\\s*=\\s*[^\\r\\n]+",
                                          QRegularExpression::CaseInsensitiveOption),
                       "fDefaultWorldFOV=" + fov);
    customIni.write(customText.toUtf8());
    if (!customIni.commit())
      return failAction(QString("Cannot commit FalloutCustom.ini for %1").arg(profileName));

    QSettings prefs(QDir(profilePath).filePath("FalloutPrefs.ini"), QSettings::IniFormat);
    prefs.setValue("Display/iSize W", width > 0 ? width : 1920);
    prefs.setValue("Display/iSize H", height > 0 ? height : 1080);
    prefs.setValue("Display/bFull Screen", 1);
    prefs.setValue("Display/iMultiSample",
                   m_state.options.value("preset").toString() == "Ultra" ? 8 : 4);
    prefs.sync();

    QSettings profileSettings(QDir(profilePath).filePath("settings.ini"),
                              QSettings::IniFormat);
    profileSettings.setValue("LocalSettings", true);
    profileSettings.setValue("LocalSaves", true);
    profileSettings.sync();
  }
  if (!selectedProfileWritten)
    return failAction(QString("Selected profile was not generated: %1")
                          .arg(selectedProfile));

  QSettings ini(QDir(m_config.instancePath).filePath("ModOrganizer.ini"), QSettings::IniFormat);
  ini.remove("General");
  ini.setValue("gameName", curatedGamePluginId(m_recipe.gamePlugin));
  ini.setValue("selected_profile", selectedProfile);
  ini.setValue("gamePath", managedGame);
  ini.setValue("first_start", false);
  ini.setValue("Settings/mod_directory", QDir(m_config.instancePath).filePath("mods"));
  ini.setValue("Settings/download_directory", m_config.downloadsPath);
  ini.setValue("Settings/profiles_directory", QDir(m_config.instancePath).filePath("profiles"));
  ini.setValue("Settings/overwrite_directory", QDir(m_config.instancePath).filePath("overwrite"));
  ini.setValue("fluorine/vfs_root_builder", false);
  ini.sync();
  completeAction(QDir(m_config.instancePath).filePath("profiles"));
}

void CuratedGuideInstaller::validateProfile(const CuratedGuideAction& action)
{
  QString error;
  for (const auto& installedAction : m_recipe.actions) {
    if (installedAction.type != "install_mod"
        && installedAction.type != "install_root") continue;
    const auto* record = m_state.action(installedAction.id);
    if (!record || record->status == CuratedActionStatus::Skipped) continue;
    if (record->status != CuratedActionStatus::Complete
        || !verifyActionOutput(installedAction, &error))
      return failAction(error.isEmpty()
                            ? QString("Required mod action is incomplete: %1")
                                  .arg(installedAction.name)
                            : error);
  }
  if (!verifyActionOutput(action, &error)) return failAction(error);
  if (action.parameters.value("registerInstance").toBool(false)) {
    const QString instance = comparablePath(m_config.instancePath);
    const QString globalRoot = comparablePath(InstanceManager::globalInstancesRootPath());
    if (instance.startsWith(globalRoot + QDir::separator()))
      InstanceManager::unregisterPortableInstance(m_config.instancePath);
    else
      InstanceManager::registerPortableInstance(m_config.instancePath);
  }
  completeAction(m_config.instancePath);
}

bool CuratedGuideInstaller::verifyActionOutput(const CuratedGuideAction& action,
                                               QString* error) const
{
  QString base = action.parameters.value("output").toString();
  if (base.isEmpty() && action.type == "install_mod")
    base = QDir(m_config.instancePath)
               .filePath("mods/" + action.parameters.value("folder").toString(action.name));
  if (base.isEmpty() && action.type == "install_root")
    base = m_state.options.value("managedGamePath").toString();
  base.replace("${instance}", m_config.instancePath);
  for (auto it = m_state.options.begin(); it != m_state.options.end(); ++it)
    base.replace("${option:" + it.key() + "}", it.value().toString());
  if (!base.isEmpty() && QDir::isRelativePath(base)) base = QDir(m_config.instancePath).filePath(base);
  if (action.type == "install_mod" && !validateCuratedModLayout(base, error))
    return false;
  for (const auto& relative : jsonStrings(action.validation.value("requiredFiles"))) {
    const QString path = QDir(base.isEmpty() ? m_config.instancePath : base).filePath(relative);
    if (!QFileInfo::exists(path)) {
      if (error) *error = QString("Required output is missing: %1").arg(path);
      return false;
    }
  }
  for (const auto& pattern : jsonStrings(action.validation.value("requiredGlobs"))) {
    QDirIterator matches(base.isEmpty() ? m_config.instancePath : base, {pattern},
                         QDir::Files, QDirIterator::Subdirectories);
    if (!matches.hasNext()) {
      if (error) *error = QString("Required output matching %1 is missing below %2")
                              .arg(pattern, base);
      return false;
    }
  }
  const int minimumFiles = action.validation.value("minimumFiles").toInt();
  if (minimumFiles > 0) {
    int count = 0;
    QDirIterator files(base.isEmpty() ? m_config.instancePath : base, QDir::Files,
                       QDirIterator::Subdirectories);
    while (files.hasNext() && count < minimumFiles) {
      files.next();
      ++count;
    }
    if (count < minimumFiles) {
      if (error) *error = QString("Expected at least %1 output file(s) below %2")
                              .arg(minimumFiles).arg(base);
      return false;
    }
  }
  if (error) error->clear();
  return true;
}

void CuratedGuideInstaller::completeAction(const QString& outputPath)
{
  auto* record = m_state.action(m_currentAction);
  if (!record) return;
  record->status = CuratedActionStatus::Complete;
  record->error.clear();
  record->completedAt = nowUtc();
  if (!outputPath.isEmpty() && QFileInfo(outputPath).isDir())
    record->outputDigest = curatedBlake3Tree(outputPath);
  m_currentAction.clear();
  persist();
  QTimer::singleShot(0, this, &CuratedGuideInstaller::drive);
}

void CuratedGuideInstaller::completeAcquisition(const QString& actionId,
                                                const QString& outputPath)
{
  auto* record = m_state.action(actionId);
  if (!record) return;
  record->status = CuratedActionStatus::Complete;
  record->error.clear();
  record->completedAt = nowUtc();
  if (!outputPath.isEmpty() && QFileInfo(outputPath).isDir())
    record->outputDigest = curatedBlake3Tree(outputPath);
  m_activeAcquisitions.remove(actionId);
  if (m_currentAction == actionId) m_currentAction.clear();
  persist();
  if (!m_cancelled && m_state.overallStatus != "failed")
    QTimer::singleShot(0, this, &CuratedGuideInstaller::drive);
}

void CuratedGuideInstaller::failAcquisition(const QString& actionId,
                                            const QString& reason)
{
  if (auto* record = m_state.action(actionId)) {
    record->status = CuratedActionStatus::Failed;
    record->error = reason;
  }
  m_activeAcquisitions.remove(actionId);
  if (m_currentAction == actionId) m_currentAction.clear();
  const bool firstFailure = m_state.overallStatus != "failed";
  m_state.overallStatus = "failed";
  persist();
  if (firstFailure) emit failed(reason);
}

void CuratedGuideInstaller::shutdownProtonSession()
{
  if (m_protonPid <= 0) return;
  const qint64 launchedPid = m_protonPid;
  m_protonPid = 0;

  const auto config = FluorineConfig::load();
  const QString prefix = config ? config->prefix_path : QString{};
  const QString expectedPrefix = prefix.isEmpty() ? QString{} : comparablePath(prefix);
  const QString launchedPrefix = procEnvironmentValue(launchedPid, "WINEPREFIX");
  const bool ownsLaunch = processAlive(launchedPid) && !expectedPrefix.isEmpty()
                          && !launchedPrefix.isEmpty()
                          && comparablePath(launchedPrefix) == expectedPrefix;
  qint64 wineserverPid = wineserverForPrefix(prefix);

  if (ownsLaunch) ::kill(static_cast<pid_t>(launchedPid), SIGTERM);
  if (wineserverPid > 0) ::kill(static_cast<pid_t>(wineserverPid), SIGTERM);

  for (int attempt = 0; attempt < 10; ++attempt) {
    if ((!ownsLaunch || !processAlive(launchedPid))
        && (wineserverPid <= 0 || !processAlive(wineserverPid)))
      break;
    QThread::msleep(50);
  }
  if (ownsLaunch && processAlive(launchedPid))
    ::kill(static_cast<pid_t>(launchedPid), SIGKILL);
  if (wineserverPid > 0 && processAlive(wineserverPid))
    ::kill(static_cast<pid_t>(wineserverPid), SIGKILL);
  emit log("Stopped the curated installer Proton/Wine session.");
}

void CuratedGuideInstaller::failAction(const QString& reason)
{
  shutdownProtonSession();
  if (auto* record = m_state.action(m_currentAction)) {
    record->status = CuratedActionStatus::Failed;
    record->error = reason;
  }
  CuratedGuideNxmBroker::instance().clear();
  m_state.overallStatus = "failed";
  persist();
  m_currentAction.clear();
  emit failed(reason);
}

void CuratedGuideInstaller::persist()
{
  QString error;
  if (!m_state.save(m_statePath, &error) && !error.isEmpty()) emit log("State save failed: " + error);
}
