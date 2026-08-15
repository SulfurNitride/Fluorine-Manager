#ifndef PREFIXSETUPDIALOG_H
#define PREFIXSETUPDIALOG_H

#include "prefixsetuprunner.h"

#include <QDialog>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QTextEdit>
#include <QThread>

class QCloseEvent;

/// Modal dialog that shows step-by-step Wine prefix setup progress.
///
/// Displays each setup step with status icons, a live log, and
/// allows retrying individual failed steps or cleaning up on failure.
class PrefixSetupDialog : public QDialog
{
  Q_OBJECT

public:
  /// Create the dialog.  Does NOT start the setup; call exec() which
  /// auto-starts on show.
  PrefixSetupDialog(const QString& prefixPath,
                    const QString& protonPath,
                    uint32_t appId,
                    QWidget* parent = nullptr);
  ~PrefixSetupDialog() override;

  /// True if all steps succeeded.
  bool succeeded() const { return m_allSucceeded; }
  /// True only after the worker has emitted its terminal completion signal.
  bool quiescent() const { return m_started && !m_running; }

public slots:
  void reject() override;

protected:
  void showEvent(QShowEvent* event) override;
  void closeEvent(QCloseEvent* event) override;

private slots:
  void onStepStarted(int index);
  void onStepFinished(int index, bool success, const QString& error);
  void onLogMessage(const QString& text);
  void onDownloadStarted(const QString& name);
  void onDownloadProgress(const QString& name, qint64 bytesReceived,
                          qint64 bytesTotal, double bytesPerSecond);
  void onDownloadFinished();
  void onProgressChanged(float progress);
  void onFinished(bool allSucceeded);

  void onCancel();
  void onRetryFailed();
  void onShowLog();
  void onDeleteAndClose();
  void onClose();

private:
  void buildUI();
  void populateStepList();
  void updateButtons();
  QString writeSetupLog() const;

  // -- state -----------------------------------------------------------------
  QString m_prefixPath;
  QString m_protonPath;
  uint32_t m_appId;
  bool m_allSucceeded = false;
  bool m_running      = false;
  bool m_started      = false;
  bool m_closeWhenFinished = false;

  // -- worker ----------------------------------------------------------------
  PrefixSetupRunner* m_runner = nullptr;
  QThread* m_workerThread     = nullptr;

  // -- widgets ---------------------------------------------------------------
  QListWidget* m_stepList     = nullptr;
  QTextEdit* m_logView        = nullptr;
  QProgressBar* m_progressBar = nullptr;
  QLabel* m_downloadLabel     = nullptr;
  QProgressBar* m_downloadProgressBar = nullptr;
  QLabel* m_statusLabel       = nullptr;
  QPushButton* m_cancelBtn    = nullptr;
  QPushButton* m_retryBtn     = nullptr;
  QPushButton* m_showLogBtn   = nullptr;
  QPushButton* m_deleteBtn    = nullptr;
  QPushButton* m_closeBtn     = nullptr;
};

#endif // PREFIXSETUPDIALOG_H
