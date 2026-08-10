#ifndef SETTINGSDIALOGPROTON_H
#define SETTINGSDIALOGPROTON_H

#include <QFutureWatcher>
#include <QObject>
#include <QPointer>

#include "settings.h"
#include "settingsdialog.h"
#include "slroperationcontext.h"

class QProgressDialog;

class ProtonSettingsTab : public QObject, public SettingsTab
{
  Q_OBJECT

public:
  ProtonSettingsTab(Settings& settings, SettingsDialog& dialog);
  ~ProtonSettingsTab() override;

  void update() override;

private:
  struct InstallResult
  {
    QString error;
  };

  void populateProtons();
  void refreshState();
  void setBusy(bool busy);

  void onCreatePrefix();
  void onDeletePrefix();
  void onRecreatePrefix();
  void onOpenPrefixFolder();
  void onWinetricks();
  void onBrowsePrefixLocation();
  void onDownloadSLR();

  static QString ensureWinetricks();
  static QString findProtonWine(const QString& protonPath);

  void runPrefixSetupDialog(uint32_t appId, const QString& prefixPath,
                            const QString& protonName, const QString& protonPath);

  void appendInstallLog(const QString& message);
  void cancelAndWaitForSlrWorker() noexcept;

  static void logCallback(const char* message);

private slots:
  void onInstallFinished();

private:
  QFutureWatcher<InstallResult> m_installWatcher;
  QFutureWatcher<QString> m_slrWatcher;
  SlrCancellationSource m_slrCancellation;
  QPointer<QProgressDialog> m_slrProgress;

  uint32_t m_pendingAppId = 0;
  QString m_pendingPrefixPath;
  QString m_pendingProtonName;
  QString m_pendingProtonPath;

  bool m_busy = false;
  bool m_settingsDialogClosing = false;
};

#endif  // SETTINGSDIALOGPROTON_H
