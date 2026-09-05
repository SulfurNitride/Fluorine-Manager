#pragma once

#include "clf3processcontroller.h"

#include <QDialog>
#include <QHash>
#include <QIcon>
#include <QJsonObject>
#include <QProcess>
#include <QQueue>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <optional>

class QLabel;
class QLineEdit;
class QListWidget;
class QComboBox;
class QNetworkAccessManager;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QCheckBox;
class QListWidgetItem;
class WabbajackPostInstall;

class Clf3InstallerDialog : public QDialog
{
  Q_OBJECT

public:
  explicit Clf3InstallerDialog(QWidget* parent = nullptr);
  ~Clf3InstallerDialog() override;

  QString createdInstanceDir() const { return m_createdInstanceDir; }
  bool shouldSwitchToInstance() const;
  void done(int result) override;

private:
  struct NexusRequest
  {
    QString consumer;
    QString requestId;
    QString archiveName;
    QString domain;
    int modId{0};
    int fileId{0};
    qint64 expectedSize{0};
  };

  struct ActiveCard
  {
    QListWidgetItem* item{};
    QLabel* image{};
    QLabel* stage{};
    QProgressBar* progress{};
    QLabel* speed{};
  };

  struct ActiveImageRequest
  {
    QString key;
    QString name;
    QUrl url;
  };

  Clf3ProcessController m_controller;
  QProcess m_galleryProcess;
  QByteArray m_galleryOutput;
  QVector<QJsonObject> m_gallery;
  QSet<QString> m_installedGames;
  QStringList m_allMods;
  QHash<QString, QSet<QString>> m_modsPerList;
  QHash<QString, QIcon> m_imageIcons;
  QSet<QString> m_imageQueued;
  QQueue<QPair<QString, QUrl>> m_imageQueue;
  QNetworkAccessManager* m_imageNetwork{};
  WabbajackPostInstall* m_postInstall{};
  int m_activeImageRequests{0};
  int m_activePipelineImageRequests{0};
  QQueue<ActiveImageRequest> m_activeImageQueue;
  QQueue<NexusRequest> m_nexusQueue;
  std::optional<NexusRequest> m_currentNexus;
  QString m_machineName;
  QString m_gameId;
  QString m_setupJobId;
  QString m_pendingSaveError;
  QString m_createdInstanceDir;
  QJsonObject m_installStats;
  QDialog* m_browserDialog{};
  bool m_postInstallRunning{false};
  bool m_stopping{false};
  std::optional<int> m_deferredClose;
  QHash<QString, QListWidgetItem*> m_manualRequests;

  QStackedWidget* m_pages{};
  QLineEdit* m_search{};
  QComboBox* m_gameFilter{};
  QComboBox* m_sortOrder{};
  QCheckBox* m_installedOnly{};
  QCheckBox* m_showNsfw{};
  QCheckBox* m_showUnavailable{};
  QCheckBox* m_officialOnly{};
  QLineEdit* m_includeMods{};
  QLineEdit* m_excludeMods{};
  QLabel* m_resultCount{};
  QListWidget* m_galleryList{};
  QLabel* m_details{};
  QLineEdit* m_source{};
  QLineEdit* m_instanceName{};
  QLineEdit* m_output{};
  QLineEdit* m_downloads{};
  QLineEdit* m_game{};
  QComboBox* m_store{};
  QLabel* m_preflightSummary{};
  QLabel* m_status{};
  QLabel* m_engineVersion{};
  QProgressBar* m_overall{};
  QLabel* m_pipelineSummary{};
  QListWidget* m_activeDownloads{};
  QHash<QString, ActiveCard> m_activeItems;
  QHash<QString, double> m_activeSpeeds;
  QHash<QString, QString> m_activeImageUrls;
  QHash<QString, QString> m_activeDisplayNames;
  QHash<QString, QString> m_activeSubtitles;
  QPlainTextEdit* m_log{};
  QPushButton* m_cancel{};
  QPushButton* m_close{};
  QPushButton* m_retrySetup{};
  QListWidget* m_manualDownloads{};
  QCheckBox* m_switchInstance{};

  void buildUi();
  void loadGallery(bool refresh = false);
  void populateGallery();
  void updateGameFilter();
  void queueThumbnail(const QJsonObject& item);
  void pumpThumbnailQueue();
  void startActiveItem(const QString& itemId, const QString& name,
                       const QString& displayName, const QString& subtitle,
                       const QString& stage, const QString& imageUrl,
                       qint64 total, const QString& unit);
  void updateActiveItem(const QString& itemId, qint64 completed,
                        qint64 total, double speed, const QString& unit);
  void setActiveItemMessage(const QString& itemId, const QString& message);
  void finishActiveItem(const QString& itemId);
  void failActiveItem(const QString& itemId, const QString& message);
  void loadActiveImage(const QString& itemId, const QString& name,
                       const QString& imageUrl);
  void pumpActiveImageQueue();
  void updatePipelineSummary();
  void detectSelectedGamePath();
  void selectGalleryItem();
  void chooseSource();
  void chooseDirectory(QLineEdit* target);
  void showConfiguration();
  void startInstall();
  bool savePendingJob(const QString& stage = QStringLiteral("install"));
  bool checkInstallation();
  void updatePreflightSummary();
  QJsonObject selectedDownloadMetadata() const;
  void cancelInstall();
  void closeWhenIdle();
  void clearManualRequests();
  void exportLog();
  void clearPendingJob();
  void offerResume();
  void finishInstall(const QJsonObject& stats);
  void completePostInstall(const QStringList& adjustments,
                           const QStringList& warnings);
  void failPostInstall(const QString& error);

  void queueNexus(const QString& requestId, const QString& archiveName,
                  const QString& domain, int modId, int fileId,
                  qint64 expectedSize);
  void queuePostInstallNexus(const QString& requestId,
                             const QString& artifactName,
                             const QString& domain, int modId, int fileId,
                             qint64 expectedSize);
  void beginNextNexus();
  void showNexusBrowser(const NexusRequest& request);
  void resolveNexus(const NexusRequest& request, const QString& nxmUrl = {});
  void nexusLinkAccepted(const QString& consumer, const QString& requestId,
                         const QString& url);
  void requestManualFile(const QString& requestId, const QString& archiveName,
                         const QString& url, const QString& prompt,
                         qint64 expectedSize, const QString& expectedHash);
};
