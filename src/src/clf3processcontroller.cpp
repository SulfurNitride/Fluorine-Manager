#include "clf3processcontroller.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>

Clf3ProcessController::Clf3ProcessController(QObject* parent)
    : QObject(parent)
{
  connect(&m_engineManager, &Clf3EngineManager::ready, this, [this](const QString& path) {
    m_preparing = false;
    m_managedEnginePath = path;
    m_process.start(path, m_arguments, QIODevice::ReadWrite);
  });
  connect(&m_engineManager, &Clf3EngineManager::statusChanged, this, [this](const QString& status) {
    emit statusChanged(status);
    emit logLine(status);
  });
  connect(&m_engineManager, &Clf3EngineManager::failed, this, [this](const QString& reason) {
    m_preparing = false;
    emit failed(reason);
  });
  connect(&m_engineManager, &Clf3EngineManager::cancelled, this, [this] {
    m_preparing = false;
    emit cancelled();
  });
  m_process.setProcessChannelMode(QProcess::SeparateChannels);
  connect(&m_process, &QProcess::readyReadStandardOutput, this,
          &Clf3ProcessController::consumeStdout);
  connect(&m_process, &QProcess::readyReadStandardError, this,
          &Clf3ProcessController::consumeStderr);
  m_cancelTimer.setSingleShot(true);
  m_killTimer.setSingleShot(true);
  m_handshakeTimer.setSingleShot(true);
  connect(&m_cancelTimer, &QTimer::timeout, this, [this] {
    if (isRunning() && m_cancelRequested) {
      m_process.terminate();
      m_killTimer.start(2000);
    }
  });
  connect(&m_killTimer, &QTimer::timeout, this, [this] {
    if (isRunning() && m_cancelRequested) m_process.kill();
  });
  connect(&m_handshakeTimer, &QTimer::timeout, this, [this] {
    m_failure = tr("CLF3 did not respond to the startup handshake within 30 seconds.");
    m_completed = true;
    m_process.kill();
  });
  connect(&m_process, &QProcess::started, this, [this] {
    if (m_cancelRequested) {
      send({{"type", "cancel"}});
    } else {
      m_handshakeTimer.start(30000);
    }
  });
  connect(&m_process, &QProcess::errorOccurred, this,
          [this](QProcess::ProcessError error) {
            // Crashes also emit finished(); report one terminal result, after exit.
            if (error != QProcess::FailedToStart) return;
            m_cancelTimer.stop();
            m_killTimer.stop();
            m_handshakeTimer.stop();
            m_completed = true;
            if (m_cancelRequested) emit cancelled();
            else emit failed(tr("CLF3 could not be started: %1").arg(m_process.errorString()));
          });
  connect(&m_process,
          qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [this](int code, QProcess::ExitStatus status) {
            consumeStdout();
            consumeStderr();
            m_cancelTimer.stop();
            m_killTimer.stop();
            m_handshakeTimer.stop();
            if (m_cancelRequested) {
              emit cancelled();
            } else if (!m_failure.isEmpty()) {
              emit failed(m_failure);
            } else if (status != QProcess::NormalExit || code != 0 || !m_completed) {
              emit failed(status == QProcess::CrashExit
                              ? tr("CLF3 crashed during installation.")
                              : tr("CLF3 exited with code %1 without a successful installation.").arg(code));
            } else {
              emit completed(m_result);
            }
          });
}

QString Clf3ProcessController::enginePath() const
{
  const QString override = qEnvironmentVariable("FLUORINE_CLF3_PATH");
  if (!override.isEmpty()) return QFileInfo(override).absoluteFilePath();
  if (!m_managedEnginePath.isEmpty()) return m_managedEnginePath;
  return m_engineManager.cachedEnginePath();
}

bool Clf3ProcessController::isRunning() const
{
  return m_preparing || m_process.state() != QProcess::NotRunning;
}

void Clf3ProcessController::startInstall(const QString& source,
                                         const QString& downloads,
                                         const QString& output,
                                         const QString& game,
                                         const QString& machineName)
{
  if (isRunning()) return;
  m_stdoutBuffer.clear();
  m_stderrBuffer.clear();
  m_result = {};
  m_failure.clear();
  m_cancelTimer.stop();
  m_killTimer.stop();
  m_handshakeTimer.stop();
  m_completed       = false;
  m_cancelRequested = false;

  QStringList arguments{QStringLiteral("install"), source, downloads, output};
  if (!game.isEmpty()) arguments << QStringLiteral("--game") << game;
  arguments << QStringLiteral("--jackify") << QStringLiteral("--hosted");
  if (!machineName.isEmpty())
    arguments << QStringLiteral("--machine-name") << machineName;
  m_arguments = arguments;
  if (!qEnvironmentVariableIsEmpty("FLUORINE_CLF3_PATH")) {
    m_process.start(enginePath(), arguments, QIODevice::ReadWrite);
  } else {
    m_preparing = true;
    m_engineManager.prepare();
  }
}

void Clf3ProcessController::sendNexusUrls(const QString& requestId,
                                          const QStringList& urls)
{
  QJsonArray jsonUrls;
  for (const auto& url : urls) jsonUrls.push_back(url);
  send({{"type", "download_authorization_result"},
        {"request_id", requestId},
        {"urls", jsonUrls}});
}

void Clf3ProcessController::sendManualFile(const QString& requestId,
                                           const QString& path)
{
  send({{"type", "manual_download_result"},
        {"request_id", requestId},
        {"path", path}});
}

void Clf3ProcessController::rejectRequest(const QString& requestId,
                                          const QString& reason)
{
  send({{"type", "authorization_failed"},
        {"request_id", requestId},
        {"error", reason}});
}

void Clf3ProcessController::cancel()
{
  if (!isRunning() || m_cancelRequested) return;
  m_cancelRequested = true;
  if (m_preparing) {
    m_engineManager.cancel();
    return;
  }
  m_handshakeTimer.stop();
  send({{"type", "cancel"}});
  m_cancelTimer.start(5000);
}

void Clf3ProcessController::consumeStdout()
{
  m_stdoutBuffer += m_process.readAllStandardOutput();
  // An engine may exit without a trailing newline on its final event.
  if (m_process.state() == QProcess::NotRunning && !m_stdoutBuffer.isEmpty()
      && !m_stdoutBuffer.endsWith('\n')) m_stdoutBuffer += '\n';
  qsizetype newline = -1;
  while ((newline = m_stdoutBuffer.indexOf('\n')) >= 0) {
    const QByteArray line = m_stdoutBuffer.left(newline).trimmed();
    m_stdoutBuffer.remove(0, newline + 1);
    if (line.isEmpty()) continue;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
      emit logLine(tr("Invalid CLF3 protocol line: %1").arg(QString::fromUtf8(line)));
      continue;
    }
    handleEvent(document.object());
  }
}

void Clf3ProcessController::consumeStderr()
{
  m_stderrBuffer += m_process.readAllStandardError();
  if (m_process.state() == QProcess::NotRunning && !m_stderrBuffer.isEmpty()
      && !m_stderrBuffer.endsWith('\n')) m_stderrBuffer += '\n';
  qsizetype newline;
  while ((newline = m_stderrBuffer.indexOf('\n')) >= 0) {
    const QByteArray line = m_stderrBuffer.left(newline);
    m_stderrBuffer.remove(0, newline + 1);
    if (!line.isEmpty()) emit logLine(QString::fromUtf8(line));
  }
}

void Clf3ProcessController::handleEvent(const QJsonObject& event)
{
  if (m_completed) return;
  const QString type = event.value("type").toString();
  if (m_cancelRequested) {
    // A cancellation during process startup must still unblock the host handshake.
    if (type == "hello") {
      send({{"type", "hello_ack"}, {"protocol_version", ProtocolVersion}});
      send({{"type", "cancel"}});
    }
    return;
  }
  if (type == "hello") {
    m_handshakeTimer.stop();
    const int protocol = event.value("protocol_version").toInt();
    if (protocol != ProtocolVersion) {
      m_failure = tr("CLF3 protocol %1 is incompatible with Fluorine protocol %2.")
                      .arg(protocol).arg(ProtocolVersion);
      m_completed = true;
      m_process.kill();
      return;
    }
    send({{"type", "hello_ack"}, {"protocol_version", ProtocolVersion}});
    emit engineReady(event.value("engine_version").toString());
  } else if (type == "PhaseChange" || type == "phase_changed") {
    emit phaseChanged(event.value("phase").toString());
  } else if (type == "plan_ready") {
    emit statusChanged(tr("Installing %1 · %2 archives")
                           .arg(event.value("name").toString())
                           .arg(event.value("archive_count").toInt()));
    for (const auto& value : event.value("artifacts").toArray()) {
      const auto artifact = value.toObject();
      emit itemMetadata(artifact.value("name").toString(),
                        artifact.value("display_name").toString(),
                        artifact.value("subtitle").toString(),
                        artifact.value("image_url").toString());
    }
  } else if (type == "Status" || type == "status") {
    emit statusChanged(event.value("message").toString());
  } else if (type == "DownloadProgress" || type == "artifact_progress") {
    emit artifactProgress(event.value("name").toString(),
                          event.value("downloaded").toVariant().toLongLong(),
                          event.value("total").toVariant().toLongLong(),
                          event.value("speed").toDouble());
  } else if (type == "item_started") {
    emit itemStarted(event.value("item_id").toString(),
                     event.value("name").toString(),
                     event.value("display_name").toString(),
                     event.value("subtitle").toString(),
                     event.value("stage").toString(),
                     event.value("image_url").toString(),
                     event.value("total").toVariant().toLongLong(),
                     event.value("unit").toString());
  } else if (type == "item_progress") {
    emit itemProgress(event.value("item_id").toString(),
                      event.value("completed").toVariant().toLongLong(),
                      event.value("total").toVariant().toLongLong(),
                      event.value("speed").toDouble(),
                      event.value("unit").toString());
  } else if (type == "item_message") {
    emit itemMessage(event.value("item_id").toString(),
                     event.value("message").toString());
  } else if (type == "item_completed") {
    emit itemCompleted(event.value("item_id").toString());
  } else if (type == "item_failed") {
    emit itemFailed(event.value("item_id").toString(),
                    event.value("message").toString());
  } else if (type == "ArchiveComplete" || type == "overall_progress") {
    emit overallProgress(event.value("index").toInt(), event.value("total").toInt());
  } else if (type == "download_authorization_required") {
    emit nexusAuthorizationRequired(
        event.value("request_id").toString(), event.value("archive_name").toString(),
        event.value("domain").toString(), event.value("mod_id").toInt(),
        event.value("file_id").toInt(),
        event.value("expected_size").toVariant().toLongLong());
  } else if (type == "manual_download_required") {
    emit manualDownloadRequired(
        event.value("request_id").toString(), event.value("archive_name").toString(),
        event.value("url").toString(), event.value("prompt").toString(),
        event.value("expected_size").toVariant().toLongLong(),
        event.value("expected_hash").toString());
  } else if (type == "install_completed") {
    m_completed = true;
    QJsonObject stats = event.value("stats").toObject();
    const QString gamePath = event.value("game_path").toString();
    if (!gamePath.isEmpty()) stats.insert(QStringLiteral("game_path"), gamePath);
    m_result = stats;
  } else if (type == "install_failed") {
    m_completed = true;
    m_failure = event.value("message").toString(tr("CLF3 installation failed."));
    if (m_failure.isEmpty()) m_failure = tr("CLF3 installation failed.");
  } else if (type == "protocol_warning") {
    emit logLine(event.value("message").toString());
  }
}

void Clf3ProcessController::send(const QJsonObject& command)
{
  if (!isRunning()) return;
  m_process.write(QJsonDocument(command).toJson(QJsonDocument::Compact));
  m_process.write("\n");
}
