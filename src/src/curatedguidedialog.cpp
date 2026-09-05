#include "curatedguidedialog.h"

#include "fluorinepaths.h"
#include "gamedetection.h"
#include "instancemanager.h"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

CuratedGuideDialog::CuratedGuideDialog(QWidget* parent) : QDialog(parent)
{
  m_artworkNetwork = new QNetworkAccessManager(this);
  buildUi();
  QStringList errors;
  m_recipes = CuratedGuideCatalog::bundled(&errors);
  for (const auto& recipe : m_recipes) m_recipe->addItem(recipe.displayName, recipe.id);
  if (!errors.isEmpty()) m_recipeDetails->setText(errors.join("\n"));
  detectGames();
  recipeChanged(0);

  connect(m_recipe, qOverload<int>(&QComboBox::currentIndexChanged),
          this, &CuratedGuideDialog::recipeChanged);
  connect(m_install, &QPushButton::clicked, this, &CuratedGuideDialog::beginInstall);
  connect(m_close, &QPushButton::clicked, this, &QDialog::accept);
  m_manualWatcher.setInterval(2000);
  connect(&m_manualWatcher, &QTimer::timeout,
          this, &CuratedGuideDialog::scanForManualArtifact);
  connect(&m_installer, &CuratedGuideInstaller::progress, this,
          [this](int done, int total, const QString& action) {
            m_progress->setRange(0, total);
            m_progress->setValue(done);
            m_status->setText(action);
          });
  connect(&m_installer, &CuratedGuideInstaller::artifactProgress, this,
          [this](const QString&, qint64 received, qint64 total) {
            if (total > 0) m_status->setText(tr("Downloading: %1 / %2 MiB")
                .arg(received / 1024 / 1024).arg(total / 1024 / 1024));
          });
  connect(&m_installer, &CuratedGuideInstaller::log, m_log, &QPlainTextEdit::appendPlainText);
  connect(&m_installer, &CuratedGuideInstaller::nexusDownloadRequired, this,
          [this](const QString&, const QString& name, const QString& url) {
            m_log->appendPlainText(tr("Waiting for free Nexus download: %1").arg(name));
            QDesktopServices::openUrl(QUrl(url));
          });
  connect(&m_installer, &CuratedGuideInstaller::manualArtifactRequired,
          this, &CuratedGuideDialog::showManualArtifact);
  connect(&m_installer, &CuratedGuideInstaller::assistedActionRequired,
          this, &CuratedGuideDialog::showAssistedAction);
  connect(&m_installer, &CuratedGuideInstaller::failed, this, [this](const QString& error) {
    m_status->setText(tr("Stopped: %1").arg(error));
    m_log->appendPlainText(tr("FAILED: %1").arg(error));
    QMessageBox::critical(this, tr("Curated guide installation stopped"), error);
    m_close->setEnabled(true);
  });
  connect(&m_installer, &CuratedGuideInstaller::finished, this,
          [this](const QString& path) {
            m_createdInstanceDir = path;
            m_progress->setValue(m_progress->maximum());
            m_status->setText(tr("Installation verified and registered."));
            m_close->setEnabled(true);
          });
  connect(&m_installer, &CuratedGuideInstaller::cancelled, this, [this] {
    m_status->setText(tr("Installation cancelled. The saved job can be resumed later."));
    m_close->setEnabled(true);
  });
  QTimer::singleShot(0, this, &CuratedGuideDialog::offerResume);
}

void CuratedGuideDialog::buildUi()
{
  setWindowTitle(tr("Install Curated Guide"));
  resize(1000, 760);
  auto* outer = new QVBoxLayout(this);
  m_pages = new QStackedWidget(this);
  outer->addWidget(m_pages, 1);

  auto* configPage = new QWidget;
  auto* configLayout = new QVBoxLayout(configPage);
  auto* title = new QLabel(tr("Curated ModdingLinked guide installer"));
  QFont titleFont = title->font(); titleFont.setPointSize(titleFont.pointSize() + 4);
  titleFont.setBold(true); title->setFont(titleFont);
  configLayout->addWidget(title);
  auto* guideForm = new QFormLayout;
  m_recipe = new QComboBox;
  guideForm->addRow(tr("Guide:"), m_recipe);
  configLayout->addLayout(guideForm);

  auto* overview = new QWidget;
  auto* overviewLayout = new QHBoxLayout(overview);
  overviewLayout->setContentsMargins(0, 4, 0, 8);
  m_artwork = new QLabel(tr("Artwork unavailable"));
  m_artwork->setAlignment(Qt::AlignCenter);
  m_artwork->setFixedSize(320, 180);
  m_artwork->setStyleSheet("QLabel { background: palette(alternate-base); border: 1px solid palette(mid); }");
  overviewLayout->addWidget(m_artwork);
  m_recipeDetails = new QLabel;
  m_recipeDetails->setWordWrap(true);
  m_recipeDetails->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  m_recipeDetails->setTextInteractionFlags(Qt::TextBrowserInteraction);
  m_recipeDetails->setOpenExternalLinks(true);
  overviewLayout->addWidget(m_recipeDetails, 1);
  configLayout->addWidget(overview);
  auto* form = new QFormLayout;
  m_store = new QComboBox; form->addRow(tr("Store:"), m_store);
  m_instanceName = new QLineEdit; form->addRow(tr("Instance name:"), m_instanceName);
  auto pathRow = [this, form](const QString& label, QLineEdit*& edit) {
    auto* row = new QWidget; auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0); edit = new QLineEdit;
    auto* browse = new QPushButton(tr("Browse…")); layout->addWidget(edit, 1); layout->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, [this, edit] { browsePath(edit); });
    form->addRow(label, row);
  };
  pathRow(tr("Instance folder:"), m_instancePath);
  pathRow(tr("Download cache:"), m_downloadsPath);
  pathRow(tr("Fallout: New Vegas:"), m_fnvPath);
  pathRow(tr("Fallout 3:"), m_fo3Path);
  m_preset = new QComboBox; m_preset->addItems({tr("Ultra"), tr("Medium")});
  form->addRow(tr("Graphics preset:"), m_preset);
  m_resolution = new QLineEdit; form->addRow(tr("Resolution:"), m_resolution);
  m_fov = new QLineEdit("75"); form->addRow(tr("World FOV:"), m_fov);
  configLayout->addLayout(form);
  m_isolated = new QCheckBox(tr("Create isolated stock game copy (recommended)"));
  m_isolated->setChecked(true); configLayout->addWidget(m_isolated);
  m_cleanConfirm = new QCheckBox(tr("I confirm the selected game installations are clean and unmodded"));
  configLayout->addWidget(m_cleanConfirm);
  auto* warning = new QLabel(tr("Direct mode modifies the original game. It is not recommended and is allowed only after the clean-install check."));
  warning->setWordWrap(true); configLayout->addWidget(warning);
  auto updateCleanMode = [this, warning](bool isolated) {
    m_cleanConfirm->setEnabled(!isolated);
    m_cleanConfirm->setVisible(!isolated);
    warning->setVisible(!isolated);
  };
  connect(m_isolated, &QCheckBox::toggled, this, updateCleanMode);
  updateCleanMode(m_isolated->isChecked());
  m_install = new QPushButton(tr("Validate and Install")); configLayout->addWidget(m_install);
  m_pages->addWidget(configPage);

  auto* progressPage = new QWidget; auto* progressLayout = new QVBoxLayout(progressPage);
  m_status = new QLabel(tr("Preparing…")); progressLayout->addWidget(m_status);
  m_progress = new QProgressBar; progressLayout->addWidget(m_progress);
  m_log = new QPlainTextEdit; m_log->setReadOnly(true); progressLayout->addWidget(m_log, 1);
  m_switchInstance = new QCheckBox(tr("Switch to this instance when the wizard closes"));
  m_switchInstance->setChecked(true); progressLayout->addWidget(m_switchInstance);
  m_close = new QPushButton(tr("Close")); m_close->setEnabled(false); progressLayout->addWidget(m_close);
  m_pages->addWidget(progressPage);
}

void CuratedGuideDialog::detectGames()
{
  const auto detected = detectAllGames();
  for (const auto& game : detected.games) {
    QString store;
    if (game.launcher.contains("GOG", Qt::CaseInsensitive)) store = "GOG";
    else if (game.launcher.contains("Epic", Qt::CaseInsensitive)) store = "Epic Games";
    else if (game.launcher.contains("Steam", Qt::CaseInsensitive)) store = "Steam";
    if (!store.isEmpty())
      m_detectedStores.insert(QDir::cleanPath(QFileInfo(game.install_path).absoluteFilePath()),
                              store);
    const QString haystack = (game.name + " " + game.app_id).toLower();
    if (m_fnvPath->text().isEmpty()
        && (haystack.contains("new vegas") || game.app_id == "22380" || game.app_id == "22490"))
      m_fnvPath->setText(game.install_path);
    if (m_fo3Path->text().isEmpty()
        && (haystack.contains("fallout 3") || game.app_id == "22370"))
      m_fo3Path->setText(game.install_path);
  }
  const QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
                            + "/Fluorine/curated";
  m_downloadsPath->setText(downloads);
  if (auto* screen = QGuiApplication::primaryScreen()) {
    const QSize size = screen->geometry().size();
    m_resolution->setText(QString("%1x%2").arg(size.width()).arg(size.height()));
  }
}

void CuratedGuideDialog::recipeChanged(int index)
{
  if (index < 0 || index >= m_recipes.size()) return;
  const auto& recipe = m_recipes[index];
  const auto formatSize = [](qint64 bytes) {
    if (bytes <= 0) return QObject::tr("Not available");
    const double gib = double(bytes) / double(1024LL * 1024LL * 1024LL);
    return QObject::tr("about %1 GiB").arg(gib, 0, 'f', gib < 10.0 ? 1 : 0);
  };
  const QDateTime updated = QDateTime::fromString(recipe.updatedAt, Qt::ISODate);
  const QString updatedText = updated.isValid()
      ? QLocale().toString(updated.toLocalTime().date(), QLocale::LongFormat)
      : tr("Unknown");
  m_recipeDetails->setText(
      QString("<h2>%1</h2>%2<p><b>%3</b> %4<br><b>%5</b> %6 · <b>%7</b> %8"
              "<br><b>%9</b> %10 · <b>%11</b> %12</p><p><small>%13</small></p>"
              "<a href=\"%14\">%15</a>")
          .arg(recipe.displayName.toHtmlEscaped(), recipe.description.toHtmlEscaped(),
               tr("Recipe updated:"), updatedText.toHtmlEscaped(),
               tr("Version:"), recipe.version.toHtmlEscaped(),
               tr("Source:"), recipe.sourceCommit.left(12).toHtmlEscaped(),
               tr("Estimated download:"), formatSize(recipe.estimatedDownloadSize),
               tr("Estimated installed footprint:"), formatSize(recipe.estimatedInstallSize),
               recipe.sizeEstimateNote.toHtmlEscaped(), recipe.guideUrl,
               tr("Open the original guide")));
  loadArtwork(recipe);
  m_store->clear(); m_store->addItems(recipe.supportedStores);
  const QString store = detectedStore(m_fnvPath->text());
  const int storeIndex = m_store->findText(store);
  if (storeIndex >= 0) m_store->setCurrentIndex(storeIndex);
  m_instanceName->setText(recipe.displayName);
  const QString unique = InstanceManager::singleton().makeUniqueName(recipe.displayName);
  m_instancePath->setText(InstanceManager::singleton().instancePath(unique));
  m_fo3Path->setEnabled(recipe.requiredGames.contains("fallout3"));
}

void CuratedGuideDialog::loadArtwork(const CuratedGuideRecipe& recipe)
{
  m_artworkDigest = recipe.artworkSha256;
  m_artwork->setPixmap({});
  m_artwork->setText(recipe.artworkUrl.isEmpty() ? tr("Artwork unavailable")
                                                 : tr("Loading artwork…"));
  if (recipe.artworkUrl.isEmpty() || recipe.artworkSha256.size() != 64) return;

  const QString cacheRoot =
      QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
      + "/curated-guide-artwork";
  QDir().mkpath(cacheRoot);
  const QString cachePath = QDir(cacheRoot).filePath(recipe.artworkSha256 + ".webp");
  auto apply = [this, expected = recipe.artworkSha256](const QByteArray& bytes) {
    if (expected != m_artworkDigest
        || QString::fromLatin1(QCryptographicHash::hash(
               bytes, QCryptographicHash::Sha256).toHex()) != expected)
      return false;
    QPixmap image;
    if (!image.loadFromData(bytes)) return false;
    m_artwork->setText({});
    m_artwork->setPixmap(image.scaled(m_artwork->size(), Qt::KeepAspectRatio,
                                      Qt::SmoothTransformation));
    return true;
  };

  QFile cached(cachePath);
  if (cached.open(QIODevice::ReadOnly) && apply(cached.readAll())) return;
  cached.remove();

  QNetworkRequest request(QUrl(recipe.artworkUrl));
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  auto* reply = m_artworkNetwork->get(request);
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, cachePath, expected = recipe.artworkSha256, apply] {
            const QByteArray bytes = reply->readAll();
            const bool networkOk = reply->error() == QNetworkReply::NoError;
            reply->deleteLater();
            if (!networkOk || !apply(bytes)) {
              if (expected == m_artworkDigest) m_artwork->setText(tr("Artwork unavailable"));
              return;
            }
            QFile output(cachePath);
            if (output.open(QIODevice::WriteOnly | QIODevice::Truncate))
              output.write(bytes);
          });
}

void CuratedGuideDialog::browsePath(QLineEdit* edit)
{
  const QString path = QFileDialog::getExistingDirectory(this, tr("Select folder"), edit->text());
  if (!path.isEmpty()) {
    edit->setText(path);
    if (edit == m_fnvPath) {
      const int index = m_store->findText(detectedStore(path));
      if (index >= 0) m_store->setCurrentIndex(index);
    }
  }
}

QString CuratedGuideDialog::detectedStore(const QString& path) const
{
  return m_detectedStores.value(
      QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
}

bool CuratedGuideDialog::validateConfiguration(QString* error) const
{
  if (m_recipe->currentIndex() < 0) { *error = tr("No valid bundled recipe is available."); return false; }
  if (!m_isolated->isChecked() && !m_cleanConfirm->isChecked()) {
    *error = tr("Confirm that the source installations are clean before using direct mode.");
    return false;
  }
  const QDir fnv(m_fnvPath->text());
  if (!fnv.exists("FalloutNV.exe") || !fnv.exists("Data/FalloutNV.esm")) {
    *error = tr("The Fallout: New Vegas path is incomplete or unsupported."); return false;
  }
  const QStringList fnvDlc = {"DeadMoney.esm", "HonestHearts.esm", "OldWorldBlues.esm",
                              "LonesomeRoad.esm", "GunRunnersArsenal.esm"};
  for (const auto& plugin : fnvDlc) {
    if (!fnv.exists("Data/" + plugin)) {
      *error = tr("The Fallout: New Vegas installation is missing required DLC: %1").arg(plugin);
      return false;
    }
  }
  const auto& recipe = m_recipes[m_recipe->currentIndex()];
  if (recipe.requiredGames.contains("fallout3")) {
    const QDir fo3(m_fo3Path->text());
    if (!fo3.exists("Fallout3.exe") || !fo3.exists("Data/Fallout3.esm")) {
      *error = tr("The Fallout 3 path is incomplete or unsupported."); return false;
    }
    const QStringList fo3Dlc = {"Anchorage.esm", "ThePitt.esm", "BrokenSteel.esm",
                                "PointLookout.esm", "Zeta.esm"};
    for (const auto& plugin : fo3Dlc) {
      if (!fo3.exists("Data/" + plugin)) {
        *error = tr("The Fallout 3 installation is missing required DLC: %1").arg(plugin);
        return false;
      }
    }
  }
  if (!m_isolated->isChecked()) {
    const QStringList knownRootMods = {"nvse_loader.exe", "d3d9.dll", "dxgi.dll", "FalloutNV_backup.exe"};
    for (const auto& file : knownRootMods) {
      if (fnv.exists(file)) {
        *error = tr("The New Vegas folder is not clean; found %1.").arg(file);
        return false;
      }
    }
  }
  if (m_instanceName->text().trimmed().isEmpty() || m_instancePath->text().trimmed().isEmpty()
      || m_downloadsPath->text().trimmed().isEmpty()) {
    *error = tr("Instance and download paths are required."); return false;
  }
  if (QFileInfo::exists(m_instancePath->text())) {
    *error = tr("The target instance folder already exists."); return false;
  }
  return true;
}

void CuratedGuideDialog::beginInstall()
{
  QString error;
  if (!validateConfiguration(&error)) { QMessageBox::warning(this, tr("Cannot install"), error); return; }
  const auto& recipe = m_recipes[m_recipe->currentIndex()];
  const bool isolated = m_isolated->isChecked();
  if (!isolated && QMessageBox::warning(this, tr("Direct game modification"),
      tr("This will patch the original clean game installation. Continue?"),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
  QDir().mkpath(m_instancePath->text());
  const QString jobPath = QDir(fluorineDataDir()).filePath("curated-installs/"
      + recipe.id + "-" + QString::number(QDateTime::currentMSecsSinceEpoch()));
  const QString stockFnv = QDir(m_instancePath->text()).filePath("stock/Fallout New Vegas");
  QJsonObject options{{"store", m_store->currentText()},
                      {"fnvStore", detectedStore(m_fnvPath->text()).isEmpty()
                                       ? m_store->currentText()
                                       : detectedStore(m_fnvPath->text())},
                      {"fo3Store", detectedStore(m_fo3Path->text()).isEmpty()
                                       ? m_store->currentText()
                                       : detectedStore(m_fo3Path->text())},
                      {"isolated", isolated},
                      {"fnvSource", m_fnvPath->text()},
                      {"fo3Source", m_fo3Path->text()},
                      {"managedGamePath", isolated ? stockFnv : m_fnvPath->text()},
                      {"preset", m_preset->currentText()},
                      {"resolution", m_resolution->text()},
                      {"worldFov", m_fov->text()},
                      {"bsaDecompress", true}};
  CuratedGuideInstallConfig config{m_instanceName->text(), m_instancePath->text(),
                                   m_downloadsPath->text(), jobPath, options};
  m_pages->setCurrentIndex(1);
  m_installer.start(recipe, config);
}

void CuratedGuideDialog::offerResume()
{
  const QString jobsRoot = QDir(fluorineDataDir()).filePath("curated-installs");
  QDirIterator iterator(jobsRoot, {"install-state.json"}, QDir::Files,
                        QDirIterator::Subdirectories);
  QString newestPath;
  QDateTime newest;
  CuratedGuideInstallState newestState;
  int recipeIndex = -1;
  while (iterator.hasNext()) {
    const QString path = iterator.next();
    QString error;
    const auto state = CuratedGuideInstallState::load(path, &error);
    if (!error.isEmpty()) continue;
    int candidateRecipe = -1;
    for (int i = 0; i < m_recipes.size(); ++i) {
      if (m_recipes[i].id == state.recipeId
          && m_recipes[i].matchesDigest(state.recipeDigest)) {
        candidateRecipe = i;
        break;
      }
    }
    if (candidateRecipe < 0) continue;
    const QDateTime modified = QFileInfo(path).lastModified();
    if (newestPath.isEmpty() || modified > newest) {
      newestPath = path;
      newest = modified;
      newestState = state;
      recipeIndex = candidateRecipe;
    }
  }
  if (newestPath.isEmpty()) return;

  QMessageBox box(QMessageBox::Question, tr("Saved curated install found"),
                  tr("%1 has a saved %2 job. Resume it, verify and repair its outputs, or start a new job?")
                      .arg(newestState.instanceName, newestState.overallStatus),
                  QMessageBox::NoButton, this);
  auto* resume = box.addButton(tr("Resume"), QMessageBox::AcceptRole);
  auto* repair = box.addButton(tr("Repair"), QMessageBox::ActionRole);
  box.addButton(tr("Start New"), QMessageBox::RejectRole);
  box.exec();
  if (box.clickedButton() != resume && box.clickedButton() != repair) return;
  m_recipe->setCurrentIndex(recipeIndex);
  m_pages->setCurrentIndex(1);
  if (box.clickedButton() == repair)
    m_installer.repair(m_recipes[recipeIndex], newestPath);
  else
    m_installer.resume(m_recipes[recipeIndex], newestPath);
}

void CuratedGuideDialog::scanForManualArtifact()
{
  if (m_manualArtifactId.isEmpty() || m_manualFilename.isEmpty()) return;
  const QStringList roots = {
      QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
      m_downloadsPath->text()};
  QString candidate;
  for (const auto& root : roots) {
    QDirIterator iterator(root, {m_manualFilename}, QDir::Files,
                          QDirIterator::Subdirectories);
    if (iterator.hasNext()) {
      candidate = iterator.next();
      break;
    }
  }
  if (candidate.isEmpty()) return;
  const qint64 size = QFileInfo(candidate).size();
  if (candidate != m_manualCandidate || size <= 0 || size != m_manualCandidateSize) {
    m_manualCandidate = candidate;
    m_manualCandidateSize = size;
    return;
  }
  m_manualWatcher.stop();
  const QString artifactId = m_manualArtifactId;
  m_manualArtifactId.clear();
  m_log->appendPlainText(tr("Detected completed manual download: %1").arg(candidate));
  m_installer.provideManualArtifact(artifactId, candidate);
}

void CuratedGuideDialog::showManualArtifact(const QString& id, const QString& name,
                                            const QString& sourceUrl,
                                            const QString& filename)
{
  QMessageBox box(QMessageBox::Information, tr("Manual download required"),
                  tr("Download %1 from the official page. Fluorine will watch your Downloads folder for the completed file.\n\nExpected: %2")
                      .arg(name, filename), QMessageBox::NoButton, this);
  auto* open = box.addButton(tr("Open Download Page"), QMessageBox::AcceptRole);
  auto* locate = box.addButton(tr("Locate Existing File"), QMessageBox::ActionRole);
  box.addButton(QMessageBox::Cancel);
  box.exec();
  if (box.clickedButton() == locate) {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Locate %1").arg(name),
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    if (!path.isEmpty()) m_installer.provideManualArtifact(id, path);
    return;
  }
  if (box.clickedButton() != open) {
    m_installer.cancel();
    return;
  }
  QDesktopServices::openUrl(QUrl(sourceUrl));
  m_manualArtifactId = id;
  m_manualFilename = filename;
  m_manualCandidate.clear();
  m_manualCandidateSize = -1;
  m_status->setText(tr("Watching Downloads for %1…").arg(filename));
  m_manualWatcher.start();
  scanForManualArtifact();
}

void CuratedGuideDialog::showAssistedAction(const QString&, const QString& title,
                                            const QString& instructions,
                                            const QString& sourceUrl,
                                            const QString& executable,
                                            const QString& outputPath,
                                            const QString& gamePath)
{
  QDialog dialog(this);
  dialog.setWindowTitle(title);
  dialog.resize(760, 360);
  auto* layout = new QVBoxLayout(&dialog);
  auto* explanation = new QLabel(instructions);
  explanation->setWordWrap(true);
  layout->addWidget(explanation);

  auto addCopyablePath = [&](const QString& label, const QString& path) {
    if (path.isEmpty()) return;
    layout->addWidget(new QLabel(label));
    auto* row = new QHBoxLayout;
    auto* value = new QLineEdit(path);
    value->setReadOnly(true);
    value->setCursorPosition(0);
    auto* copy = new QPushButton(tr("Copy Path"));
    row->addWidget(value, 1);
    row->addWidget(copy);
    layout->addLayout(row);
    connect(copy, &QPushButton::clicked, &dialog, [path] {
      QGuiApplication::clipboard()->setText(path);
    });
  };

  addCopyablePath(tr("Game folder — use this Windows path in the tool:"), gamePath);

  addCopyablePath(gamePath.isEmpty()
                      ? tr("Output folder — paste this into the tool:")
                      : tr("Output folder — use this Windows path in the tool:"),
                  outputPath);

  auto* sourcePath = new QLineEdit(executable);
  sourcePath->setReadOnly(true);
  layout->addWidget(new QLabel(QFileInfo(executable).isDir()
                                   ? tr("Extracted source folder:")
                                   : tr("Tool executable:")));
  layout->addWidget(sourcePath);
  auto* sessionStatus = new QLabel;
  sessionStatus->setWordWrap(true);
  layout->addWidget(sessionStatus);
  layout->addStretch(1);

  auto* buttons = new QDialogButtonBox;
  auto* open = buttons->addButton(QFileInfo(executable).isDir()
                                      ? tr("Open Source Folder")
                                      : tr("Launch Tool"),
                                  QDialogButtonBox::ActionRole);
  QPushButton* upstream = nullptr;
  if (!sourceUrl.isEmpty())
    upstream = buttons->addButton(tr("View Upstream"), QDialogButtonBox::HelpRole);
  auto* complete = buttons->addButton(QFileInfo(executable).isDir()
                                          ? tr("Verify and Continue")
                                          : tr("Finish Tool Session and Verify"),
                                      QDialogButtonBox::AcceptRole);
  auto* cancel = buttons->addButton(QDialogButtonBox::Cancel);
  layout->addWidget(buttons);

  qint64 launchedPid = 0;
  int inactiveChecks = 0;
  QTimer sessionTimer(&dialog);
  sessionTimer.setInterval(500);
  auto refreshLock = [&] {
    const bool running = m_installer.assistedSessionRunning(launchedPid);
    if (running) {
      inactiveChecks = 0;
      complete->setEnabled(true);
      open->setEnabled(false);
      sessionStatus->setText(
          tr("When the tool reports completion, close its window and click Finish. Fluorine will stop the remaining wineserver for this prefix and verify the output."));
      return;
    }
    if (launchedPid > 0 && ++inactiveChecks < 2) return;
    complete->setEnabled(true);
    open->setEnabled(true);
    sessionStatus->setText(launchedPid > 0
                               ? tr("The tool and wineserver have exited. You can now verify the output.")
                               : tr("Launch the tool, paste the output path above, and complete its work."));
  };
  connect(&sessionTimer, &QTimer::timeout, &dialog, refreshLock);
  connect(open, &QPushButton::clicked, &dialog, [&] {
    QString error;
    qint64 pid = 0;
    if (!m_installer.launchAssistedExecutable(executable, &pid, &error)) {
      QMessageBox::warning(&dialog, tr("Cannot launch tool"), error);
      return;
    }
    launchedPid = pid;
    inactiveChecks = 0;
    complete->setEnabled(QFileInfo(executable).isDir());
    refreshLock();
  });
  if (upstream) {
    connect(upstream, &QPushButton::clicked, &dialog,
            [sourceUrl] { QDesktopServices::openUrl(QUrl(sourceUrl)); });
  }
  connect(complete, &QPushButton::clicked, &dialog, [&] {
    QString error;
    if (!m_installer.finishAssistedAction(&error)) {
      complete->setEnabled(true);
      sessionStatus->setText(tr("Output is not ready: %1").arg(error));
      return;
    }
    dialog.accept();
  });
  connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);

  refreshLock();
  sessionTimer.start();
  if (dialog.exec() != QDialog::Accepted)
    m_installer.cancel();
}

bool CuratedGuideDialog::shouldSwitchToInstance() const
{
  return m_switchInstance && m_switchInstance->isChecked();
}
