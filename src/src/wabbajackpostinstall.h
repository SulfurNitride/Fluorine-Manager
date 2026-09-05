#pragma once

#include "nexusartifactdownloader.h"

#include <QObject>
#include <QProcess>
#include <QStringList>

struct WabbajackPostInstallConfig
{
  QString machineName;
  QString gameId;
  QString installPath;
  QString downloadsPath;
  QString sourceGamePath;
  QString store;
  QString jobId;
};

// Applies trusted Linux/Fluorine adjustments after CLF3 has materialized the
// authored Wabbajack output. It never interprets or executes README text.
class WabbajackPostInstall : public QObject
{
  Q_OBJECT

public:
  explicit WabbajackPostInstall(QObject* parent = nullptr);

  static bool hasAdapter(const QString& machineName);
  static QString detectedStore(const QString& gamePath);
  void start(const WabbajackPostInstallConfig& config);
  void provideNexusAuthorization(const QString& requestId, const QString& url);
  void rejectNexusAuthorization(const QString& requestId, const QString& reason);
  void cancel();

signals:
  void stepStarted(QString id, QString title);
  void stepFinished(QString id);
  void statusChanged(QString status);
  void logLine(QString line);
  void nexusAuthorizationRequired(QString requestId, QString artifactName,
                                   QString domain, int modId, int fileId,
                                   qint64 expectedSize);
  void toolProgress(QString id, qint64 completed, qint64 total);
  void completed(QStringList adjustments, QStringList warnings);
  void failed(QString message);

private:
  WabbajackPostInstallConfig m_config;
  NexusArtifactDownloader m_nexus;
  QProcess m_extractor;
  QProcess m_nativePatcher;
  QStringList m_adjustments;
  QStringList m_warnings;
  QJsonObject m_profile;
  QString m_gameRootPath;
  QString m_manualRootPath;
  QString m_patcherExtractPath;
  QString m_gameExecutable;
  QString m_nativePatcherToolId;
  QStringList m_nativePatcherTargets;
  QStringList m_requiredPatcherBackups;
  bool m_active{false};

  void ensureInstance();
  void beginAdapter();
  void gameRootReady();
  void deployRootFiles();
  void runProfilePatcher();
  void runVnvPatcher();
  void runOblivionPatcher();
  void acquireNativePatcher(const QString& toolId);
  void extractNativePatcher(const QString& archivePath);
  void launchExtractedNativePatcher();
  void nativePatcherFinished(int code, QProcess::ExitStatus status);
  void configureInstance();
  void finish();
  void fail(const QString& message);
};
