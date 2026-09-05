#pragma once

#include "curatedguideinstallstate.h"
#include "curatedgamemanifest.h"
#include "curatedguiderecipe.h"

#include <QFutureWatcher>
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QTimer>

struct CuratedGuideInstallConfig
{
  QString instanceName;
  QString instancePath;
  QString downloadsPath;
  QString jobPath;
  QJsonObject options;
};

class CuratedGuideInstaller : public QObject
{
  Q_OBJECT

public:
  explicit CuratedGuideInstaller(QObject* parent = nullptr);
  ~CuratedGuideInstaller() override;

  void start(const CuratedGuideRecipe& recipe, const CuratedGuideInstallConfig& config);
  void resume(const CuratedGuideRecipe& recipe, const QString& statePath);
  void repair(const CuratedGuideRecipe& recipe, const QString& statePath);
  void cancel();
  void provideManualArtifact(const QString& artifactId, const QString& path);
  bool finishAssistedAction(QString* error = nullptr);
  bool launchAssistedExecutable(const QString& executable, qint64* pid = nullptr,
                                QString* error = nullptr);
  bool assistedSessionRunning(qint64 pid) const;

  const CuratedGuideInstallState& state() const { return m_state; }
  QString statePath() const { return m_statePath; }

signals:
  void progress(int completed, int total, QString currentAction);
  void log(QString message);
  void artifactProgress(QString artifactId, qint64 received, qint64 total);
  void nexusDownloadRequired(QString artifactId, QString displayName, QString pageUrl);
  void manualArtifactRequired(QString artifactId, QString displayName,
                              QString sourceUrl, QString expectedFilename);
  void assistedActionRequired(QString actionId, QString title, QString instructions,
                              QString sourceUrl, QString executablePath,
                              QString outputPath, QString gamePath);
  void finished(QString instancePath);
  void failed(QString reason);
  void cancelled();

private:
  CuratedGuideRecipe m_recipe;
  CuratedGuideInstallConfig m_config;
  CuratedGuideInstallState m_state;
  QString m_statePath;
  QString m_currentAction;
  bool m_cancelled{false};
  bool m_repairing{false};
  QNetworkAccessManager m_network;
  QProcess m_process;
  QTimer m_manualScan;
  QFutureWatcher<CuratedVerifiedCopyResult> m_copyWatcher;
  QFutureWatcher<QPair<bool, QString>> m_fomodWatcher;
  qint64 m_protonPid{0};
  QSet<QString> m_activeAcquisitions;
  QHash<QString, QProcess*> m_extractionProcesses;
  bool m_nexusAccountCheckStarted{false};

  void initialiseState();
  void reconcileStateWithRecipe();
  void drive();
  bool ensureNexusAccountReady();
  void scheduleBackgroundExtractions();
  void finishBackgroundExtraction(const QString& actionId, const QString& output,
                                  QProcess* process, bool success,
                                  const QString& error = {});
  void stopBackgroundExtractions();
  bool conditionMatches(const CuratedGuideAction& action) const;
  bool dependenciesComplete(const CuratedGuideAction& action) const;
  void execute(const CuratedGuideAction& action);
  void acquire(const CuratedGuideAction& action);
  void acquireNexus(const CuratedGuideAction& action,
                    const CuratedGuideArtifact& artifact,
                    const QString& nxmUrl = {});
  void resolveNexusFile(const CuratedGuideAction& action,
                        const CuratedGuideArtifact& artifact);
  void download(const QString& actionId, const QString& artifactId, const QUrl& url);
  void copyGame(const CuratedGuideAction& action);
  void extract(const CuratedGuideAction& action);
  void installMod(const CuratedGuideAction& action, bool root);
  void runNative(const CuratedGuideAction& action);
  void runProton(const CuratedGuideAction& action);
  void pollProtonAction(const CuratedGuideAction& action, qint64 pid, int attempts);
  void assistedTool(const CuratedGuideAction& action);
  void editIni(const CuratedGuideAction& action);
  void writeProfile(const CuratedGuideAction& action);
  void validateProfile(const CuratedGuideAction& action);
  void completeAction(const QString& outputPath = {});
  void completeAcquisition(const QString& actionId, const QString& outputPath = {});
  void failAcquisition(const QString& actionId, const QString& reason);
  void failAction(const QString& reason);
  void shutdownProtonSession();
  void persist();
  QString artifactPath(const CuratedGuideArtifact& artifact) const;
  QString actionOutputPath(const QString& actionId) const;
  bool verifyArtifact(const CuratedGuideArtifact& artifact, const QString& path,
                      QString* error) const;
  bool verifyActionOutput(const CuratedGuideAction& action, QString* error) const;
  static QPair<bool, QString> copyDirectory(const QString& source,
                                             const QString& destination);
  static QString find7z();
};
