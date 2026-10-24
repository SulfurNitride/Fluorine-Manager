/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <bsatk/bsafolder.h>
#include <uibase/errorcodes.h>
#include <uibase/imoinfo.h>
#include <uibase/iplugingame.h>
#include <uibase/log.h>
#include <uibase/tutorialcontrol.h>

#include "delayedfilewriter.h"
#include "fluorineupdater.h"
#include "iuserinterface.h"
#include "modinfo.h"
#include "modlistbypriorityproxy.h"
#include "modlistsortproxy.h"
#include "plugincontainer.h"
#include "problemcheckrunner.h"
#include "shared/fileregisterfwd.h"
#include "systemtraymanager.h"

struct SetupCheckInputs;
class Executable;
class WorkspaceFrame;
class WorkspaceLayout;
class SelectedModPanel;
class LibraryView;
class QStackedWidget;
class QToolButton;
class CategoryFactory;
class OrganizerCore;
class FilterList;
class DataTab;
class DownloadsTab;
class SavesTab;
#ifdef MO2_WEBENGINE
class BrowserDialog;
#endif

class PluginListSortProxy;
namespace BSA
{
class Archive;
}

namespace MOBase
{
class IPluginModPage;
}
namespace MOBase
{
class IPluginTool;
}

namespace MOShared
{
class DirectoryEntry;
}

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QList>
#include <QMainWindow>
#include <QObject>
#include <QPersistentModelIndex>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTime>
#include <QTimer>
#include <QVariant>
#include <Qt>
#include <QtConcurrent/QtConcurrentRun>

class QAction;
class QAbstractItemModel;
class QDateTime;
class QEvent;
class QFile;
class QListWidgetItem;
class QMenu;
class QModelIndex;
class QPoint;
class QProgressDialog;
class QTranslator;
class QTreeWidgetItem;
class QUrl;
class QWidget;

#ifndef Q_MOC_RUN
#include <boost/signals2.hpp>
#endif

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Ui
{
class MainWindow;
}

class Settings;

class MainWindow : public QMainWindow, public IUserInterface
{
  Q_OBJECT

  friend class OrganizerProxy;

public:
  explicit MainWindow(Settings& settings, OrganizerCore& organizerCore,
                      PluginContainer& pluginContainer, QWidget* parent = nullptr);
  ~MainWindow() override;

  void processUpdates();

  QMainWindow* mainWindow() override;

  void showNotification(const QString& title, const QString& message,
                        QSystemTrayIcon::MessageIcon icon =
                            QSystemTrayIcon::MessageIcon::Information) override;

  bool addProfile();
  void updateBSAList(const QStringList& defaultArchives,
                     const QStringList& activeArchives) override;

  void saveArchiveList();

  void installTranslator(const QString& name) override;

  void displayModInformation(ModInfo::Ptr modInfo, unsigned int modIndex,
                             ModInfoTabIDs tabID) override;

  bool canExit();
  void onBeforeClose();

  bool closeWindow() override;
  void setWindowEnabled(bool enabled) override;

  MOBase::DelayedFileWriterBase& archivesWriter() override
  {
    return m_ArchiveListWriter;
  }

public slots:
  void refresherProgress(const DirectoryRefreshProgress* p);

signals:
  // emitted after the information dialog has been closed, used by tutorials
  //
  void modInfoDisplayed();

  /**
   * @brief emitted when the selected style changes
   */
  void styleChanged(const QString& styleFile);

  void checkForProblemsDone();

protected:
  void showEvent(QShowEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  void closeEvent(QCloseEvent* event) override;
  bool eventFilter(QObject* obj, QEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dropEvent(QDropEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;

private slots:
  void on_actionChange_Game_triggered();

private:
  // update data tab and schedule a problem check after a directory
  // structure update
  //
  void onDirectoryStructureChanged();

  void cleanup();

  void setupMenus();
  void updateHeaderControls();
  void setupWorkspace();
  void translateWorkspace();
  void updateProfileControl();
  void showWorkspaceDestination(const QString& destination);
  void restoreWorkspaceSettings();
  void saveWorkspaceSettings();
  void applyWorkspaceTheme();
  void queueSelectedModUpdate();
  void updateSelectedMod();
  void openSelectedMod(bool conflicts);
  SetupCheckInputs setupCheckInputs();
  void showSetupChecks();
  void openSettings(const QString& section = {});
  void setupActionMenu(QAction* a);
  void createHelpMenu();

  void updatePinnedExecutables();

  void registerModPage(MOBase::IPluginModPage* modPage) override;
  bool registerNexusPage(const QString& gameName);
  void registerPluginTool(MOBase::IPluginTool* tool, QString name = QString(),
                          QMenu* menu = nullptr);

  void updateToolMenu();
  void updateModPageMenu();
  void updateViewMenu();

  QMenu* createPopupMenu() override;
  void activateSelectedProfile();

  bool refreshProfiles(bool selectProfile = true, QString newProfile = QString());
  void refreshExecutablesList();

  bool modifyExecutablesDialog(int selection);

  // remove invalid category-references from mods
  void fixCategories();

  bool extractProgress(QProgressDialog& extractProgress, int percentage,
                       std::string fileName);

  void setCategoryListVisible(bool visible);

  static bool errorReported(QString& logFile);

  static void setupNetworkProxy(bool activate);
  void activateProxy(bool activate);

  bool createBackup(const QString& filePath, const QDateTime& time);
  QString queryRestore(const QString& filePath);

  QMenu* openFolderMenu();

  void dropLocalFile(const QUrl& url, const QString& outputDir, bool move);

  void toggleUpdateAction();
  void clearFluorineUpdateAvailable();
  void showFluorineUpdatePrompt(const FluorineUpdater::ReleaseInfo& info);

  // update info
  struct NxmUpdateInfoData
  {
    QString game;
    std::set<ModInfo::Ptr> finalMods;
  };
  void finishUpdateInfo(const NxmUpdateInfoData& data);

private:
  static const char* PATTERN_BACKUP_GLOB;
  static const char* PATTERN_BACKUP_REGEX;
  static const char* PATTERN_BACKUP_DATE;

private:
  Ui::MainWindow* ui;
  WorkspaceFrame* m_WorkspaceFrame = nullptr;
  WorkspaceLayout* m_WorkspaceLayout = nullptr;
  SelectedModPanel* m_SelectedModPanel = nullptr;
  LibraryView* m_LibraryView = nullptr;
  QStackedWidget* m_WorkspacePages = nullptr;
  QTimer m_SelectedModTimer;
  bool m_WorkspaceReady = false;
  bool m_CompactWorkspace = false;

  bool m_WasVisible{false};
  bool m_FirstPaint{true};
  bool m_PluginListCustom{false};

  MOBase::TutorialControl m_Tutorial;

  std::unique_ptr<DataTab> m_DataTab;
  std::unique_ptr<DownloadsTab> m_DownloadsTab;
  std::unique_ptr<SavesTab> m_SavesTab;

  int m_OldProfileIndex{-1};

  std::vector<QString>
      m_ModNameList;  // the mod-list to go with the directory structure

  QStringList m_DefaultArchives;


  QAction* m_ContextAction;

  CategoryFactory& m_CategoryFactory;

  QTimer m_CheckBSATimer;
  QTimer m_SaveMetaTimer;
  QTimer m_UpdateProblemsTimer;

  QFuture<void> m_MetaSave;

  QTime m_StartTime;

  // Retain the actionable release so the status-bar update action can reopen
  // the same install prompt without performing another network request.
  std::optional<FluorineUpdater::ReleaseInfo> m_FluorineUpdate;

  OrganizerCore& m_OrganizerCore;
  PluginContainer& m_PluginContainer;

  QString m_CurrentLanguage;
  std::vector<QTranslator*> m_Translators;

#ifdef MO2_WEBENGINE
  std::unique_ptr<BrowserDialog> m_IntegratedBrowser;
#endif

  MOBase::DelayedFileWriter m_ArchiveListWriter;

  QAction* m_LinkRunMenu{nullptr};
  QAction* m_LinkDesktop{nullptr};
  QAction* m_LinkStartMenu{nullptr};
  QAction* m_SteamCopy{nullptr};
  QAction* m_SteamSet{nullptr};

  SystemTrayManager* m_SystemTrayManager{nullptr};

  // icon set by the stylesheet, used to remember its original appearance
  // when painting the count
  QIcon m_originalNotificationIcon;

  std::size_t m_NumberOfProblems;
  ProblemCheckRunner m_ProblemCheckRunner;
  bool m_ProblemChecksAccepting{true};
  bool m_ProblemsDialogActive{false};
  bool m_ProblemsCheckDirty{false};

  QVersionNumber m_LastVersion;

  Executable* getSelectedExecutable();

private slots:
  void updateWindowTitle(const APIUserAccount& user);
  void showMessage(const QString& message);
  static void showError(const QString& message);

  // main window actions
  static void helpTriggered();
  void issueTriggered();
  static void wikiTriggered();
  void gameSupportTriggered();
  static void discordTriggered();
  void tutorialTriggered();
  void extractBSATriggered(QTreeWidgetItem* item);

  void refreshProfile_activated();

  void linkRunMenu();
  void linkDesktop();
  void linkMenu();
  void copySteamLaunchOption();
  void setSteamLaunchOption();

  // The Steam launch-option line for `exe` in the current instance.
  QString steamLaunchOptionLineFor(const Executable& exe) const;

  void languageChange(const QString& newLanguage);

  void windowTutorialFinished(const QString& windowName);

  BSA::EErrorCode extractBSA(BSA::Archive& archive, BSA::Folder::Ptr folder,
                             const QString& destination,
                             QProgressDialog& extractProgress);

  // nexus related
  void updateAvailable();


  void motdReceived(const QString& motd);

  void originModified(int originID);

  void modInstalled(const QString& modName);

  void importCategories(bool);

  void refreshNexusCategories(CategoriesDialog* dialog);
  void categoriesSaved();

  // update info
  void nxmUpdateInfoAvailable(QString gameName, QVariant userData, QVariant resultData,
                              int requestID);

  void nxmEndorsementsAvailable(QVariant userData, QVariant resultData, int);
  void nxmUpdatesAvailable(QString gameName, int modID, QVariant userData,
                           QVariant resultData, int requestID);
  void nxmModInfoAvailable(QString gameName, int modID, QVariant userData,
                           QVariant resultData, int requestID);
  void nxmEndorsementToggled(QString, int, QVariant, QVariant resultData, int);
  void nxmTrackedModsAvailable(QVariant userData, QVariant resultData, int);
  void nxmDownloadURLs(QString, int modID, int fileID, QVariant userData,
                       QVariant resultData, int requestID);
  static void nxmGameInfoAvailable(QString gameName, QVariant, QVariant resultData, int);
  void nxmRequestFailed(QString gameName, int modID, int fileID, QVariant userData,
                        int requestID, int errorCode, const QString& errorString);

  void modRenamed(const QString& oldName, const QString& newName);
  void modRemoved(const QString& fileName);

  void hookUpWindowTutorials();
  static bool shouldStartTutorial() ;

  static void openInstanceFolder();
  static void openInstallFolder();
  static void openPluginsFolder();
  static void openStylesheetsFolder();
  void openDownloadsFolder();
  void openModsFolder();
  void openProfileFolder();
  void openIniFolder();
  void openGameFolder();
  void openMyGamesFolder();
  void startExeAction();

  void checkBSAList();

  // Only visually update the problems icon.
  void updateProblemsButton();

  // Queue a problem check to allow collapsing of multiple requests in short amount of
  // time.
  void scheduleCheckForProblems();

  // Starts an incremental owning-thread scan, one diagnosis plugin per event turn.
  void startProblemCheck();
  void showProblemsDialog();

  void saveModMetas();

  void updateStyle(const QString& style);

  void resizeLists(bool pluginListCustom);

  void fileMoved(const QString& filePath, const QString& oldOriginName,
                 const QString& newOriginName);

  /**
   * @brief allow columns in mod list and plugin list to be resized
   */
  void allowListResize();


  void about();

  void resetActionIcons();
  void resetButtonIcons();

private slots:  // ui slots
  // actions
  void on_actionAdd_Profile_triggered();
  void on_actionInstallMod_triggered();
  void on_action_Refresh_triggered();
  void on_actionModify_Executables_triggered();
  void on_actionNotifications_triggered();
  void on_actionSettings_triggered();
  void on_actionUpdate_triggered();
  static void on_actionExit_triggered();
  void on_actionStatusBarToggle_triggered();
  void on_actionViewLog_triggered();
  void on_actionBalancedListLayout_triggered();

  void on_centralWidget_customContextMenuRequested(const QPoint& pos);
  void on_bsaList_customContextMenuRequested(const QPoint& pos);
  void on_executablesListBox_currentIndexChanged(int index);
  void on_profileBox_currentIndexChanged(int index);
  void on_startButton_clicked();
  void on_tabWidget_currentChanged(int index);

  void on_actionShowFilters_toggled(bool checked);
  void updateLaunchMenu();
  void on_bsaList_itemChanged(QTreeWidgetItem* item, int column);

  void on_sortButton_clicked();
  void on_saveButton_clicked();
  void on_restoreButton_clicked();
  void on_actionRestoreModList_triggered();
  void on_actionBackupModList_triggered();
  void on_managedArchiveLabel_linkHovered(const QString& link);

  void onPluginRegistrationChanged();

  void storeSettings();
  void readSettings();

  void setupModList();
};

#endif  // MAINWINDOW_H
