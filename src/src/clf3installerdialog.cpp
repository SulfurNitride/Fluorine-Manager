#include "clf3installerdialog.h"
#include "clf3installutils.h"

#include "curatedguidenxmbroker.h"
#include "gamedetection.h"
#include "instancemanager.h"
#include "knowngames.h"
#include "nexusinterface.h"
#include "nxmaccessmanager.h"
#include "wabbajackpostinstall.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QDateTime>
#include <QUuid>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <nxmurl.h>
#include <algorithm>
#include <utility>

#ifdef MO2_WEBENGINE
#include <QWebEngineCookieStore>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>
#include <functional>

namespace
{
class NexusAuthorizationPage : public QWebEnginePage
{
public:
  NexusAuthorizationPage(QWebEngineProfile* profile,
                         std::function<void(const QUrl&)> accepted,
                         QObject* parent)
      : QWebEnginePage(profile, parent), m_accepted(std::move(accepted))
  {}

protected:
  bool acceptNavigationRequest(const QUrl& url, NavigationType type,
                               bool mainFrame) override
  {
    if (url.scheme().compare(QStringLiteral("nxm"), Qt::CaseInsensitive) == 0) {
      m_accepted(url);
      return false;
    }
    return QWebEnginePage::acceptNavigationRequest(url, type, mainFrame);
  }

private:
  std::function<void(const QUrl&)> m_accepted;
};

QWebEngineProfile* nexusProfile()
{
  static QWebEngineProfile* profile = [] {
    auto* result = new QWebEngineProfile(QStringLiteral("FluorineNexus"), qApp);
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                         + QStringLiteral("/nexus-web-profile");
    result->setPersistentStoragePath(root);
    result->setCachePath(root + QStringLiteral("/cache"));
    result->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
    return result;
  }();
  return profile;
}
}
#endif

namespace
{
QString formatBytes(qint64 bytes)
{
  if (bytes <= 0) return QObject::tr("Unknown size");
  const double gib = double(bytes) / (1024.0 * 1024.0 * 1024.0);
  if (gib >= 1.0) return QObject::tr("%1 GiB").arg(gib, 0, 'f', 1);
  return QObject::tr("%1 MiB").arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
}

QString displayGameName(const QString& game)
{
  static const QHash<QString, QString> names{
      {QStringLiteral("skyrimspecialedition"), QStringLiteral("Skyrim Special Edition")},
      {QStringLiteral("skyrimvr"), QStringLiteral("Skyrim VR")},
      {QStringLiteral("fallout4"), QStringLiteral("Fallout 4")},
      {QStringLiteral("fallout4vr"), QStringLiteral("Fallout 4 VR")},
      {QStringLiteral("falloutnewvegas"), QStringLiteral("Fallout: New Vegas")},
      {QStringLiteral("oblivion"), QStringLiteral("Oblivion")},
      {QStringLiteral("oblivionremastered"), QStringLiteral("Oblivion Remastered")},
      {QStringLiteral("morrowind"), QStringLiteral("Morrowind")},
      {QStringLiteral("cyberpunk2077"), QStringLiteral("Cyberpunk 2077")},
      {QStringLiteral("stardewvalley"), QStringLiteral("Stardew Valley")},
  };
  return names.value(game.toLower(), game);
}

QString knownGameNameForWabbajack(const QString& gameId)
{
  static const QHash<QString, QString> names{
      {QStringLiteral("falloutnewvegas"), QStringLiteral("Fallout New Vegas")},
      {QStringLiteral("fallout3"), QStringLiteral("Fallout 3")},
      {QStringLiteral("fallout4"), QStringLiteral("Fallout 4")},
      {QStringLiteral("fallout4vr"), QStringLiteral("Fallout 4 VR")},
      {QStringLiteral("oblivion"), QStringLiteral("Oblivion")},
      {QStringLiteral("morrowind"), QStringLiteral("Morrowind")},
      {QStringLiteral("skyrim"), QStringLiteral("Skyrim")},
      {QStringLiteral("skyrimspecialedition"),
       QStringLiteral("Skyrim Special Edition")},
      {QStringLiteral("skyrimvr"), QStringLiteral("Skyrim VR")},
      {QStringLiteral("starfield"), QStringLiteral("Starfield")},
      {QStringLiteral("cyberpunk2077"), QStringLiteral("Cyberpunk 2077")},
  };
  return names.value(gameId.toLower());
}

QString launcherStore(const QString& launcher)
{
  if (launcher.contains(QStringLiteral("GOG"), Qt::CaseInsensitive))
    return QStringLiteral("GOG");
  if (launcher.contains(QStringLiteral("Epic"), Qt::CaseInsensitive))
    return QStringLiteral("Epic Games");
  if (launcher.contains(QStringLiteral("Steam"), Qt::CaseInsensitive))
    return QStringLiteral("Steam");
  return {};
}

const KnownGame* knownGameForDetected(const DetectedGame& game)
{
  if (game.launcher.contains(QStringLiteral("GOG"), Qt::CaseInsensitive))
    if (const auto* known = findKnownGameByGogId(game.app_id)) return known;
  if (game.launcher.contains(QStringLiteral("Epic"), Qt::CaseInsensitive))
    if (const auto* known = findKnownGameByEpicId(game.app_id)) return known;
  if (game.launcher.contains(QStringLiteral("Steam"), Qt::CaseInsensitive))
    if (const auto* known = findKnownGameBySteamId(game.app_id)) return known;
  return findKnownGameByTitle(game.name);
}

QIcon galleryPlaceholder()
{
  QPixmap pixmap(240, 135);
  pixmap.fill(QColor(35, 39, 42));
  QPainter painter(&pixmap);
  painter.setPen(QColor(105, 111, 116));
  QFont font = painter.font();
  font.setBold(true);
  font.setPointSize(24);
  painter.setFont(font);
  painter.drawText(pixmap.rect(), Qt::AlignCenter, QStringLiteral("W"));
  return QIcon(pixmap);
}

QPixmap activeItemPlaceholder()
{
  QPixmap pixmap(128, 128);
  pixmap.fill(QColor(35, 39, 42));
  QPainter painter(&pixmap);
  painter.setPen(QColor(105, 111, 116));
  QFont font = painter.font();
  font.setBold(true);
  font.setPointSize(20);
  painter.setFont(font);
  painter.drawText(pixmap.rect(), Qt::AlignCenter, QStringLiteral("MOD"));
  return pixmap;
}

QString galleryImageKey(const QJsonObject& item)
{
  const QString machineName = item.value("machine_name").toString();
  return machineName.isEmpty()
             ? item.value("links").toObject().value("image").toString()
             : machineName;
}

bool isGalleryUnavailable(const QJsonObject& item)
{
  return item.value("force_down").toBool()
         || item.value("links").toObject().value("download").toString().isEmpty();
}

QSet<QString> requestedMods(const QString& text)
{
  QSet<QString> result;
  for (const QString& part : text.split(';', Qt::SkipEmptyParts)) {
    const QString name = part.trimmed().toLower();
    if (!name.isEmpty()) result.insert(name);
  }
  return result;
}

QString imageCacheDirectory()
{
  return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
         + QStringLiteral("/wabbajack-images-v1");
}

QString imageCachePath(const QUrl& url)
{
  const QByteArray digest =
      QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Sha256).toHex();
  return imageCacheDirectory() + QLatin1Char('/') + QString::fromLatin1(digest)
         + QStringLiteral(".image");
}

QPixmap cachedImage(const QUrl& url)
{
  QPixmap image;
  if (url.isValid()) image.load(imageCachePath(url));
  return image;
}

void storeCachedImage(const QUrl& url, const QByteArray& data)
{
  if (!url.isValid() || data.isEmpty()) return;
  QSaveFile file(imageCachePath(url));
  if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size()) file.commit();
}

QPixmap croppedImage(const QPixmap& source, int width, int height)
{
  const QPixmap scaled = source.scaled(width, height, Qt::KeepAspectRatioByExpanding,
                                       Qt::SmoothTransformation);
  const int x = qMax(0, (scaled.width() - width) / 2);
  const int y = qMax(0, (scaled.height() - height) / 2);
  return scaled.copy(x, y, width, height);
}

void pruneImageCache()
{
  constexpr qint64 maximumBytes = 256LL * 1024LL * 1024LL;
  QDir directory(imageCacheDirectory());
  if (!directory.exists()) directory.mkpath(QStringLiteral("."));
  const QFileInfoList files = directory.entryInfoList(
      {QStringLiteral("*.image")}, QDir::Files, QDir::Time | QDir::Reversed);
  qint64 total = 0;
  for (const QFileInfo& file : files) total += file.size();
  for (const QFileInfo& file : files) {
    if (total <= maximumBytes) break;
    if (QFile::remove(file.absoluteFilePath())) total -= file.size();
  }
}
}

Clf3InstallerDialog::Clf3InstallerDialog(QWidget* parent)
    : QDialog(parent)
{
  m_imageNetwork = new QNetworkAccessManager(this);
  m_postInstall = new WabbajackPostInstall(this);
  pruneImageCache();
  buildUi();

  connect(&m_controller, &Clf3ProcessController::engineReady, this,
          [this](const QString& version) {
            m_engineVersion->setText(tr("CLF3 %1 · protocol %2")
                                         .arg(version)
                                         .arg(Clf3ProcessController::ProtocolVersion));
          });
  connect(&m_controller, &Clf3ProcessController::phaseChanged, this,
          [this](const QString& phase) {
            if (!m_stopping) m_status->setText(phase);
            m_log->appendPlainText(tr("Phase: %1").arg(phase));
          });
  connect(&m_controller, &Clf3ProcessController::statusChanged, this,
          [this](const QString& status) { if (!m_stopping) m_status->setText(status); });
  connect(&m_controller, &Clf3ProcessController::itemMetadata, this,
          [this](const QString& name, const QString& displayName,
                 const QString& subtitle, const QString& imageUrl) {
            if (!displayName.isEmpty()) m_activeDisplayNames.insert(name, displayName);
            if (!subtitle.isEmpty()) m_activeSubtitles.insert(name, subtitle);
            if (!imageUrl.isEmpty()) m_activeImageUrls.insert(name, imageUrl);
          });
  connect(&m_controller, &Clf3ProcessController::itemStarted, this,
          &Clf3InstallerDialog::startActiveItem);
  connect(&m_controller, &Clf3ProcessController::itemProgress, this,
          &Clf3InstallerDialog::updateActiveItem);
  connect(&m_controller, &Clf3ProcessController::itemMessage, this,
          &Clf3InstallerDialog::setActiveItemMessage);
  connect(&m_controller, &Clf3ProcessController::itemCompleted, this,
          &Clf3InstallerDialog::finishActiveItem);
  connect(&m_controller, &Clf3ProcessController::itemFailed, this,
          &Clf3InstallerDialog::failActiveItem);
  connect(&m_controller, &Clf3ProcessController::overallProgress, this,
          [this](int done, int total) {
            m_overall->setMaximum(qMax(1, total));
            m_overall->setValue(done);
            m_overall->setFormat(tr("%1 of %2 archives").arg(done).arg(total));
          });
  connect(&m_controller, &Clf3ProcessController::logLine, m_log,
          &QPlainTextEdit::appendPlainText);
  connect(&m_controller, &Clf3ProcessController::nexusAuthorizationRequired,
          this, &Clf3InstallerDialog::queueNexus);
  connect(&m_controller, &Clf3ProcessController::manualDownloadRequired,
          this, &Clf3InstallerDialog::requestManualFile);
  connect(&m_controller, &Clf3ProcessController::completed, this,
          &Clf3InstallerDialog::finishInstall);
  connect(m_postInstall, &WabbajackPostInstall::statusChanged, m_status,
          &QLabel::setText);
  connect(m_postInstall, &WabbajackPostInstall::logLine, m_log,
          &QPlainTextEdit::appendPlainText);
  connect(m_postInstall, &WabbajackPostInstall::stepStarted, this,
          [this](const QString& id, const QString& title) {
            startActiveItem(QStringLiteral("post:") + id, title, title, {},
                            tr("Fluorine setup"), {}, 0, QStringLiteral("items"));
          });
  connect(m_postInstall, &WabbajackPostInstall::stepFinished, this,
          [this](const QString& id) {
            finishActiveItem(QStringLiteral("post:") + id);
          });
  connect(m_postInstall, &WabbajackPostInstall::toolProgress, this,
          [this](const QString& id, qint64 completed, qint64 total) {
            const QString step =
                id == QStringLiteral("oblivion-4gb-patcher-linux")
                    ? QStringLiteral("post:oblivion-4gb")
                    : QStringLiteral("post:vnv-patcher");
            updateActiveItem(step, completed, total, 0.0,
                             QStringLiteral("bytes"));
          });
  connect(m_postInstall, &WabbajackPostInstall::nexusAuthorizationRequired,
          this, &Clf3InstallerDialog::queuePostInstallNexus);
  connect(m_postInstall, &WabbajackPostInstall::completed, this,
          &Clf3InstallerDialog::completePostInstall);
  connect(m_postInstall, &WabbajackPostInstall::failed, this,
          &Clf3InstallerDialog::failPostInstall);
  connect(&m_controller, &Clf3ProcessController::failed, this,
          [this](const QString& error) {
            CuratedGuideNxmBroker::instance().clearConsumer(QStringLiteral("clf3"));
            CuratedGuideNxmBroker::instance().clearConsumer(
                QStringLiteral("wabbajack-postinstall"));
            m_nexusQueue.clear();
            m_currentNexus.reset();
            if (m_browserDialog) m_browserDialog->hide();
            m_status->setText(tr("Stopped: %1").arg(error));
            m_log->appendPlainText(tr("FAILED: %1").arg(error));
            m_log->setVisible(true);
            m_cancel->setEnabled(false);
            m_close->setEnabled(true);
            m_stopping = false;
            clearManualRequests();
            if (!m_deferredClose)
              QMessageBox::critical(this, tr("Modlist installation stopped"), error);
            closeWhenIdle();
          });
  connect(&m_controller, &Clf3ProcessController::cancelled, this, [this] {
    CuratedGuideNxmBroker::instance().clearConsumer(QStringLiteral("clf3"));
    CuratedGuideNxmBroker::instance().clearConsumer(
        QStringLiteral("wabbajack-postinstall"));
    m_nexusQueue.clear();
    m_currentNexus.reset();
    if (m_browserDialog) m_browserDialog->hide();
    m_stopping = false;
    clearManualRequests();
    m_status->setText(tr("Cancelled. The saved installation can be continued later."));
    m_cancel->setEnabled(false);
    m_close->setEnabled(true);
    closeWhenIdle();
  });
  connect(&CuratedGuideNxmBroker::instance(),
          &CuratedGuideNxmBroker::acceptedForConsumer, this,
          &Clf3InstallerDialog::nexusLinkAccepted);
  connect(&CuratedGuideNxmBroker::instance(),
          &CuratedGuideNxmBroker::rejectedForConsumer, this,
          [this](const QString& consumer, const QString& requestId,
                 const QString& reason) {
            if (!m_currentNexus || m_currentNexus->consumer != consumer
                || m_currentNexus->requestId != requestId)
              return;
            if (consumer == QStringLiteral("clf3"))
              m_controller.rejectRequest(requestId, reason);
            else if (consumer == QStringLiteral("wabbajack-postinstall"))
              m_postInstall->rejectNexusAuthorization(requestId, reason);
            if (m_browserDialog) m_browserDialog->hide();
            QMessageBox::warning(this, tr("Nexus account mismatch"), reason);
            m_currentNexus.reset();
            QTimer::singleShot(0, this, &Clf3InstallerDialog::beginNextNexus);
          });

  loadGallery();
  QTimer::singleShot(0, this, &Clf3InstallerDialog::offerResume);
}

Clf3InstallerDialog::~Clf3InstallerDialog()
{
  CuratedGuideNxmBroker::instance().clearConsumer(QStringLiteral("clf3"));
  CuratedGuideNxmBroker::instance().clearConsumer(
      QStringLiteral("wabbajack-postinstall"));
  m_postInstall->cancel();
  if (m_controller.isRunning()) m_controller.cancel();
}

bool Clf3InstallerDialog::shouldSwitchToInstance() const
{
  return m_switchInstance && m_switchInstance->isChecked();
}

void Clf3InstallerDialog::buildUi()
{
  setWindowTitle(tr("Install Wabbajack Modlist"));
  resize(1280, 820);
  auto* outer = new QVBoxLayout(this);
  m_pages     = new QStackedWidget(this);
  outer->addWidget(m_pages, 1);

  auto* browsePage   = new QWidget;
  auto* browseLayout = new QVBoxLayout(browsePage);
  auto* heading      = new QLabel(tr("Choose a Wabbajack modlist"));
  QFont headingFont  = heading->font();
  headingFont.setPointSize(headingFont.pointSize() + 5);
  headingFont.setBold(true);
  heading->setFont(headingFont);
  browseLayout->addWidget(heading);
  auto* searchRow = new QHBoxLayout;
  m_search        = new QLineEdit;
  m_search->setPlaceholderText(tr("Search title, author, or game…"));
  auto* refresh = new QPushButton(tr("Refresh"));
  searchRow->addWidget(m_search, 1);
  searchRow->addWidget(refresh);
  browseLayout->addLayout(searchRow);

  auto* filterRow = new QHBoxLayout;
  m_gameFilter    = new QComboBox;
  m_sortOrder     = new QComboBox;
  m_installedOnly = new QCheckBox(tr("Installed games only"));
  m_showNsfw      = new QCheckBox(tr("Show NSFW"));
  m_showUnavailable = new QCheckBox(tr("Show unavailable"));
  m_officialOnly  = new QCheckBox(tr("Official only"));
  m_resultCount   = new QLabel;
  m_gameFilter->addItem(tr("All games"), QString());
  m_sortOrder->addItem(tr("Gallery order"), QStringLiteral("featured"));
  m_sortOrder->addItem(tr("Title A–Z"), QStringLiteral("title-asc"));
  m_sortOrder->addItem(tr("Title Z–A"), QStringLiteral("title-desc"));
  m_sortOrder->addItem(tr("Smallest download"), QStringLiteral("download-asc"));
  m_sortOrder->addItem(tr("Largest download"), QStringLiteral("download-desc"));
  m_sortOrder->addItem(tr("Smallest installation"), QStringLiteral("installed-asc"));
  m_sortOrder->addItem(tr("Largest installation"), QStringLiteral("installed-desc"));
  m_sortOrder->addItem(tr("Fewest archives"), QStringLiteral("archives-asc"));
  m_sortOrder->addItem(tr("Most archives"), QStringLiteral("archives-desc"));
  filterRow->addWidget(new QLabel(tr("Game:")));
  filterRow->addWidget(m_gameFilter);
  filterRow->addWidget(new QLabel(tr("Sort:")));
  filterRow->addWidget(m_sortOrder);
  filterRow->addWidget(m_installedOnly);
  filterRow->addWidget(m_showNsfw);
  filterRow->addWidget(m_showUnavailable);
  filterRow->addWidget(m_officialOnly);
  filterRow->addStretch();
  filterRow->addWidget(m_resultCount);
  browseLayout->addLayout(filterRow);

  auto* modFilterRow = new QHBoxLayout;
  m_includeMods      = new QLineEdit;
  m_excludeMods      = new QLineEdit;
  m_includeMods->setPlaceholderText(tr("Exact mod names; separate several with ;"));
  m_excludeMods->setPlaceholderText(tr("Exact mod names; separate several with ;"));
  m_includeMods->setToolTip(
      tr("Only show lists containing every named Mod Organizer mod."));
  m_excludeMods->setToolTip(
      tr("Hide lists containing any named Mod Organizer mod."));
  m_includeMods->setEnabled(false);
  m_excludeMods->setEnabled(false);
  modFilterRow->addWidget(new QLabel(tr("Must include:")));
  modFilterRow->addWidget(m_includeMods, 1);
  modFilterRow->addWidget(new QLabel(tr("Must not include:")));
  modFilterRow->addWidget(m_excludeMods, 1);
  browseLayout->addLayout(modFilterRow);

  auto* galleryRow = new QHBoxLayout;
  m_galleryList    = new QListWidget;
  m_galleryList->setViewMode(QListView::IconMode);
  m_galleryList->setMovement(QListView::Static);
  m_galleryList->setResizeMode(QListView::Adjust);
  m_galleryList->setIconSize(QSize(240, 135));
  m_galleryList->setGridSize(QSize(270, 205));
  m_galleryList->setSpacing(7);
  m_galleryList->setWordWrap(true);
  m_galleryList->setUniformItemSizes(true);
  m_details        = new QLabel(tr("Loading the Wabbajack gallery…"));
  m_details->setWordWrap(true);
  m_details->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  m_details->setTextInteractionFlags(Qt::TextBrowserInteraction);
  m_details->setOpenExternalLinks(true);
  m_details->setMinimumWidth(300);
  m_details->setMaximumWidth(430);
  galleryRow->addWidget(m_galleryList, 1);
  galleryRow->addWidget(m_details);
  browseLayout->addLayout(galleryRow, 1);
  auto* sourceRow = new QHBoxLayout;
  m_source        = new QLineEdit;
  m_source->setPlaceholderText(tr("Gallery URL or local .wabbajack file"));
  auto* browse = new QPushButton(tr("Browse…"));
  sourceRow->addWidget(m_source, 1);
  sourceRow->addWidget(browse);
  browseLayout->addLayout(sourceRow);
  auto* configure = new QPushButton(tr("Configure Installation →"));
  browseLayout->addWidget(configure, 0, Qt::AlignRight);
  m_pages->addWidget(browsePage);

  auto* configPage   = new QWidget;
  auto* configLayout = new QVBoxLayout(configPage);
  configLayout->addWidget(new QLabel(tr("Installation paths")));
  auto* form = new QFormLayout;
  m_instanceName = new QLineEdit;
  form->addRow(tr("Instance name:"), m_instanceName);
  auto pathRow = [this, form](const QString& label, QLineEdit*& edit) {
    auto* row = new QWidget;
    auto* box = new QHBoxLayout(row);
    box->setContentsMargins(0, 0, 0, 0);
    edit = new QLineEdit;
    auto* button = new QPushButton(tr("Browse…"));
    box->addWidget(edit, 1);
    box->addWidget(button);
    connect(button, &QPushButton::clicked, this,
            [this, edit] { chooseDirectory(edit); });
    form->addRow(label, row);
  };
  pathRow(tr("Instance folder:"), m_output);
  pathRow(tr("Download cache:"), m_downloads);
  pathRow(tr("Game folder:"), m_game);
  m_game->setPlaceholderText(tr("Leave empty to let CLF3 auto-detect the game"));
  m_store = new QComboBox;
  m_store->addItem(tr("Auto-detect"), QString());
  m_store->addItem(tr("Steam"), QStringLiteral("Steam"));
  m_store->addItem(tr("GOG"), QStringLiteral("GOG"));
  m_store->addItem(tr("Epic Games"), QStringLiteral("Epic Games"));
  form->addRow(tr("Game store:"), m_store);
  configLayout->addLayout(form);
  auto* note = new QLabel(tr("CLF3 verifies every archive and can continue an interrupted "
                              "installation using the same folders."));
  note->setWordWrap(true);
  configLayout->addWidget(note);
  m_preflightSummary = new QLabel;
  m_preflightSummary->setWordWrap(true);
  m_preflightSummary->setTextFormat(Qt::PlainText);
  configLayout->addWidget(m_preflightSummary);
  for (auto* edit : {m_output, m_downloads, m_game})
    connect(edit, &QLineEdit::editingFinished, this,
            &Clf3InstallerDialog::updatePreflightSummary);
  configLayout->addStretch();
  auto* configButtons = new QHBoxLayout;
  auto* back          = new QPushButton(tr("← Back"));
  auto* install       = new QPushButton(tr("Install"));
  configButtons->addWidget(back);
  configButtons->addStretch();
  configButtons->addWidget(install);
  configLayout->addLayout(configButtons);
  m_pages->addWidget(configPage);

  auto* progressPage   = new QWidget;
  auto* progressLayout = new QVBoxLayout(progressPage);
  m_engineVersion      = new QLabel(tr("Waiting for CLF3…"));
  m_status             = new QLabel(tr("Preparing…"));
  m_overall            = new QProgressBar;
  m_pipelineSummary = new QLabel(tr("No active work"));
  m_activeDownloads = new QListWidget;
  m_activeDownloads->setViewMode(QListView::IconMode);
  m_activeDownloads->setFlow(QListView::LeftToRight);
  m_activeDownloads->setResizeMode(QListView::Adjust);
  m_activeDownloads->setMovement(QListView::Static);
  m_activeDownloads->setWrapping(true);
  m_activeDownloads->setGridSize(QSize(400, 224));
  m_activeDownloads->setSpacing(6);
  m_activeDownloads->setUniformItemSizes(true);
  m_activeDownloads->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_activeDownloads->setSelectionMode(QAbstractItemView::NoSelection);
  m_log = new QPlainTextEdit;
  m_log->setReadOnly(true);
  m_log->setMaximumBlockCount(10000);
  m_log->setVisible(false);
  progressLayout->addWidget(m_engineVersion);
  progressLayout->addWidget(m_status);
  progressLayout->addWidget(m_overall);
  progressLayout->addWidget(m_pipelineSummary);
  progressLayout->addWidget(m_activeDownloads, 1);
  m_manualDownloads = new QListWidget;
  m_manualDownloads->setMaximumHeight(240);
  m_manualDownloads->setVisible(false);
  progressLayout->addWidget(m_manualDownloads);
  progressLayout->addWidget(m_log, 2);
  m_switchInstance = new QCheckBox(tr("Switch to this instance when this window closes"));
  m_switchInstance->setChecked(true);
  progressLayout->addWidget(m_switchInstance);
  auto* progressButtons = new QHBoxLayout;
  m_cancel              = new QPushButton(tr("Cancel"));
  m_close               = new QPushButton(tr("Close"));
  m_close->setEnabled(false);
  progressButtons->addWidget(m_cancel);
  m_retrySetup = new QPushButton(tr("Retry compatibility setup"));
  m_retrySetup->setVisible(false);
  progressButtons->addWidget(m_retrySetup);
  auto* exportButton = new QPushButton(tr("Export log…"));
  progressButtons->addWidget(exportButton);
  connect(exportButton, &QPushButton::clicked, this, &Clf3InstallerDialog::exportLog);
  connect(m_retrySetup, &QPushButton::clicked, this, [this] {
    if (!m_postInstallRunning && !m_controller.isRunning()) finishInstall(m_installStats);
  });
  progressButtons->addStretch();
  progressButtons->addWidget(m_close);
  progressLayout->addLayout(progressButtons);
  m_pages->addWidget(progressPage);

  connect(refresh, &QPushButton::clicked, this, [this] { loadGallery(true); });
  connect(m_search, &QLineEdit::textChanged, this,
          [this] { populateGallery(); });
  auto gallerySettings = Clf3InstallUtils::openSettings();
  gallerySettings->beginGroup(QStringLiteral("clf3/gallery"));
  m_showNsfw->setChecked(gallerySettings->value("showNsfw", false).toBool());
  m_showUnavailable->setChecked(
      gallerySettings->value("showUnavailable", false).toBool());
  m_officialOnly->setChecked(gallerySettings->value("officialOnly", false).toBool());
  m_installedOnly->setChecked(
      gallerySettings->value("installedOnly", false).toBool());
  const int savedSort = m_sortOrder->findData(
      gallerySettings->value("sort", QStringLiteral("featured")).toString());
  m_sortOrder->setCurrentIndex(qMax(0, savedSort));
  gallerySettings->endGroup();
  auto refilter = [this] {
    auto settings = Clf3InstallUtils::openSettings();
    settings->beginGroup(QStringLiteral("clf3/gallery"));
    settings->setValue("sort", m_sortOrder->currentData());
    settings->setValue("showNsfw", m_showNsfw->isChecked());
    settings->setValue("showUnavailable", m_showUnavailable->isChecked());
    settings->setValue("officialOnly", m_officialOnly->isChecked());
    settings->setValue("installedOnly", m_installedOnly->isChecked());
    settings->endGroup();
    populateGallery();
  };
  connect(m_gameFilter, &QComboBox::currentIndexChanged, this,
          refilter);
  connect(m_sortOrder, &QComboBox::currentIndexChanged, this,
          refilter);
  connect(m_installedOnly, &QCheckBox::toggled, this, [this, refilter] {
    updateGameFilter();
    refilter();
  });
  connect(m_showNsfw, &QCheckBox::toggled, this,
          refilter);
  connect(m_showUnavailable, &QCheckBox::toggled, this,
          refilter);
  connect(m_officialOnly, &QCheckBox::toggled, this,
          refilter);
  connect(m_includeMods, &QLineEdit::textChanged, this,
          [this] { populateGallery(); });
  connect(m_excludeMods, &QLineEdit::textChanged, this,
          [this] { populateGallery(); });
  connect(m_galleryList, &QListWidget::currentRowChanged, this,
          [this] { selectGalleryItem(); });
  connect(m_source, &QLineEdit::textEdited, this, [this] {
    m_machineName.clear();
    m_gameId.clear();
    m_game->clear();
    m_store->setCurrentIndex(0);
  });
  connect(browse, &QPushButton::clicked, this, &Clf3InstallerDialog::chooseSource);
  connect(configure, &QPushButton::clicked, this,
          &Clf3InstallerDialog::showConfiguration);
  connect(back, &QPushButton::clicked, this, [this] { m_pages->setCurrentIndex(0); });
  connect(install, &QPushButton::clicked, this, &Clf3InstallerDialog::startInstall);
  connect(m_cancel, &QPushButton::clicked, this,
          &Clf3InstallerDialog::cancelInstall);
  connect(m_close, &QPushButton::clicked, this, &QDialog::accept);

  m_downloads->setText(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
                       + QStringLiteral("/Fluorine/modlists"));
}

void Clf3InstallerDialog::loadGallery(bool refresh)
{
  if (m_galleryProcess.state() != QProcess::NotRunning) return;
  m_galleryOutput.clear();
  m_details->setText(tr("Loading the Wabbajack gallery…"));
  QStringList args{QStringLiteral("gallery"), QStringLiteral("--host-metadata")};
  if (refresh) args << QStringLiteral("--refresh");
  connect(&m_galleryProcess, &QProcess::readyReadStandardOutput, this, [this] {
    m_galleryOutput += m_galleryProcess.readAllStandardOutput();
  }, Qt::SingleShotConnection);
  connect(&m_galleryProcess,
          qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [this](int code, QProcess::ExitStatus) {
            m_galleryOutput += m_galleryProcess.readAllStandardOutput();
            if (code != 0) {
              m_details->setText(tr("The gallery is unavailable. You can still select a local "
                                    ".wabbajack file or paste its URL."));
              return;
            }
            const auto document = QJsonDocument::fromJson(m_galleryOutput);
            m_gallery.clear();
            m_installedGames.clear();
            m_allMods.clear();
            m_modsPerList.clear();
            const QJsonArray modlists = document.isObject()
                                             ? document.object().value("modlists").toArray()
                                             : document.array();
            if (document.isObject()) {
              for (const auto& game : document.object().value("installed_games").toArray())
                m_installedGames.insert(game.toString().toLower());
              const auto searchIndex = document.object().value("search_index").toObject();
              for (const auto& mod : searchIndex.value("AllMods").toArray())
                m_allMods.push_back(mod.toString());
              const auto modsPerList = searchIndex.value("ModsPerList").toObject();
              for (auto it = modsPerList.begin(); it != modsPerList.end(); ++it) {
                QSet<QString> mods;
                for (const auto& mod : it.value().toArray())
                  mods.insert(mod.toString().toLower());
                m_modsPerList.insert(it.key().toLower(), std::move(mods));
              }
            }
            for (const auto& value : modlists)
              if (value.isObject()) m_gallery.push_back(value.toObject());
            updateGameFilter();
            for (QLineEdit* edit : {m_includeMods, m_excludeMods}) {
              auto* completer = new QCompleter(m_allMods, edit);
              completer->setCaseSensitivity(Qt::CaseInsensitive);
              completer->setCompletionMode(QCompleter::PopupCompletion);
              completer->setFilterMode(Qt::MatchContains);
              completer->setMaxVisibleItems(12);
              edit->setCompleter(completer);
              edit->setEnabled(!m_allMods.isEmpty());
            }
            populateGallery();
          }, Qt::SingleShotConnection);
  m_galleryProcess.start(m_controller.enginePath(), args);
}

void Clf3InstallerDialog::populateGallery()
{
  const QString query = m_search->text().trimmed();
  const QSet<QString> requiredMods = requestedMods(m_includeMods->text());
  const QSet<QString> excludedMods = requestedMods(m_excludeMods->text());
  const QString game = m_gameFilter->currentData().toString();
  const QString sort = m_sortOrder->currentData().toString();
  const QString selectedMachine = m_galleryList->currentItem()
                                      ? m_galleryList->currentItem()
                                            ->data(Qt::UserRole + 1)
                                            .toString()
                                      : QString();
  QVector<int> visible;
  visible.reserve(m_gallery.size());

  for (int i = 0; i < m_gallery.size(); ++i) {
    const auto& item = m_gallery.at(i);
    const auto links = item.value("links").toObject();
    if (!m_showUnavailable->isChecked() && isGalleryUnavailable(item))
      continue;
    if (!m_showNsfw->isChecked() && item.value("nsfw").toBool()) continue;
    if (m_officialOnly->isChecked() && !item.value("official").toBool()) continue;
    const QString itemGame = item.value("game").toString();
    if (!game.isEmpty() && itemGame.compare(game, Qt::CaseInsensitive) != 0) continue;
    if (m_installedOnly->isChecked()
        && !m_installedGames.contains(itemGame.toLower()))
      continue;
    if (!requiredMods.isEmpty() || !excludedMods.isEmpty()) {
      const auto mods = m_modsPerList.value(
          item.value("machine_name").toString().toLower());
      bool matches = true;
      for (const auto& mod : requiredMods)
        if (!mods.contains(mod)) matches = false;
      for (const auto& mod : excludedMods)
        if (mods.contains(mod)) matches = false;
      if (!matches) continue;
    }

    QString haystack = item.value("title").toString() + ' '
                       + item.value("author").toString() + ' ' + itemGame + ' '
                       + item.value("description").toString();
    for (const auto& tag : item.value("tags").toArray())
      haystack += ' ' + tag.toString();
    if (!query.isEmpty() && !haystack.contains(query, Qt::CaseInsensitive)) continue;
    visible.push_back(i);
  }

  auto numeric = [this](int index, const char* field) {
    return m_gallery.at(index)
        .value("download_metadata")
        .toObject()
        .value(field)
        .toVariant()
        .toLongLong();
  };
  std::stable_sort(visible.begin(), visible.end(), [this, &sort, &numeric](int a, int b) {
    const auto& left  = m_gallery.at(a);
    const auto& right = m_gallery.at(b);
    if (sort == QStringLiteral("title-asc") || sort == QStringLiteral("title-desc")) {
      const int comparison = QString::localeAwareCompare(
          left.value("title").toString(), right.value("title").toString());
      return sort.endsWith("asc") ? comparison < 0 : comparison > 0;
    }
    const char* field = sort.startsWith("download")
                            ? "SizeOfArchives"
                            : sort.startsWith("installed") ? "SizeOfInstalledFiles"
                                                           : "NumberOfArchives";
    if (sort != QStringLiteral("featured")) {
      const qint64 leftValue  = numeric(a, field);
      const qint64 rightValue = numeric(b, field);
      if (leftValue != rightValue)
        return sort.endsWith("asc") ? leftValue < rightValue : leftValue > rightValue;
    }
    return false;
  });

  m_galleryList->clear();
  int restoreRow = -1;
  for (const int i : visible) {
    const auto& item = m_gallery.at(i);
    const auto metadata = item.value("download_metadata").toObject();
    const QString key = galleryImageKey(item);
    const QString official = item.value("official").toBool() ? tr(" · Official") : QString();
    const QString unavailable = isGalleryUnavailable(item)
                                    ? tr(" · Unavailable")
                                    : QString();
    auto* row = new QListWidgetItem(
        QStringLiteral("%1\n%2 · %3\n%4%5")
            .arg(item.value("title").toString(), item.value("author").toString(),
                 displayGameName(item.value("game").toString()),
                 formatBytes(metadata.value("SizeOfArchives").toVariant().toLongLong()),
                 official + unavailable),
        m_galleryList);
    row->setData(Qt::UserRole, i);
    row->setData(Qt::UserRole + 1, item.value("machine_name").toString());
    row->setIcon(m_imageIcons.value(key, galleryPlaceholder()));
    row->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
    row->setToolTip(item.value("description").toString());
    if (!selectedMachine.isEmpty()
        && item.value("machine_name").toString() == selectedMachine)
      restoreRow = m_galleryList->count() - 1;
    queueThumbnail(item);
  }
  m_resultCount->setText(tr("%1 modlists").arg(visible.size()));
  if (m_galleryList->count() > 0)
    m_galleryList->setCurrentRow(restoreRow >= 0 ? restoreRow : 0);
  else
    m_details->setText(tr("No modlists match these filters."));
  pumpThumbnailQueue();
}

void Clf3InstallerDialog::updateGameFilter()
{
  const QString selected = m_gameFilter->currentData().toString();
  QList<QPair<QString, QString>> games;
  QSet<QString> seen;
  for (const auto& item : m_gallery) {
    const QString raw = item.value("game").toString();
    if (raw.isEmpty() || seen.contains(raw.toLower())) continue;
    if (m_installedOnly->isChecked()
        && !m_installedGames.contains(raw.toLower()))
      continue;
    seen.insert(raw.toLower());
    const QString installed = m_installedGames.contains(raw.toLower())
                                  ? tr(" (installed)")
                                  : QString();
    games.push_back({raw, displayGameName(raw) + installed});
  }
  std::sort(games.begin(), games.end(), [](const auto& a, const auto& b) {
    return QString::localeAwareCompare(a.second, b.second) < 0;
  });

  const QSignalBlocker blocker(m_gameFilter);
  m_gameFilter->clear();
  m_gameFilter->addItem(m_installedOnly->isChecked() ? tr("All installed games")
                                                      : tr("All games"),
                        QString());
  for (const auto& [raw, display] : games) m_gameFilter->addItem(display, raw);
  const int selectedIndex = m_gameFilter->findData(selected);
  m_gameFilter->setCurrentIndex(qMax(0, selectedIndex));

  m_installedOnly->setEnabled(!m_installedGames.isEmpty());
  m_installedOnly->setText(
      m_installedGames.isEmpty()
          ? tr("Installed games only (none detected)")
          : tr("Installed games only (%1)").arg(m_installedGames.size()));
  if (m_installedGames.isEmpty()) m_installedOnly->setChecked(false);
}

void Clf3InstallerDialog::queueThumbnail(const QJsonObject& item)
{
  const QString key = galleryImageKey(item);
  const QUrl url(item.value("links").toObject().value("image").toString());
  if (key.isEmpty() || !url.isValid() || url.scheme().isEmpty()
      || m_imageIcons.contains(key) || m_imageQueued.contains(key))
    return;

  const QPixmap cached = cachedImage(url);
  if (!cached.isNull()) {
    m_imageIcons.insert(key, QIcon(croppedImage(cached, 240, 135)));
    for (int row = 0; row < m_galleryList->count(); ++row) {
      auto* listItem = m_galleryList->item(row);
      const auto& metadata = m_gallery.at(listItem->data(Qt::UserRole).toInt());
      if (galleryImageKey(metadata) == key) listItem->setIcon(m_imageIcons.value(key));
    }
    return;
  }
  m_imageQueued.insert(key);
  m_imageQueue.enqueue({key, url});
}

void Clf3InstallerDialog::pumpThumbnailQueue()
{
  while (m_activeImageRequests < 6 && !m_imageQueue.isEmpty()) {
    const auto [key, url] = m_imageQueue.dequeue();
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::PreferCache);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent", "Fluorine/0.3 WabbajackGallery");
    auto* reply = m_imageNetwork->get(request);
    ++m_activeImageRequests;
    connect(reply, &QNetworkReply::finished, this, [this, reply, key, url] {
      if (reply->error() == QNetworkReply::NoError) {
        const QByteArray data = reply->readAll();
        QPixmap source;
        if (source.loadFromData(data)) {
          storeCachedImage(url, data);
          m_imageIcons.insert(key, QIcon(croppedImage(source, 240, 135)));
          for (int row = 0; row < m_galleryList->count(); ++row) {
            auto* listItem = m_galleryList->item(row);
            const auto& metadata = m_gallery.at(listItem->data(Qt::UserRole).toInt());
            if (galleryImageKey(metadata) == key) listItem->setIcon(m_imageIcons.value(key));
          }
        }
      }
      reply->deleteLater();
      --m_activeImageRequests;
      pumpThumbnailQueue();
    });
  }
}

void Clf3InstallerDialog::startActiveItem(
    const QString& itemId, const QString& name, const QString& displayName,
    const QString& subtitle, const QString& stage, const QString& imageUrl,
    qint64 total, const QString& unit)
{
  if (itemId.isEmpty() || m_activeItems.contains(itemId)) return;

  auto* item = new QListWidgetItem(m_activeDownloads);
  item->setSizeHint(QSize(390, 214));
  item->setData(Qt::UserRole, name);

  auto* card = new QFrame;
  card->setFrameShape(QFrame::StyledPanel);
  card->setFrameShadow(QFrame::Plain);
  auto* cardLayout = new QVBoxLayout(card);
  cardLayout->setContentsMargins(8, 8, 8, 8);
  cardLayout->setSpacing(5);
  auto* detailsRow = new QHBoxLayout;

  auto* image = new QLabel(card);
  image->setFixedSize(128, 128);
  image->setAlignment(Qt::AlignCenter);
  image->setPixmap(activeItemPlaceholder());
  detailsRow->addWidget(image);

  const QString resolvedDisplay = m_activeDisplayNames.value(
      name, displayName.isEmpty() ? name : displayName);
  const QString resolvedSubtitle = m_activeSubtitles.value(name, subtitle);
  const QString resolvedImage = m_activeImageUrls.value(name, imageUrl);
  QStringList description;
  description << resolvedDisplay;
  if (!resolvedSubtitle.isEmpty()) description << resolvedSubtitle;
  if (!name.isEmpty() && name != resolvedDisplay) description << name;
  auto* title = new QLabel(description.join('\n'), card);
  title->setWordWrap(true);
  title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  title->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  detailsRow->addWidget(title, 1);
  cardLayout->addLayout(detailsRow, 1);

  auto* statusRow = new QHBoxLayout;
  auto* stageLabel = new QLabel(stage, card);
  auto* speedLabel = new QLabel(card);
  speedLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  statusRow->addWidget(stageLabel, 1);
  statusRow->addWidget(speedLabel);
  cardLayout->addLayout(statusRow);

  auto* progress = new QProgressBar(card);
  progress->setTextVisible(true);
  cardLayout->addWidget(progress);
  m_activeDownloads->setItemWidget(item, card);
  m_activeItems.insert(itemId,
                       {item, image, stageLabel, progress, speedLabel});
  m_activeSpeeds.insert(itemId, 0.0);

  updateActiveItem(itemId, 0, total, 0.0, unit);
  loadActiveImage(itemId, name, resolvedImage);
  updatePipelineSummary();
}

void Clf3InstallerDialog::updateActiveItem(const QString& itemId,
                                           qint64 completed, qint64 total,
                                           double speed, const QString& unit)
{
  const auto found = m_activeItems.constFind(itemId);
  if (found == m_activeItems.cend() || !found->progress) return;
  auto* progress = found->progress;

  if (total <= 0) {
    progress->setMaximum(0);
    progress->setFormat(tr("Working…"));
  } else if (unit == QStringLiteral("items")) {
    progress->setMaximum(int(qMin<qint64>(total, 2147483647)));
    progress->setValue(int(qMin(completed, total)));
    progress->setFormat(tr("%v / %m files"));
  } else {
    progress->setMaximum(1000);
    progress->setValue(int(qBound(0.0, double(completed) * 1000.0 / double(total),
                                  1000.0)));
    progress->setFormat(tr("%1 / %2")
                            .arg(formatBytes(completed), formatBytes(total)));
  }
  if (found->speed)
    found->speed->setText(
        speed > 0.0
            ? tr("%1 MiB/s").arg(speed / (1024.0 * 1024.0), 0, 'f', 1)
            : QString());
  m_activeSpeeds.insert(itemId, speed);
  updatePipelineSummary();
}

void Clf3InstallerDialog::setActiveItemMessage(const QString& itemId,
                                                const QString& message)
{
  const auto found = m_activeItems.constFind(itemId);
  if (found != m_activeItems.cend() && found->stage)
    found->stage->setText(message);
}

void Clf3InstallerDialog::finishActiveItem(const QString& itemId)
{
  const ActiveCard card = m_activeItems.take(itemId);
  m_activeSpeeds.remove(itemId);
  if (!card.item) return;
  const int row = m_activeDownloads->row(card.item);
  if (row >= 0) {
    QWidget* widget = m_activeDownloads->itemWidget(card.item);
    m_activeDownloads->removeItemWidget(card.item);
    delete m_activeDownloads->takeItem(row);
    if (widget) widget->deleteLater();
  }
  updatePipelineSummary();
}

void Clf3InstallerDialog::failActiveItem(const QString& itemId,
                                         const QString& message)
{
  const auto found = m_activeItems.constFind(itemId);
  if (found != m_activeItems.cend()) {
    if (found->stage) found->stage->setText(tr("Failed"));
    if (found->progress) {
      found->progress->setMaximum(1);
      found->progress->setValue(0);
      found->progress->setFormat(tr("Failed"));
    }
    if (found->item) found->item->setToolTip(message);
  }
  m_activeSpeeds.insert(itemId, 0.0);
  updatePipelineSummary();
  m_log->appendPlainText(message);
  m_log->setVisible(true);
}

void Clf3InstallerDialog::loadActiveImage(const QString& itemId,
                                          const QString& name,
                                          const QString& imageUrl)
{
  Q_UNUSED(itemId);
  const QString key = QStringLiteral("active:") + name;
  if (!imageUrl.isEmpty()) m_activeImageUrls.insert(name, imageUrl);
  const QUrl url(m_activeImageUrls.value(name));

  auto applyImage = [this, key, name] {
    if (!m_imageIcons.contains(key)) return;
    for (const ActiveCard& card : std::as_const(m_activeItems)) {
      if (card.item && card.image
          && card.item->data(Qt::UserRole).toString() == name)
        card.image->setPixmap(m_imageIcons.value(key).pixmap(128, 128));
    }
  };
  if (m_imageIcons.contains(key)) {
    applyImage();
    return;
  }
  if (!url.isValid() || url.scheme().isEmpty() || m_imageQueued.contains(key)) return;

  // Individual archive/mod images are useful only while this installation is
  // visible. Keep them in m_imageIcons for the dialog lifetime, but do not add
  // them to the persistent modlist-gallery thumbnail cache. Two concurrent
  // thumbnail requests keep this decoration from competing with mod downloads.
  m_imageQueued.insert(key);
  m_activeImageQueue.enqueue({key, name, url});
  pumpActiveImageQueue();
}

void Clf3InstallerDialog::pumpActiveImageQueue()
{
  while (m_activePipelineImageRequests < 2 && !m_activeImageQueue.isEmpty()) {
    const ActiveImageRequest pending = m_activeImageQueue.dequeue();
    const bool stillVisible = std::any_of(
        m_activeItems.cbegin(), m_activeItems.cend(),
        [&pending](const ActiveCard& card) {
          return card.item
                 && card.item->data(Qt::UserRole).toString() == pending.name;
        });
    if (!stillVisible) continue;
    QNetworkRequest request(pending.url);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent", "Fluorine/0.3 CLF3Pipeline");
    auto* reply = m_imageNetwork->get(request);
    ++m_activePipelineImageRequests;
    connect(reply, &QNetworkReply::finished, this, [this, reply, pending] {
      if (reply->error() == QNetworkReply::NoError) {
        const QByteArray data = reply->readAll();
        QPixmap source;
        if (source.loadFromData(data)) {
          m_imageIcons.insert(
              pending.key, QIcon(croppedImage(source, 128, 128)));
          for (const ActiveCard& card : std::as_const(m_activeItems)) {
            if (card.item && card.image
                && card.item->data(Qt::UserRole).toString() == pending.name)
              card.image->setPixmap(m_imageIcons.value(pending.key).pixmap(128, 128));
          }
        }
      }
      reply->deleteLater();
      --m_activePipelineImageRequests;
      pumpActiveImageQueue();
    });
  }
}

void Clf3InstallerDialog::updatePipelineSummary()
{
  double totalSpeed = 0.0;
  for (double speed : std::as_const(m_activeSpeeds))
    totalSpeed += qMax(0.0, speed);
  const int active = m_activeItems.size();
  if (active == 0) {
    m_pipelineSummary->setText(tr("No active work"));
  } else if (totalSpeed > 0.0) {
    m_pipelineSummary->setText(
        tr("%1 active · %2 MiB/s total")
            .arg(active)
            .arg(totalSpeed / (1024.0 * 1024.0), 0, 'f', 1));
  } else {
    m_pipelineSummary->setText(tr("%1 active").arg(active));
  }
}

void Clf3InstallerDialog::detectSelectedGamePath()
{
  if (!m_game->text().trimmed().isEmpty()) return;
  const QString targetName = knownGameNameForWabbajack(m_gameId);
  if (targetName.isEmpty()) return;

  for (const auto& game : detectAllGames().games) {
    const KnownGame* known = knownGameForDetected(game);
    if ((!known || QString::fromLatin1(known->name).compare(
                       targetName, Qt::CaseInsensitive) != 0)
        && game.name.compare(targetName, Qt::CaseInsensitive) != 0)
      continue;
    if (!QFileInfo::exists(game.install_path)) continue;
    m_game->setText(QDir::cleanPath(game.install_path));
    const QString store = launcherStore(game.launcher);
    const int storeIndex = m_store->findData(store);
    if (storeIndex >= 0) m_store->setCurrentIndex(storeIndex);
    return;
  }
}

void Clf3InstallerDialog::selectGalleryItem()
{
  const auto* row = m_galleryList->currentItem();
  if (!row) return;
  const auto item = m_gallery.value(row->data(Qt::UserRole).toInt());
  const auto links = item.value("links").toObject();
  const QString selectedGameId = item.value("game").toString();
  if (!m_gameId.isEmpty()
      && selectedGameId.compare(m_gameId, Qt::CaseInsensitive) != 0) {
    m_game->clear();
    m_store->setCurrentIndex(0);
  }
  m_source->setText(links.value("download").toString());
  m_machineName = item.value("machine_name").toString();
  m_gameId = selectedGameId;
  m_instanceName->setText(item.value("title").toString());
  const auto metadata = item.value("download_metadata").toObject();
  QString description = item.value("description").toString();
  description.replace(QStringLiteral("\\u002B"), QStringLiteral("+"),
                      Qt::CaseInsensitive);
  description.replace(QStringLiteral("\\u0026"), QStringLiteral("&"),
                      Qt::CaseInsensitive);
  QStringList tags;
  for (const auto& tag : item.value("tags").toArray()) tags.push_back(tag.toString());
  QStringList badgeParts;
  if (item.value("official").toBool()) badgeParts.push_back(tr("Official"));
  if (WabbajackPostInstall::hasAdapter(m_machineName))
    badgeParts.push_back(tr("Automatic Linux setup"));
  if (item.value("nsfw").toBool()) badgeParts.push_back(tr("NSFW"));
  if (isGalleryUnavailable(item))
    badgeParts.push_back(tr("Unavailable"));
  if (!item.value("version").toString().isEmpty())
    badgeParts.push_back(tr("Version %1").arg(item.value("version").toString()));
  const QString badges = badgeParts.join(QStringLiteral(" · "));
  m_details->setText(
      QStringLiteral("<h2>%1</h2><p><b>%2</b> · %3</p><p>%4</p><p>%5</p>"
                     "<p><b>%6:</b> %7<br><b>%8:</b> %9<br><b>%10:</b> %11</p>"
                     "<p>%12</p><p><a href=\"%13\">%14</a></p>")
          .arg(item.value("title").toString().toHtmlEscaped(),
               item.value("author").toString().toHtmlEscaped(),
               displayGameName(item.value("game").toString()).toHtmlEscaped(),
               badges.toHtmlEscaped(), description.toHtmlEscaped(), tr("Download"),
               formatBytes(metadata.value("SizeOfArchives").toVariant().toLongLong()),
               tr("Installed"),
               formatBytes(metadata.value("SizeOfInstalledFiles").toVariant().toLongLong()),
               tr("Archives"),
               QString::number(metadata.value("NumberOfArchives").toInt()),
               tags.join(QStringLiteral(" · ")).toHtmlEscaped(),
               links.value("readme").toString().toHtmlEscaped(), tr("Readme")));
}

void Clf3InstallerDialog::chooseSource()
{
  const QString file = QFileDialog::getOpenFileName(
      this, tr("Choose Wabbajack Modlist"), {}, tr("Wabbajack Modlist (*.wabbajack)"));
  if (!file.isEmpty()) {
    m_source->setText(file);
    m_machineName.clear();
    m_gameId.clear();
    m_game->clear();
    m_store->setCurrentIndex(0);
  }
}

void Clf3InstallerDialog::chooseDirectory(QLineEdit* target)
{
  const QString path = QFileDialog::getExistingDirectory(this, tr("Choose Folder"),
                                                          target->text());
  if (!path.isEmpty()) target->setText(path);
}

void Clf3InstallerDialog::showConfiguration()
{
  if (m_source->text().trimmed().isEmpty()) {
    QMessageBox::warning(this, tr("Modlist required"),
                         tr("Choose a gallery entry, local file, or URL first."));
    return;
  }
  if (m_instanceName->text().trimmed().isEmpty()) {
    const QFileInfo source(m_source->text());
    m_instanceName->setText(source.completeBaseName().isEmpty()
                                ? tr("Wabbajack Modlist")
                                : source.completeBaseName());
  }
  if (m_output->text().isEmpty())
    m_output->setText(InstanceManager::singleton().instancePath(
        InstanceManager::singleton().makeUniqueName(m_instanceName->text())));
  detectSelectedGamePath();
  updatePreflightSummary();
  m_pages->setCurrentIndex(1);
}

void Clf3InstallerDialog::startInstall()
{
  if (m_controller.isRunning() || m_postInstallRunning) return;
  if (!checkInstallation()) return;
  m_stopping = false;
  m_deferredClose.reset();
  m_createdInstanceDir.clear();
  m_installStats = {};
  m_setupJobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  if (!savePendingJob()) {
    QMessageBox::warning(this, tr("Cannot save installation"),
                         m_pendingSaveError);
    return;
  }
  // Gallery thumbnails must not compete with the installation for bandwidth.
  m_imageQueue.clear();
  m_activeImageQueue.clear();
  for (auto* reply : m_imageNetwork->findChildren<QNetworkReply*>()) reply->abort();
  m_pages->setCurrentIndex(2);
  m_retrySetup->setVisible(false);
  clearManualRequests();
  m_activeItems.clear();
  m_activeSpeeds.clear();
  m_activeImageUrls.clear();
  m_activeDisplayNames.clear();
  m_activeSubtitles.clear();
  m_activeDownloads->clear();
  updatePipelineSummary();
  m_log->clear();
  m_log->setVisible(false);
  m_cancel->setEnabled(true);
  m_close->setEnabled(false);
  m_controller.startInstall(m_source->text(), m_downloads->text(), m_output->text(),
                            m_game->text(), m_machineName);
}

bool Clf3InstallerDialog::savePendingJob(const QString& stage)
{
  auto settings = Clf3InstallUtils::openSettings();
  settings->beginGroup(QStringLiteral("clf3/pending"));
  settings->setValue(QStringLiteral("source"), m_source->text());
  settings->setValue(QStringLiteral("instanceName"), m_instanceName->text());
  settings->setValue(QStringLiteral("output"), m_output->text());
  settings->setValue(QStringLiteral("downloads"), m_downloads->text());
  settings->setValue(QStringLiteral("game"), m_game->text());
  settings->setValue(QStringLiteral("machineName"), m_machineName);
  settings->setValue(QStringLiteral("gameId"), m_gameId);
  settings->setValue(QStringLiteral("store"), m_store->currentData());
  settings->setValue(QStringLiteral("stage"), stage);
  settings->setValue(QStringLiteral("setupJobId"), m_setupJobId);
  settings->setValue(QStringLiteral("engineVersion"), m_engineVersion->text());
  settings->setValue(QStringLiteral("stats"), QJsonDocument(m_installStats).toJson(QJsonDocument::Compact));
  settings->endGroup();
  settings->sync();
  m_pendingSaveError.clear();
  if (settings->status() == QSettings::FormatError)
    m_pendingSaveError = tr("The resume settings file is invalid:\n%1").arg(settings->fileName());
  else if (settings->status() != QSettings::NoError)
    m_pendingSaveError = tr("Cannot write resume information to:\n%1").arg(settings->fileName());
  return m_pendingSaveError.isEmpty();
}

void Clf3InstallerDialog::clearPendingJob()
{
  auto settings = Clf3InstallUtils::openSettings();
  settings->remove(QStringLiteral("clf3/pending"));
  settings->sync();
}

void Clf3InstallerDialog::offerResume()
{
  auto settings = Clf3InstallUtils::openSettings();
  settings->beginGroup(QStringLiteral("clf3/pending"));
  const QString source = settings->value(QStringLiteral("source")).toString();
  if (source.isEmpty()) {
    settings->endGroup();
    return;
  }
  const bool setupOnly = settings->value(QStringLiteral("stage")).toString()
                         == QStringLiteral("post-install");
  const auto answer = QMessageBox::question(
      this, setupOnly ? tr("Continue compatibility setup?") : tr("Continue modlist installation?"),
      setupOnly ? tr("The modlist installation finished, but compatibility setup is incomplete. "
                     "Retry setup using the saved folders? Downloads will not be restarted.")
                : tr("Fluorine found an unfinished CLF3 installation. Continue it using the saved paths?"),
      QMessageBox::Yes | QMessageBox::No | QMessageBox::Discard, QMessageBox::Yes);
  if (answer == QMessageBox::Discard) {
    settings->endGroup();
    clearPendingJob();
    return;
  }
  if (answer == QMessageBox::No) {
    settings->endGroup();
    return;
  }
  m_source->setText(source);
  m_instanceName->setText(settings->value(QStringLiteral("instanceName")).toString());
  m_output->setText(settings->value(QStringLiteral("output")).toString());
  m_downloads->setText(settings->value(QStringLiteral("downloads")).toString());
  m_game->setText(settings->value(QStringLiteral("game")).toString());
  m_machineName = settings->value(QStringLiteral("machineName")).toString();
  m_gameId = settings->value(QStringLiteral("gameId")).toString();
  const int storeIndex = m_store->findData(settings->value(QStringLiteral("store")));
  m_store->setCurrentIndex(qMax(0, storeIndex));
  m_setupJobId = settings->value(QStringLiteral("setupJobId")).toString();
  if (setupOnly)
    m_engineVersion->setText(settings->value(QStringLiteral("engineVersion"),
                                           tr("Resuming compatibility setup")).toString());
  m_installStats = QJsonDocument::fromJson(settings->value(QStringLiteral("stats")).toByteArray()).object();
  settings->endGroup();
  if (setupOnly) {
    m_pages->setCurrentIndex(2);
    finishInstall(m_installStats);
  } else {
    startInstall();
  }
}

void Clf3InstallerDialog::finishInstall(const QJsonObject& stats)
{
  if (m_controller.isRunning() || m_postInstallRunning) return;
  m_postInstallRunning = true;
  m_status->setText(tr("Applying Fluorine compatibility setup…"));
  m_log->appendPlainText(tr("Phase: Fluorine compatibility setup"));
  m_retrySetup->setVisible(false);
  clearManualRequests();
  CuratedGuideNxmBroker::instance().clearConsumer(QStringLiteral("clf3"));
  m_nexusQueue.clear();
  m_currentNexus.reset();
  if (m_browserDialog) m_browserDialog->hide();
  m_createdInstanceDir = m_output->text();
  m_installStats = stats;
  m_activeItems.clear();
  m_activeSpeeds.clear();
  m_activeDownloads->clear();
  updatePipelineSummary();
  m_cancel->setEnabled(false);
  m_close->setEnabled(false);
  m_overall->setMaximum(0);
  m_overall->setFormat(tr("Applying Fluorine compatibility setup…"));

  if (m_setupJobId.isEmpty()) m_setupJobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  // Persist the stage before any setup work or nested game-folder dialog.
  if (!savePendingJob(QStringLiteral("post-install"))) {
    failPostInstall(m_pendingSaveError);
    return;
  }
  if (!QFileInfo(m_createdInstanceDir).isDir()
      || QDir(m_createdInstanceDir).isEmpty()) {
    failPostInstall(tr("The installed modlist folder is missing or empty. Restore it before retrying setup."));
    return;
  }
  const QString resolvedByClf3 = stats.value(QStringLiteral("game_path")).toString();
  if (!resolvedByClf3.isEmpty() && QFileInfo::exists(resolvedByClf3))
    m_game->setText(QDir::cleanPath(resolvedByClf3));
  detectSelectedGamePath();
  if (WabbajackPostInstall::hasAdapter(m_machineName)
      && m_game->text().trimmed().isEmpty()) {
    const QString selected = QFileDialog::getExistingDirectory(
        this, tr("Choose the original %1 game folder").arg(displayGameName(m_gameId)),
        QDir::homePath());
    if (selected.isEmpty()) {
      failPostInstall(
          tr("The modlist is installed, but automatic Linux compatibility setup was "
             "deferred because no original game folder was selected."));
      return;
    }
    m_game->setText(QDir::cleanPath(selected));
  }
  if (!savePendingJob(QStringLiteral("post-install"))) {
    failPostInstall(m_pendingSaveError);
    return;
  }
  m_postInstall->start({m_machineName, m_gameId, m_createdInstanceDir,
                        m_downloads->text(), m_game->text(),
                        m_store->currentData().toString(), m_setupJobId});
}

void Clf3InstallerDialog::completePostInstall(const QStringList& adjustments,
                                              const QStringList& warnings)
{
  m_postInstallRunning = false;
  m_retrySetup->setVisible(false);
  CuratedGuideNxmBroker::instance().clearConsumer(
      QStringLiteral("wabbajack-postinstall"));
  m_nexusQueue.clear();
  m_currentNexus.reset();
  if (m_browserDialog) m_browserDialog->hide();
  clearPendingJob();
  if (QFileInfo::exists(QDir(m_createdInstanceDir).filePath("ModOrganizer.ini"))) {
    InstanceManager::registerPortableInstance(m_createdInstanceDir);
    m_status->setText(warnings.isEmpty()
                          ? tr("Installation, compatibility setup, and instance "
                               "registration complete.")
                          : tr("Installation complete with compatibility warnings."));
  } else {
    m_status->setText(tr("Installation complete. ModOrganizer.ini was not produced, so the "
                         "folder was not registered automatically."));
  }
  m_overall->setMaximum(1);
  m_overall->setValue(1);
  m_overall->setFormat(tr("Complete"));
  m_activeItems.clear();
  m_activeSpeeds.clear();
  m_activeDownloads->clear();
  updatePipelineSummary();
  m_log->appendPlainText(tr("Completed: %1 downloaded, %2 reused, %3 failed.")
                             .arg(m_installStats.value("archives_downloaded").toInt())
                             .arg(m_installStats.value("archives_skipped").toInt())
                             .arg(m_installStats.value("archives_failed").toInt()));
  for (const QString& adjustment : adjustments)
    m_log->appendPlainText(tr("Setup: %1").arg(adjustment));
  for (const QString& warning : warnings)
    m_log->appendPlainText(tr("Warning: %1").arg(warning));
  if (!warnings.isEmpty()) m_log->setVisible(true);
  m_cancel->setEnabled(false);
  m_close->setEnabled(true);
  closeWhenIdle();
}

void Clf3InstallerDialog::failPostInstall(const QString& error)
{
  m_postInstallRunning = false;
  m_retrySetup->setVisible(true);
  CuratedGuideNxmBroker::instance().clearConsumer(
      QStringLiteral("wabbajack-postinstall"));
  m_nexusQueue.clear();
  m_currentNexus.reset();
  if (m_browserDialog) m_browserDialog->hide();
  m_status->setText(
      tr("Modlist installation complete. Compatibility setup needs attention: %1")
          .arg(error));
  m_log->appendPlainText(tr("POST-INSTALL FAILED: %1").arg(error));
  m_log->setVisible(true);
  m_overall->setMaximum(1);
  m_overall->setValue(1);
  m_overall->setFormat(tr("Installed · setup incomplete"));
  if (QFileInfo::exists(QDir(m_createdInstanceDir).filePath("ModOrganizer.ini")))
    InstanceManager::registerPortableInstance(m_createdInstanceDir);
  m_cancel->setEnabled(false);
  m_close->setEnabled(true);
  if (!m_deferredClose)
    QMessageBox::warning(
        this, tr("Installation complete — setup needs attention"),
        tr("The modlist installation completed successfully.\n\n%1").arg(error));
  closeWhenIdle();
}

void Clf3InstallerDialog::queueNexus(const QString& requestId,
                                     const QString& archiveName,
                                     const QString& domain, int modId, int fileId,
                                     qint64 expectedSize)
{
  m_nexusQueue.enqueue({QStringLiteral("clf3"), requestId, archiveName, domain,
                        modId, fileId, expectedSize});
  beginNextNexus();
}

void Clf3InstallerDialog::queuePostInstallNexus(
    const QString& requestId, const QString& artifactName,
    const QString& domain, int modId, int fileId, qint64 expectedSize)
{
  m_nexusQueue.enqueue({QStringLiteral("wabbajack-postinstall"), requestId,
                        artifactName, domain, modId, fileId, expectedSize});
  beginNextNexus();
}

void Clf3InstallerDialog::beginNextNexus()
{
  if (m_stopping || m_currentNexus || m_nexusQueue.isEmpty()) return;
  m_currentNexus = m_nexusQueue.dequeue();
  const auto account = NexusInterface::instance().getAPIUserAccount();
  if (account.type() == APIUserAccountTypes::None) {
    const QString requestId = m_currentNexus->requestId;
    if (m_currentNexus->consumer == QStringLiteral("clf3"))
      m_controller.rejectRequest(requestId,
                                 tr("Sign in to Nexus in Fluorine before installing."));
    else
      m_postInstall->rejectNexusAuthorization(
          requestId, tr("Sign in to Nexus in Fluorine before installing."));
    m_currentNexus.reset();
    QTimer::singleShot(0, this, &Clf3InstallerDialog::beginNextNexus);
  } else if (m_currentNexus->consumer == QStringLiteral("clf3")
             && account.type() == APIUserAccountTypes::Premium) {
    resolveNexus(*m_currentNexus);
  } else {
    CuratedGuideNxmBroker::instance().expectForConsumer(
        m_currentNexus->consumer, m_currentNexus->requestId, m_currentNexus->domain,
        m_currentNexus->modId, m_currentNexus->fileId, account.id().toInt());
    showNexusBrowser(*m_currentNexus);
  }
}

void Clf3InstallerDialog::showNexusBrowser(const NexusRequest& request)
{
  const QUrl page(QString("https://www.nexusmods.com/%1/mods/%2?tab=files&file_id=%3&nmm=1")
                      .arg(request.domain)
                      .arg(request.modId)
                      .arg(request.fileId));
  m_status->setText(tr("Waiting for your Nexus download click: %1")
                        .arg(request.archiveName));
#ifdef MO2_WEBENGINE
  if (m_browserDialog) m_browserDialog->deleteLater();
  m_browserDialog = new QDialog(this);
  m_browserDialog->setAttribute(Qt::WA_DeleteOnClose, false);
  m_browserDialog->setWindowTitle(tr("Authorize Nexus Download — %1")
                                      .arg(request.archiveName));
  m_browserDialog->resize(1100, 800);
  auto* layout = new QVBoxLayout(m_browserDialog);
  auto* help = new QLabel(tr("Choose Mod Manager Download and then Slow Download. "
                             "Fluorine will return to installation progress automatically."));
  help->setWordWrap(true);
  layout->addWidget(help);
  auto* view = new QWebEngineView(m_browserDialog);
  auto* authPage = new NexusAuthorizationPage(
      nexusProfile(), [](const QUrl& url) {
        CuratedGuideNxmBroker::instance().tryConsume(url.toString());
      }, view);
  view->setPage(authPage);
  layout->addWidget(view, 1);
  auto* buttons = new QHBoxLayout;
  auto* external = new QPushButton(tr("Open in External Browser"));
  auto* clearSession = new QPushButton(tr("Clear Nexus Session"));
  auto* hide = new QPushButton(tr("Hide"));
  buttons->addWidget(external);
  buttons->addWidget(clearSession);
  buttons->addStretch();
  buttons->addWidget(hide);
  layout->addLayout(buttons);
  connect(external, &QPushButton::clicked, m_browserDialog,
          [page] { QDesktopServices::openUrl(page); });
  connect(clearSession, &QPushButton::clicked, m_browserDialog, [] {
    nexusProfile()->cookieStore()->deleteAllCookies();
  });
  connect(hide, &QPushButton::clicked, m_browserDialog, &QDialog::hide);
  view->load(page);
  m_browserDialog->show();
  m_browserDialog->raise();
  m_browserDialog->activateWindow();
#else
  QDesktopServices::openUrl(page);
  QMessageBox::information(
      this, tr("Nexus authorization"),
      tr("The embedded browser is unavailable in this build. Complete the Nexus "
         "download click in your browser; Fluorine will capture the NXM link."));
#endif
}

void Clf3InstallerDialog::resolveNexus(const NexusRequest& request,
                                      const QString& nxmUrl)
{
  QUrl endpoint(QString("https://api.nexusmods.com/v1/games/%1/mods/%2/files/%3/download_link.json")
                    .arg(request.domain)
                    .arg(request.modId)
                    .arg(request.fileId));
  if (!nxmUrl.isEmpty()) {
    const NXMUrl parsed(nxmUrl);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("key"), parsed.key());
    query.addQueryItem(QStringLiteral("expires"), QString::number(parsed.expires()));
    endpoint.setQuery(query);
  }
  auto* manager = NexusInterface::instance().getAccessManager();
  auto* reply   = manager ? manager->makeAuthenticatedGetRequest(endpoint) : nullptr;
  if (!reply) {
    m_controller.rejectRequest(request.requestId, tr("Nexus authentication is unavailable."));
    m_currentNexus.reset();
    beginNextNexus();
    return;
  }
  connect(reply, &QNetworkReply::finished, this, [this, reply, request] {
    reply->deleteLater();
    if (m_stopping || !m_currentNexus
        || m_currentNexus->requestId != request.requestId
        || m_currentNexus->consumer != request.consumer) return;
    QStringList urls;
    if (reply->error() == QNetworkReply::NoError) {
      for (const auto& value : QJsonDocument::fromJson(reply->readAll()).array()) {
        const QString uri = value.toObject().value("URI").toString();
        if (!uri.isEmpty()) urls.push_back(uri);
      }
    }
    if (urls.isEmpty()) {
      m_controller.rejectRequest(request.requestId,
                                 tr("Nexus returned no usable download locations: %1")
                                     .arg(reply->errorString()));
    } else {
      m_controller.sendNexusUrls(request.requestId, urls);
      m_status->setText(tr("Downloading %1").arg(request.archiveName));
      if (m_browserDialog) m_browserDialog->hide();
    }
    m_currentNexus.reset();
    QTimer::singleShot(350, this, &Clf3InstallerDialog::beginNextNexus);
  });
}

void Clf3InstallerDialog::nexusLinkAccepted(const QString& consumer,
                                            const QString& requestId,
                                            const QString& url)
{
  if (!m_currentNexus || m_currentNexus->consumer != consumer
      || m_currentNexus->requestId != requestId)
    return;
  if (consumer == QStringLiteral("clf3")) {
    resolveNexus(*m_currentNexus, url);
    return;
  }
  if (consumer == QStringLiteral("wabbajack-postinstall")) {
    if (m_browserDialog) m_browserDialog->hide();
    m_currentNexus.reset();
    m_postInstall->provideNexusAuthorization(requestId, url);
    QTimer::singleShot(0, this, &Clf3InstallerDialog::beginNextNexus);
  }
}

void Clf3InstallerDialog::requestManualFile(const QString& requestId,
                                            const QString& archiveName,
                                            const QString& url,
                                            const QString& prompt,
                                            qint64 expectedSize,
                                            const QString& expectedHash)
{
  if (m_stopping || !m_controller.isRunning() || m_manualRequests.contains(requestId)) return;
  Q_UNUSED(expectedHash); // CLF3 verifies the hash when the selected file is submitted.
  m_log->appendPlainText(tr("Manual download required: %1").arg(archiveName));
  auto* item = new QListWidgetItem(m_manualDownloads);
  m_manualRequests.insert(requestId, item);
  auto* row = new QWidget;
  auto* layout = new QVBoxLayout(row);
  auto* description = new QLabel(tr("Manual download: %1 (%2)\n%3")
                                    .arg(archiveName, formatBytes(expectedSize), prompt));
  description->setTextFormat(Qt::PlainText);
  description->setWordWrap(true);
  layout->addWidget(description);
  auto* buttons = new QHBoxLayout;
  auto* open = new QPushButton(tr("Open download page"));
  auto* select = new QPushButton(tr("Select downloaded file…"));
  const QUrl page(url);
  open->setEnabled(page.isValid() && (page.scheme() == "https" || page.scheme() == "http"));
  buttons->addWidget(open);
  buttons->addWidget(select);
  buttons->addStretch();
  layout->addLayout(buttons);
  item->setSizeHint(row->sizeHint());
  m_manualDownloads->setItemWidget(item, row);
  m_manualDownloads->show();
  connect(open, &QPushButton::clicked, this, [page] { QDesktopServices::openUrl(page); });
  connect(select, &QPushButton::clicked, this,
          [this, requestId, archiveName, expectedSize] {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select downloaded archive: %1").arg(archiveName), m_downloads->text());
    // Requests can finish/cancel while the file picker runs its nested event loop.
    if (path.isEmpty() || m_stopping || !m_manualRequests.contains(requestId)) return;
    const QFileInfo file(path);
    if (!file.isFile() || !file.isReadable()
        || (expectedSize > 0 && file.size() != expectedSize)) {
      QMessageBox::warning(this, tr("Archive does not match"),
                           tr("Select a readable archive with the expected size: %1. "
                              "This download remains in the queue.").arg(formatBytes(expectedSize)));
      return;
    }
    // CLF3 performs the authoritative hash check before accepting this archive.
    m_controller.sendManualFile(requestId, path);
    delete m_manualRequests.take(requestId);
    m_manualDownloads->setVisible(!m_manualRequests.isEmpty());
  });
}

void Clf3InstallerDialog::done(int result)
{
  if (m_controller.isRunning() || m_postInstallRunning) {
    m_deferredClose = result;
    if (m_controller.isRunning()) {
      cancelInstall();
    } else {
      // Native patchers may be updating game executables. Keep their owner alive
      // until completion, including any required Nexus authorization.
      m_status->setText(tr("Finishing compatibility setup before closing…"));
    }
    return;
  }
  QDialog::done(result);
}

void Clf3InstallerDialog::cancelInstall()
{
  if (!m_controller.isRunning() || m_stopping) return;
  m_stopping = true;
  m_cancel->setEnabled(false);
  m_close->setEnabled(false);
  m_status->setText(tr("Stopping safely… Your installation can be continued later."));
  CuratedGuideNxmBroker::instance().clearConsumer(QStringLiteral("clf3"));
  m_nexusQueue.clear();
  m_currentNexus.reset();
  clearManualRequests();
  if (m_browserDialog) m_browserDialog->hide();
  m_controller.cancel();
}

void Clf3InstallerDialog::closeWhenIdle()
{
  if (!m_deferredClose || m_controller.isRunning() || m_postInstallRunning) return;
  const int result = *m_deferredClose;
  m_deferredClose.reset();
  QTimer::singleShot(0, this, [this, result] { done(result); });
}

void Clf3InstallerDialog::clearManualRequests()
{
  m_manualRequests.clear();
  m_manualDownloads->clear();
  m_manualDownloads->hide();
}

void Clf3InstallerDialog::exportLog()
{
  const QString path = QFileDialog::getSaveFileName(
      this, tr("Export installation log"),
      QDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation))
          .filePath(QStringLiteral("fluorine-install.log")), tr("Log files (*.log);;All files (*)"));
  if (path.isEmpty()) return;
  const QString report = QStringLiteral("Fluorine %1\n%2\n%3 UTC\nStatus: %4\n\n%5\n")
                             .arg(QCoreApplication::applicationVersion(),
                                  m_engineVersion->text(),
                                  QDateTime::currentDateTimeUtc().toString(Qt::ISODate),
                                  m_status->text(), m_log->toPlainText());
  const auto bytes = Clf3InstallUtils::redactLog(report).toUtf8();
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    QMessageBox::warning(this, tr("Log export failed"), file.errorString());
}

QJsonObject Clf3InstallerDialog::selectedDownloadMetadata() const
{
  // Editing a URL must not reuse another list's size estimate.
  for (const auto& item : m_gallery) {
    if (item.value("links").toObject().value("download").toString()
        == m_source->text().trimmed())
      return item.value("download_metadata").toObject();
  }
  return {};
}

namespace
{
QStorageInfo storageForPath(QString path)
{
  // QStorageInfo cannot reliably resolve a folder that has not been created yet.
  while (!path.isEmpty() && !QFileInfo::exists(path)) {
    const QString parent = QFileInfo(path).absolutePath();
    if (parent == path) break;
    path = parent;
  }
  return QStorageInfo(path);
}

QVector<Clf3InstallUtils::SpaceRequirement> installationSpace(
    const QString& output, const QString& downloads, const QJsonObject& metadata)
{
  const qint64 archives = qMax(qint64(0), metadata.value("SizeOfArchives").toVariant().toLongLong());
  const qint64 installed = qMax(qint64(0), metadata.value("SizeOfInstalledFiles").toVariant().toLongLong());
  auto requirement = [](const QString& path, const QString& purpose, qint64 bytes) {
    const QStorageInfo storage = storageForPath(path);
    const QString device = storage.device().isEmpty() ? storage.rootPath()
                                                     : QString::fromUtf8(storage.device());
    return Clf3InstallUtils::SpaceRequirement{
        device, purpose, storage.isValid() && storage.isReady() ? storage.bytesAvailable() : -1, bytes};
  };
  // CLF3 stages extracted files and texture spills under the output directory.
  return Clf3InstallUtils::combineSpaceRequirements({
      requirement(output, QObject::tr("Installed files"), installed),
      requirement(downloads, QObject::tr("Downloads"), archives),
      requirement(output, QObject::tr("Temporary extraction"),
                  Clf3InstallUtils::temporarySpaceEstimate(archives, installed))});
}
}

void Clf3InstallerDialog::updatePreflightSummary()
{
  const auto metadata = selectedDownloadMetadata();
  QStringList lines;
  for (const auto& space : installationSpace(m_output->text(), m_downloads->text(), metadata)) {
    lines << tr("%1: %2 estimated additional space; %3 available.")
                 .arg(space.purpose, formatBytes(space.required),
                      space.available < 0 ? tr("unknown") : formatBytes(space.available));
  }
  if (metadata.value("SizeOfArchives").toVariant().toLongLong() <= 0
      || metadata.value("SizeOfInstalledFiles").toVariant().toLongLong() <= 0)
    lines << tr("A complete gallery size estimate is unavailable. The space estimate above is incomplete.");
  lines << tr("Estimates include a temporary extraction allowance of 20% (at least 2 GiB). "
              "Actual peak usage may be higher; cached files may reduce what is needed.");
  const auto account = NexusInterface::instance().getAPIUserAccount();
  if (account.type() == APIUserAccountTypes::None)
    lines << tr("Nexus: not signed in. Sign in through Fluorine settings before installing a list "
                "that needs new Nexus downloads. Cached and public downloads can still be used.");
  else if (account.type() == APIUserAccountTypes::Premium)
    lines << tr("Nexus: Premium account connected.");
  else
    lines << tr("Nexus: account connected. Free downloads require your browser clicks.");
  m_preflightSummary->setText(lines.join('\n'));
}

bool Clf3InstallerDialog::checkInstallation()
{
  detectSelectedGamePath();
  for (auto* edit : {m_source, m_output, m_downloads, m_game}) edit->setText(edit->text().trimmed());
  const QString source = m_source->text();
  auto fail = [this](const QString& error) {
    QMessageBox::warning(this, tr("Installation checks"), error);
    return false;
  };
  if (source.isEmpty() || m_output->text().isEmpty() || m_downloads->text().isEmpty())
    return fail(tr("Source, instance, and download paths are required."));
  const QUrl sourceUrl(source);
  const bool remote = sourceUrl.scheme() == QStringLiteral("https")
                      || sourceUrl.scheme() == QStringLiteral("http");
  if (remote) {
    if (!sourceUrl.isValid() || sourceUrl.host().isEmpty())
      return fail(tr("Enter a valid HTTP or HTTPS modlist URL."));
  } else {
    if (!QFileInfo(source).isFile() || !QFileInfo(source).isReadable())
      return fail(tr("The local modlist file does not exist or cannot be read."));
    m_source->setText(QFileInfo(source).absoluteFilePath());
  }
  if (!m_game->text().isEmpty()
      && (!QFileInfo(m_game->text()).isDir() || !QFileInfo(m_game->text()).isReadable()))
    return fail(tr("The selected game folder does not exist or cannot be read."));
  for (auto* edit : {m_output, m_downloads}) {
    if (Clf3InstallUtils::pathsOverlap(edit->text(), m_game->text()))
      return fail(tr("The instance and download folders must be separate from the original game folder."));
    const QString error = Clf3InstallUtils::prepareWritableDirectory(edit->text());
    if (!error.isEmpty()) return fail(error);
    edit->setText(QFileInfo(edit->text()).canonicalFilePath());
  }
  if (m_output->text() == m_downloads->text())
    return fail(tr("Choose different folders for the installed instance and download cache."));

  updatePreflightSummary();
  QStringList warnings;
  for (const auto& space : installationSpace(m_output->text(), m_downloads->text(), selectedDownloadMetadata())) {
    if (space.available < 0)
      warnings << tr("Available space could not be measured for %1.").arg(space.purpose);
    else if (space.available < space.required)
      warnings << tr("%1: %2 estimated additional space, but only %3 is available.")
                      .arg(space.purpose, formatBytes(space.required), formatBytes(space.available));
  }
  if (NexusInterface::instance().getAPIUserAccount().type() == APIUserAccountTypes::None)
    warnings << tr("You are not signed in to Nexus. New Nexus downloads will require a connected "
                   "account; cached and public downloads can still be used.");
  if (!warnings.isEmpty()) {
    QMessageBox box(QMessageBox::Warning, tr("Review installation checks"),
                    warnings.join(QStringLiteral("\n\n"))
                        + tr("\n\nSpace estimates include temporary extraction and do not subtract cached files. "
                             "Continue only if these folders have enough space for this installation."),
                    QMessageBox::NoButton, this);
    auto* back = box.addButton(tr("Back"), QMessageBox::RejectRole);
    auto* proceed = box.addButton(tr("Continue"), QMessageBox::AcceptRole);
    box.setDefaultButton(back);
    box.exec();
    if (box.clickedButton() != proceed) return false;
  }
  return true;
}
