#include "collectiondownloaddialog.h"
#include "ui_collectiondownloaddialog.h"
#include "gamedetection.h"
#include "nexusinterface.h"
#include "nxmaccessmanager.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QStandardPaths>
#include <log.h>

static constexpr int kCollectionSlugRole = Qt::UserRole;

// ─── Ctor / dtor ──────────────────────────────────────────────────────────

CollectionDownloadDialog::CollectionDownloadDialog(PluginContainer* pc,
                                                   QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::CollectionDownloadDialog)
    , m_pc(pc)
{
  ui->setupUi(this);
  setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint);
  resize(1100, 720);

  // ── Gallery grid layout ───────────────────────────────────────────────────
  ui->collectionList->setViewMode(QListView::IconMode);
  ui->collectionList->setResizeMode(QListView::Adjust);
  ui->collectionList->setWrapping(true);
  ui->collectionList->setUniformItemSizes(true);
  ui->collectionList->setIconSize(QSize(230, 130));
  ui->collectionList->setGridSize(QSize(254, 200));
  ui->collectionList->setSpacing(6);
  ui->collectionList->setWordWrap(true);
  ui->collectionList->setTextElideMode(Qt::ElideRight);
  ui->collectionList->setMovement(QListView::Static);

  // ── Browse page ──────────────────────────────────────────────────────────
  connect(ui->collectionList, &QListWidget::currentItemChanged, this,
      [this](QListWidgetItem* item, QListWidgetItem*) {
        onCollectionSelected(item);
      });
  connect(ui->showAllGames, &QCheckBox::toggled,
          this, &CollectionDownloadDialog::onShowAllGamesToggled);
  connect(ui->sortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &CollectionDownloadDialog::onSortChanged);
  connect(ui->searchBox, &QLineEdit::textChanged,
          this, &CollectionDownloadDialog::onSearchChanged);
  connect(ui->refreshButton,   &QPushButton::clicked,
          this, &CollectionDownloadDialog::loadGallery);
  connect(ui->loadMoreButton, &QPushButton::clicked,
          this, &CollectionDownloadDialog::onLoadMore);
  connect(ui->browseGamePath, &QPushButton::clicked,
          this, &CollectionDownloadDialog::onBrowseGamePath);

  // ── Config page ──────────────────────────────────────────────────────────
  connect(ui->browseDownloads, &QPushButton::clicked,
          this, &CollectionDownloadDialog::onBrowseDownloads);
  connect(ui->instanceNameEdit, &QLineEdit::textChanged,
          this, &CollectionDownloadDialog::onInstanceNameChanged);
  connect(ui->downloadsPathEdit, &QLineEdit::textChanged,
          this, &CollectionDownloadDialog::onDownloadsPathChanged);
  connect(ui->adultContentCheck, &QCheckBox::toggled,
          this, &CollectionDownloadDialog::onAdultContentToggled);

  // ── Navigation ───────────────────────────────────────────────────────────
  connect(ui->nextButton,   &QPushButton::clicked, this,
          &CollectionDownloadDialog::onNext);
  connect(ui->backButton,   &QPushButton::clicked, this,
          &CollectionDownloadDialog::onBack);
  connect(ui->cancelButton, &QPushButton::clicked, this,
          &CollectionDownloadDialog::onCancel);

  // ── NexusCollections signals ──────────────────────────────────────────────
  connect(&m_nexus, &NexusCollections::galleryReady,
          this, &CollectionDownloadDialog::onGalleryReady);
  connect(&m_nexus, &NexusCollections::galleryError,
          this, &CollectionDownloadDialog::onGalleryError);
  connect(&m_nexus, &NexusCollections::manifestReady,
          this, &CollectionDownloadDialog::onManifestReady);
  connect(&m_nexus, &NexusCollections::manifestError,
          this, &CollectionDownloadDialog::onManifestError);
  connect(&m_nexus, &NexusCollections::manifestProgress,
          this, &CollectionDownloadDialog::onManifestProgress);
  connect(&m_nexus, &NexusCollections::validateReady, this,
      [this](const QString& name, bool premium) {
        m_authenticated = true;
        m_isPremium     = premium;
        ui->configNote->setText(
            premium ? QString("Signed in as %1 (Premium). Automated downloads enabled.").arg(name)
                    : QString("Signed in as %1 (Free). Only direct-URL and cached mods can be "
                              "downloaded automatically. Nexus Premium support is required for "
                              "full automated installs.").arg(name));
      });
  connect(&m_nexus, &NexusCollections::validateError, this,
      [this](const QString& message) {
        m_authenticated = false;
        ui->collectionList->clear();
        ui->collectionList->addItem("(" + message + ")");
      });

  // ── CollectionInstaller signals ───────────────────────────────────────────
  connect(&m_installer, &CollectionInstaller::progress,
          this, &CollectionDownloadDialog::onProgress);
  connect(&m_installer, &CollectionInstaller::log,
          this, &CollectionDownloadDialog::onLog);
  connect(&m_installer, &CollectionInstaller::finished,
          this, &CollectionDownloadDialog::onInstallFinished);
  connect(&m_installer, &CollectionInstaller::failed,
          this, &CollectionDownloadDialog::onInstallFailed);

  // Pre-fill downloads dir from QStandardPaths.
  const QString defDl =
      QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
      + "/Fluorine/downloads";
  ui->downloadsPathEdit->setText(defDl);

  setPage(PageBrowse);
}

CollectionDownloadDialog::~CollectionDownloadDialog() = default;

// ─── Public setters ───────────────────────────────────────────────────────

void CollectionDownloadDialog::setGameDomain(const QString& domain)
{
  const int idx = ui->gameFilter->findData(domain);
  if (idx >= 0)
    ui->gameFilter->setCurrentIndex(idx);
}

void CollectionDownloadDialog::setDetectedGamePath(const QString& path)
{
  ui->gamePathEdit->setText(path);
}

QString CollectionDownloadDialog::createdInstanceDir() const
{
  return m_createdInstanceDir;
}

bool CollectionDownloadDialog::shouldSwitchToInstance() const
{
  return m_switchOnClose;
}

// ─── showEvent ────────────────────────────────────────────────────────────

void CollectionDownloadDialog::showEvent(QShowEvent* e)
{
  QDialog::showEvent(e);
  detectGamesAndFillPaths();
  populateGameFilter();

  m_nexus.checkAuth();
  loadGallery();
}

// ─── Page management ──────────────────────────────────────────────────────

void CollectionDownloadDialog::setPage(Page p)
{
  ui->pages->setCurrentIndex(static_cast<int>(p));

  static const char* titles[] = {
      "Step 1 of 4 — Browse Collections",
      "Step 2 of 4 — Configure Installation",
      "Step 3 of 4 — Installing…",
      "Step 4 of 4 — Done",
  };
  ui->stepLabel->setText(titles[p]);

  updateNav();
}

void CollectionDownloadDialog::updateNav()
{
  const int p = ui->pages->currentIndex();
  ui->backButton->setEnabled(p > PageBrowse && p < PageProgress);
  ui->nextButton->setEnabled(canGoNext());

  if (p == PageDone) {
    ui->nextButton->setText("Close");
    ui->backButton->setVisible(false);
    ui->cancelButton->setVisible(false);
  } else if (p == PageProgress) {
    ui->nextButton->setVisible(false);
    ui->backButton->setVisible(false);
  } else {
    ui->nextButton->setText(p == PageConfig ? "Install" : "Next");
    ui->nextButton->setVisible(true);
    ui->backButton->setVisible(true);
    ui->cancelButton->setVisible(true);
  }
}

bool CollectionDownloadDialog::canGoNext() const
{
  switch (ui->pages->currentIndex()) {
    case PageBrowse:
      return !m_selected.slug.isEmpty();
    case PageConfig:
      return !ui->instanceNameEdit->text().trimmed().isEmpty()
          && !ui->downloadsPathEdit->text().trimmed().isEmpty();
    case PageDone:
      return true;
    default:
      return false;
  }
}

bool CollectionDownloadDialog::canGoBack() const
{
  const int p = ui->pages->currentIndex();
  return p == PageConfig;
}

// ─── Navigation slots ─────────────────────────────────────────────────────

void CollectionDownloadDialog::onNext()
{
  const int p = ui->pages->currentIndex();
  if (p == PageBrowse) {
    if (!m_authenticated) {
      QMessageBox::warning(this, "Not Signed In",
          "Please connect your Nexus account in Settings \342\206\222 Nexus first.");
      return;
    }

    // Pre-fill instance name from collection name.
    if (ui->instanceNameEdit->text().isEmpty())
      ui->instanceNameEdit->setText(m_selected.name);

    // Derive and display instance dir (not editable by user).
    {
      QString safeName = ui->instanceNameEdit->text().trimmed();
      safeName.replace('/', '_').replace('\\', '_');
      const QString instDir =
          QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
          + "/Fluorine/instances/" + safeName;
      ui->instanceDirLabel->setText(instDir);
    }

    setPage(PageConfig);
    return;
  }

  if (p == PageConfig) {
    startInstall();
    return;
  }

  if (p == PageDone) {
    m_switchOnClose = ui->openAfterCheck->isChecked();
    accept();
    return;
  }
}

void CollectionDownloadDialog::onBack()
{
  if (ui->pages->currentIndex() == PageConfig)
    setPage(PageBrowse);
}

void CollectionDownloadDialog::onCancel()
{
  if (ui->pages->currentIndex() == PageProgress) {
    m_installer.cancel();
    ui->cancelButton->setEnabled(false);
    ui->progressLabel->setText("Cancelling…");
  } else {
    reject();
  }
}

// ─── Game detection ───────────────────────────────────────────────────────

void CollectionDownloadDialog::detectGamesAndFillPaths()
{
  const GameScanResult scan = detectAllGames();
  for (const DetectedGame& g : scan.games) {
    // Try name first, then Steam App ID (covers full titles like
    // "The Elder Scrolls V: Skyrim Special Edition").
    QString domain = NexusCollections::domainForGameName(g.name);
    if (domain.isEmpty() && !g.app_id.isEmpty())
      domain = NexusCollections::domainForSteamAppId(g.app_id);
    if (!domain.isEmpty() && !g.install_path.isEmpty()) {
      if (!m_detectedGamePaths.contains(domain))
        m_detectedGamePaths.insert(domain, g.install_path);
      if (!m_detectedDomains.contains(domain))
        m_detectedDomains.append(domain);
    }
  }
}

void CollectionDownloadDialog::populateGameFilter()
{
  ui->gameFilter->clear();
  ui->gameFilter->addItem("All detected games", QString(""));

  for (const QString& domain : m_detectedDomains) {
    const QString name = NexusCollections::gameNameForDomain(domain);
    ui->gameFilter->addItem(name, domain);
  }
}

QString CollectionDownloadDialog::resolvedGamePath() const
{
  const QString domain = m_selected.gameDomain;
  if (!ui->gamePathEdit->text().trimmed().isEmpty())
    return ui->gamePathEdit->text().trimmed();
  return m_detectedGamePaths.value(domain);
}

// ─── Gallery slots ────────────────────────────────────────────────────────

void CollectionDownloadDialog::loadGallery()
{
  m_cards.clear();
  m_galleryTotalCount = 0;
  ui->loadMoreButton->setVisible(false);
  ui->collectionList->clear();
  ui->collectionList->addItem("Loading…");
  ui->collectionCount->setText("Fetching…");

  const QString domain = ui->gameFilter->currentData().toString();

  NexusCollections::SortBy sort = NexusCollections::SortBy::Endorsements;
  if (ui->sortCombo->currentIndex() == 1)
    sort = NexusCollections::SortBy::Downloads;
  else if (ui->sortCombo->currentIndex() == 2)
    sort = NexusCollections::SortBy::Recent;

  m_nexus.fetchGallery(domain, sort, 0, 50);
}

void CollectionDownloadDialog::onLoadMore()
{
  ui->loadMoreButton->setEnabled(false);

  const QString domain = ui->gameFilter->currentData().toString();
  NexusCollections::SortBy sort = NexusCollections::SortBy::Endorsements;
  if (ui->sortCombo->currentIndex() == 1)      sort = NexusCollections::SortBy::Downloads;
  else if (ui->sortCombo->currentIndex() == 2)  sort = NexusCollections::SortBy::Recent;

  m_nexus.fetchGallery(domain, sort, m_cards.size(), 50);
}

void CollectionDownloadDialog::onGalleryReady(QVector<CollectionCard> cards,
                                              int offset, int totalCount)
{
  if (offset == 0)
    m_cards = cards;
  else
    m_cards.append(cards);

  m_galleryTotalCount = totalCount;
  filterCards();
  loadThumbnails();
  ui->loadMoreButton->setEnabled(true);
}

void CollectionDownloadDialog::onGalleryError(QString message)
{
  ui->collectionList->clear();
  ui->collectionList->addItem("Error: " + message);
  ui->collectionCount->setText("0 collections");
}

void CollectionDownloadDialog::filterCards()
{
  const QString search = ui->searchBox->text().trimmed().toLower();
  ui->collectionList->clear();

  int shown = 0;
  for (const CollectionCard& card : m_cards) {
    if (!search.isEmpty()) {
      if (!card.name.toLower().contains(search)
          && !card.author.toLower().contains(search)
          && !card.summary.toLower().contains(search))
        continue;
    }

    if (!ui->showAllGames->isChecked()
        && !m_detectedDomains.isEmpty()
        && !m_detectedDomains.contains(card.gameDomain))
      continue;

    if (card.isAdult && !ui->adultContentCheck->isChecked())
      continue;

    auto* item = new QListWidgetItem();
    applyCardToList(card, item);
    ui->collectionList->addItem(item);
    ++shown;
  }

  ui->collectionCount->setText(QString("%1 collection(s)").arg(shown));
  ui->collectionList->scrollToTop();

  // Show "Load more" only when there are more results on the server and we
  // aren't mid-search (searching local results, not server-side).
  const bool hasMore = search.isEmpty() && (m_cards.size() < m_galleryTotalCount);
  ui->loadMoreButton->setVisible(hasMore);
}

void CollectionDownloadDialog::applyCardToList(const CollectionCard& card,
                                               QListWidgetItem* item) const
{
  item->setText(QString("%1\nby %2  ·  %3 mods").arg(card.name, card.author).arg(card.modCount));
  item->setData(kCollectionSlugRole, card.slug);
  item->setToolTip(QString("<b>%1</b><br>by %2<br>%3 mods  ·  %4 endorsements<br><br>%5")
      .arg(card.name.toHtmlEscaped(), card.author.toHtmlEscaped())
      .arg(card.modCount).arg(card.endorsements)
      .arg(card.summary.toHtmlEscaped()));

  if (m_thumbnailCache.contains(card.slug))
    item->setIcon(QIcon(m_thumbnailCache.value(card.slug)));
}

void CollectionDownloadDialog::loadThumbnails()
{
  // Use the same NXMAccessManager as all other Nexus calls — it has the right
  // SSL configuration and OAuth session already set up.
  NXMAccessManager* am = NexusInterface::instance().getAccessManager();

  for (int i = 0; i < ui->collectionList->count(); ++i) {
    QListWidgetItem* item = ui->collectionList->item(i);
    const QString slug = item->data(kCollectionSlugRole).toString();
    if (slug.isEmpty()) continue;

    if (m_thumbnailCache.contains(slug)) {
      item->setIcon(QIcon(m_thumbnailCache.value(slug)));
      continue;
    }

    // Find the card URL.
    QString url;
    for (const CollectionCard& c : m_cards) {
      if (c.slug == slug) { url = c.tileImageUrl; break; }
    }
    if (url.isEmpty()) continue;

    MOBase::log::debug("[collections] thumbnail fetch: {}",
                       MOBase::log::safeUrlForLog(url));

    const QUrl qurl(url);
    QNetworkRequest req(qurl);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  "Mozilla/5.0 (X11; Linux x86_64; rv:125.0) Gecko/20100101 Firefox/125.0");
    req.setRawHeader("Accept", "image/webp,image/png,image/jpeg,image/*,*/*;q=0.8");

    // Prefer the NXMAccessManager; fall back to our own NAM if not available.
    QNetworkReply* reply = am ? am->get(req) : m_thumbnailNam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, slug]() {
      reply->deleteLater();
      if (reply->error() != QNetworkReply::NoError) {
        MOBase::log::debug("[collections] thumbnail error ({}) network code {}",
                           slug, reply->error());
        return;
      }

      const QByteArray data = reply->readAll();
      QPixmap px;
      if (!px.loadFromData(data)) {
        MOBase::log::debug("[collections] thumbnail decode failed for {} ({} bytes, content-type: {})",
                           slug, data.size(),
                           reply->header(QNetworkRequest::ContentTypeHeader).toString());
        return;
      }
      px = px.scaled(230, 130, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
      if (px.width() > 230 || px.height() > 130) {
        const int x = (px.width()  - 230) / 2;
        const int y = (px.height() - 130) / 2;
        px = px.copy(x, y, 230, 130);
      }
      m_thumbnailCache.insert(slug, px);

      for (int j = 0; j < ui->collectionList->count(); ++j) {
        QListWidgetItem* it = ui->collectionList->item(j);
        if (it && it->data(kCollectionSlugRole).toString() == slug)
          it->setIcon(QIcon(px));
      }
    });
  }
}

void CollectionDownloadDialog::onCollectionSelected(QListWidgetItem* item)
{
  if (!item)
    return;

  const QString slug = item->data(kCollectionSlugRole).toString();
  for (const CollectionCard& c : m_cards) {
    if (c.slug == slug) {
      m_selected = c;
      break;
    }
  }
  if (m_selected.slug.isEmpty())
    return;

  // Fill detail pane.
  ui->detailName->setText(m_selected.name);
  ui->detailMeta->setText(
      QString("by %1  |  %2 mods  |  %3 endorsements  |  v%4")
          .arg(m_selected.author)
          .arg(m_selected.modCount)
          .arg(m_selected.endorsements)
          .arg(m_selected.latestRevision));
  ui->detailSummary->setText(m_selected.summary);

  // Show game-path widget if the game is not detected.
  const bool detected = m_detectedGamePaths.contains(m_selected.gameDomain);
  ui->undetectedGameWidget->setVisible(!detected);

  updateNav();
}

void CollectionDownloadDialog::onShowAllGamesToggled(bool /*checked*/)
{
  filterCards();
}

void CollectionDownloadDialog::onSortChanged(int /*index*/)
{
  loadGallery();
}

void CollectionDownloadDialog::onSearchChanged(const QString& /*text*/)
{
  filterCards();
}

void CollectionDownloadDialog::onBrowseGamePath()
{
  const QString dir = QFileDialog::getExistingDirectory(
      this, "Select Game Installation Folder", QDir::homePath());
  if (!dir.isEmpty())
    ui->gamePathEdit->setText(dir);
}

// ─── Config page slots ────────────────────────────────────────────────────

void CollectionDownloadDialog::onBrowseDownloads()
{
  const QString dir = QFileDialog::getExistingDirectory(
      this, "Select Downloads Folder",
      ui->downloadsPathEdit->text().isEmpty()
          ? QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
          : ui->downloadsPathEdit->text());
  if (!dir.isEmpty())
    ui->downloadsPathEdit->setText(dir);
}

void CollectionDownloadDialog::onAdultContentToggled(bool /*checked*/)
{
  filterCards();
}

void CollectionDownloadDialog::onInstanceNameChanged(const QString& text)
{
  QString safeName = text.trimmed();
  safeName.replace('/', '_').replace('\\', '_');
  const QString instDir =
      QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
      + "/Fluorine/instances/" + safeName;
  ui->instanceDirLabel->setText(instDir);
  updateNav();
}

void CollectionDownloadDialog::onDownloadsPathChanged(const QString& /*text*/)
{
  updateNav();
}

// ─── Install ──────────────────────────────────────────────────────────────

void CollectionDownloadDialog::startInstall()
{
  setPage(PageProgress);
  ui->progressLabel->setText("Fetching collection manifest…");
  ui->progressBar->setRange(0, 0); // indeterminate while manifest downloads

  const QString workDir = ui->instanceDirLabel->text().trimmed() + "/.collection_work";
  QDir().mkpath(workDir);

  m_nexus.fetchManifest(m_selected.slug, m_selected.gameDomain, workDir);
}

void CollectionDownloadDialog::onManifestReady(QString jsonPath,
                                               QString extractedDir)
{
  m_extractedDir = extractedDir;

  QFile f(jsonPath);
  if (!f.open(QIODevice::ReadOnly)) {
    onInstallFailed("Cannot read collection.json: " + jsonPath);
    return;
  }
  m_manifest = CollectionManifest::fromJson(f.readAll());
  if (!m_manifest.isValid()) {
    onInstallFailed("Failed to parse collection.json.");
    return;
  }
  m_manifest.slug = m_selected.slug;

  ui->progressLabel->setText(
      QString("Installing '%1' (%2 mods)…").arg(m_manifest.name).arg(m_manifest.mods.size()));
  ui->progressBar->setRange(0, m_manifest.mods.size());
  ui->progressBar->setValue(0);

  CollectionInstallConfig cfg;
  cfg.extractedCollectionDir = m_extractedDir;
  cfg.downloadsDir          = ui->downloadsPathEdit->text().trimmed();
  cfg.instanceDir           = ui->instanceDirLabel->text().trimmed();
  cfg.instanceName          = ui->instanceNameEdit->text().trimmed();
  cfg.gamePath              = resolvedGamePath();
  cfg.gameDomain            = m_selected.gameDomain;
  cfg.portable              = false;

  m_installer.start(m_manifest, cfg);
}

void CollectionDownloadDialog::onManifestError(QString message)
{
  onInstallFailed("Manifest download failed: " + message);
}

void CollectionDownloadDialog::onManifestProgress(qint64 recv, qint64 total)
{
  if (total > 0) {
    ui->progressBar->setRange(0, static_cast<int>(total));
    ui->progressBar->setValue(static_cast<int>(recv));
  }
}

// ─── Installer progress slots ──────────────────────────────────────────────

void CollectionDownloadDialog::onProgress(int done, int total)
{
  if (total > 0) {
    ui->progressBar->setRange(0, total);
    ui->progressBar->setValue(done);
  }
  ui->progressLabel->setText(
      QString("Installing… %1 / %2").arg(done).arg(total));
}

void CollectionDownloadDialog::onLog(QString message)
{
  ui->logView->append(message);
}

void CollectionDownloadDialog::onInstallFinished(QString instanceDir)
{
  m_createdInstanceDir = instanceDir;

  ui->doneLabel->setText("Collection installed successfully!");
  ui->doneSummary->setText(
      QString("Instance: %1\n\nMods installed to: %2/mods")
          .arg(ui->instanceNameEdit->text(), instanceDir));

  setPage(PageDone);
  ui->nextButton->setText("Close");
}

void CollectionDownloadDialog::onInstallFailed(QString reason)
{
  ui->doneLabel->setText("Installation failed.");
  ui->doneSummary->setText(reason);
  ui->doneLabel->setStyleSheet("font-size:16px; font-weight:bold; color:red;");
  ui->openAfterCheck->setChecked(false);
  ui->openAfterCheck->setEnabled(false);
  setPage(PageDone);
}
