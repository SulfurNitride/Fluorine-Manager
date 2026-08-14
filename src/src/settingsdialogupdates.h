#ifndef SETTINGSDIALOGUPDATES_H
#define SETTINGSDIALOGUPDATES_H

#include "fluorineupdater.h"
#include "settingsdialog.h"

#include <QCoreApplication>

class QCheckBox;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;

class FluorineUpdater;
class FluorineUpdateInstaller;

// Dedicated Settings > Updates tab. Hosts the Fluorine self-update
// preferences (channel selector, startup check toggle) plus an explicit
// "Check for updates now" button with live status feedback.
class UpdatesSettingsTab : public SettingsTab
{
  Q_DECLARE_TR_FUNCTIONS(UpdatesSettingsTab)
public:
  UpdatesSettingsTab(Settings& settings, SettingsDialog& dialog);

  void update() override;

private:
  void onCheckNow();
  void onInstall();
  void clearPendingUpdate();
  void setCheckInProgress(bool checking);
  FluorineUpdater::Channel selectedChannel() const;

  QCheckBox* m_checkForUpdates   = nullptr;
  QComboBox* m_channelBox        = nullptr;
  QLabel* m_currentVersionLabel  = nullptr;
  QLabel* m_buildInfoLabel       = nullptr;
  QPushButton* m_checkNowButton  = nullptr;
  QPushButton* m_installButton   = nullptr;
  QLabel* m_statusLabel          = nullptr;
  QProgressBar* m_progressBar    = nullptr;
  FluorineUpdater* m_updater     = nullptr;
  FluorineUpdateInstaller* m_installer = nullptr;
  FluorineUpdater::ReleaseInfo m_pendingUpdate{};
  bool m_updatePending           = false;
  bool m_checkInProgress         = false;
};

#endif  // SETTINGSDIALOGUPDATES_H
