#ifndef PREFIXSETUPRUNNER_H
#define PREFIXSETUPRUNNER_H

#include <QAtomicInt>
#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

/// Represents a single step in the prefix setup process.
struct SetupStep {
  enum Status { Pending, Running, Succeeded, Failed, Skipped };

  QString id;            ///< Machine-readable identifier
  QString displayName;   ///< Human-readable name for the UI
  Status status = Pending;
  QString errorMessage;
};

/// Runs the Wine prefix setup as a sequence of discrete, retryable steps.
///
/// Lives on a worker thread.  Communicates with the UI via signals.
/// Each step spawns a QProcess wrapped in Steam Linux Runtime if available.
class PrefixSetupRunner : public QObject
{
  Q_OBJECT

public:
  explicit PrefixSetupRunner(const QString& prefixPath,
                             const QString& protonPath,
                             uint32_t appId,
                             QObject* parent = nullptr);

  /// Read-only access to the step list for the UI.
  const QVector<SetupStep>& steps() const { return m_steps; }

  /// Request cancellation (thread-safe).
  void cancel() { m_cancelled.storeRelease(1); }

signals:
  void stepStarted(int index);
  void stepFinished(int index, bool success, const QString& error);
  void logMessage(const QString& text);
  void downloadStarted(const QString& name);
  void downloadProgress(const QString& name, qint64 bytesReceived,
                        qint64 bytesTotal, double bytesPerSecond);
  void downloadFinished();
  void progressChanged(float progress);   ///< 0.0 – 1.0
  void finished(bool allSucceeded);

public slots:
  /// Run all pending steps from the beginning.
  void start();

  /// Re-run only the steps that previously failed.
  void retryFailed();

  /// Re-run a single step by index.
  void retryStep(int index);

private:
  // -- helpers ---------------------------------------------------------------
  void buildStepList();
  bool runStep(int index);
  bool isCancelled() const { return m_cancelled.loadAcquire() != 0; }

  /// Run an external process with SLR wrapping and log its output.
  /// Returns exit code (0 = success).  If captured is non-null, a copy of
  /// the merged stdout/stderr output is appended to it for post-mortem
  /// pattern matching.
  int runProcess(const QString& exe,
                 const QStringList& args,
                 const QMap<QString, QString>& extraEnv,
                 int timeoutMs = -1,
                 QByteArray* captured = nullptr);

  /// Run a plain host command (NO SLR wrapping).
  /// Used for host utilities like curl, unzip that must not run in the container.
  int runHostProcess(const QString& exe,
                     const QStringList& args,
                     int timeoutMs = -1);

  /// Run a host command with extra env vars (NO SLR wrapping).
  /// Used for winetricks which is a host script that calls wine internally.
  int runHostProcessWithEnv(const QString& exe,
                            const QStringList& args,
                            const QMap<QString, QString>& extraEnv,
                            int timeoutMs = -1);

  /// Build a QProcess configured with SLR wrapping + cleaned environment.
  QProcess* buildWrappedProcess(const QString& exe,
                                const QMap<QString, QString>& extraEnv);

  // -- step implementations --------------------------------------------------
  bool stepProtonInit();
  bool stepDriveCleanup();
  bool stepD3DCompiler47();
  bool stepDirectXRuntime();
  bool stepVcrun2022();
  bool stepDotNetRuntimes();
  bool stepDotNetInstall(const QString& url, const QString& name,
                         const QStringList& knownSha256 = {});
  bool stepDotNetInstallPair(const QString& url32, const QString& url64,
                             const QString& name,
                             const QStringList& knownSha25632 = {},
                             const QStringList& knownSha25664 = {});
  int runDotNetInstaller(const QString& installerPath,
                         const QString& displayName,
                         const QMap<QString, QString>& env,
                         QByteArray* captured,
                         QString* installerLogPath);
  QStringList storageDiagnostics(const QString& installerPath) const;
  QString reportInstallerFailure(const QString& displayName,
                                 const QString& installerPath,
                                 int exitCode,
                                 const QByteArray& processOutput,
                                 const QString& installerLogPath);
  bool stepNuGetSignaturePolicy();
  bool stepGameDetection();
  bool stepWineRegistry();
  bool stepWin11Mode();
  bool stepPostSetup();

  // -- DirectX runtime helper -------------------------------------------------
  bool ensureDirectXRedist(QString& redistPath);

  // -- tool management -------------------------------------------------------
  bool downloadFile(const QString& url, const QString& destPath,
                    const QString& displayName = {});
  bool downloadAndVerify(const QString& url, const QString& destPath,
                         const QString& expectedSha256);
  bool downloadAndVerifyAny(const QStringList& urls, const QString& destPath,
                            const QStringList& expectedSha256);
  bool downloadRuntimeInstaller(const QString& url, const QString& destPath,
                                const QString& displayName,
                                const QStringList& knownSha256 = {});
  bool validateRuntimeInstaller(const QString& path, const QString& displayName,
                                const QStringList& knownSha256,
                                bool* knownHashMatch = nullptr);
  static QString fileSha256(const QString& filePath);
  static bool verifySha256(const QString& filePath, const QString& expectedHex);
  bool ensure7zz();
  bool ensureWinetricks();
  bool ensureCabextract();

  // -- Wine environment helpers ----------------------------------------------
  QString findWineBinary() const;
  QString findWineserverBinary() const;
  QString findProtonScript() const;
  static QString detectSteamPath() ;
  QString detectSLRRunScript() const;
  bool ensureSLRRunScript();
  static QString fluorineBinDir() ;
  static QString fluorineCacheDir() ;
  static QString fluorineTmpDir() ;
  QMap<QString, QString> baseWineEnv() const;
  bool applyDllOverrides(const QStringList& native,
                         const QStringList& nativeBuiltin);
  static QString makeDllOverrideEnv(const QString& base,
                                    const QStringList& native,
                                    const QStringList& nativeBuiltin);
  static bool isMicrosoftInstallerSuccess(int exitCode);
  static QString describeInstallerExitCode(int exitCode);
  SetupStep& currentStep();

  /// Kill any wineboot/wineserver/pv-adverb processes still bound to
  /// m_prefixPath from a previous aborted run. Safe to call when none exist.
  void killStalePrefixProcesses() const;

  // -- state -----------------------------------------------------------------
  QString m_prefixPath;
  QString m_protonPath;
  uint32_t m_appId;
  QAtomicInt m_cancelled{0};
  int m_currentStepIndex = -1;

  QString m_wineBin;
  QString m_wineserverBin;
  QString m_slrRunScript;
  QString m_winetricksPath;
  QString m_7zzPath;

  QVector<SetupStep> m_steps;

  // Step execution functions indexed parallel to m_steps.
  QVector<std::function<bool()>> m_stepFunctions;
};

#endif // PREFIXSETUPRUNNER_H
