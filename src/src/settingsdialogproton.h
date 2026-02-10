#ifndef SETTINGSDIALOGPROTON_H
#define SETTINGSDIALOGPROTON_H

#include <QFutureWatcher>
#include <QObject>

#include "settings.h"
#include "settingsdialog.h"

class ProtonSettingsTab : public QObject, public SettingsTab
{
  Q_OBJECT

public:
  ProtonSettingsTab(Settings& settings, SettingsDialog& dialog);

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

  void startInstallTask(uint32_t appId, const QString& prefixPath,
                        const QString& protonName, const QString& protonPath,
                        bool useUmuForPrefix, bool preferSystemUmu,
                        bool useSteamRun);

  void enqueueStatus(const QString& message);
  void enqueueProgress(float progress);

  static void statusCallback(const char* message);
  static void logCallback(const char* message);
  static void progressCallback(float progress);

private slots:
  void onInstallFinished();

private:
  QFutureWatcher<InstallResult> m_installWatcher;

  uint32_t m_pendingAppId = 0;
  QString m_pendingPrefixPath;
  QString m_pendingProtonName;
  QString m_pendingProtonPath;

  bool m_busy = false;
};

#endif  // SETTINGSDIALOGPROTON_H
