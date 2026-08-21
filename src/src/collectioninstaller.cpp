#include "collectioninstaller.h"
#include "apiuseraccount.h"
#include "fluorinepaths.h"
#include "instancemanager.h"
#include "nexusinterface.h"
#include "nxmaccessmanager.h"
#include "portablelauncherscript.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>
#include <QXmlStreamReader>
#include <log.h>

// ─── Helpers ─────────────────────────────────────────────────────────────

static QString sanitiseFolderName(const QString& name)
{
  QString s = name;
  for (QChar& ch : s) {
    if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' ||
        ch == '?' || ch == '"'  || ch == '<' || ch == '>' || ch == '|')
      ch = '_';
  }
  return s.trimmed();
}

// ─── Ctor ─────────────────────────────────────────────────────────────────

CollectionInstaller::CollectionInstaller(QObject* parent)
    : QObject(parent)
{
}

// ─── Path helpers ─────────────────────────────────────────────────────────

QString CollectionInstaller::modsDir() const
{
  return m_config.instanceDir + "/mods";
}

QString CollectionInstaller::profileDir() const
{
  return m_config.instanceDir + "/profiles/Default";
}

QString CollectionInstaller::downloadsDir() const
{
  return m_config.downloadsDir;
}

// ─── Start ────────────────────────────────────────────────────────────────

void CollectionInstaller::start(const CollectionManifest& manifest,
                                const CollectionInstallConfig& config)
{
  m_manifest = manifest;
  m_config   = config;

  if (m_config.gameDomain.isEmpty())
    m_config.gameDomain = manifest.domainName;

  // Derive instance dir from downloads dir when not explicitly set.
  if (m_config.instanceDir.isEmpty()) {
    m_config.instanceDir = m_config.downloadsDir + "/"
        + (m_config.instanceName.isEmpty() ? manifest.name : m_config.instanceName);
  }
  if (m_config.instanceName.isEmpty())
    m_config.instanceName = manifest.name;

  // Create directory structure.
  QDir().mkpath(modsDir());
  QDir().mkpath(profileDir());
  QDir().mkpath(downloadsDir());

  m_results.clear();
  m_results.reserve(m_manifest.mods.size());
  for (const auto& mod : m_manifest.mods)
    m_results.append({mod.name, ModInstallResult::Pending, {}});

  m_currentIdx = 0;
  m_cancelled  = false;

  emit log(QString("Starting collection install: %1 (%2 mods)")
               .arg(manifest.name)
               .arg(manifest.mods.size()));

  // Sort mods by phase (ascending) — phase-0 mods install before phase-1, etc.
  // We operate on indices into m_manifest.mods / m_results in phase order.
  // Simple linear scan for phase order is fine for a few hundred mods.
  installNext();
}

void CollectionInstaller::cancel()
{
  m_cancelled = true;
}

// ─── Sequential install loop ──────────────────────────────────────────────

void CollectionInstaller::installNext()
{
  if (m_cancelled) {
    emit failed("Installation cancelled by user.");
    return;
  }

  // Find next pending mod (phase order: we inserted them in JSON order, which
  // Vortex writes phase-ascending, so a plain linear scan is correct).
  while (m_currentIdx < m_results.size() &&
         m_results[m_currentIdx].status != ModInstallResult::Pending) {
    ++m_currentIdx;
  }

  if (m_currentIdx >= m_results.size()) {
    // All mods processed — write outputs and finish.
    finalise();
    return;
  }

  const int idx = m_currentIdx;
  emit progress(idx, m_results.size());
  emit log(QString("[%1/%2] %3")
               .arg(idx + 1)
               .arg(m_results.size())
               .arg(m_manifest.mods[idx].name));

  downloadMod(idx);
}

// ─── Download ─────────────────────────────────────────────────────────────

void CollectionInstaller::downloadMod(int idx)
{
  const CollectionMod& mod = m_manifest.mods[idx];

  // Prefer the local downloads cache first (by logical filename).
  const QString cachedPath =
      downloadsDir() + "/" + mod.logicalFilename;
  if (!cachedPath.isEmpty() && QFile::exists(cachedPath)) {
    emit log("  (cached) " + mod.logicalFilename);
    m_results[idx].status = ModInstallResult::Downloading; // brief transitional
    onDownloadFinished(idx, cachedPath);
    return;
  }

  const auto& src = mod.source;

  // "browse" type — requires manual download; skip in automated mode.
  if (src.type == QLatin1String("browse")) {
    emit log("  SKIP (manual download required): " + mod.name);
    if (!src.instructions.isEmpty())
      emit log("  Instructions: " + src.instructions);
    m_results[idx].status = ModInstallResult::Skipped;
    ++m_currentIdx;
    installNext();
    return;
  }

  // Direct URL download (non-Nexus sources: Google Drive, MediaFire, etc.).
  if (src.type == QLatin1String("direct") && !src.url.isEmpty()) {
    emit log("  Direct download: " + src.url);
    emit modStatus(idx, ModInstallResult::Downloading, {});

    QNetworkRequest req(QUrl(src.url));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    auto* reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, idx]() {
      reply->deleteLater();
      if (reply->error() != QNetworkReply::NoError) {
        m_results[idx].status = ModInstallResult::Failed;
        m_results[idx].error  = reply->errorString();
        emit modStatus(idx, ModInstallResult::Failed, reply->errorString());
        emit log("  FAILED: " + reply->errorString());
        ++m_currentIdx;
        installNext();
        return;
      }
      const QString destPath =
          downloadsDir() + "/" + m_manifest.mods[idx].logicalFilename;
      QFile f(destPath);
      if (f.open(QIODevice::WriteOnly)) {
        f.write(reply->readAll());
        f.close();
        onDownloadFinished(idx, destPath);
      } else {
        m_results[idx].status = ModInstallResult::Failed;
        m_results[idx].error  = "Cannot write " + destPath;
        emit modStatus(idx, ModInstallResult::Failed, m_results[idx].error);
        ++m_currentIdx;
        installNext();
      }
    });
    return;
  }

  // Nexus source — uses the shared OAuth/API-key access manager.
  if (src.type == QLatin1String("nexus") && src.modId > 0 && src.fileId > 0) {
    const bool premium =
        NexusInterface::instance().getAPIUserAccount().type()
        == APIUserAccountTypes::Premium;
    if (!premium) {
      emit log("  SKIP (Nexus Premium required for automated download): " + mod.name);
      m_results[idx].status = ModInstallResult::Skipped;
      m_results[idx].error  = "Nexus Premium required";
      emit modStatus(idx, ModInstallResult::Skipped, m_results[idx].error);
      ++m_currentIdx;
      installNext();
      return;
    }

    emit modStatus(idx, ModInstallResult::Downloading, {});

    const QString domain  = mod.domainName.isEmpty() ? m_config.gameDomain : mod.domainName;
    const QString linkUrl =
        QString("https://api.nexusmods.com/v1/games/%1/mods/%2/files/%3/download_link.json")
            .arg(domain)
            .arg(src.modId)
            .arg(src.fileId);

    NXMAccessManager* am = NexusInterface::instance().getAccessManager();
    auto* reply = am ? am->makeOAuthGetRequest(QUrl(linkUrl)) : nullptr;
    if (!reply) {
      const QString err = "Not authenticated — cannot download from Nexus.";
      m_results[idx].status = ModInstallResult::Failed;
      m_results[idx].error  = err;
      emit modStatus(idx, ModInstallResult::Failed, err);
      emit log("  FAILED: " + err);
      ++m_currentIdx;
      installNext();
      return;
    }

    connect(reply, &QNetworkReply::finished, this, [this, reply, idx]() {
      reply->deleteLater();
      if (reply->error() != QNetworkReply::NoError) {
        m_results[idx].status = ModInstallResult::Failed;
        m_results[idx].error  = reply->errorString();
        emit modStatus(idx, ModInstallResult::Failed, reply->errorString());
        emit log("  FAILED resolving download link: " + reply->errorString());
        ++m_currentIdx;
        installNext();
        return;
      }

      const QJsonArray links = QJsonDocument::fromJson(reply->readAll()).array();
      if (links.isEmpty()) {
        const QString err = "No download links returned for mod "
            + QString::number(m_manifest.mods[idx].source.modId);
        m_results[idx].status = ModInstallResult::Failed;
        m_results[idx].error  = err;
        emit modStatus(idx, ModInstallResult::Failed, err);
        emit log("  FAILED: " + err);
        ++m_currentIdx;
        installNext();
        return;
      }

      const QString cdnUrl = links.first().toObject()["URI"].toString();
      emit log("  Downloading from CDN...");
      downloadFromCdnUrl(idx, cdnUrl);
    });
    return;
  }

  // Unknown or unsupported source type.
  emit log("  SKIP (unsupported source type '" + src.type + "'): " + mod.name);
  m_results[idx].status = ModInstallResult::Skipped;
  ++m_currentIdx;
  installNext();
}

void CollectionInstaller::downloadFromCdnUrl(int idx, const QString& cdnUrl)
{
  const QUrl cdnQUrl(cdnUrl);
  QNetworkRequest req(cdnQUrl);
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::NoLessSafeRedirectPolicy);
  auto* reply = m_nam.get(req);

  connect(reply, &QNetworkReply::finished, this, [this, reply, idx]() {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
      m_results[idx].status = ModInstallResult::Failed;
      m_results[idx].error  = reply->errorString();
      emit modStatus(idx, ModInstallResult::Failed, reply->errorString());
      emit log("  FAILED downloading: " + reply->errorString());
      ++m_currentIdx;
      installNext();
      return;
    }

    const QString dest =
        downloadsDir() + "/" + m_manifest.mods[idx].logicalFilename;
    QFile f(dest);
    if (!f.open(QIODevice::WriteOnly)) {
      const QString err = "Cannot write " + dest;
      m_results[idx].status = ModInstallResult::Failed;
      m_results[idx].error  = err;
      emit modStatus(idx, ModInstallResult::Failed, err);
      emit log("  FAILED: " + err);
      ++m_currentIdx;
      installNext();
      return;
    }
    f.write(reply->readAll());
    f.close();
    onDownloadFinished(idx, dest);
  });
}

void CollectionInstaller::onDownloadFinished(int idx, const QString& archivePath)
{
  m_results[idx].status = ModInstallResult::Extracting;
  emit modStatus(idx, ModInstallResult::Extracting, {});
  extractMod(idx, archivePath);
}

// ─── Extract ──────────────────────────────────────────────────────────────

void CollectionInstaller::extractMod(int idx, const QString& archivePath)
{
  const CollectionMod& mod = m_manifest.mods[idx];
  const QString folderName = sanitiseFolderName(mod.folderName.isEmpty() ? mod.name : mod.folderName);
  const QString tempDir    = m_config.instanceDir + "/.tmp_extract/" + folderName;
  QDir().mkpath(tempDir);

  const QString bin = find7z();
  if (bin.isEmpty()) {
    const QString err = "7z not found — install p7zip to extract mods";
    m_results[idx].status = ModInstallResult::Failed;
    m_results[idx].error  = err;
    emit modStatus(idx, ModInstallResult::Failed, err);
    emit log("  FAILED: " + err);
    ++m_currentIdx;
    installNext();
    return;
  }

  auto* proc = new QProcess(this);
  proc->start(bin, {"x", "-y", "-o" + tempDir, archivePath});

  connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
      this, [this, proc, idx, archivePath, tempDir](int exitCode, QProcess::ExitStatus) {
        proc->deleteLater();
        if (exitCode != 0) {
          const QString err = "Extraction failed for " + QFileInfo(archivePath).fileName();
          m_results[idx].status = ModInstallResult::Failed;
          m_results[idx].error  = err;
          emit modStatus(idx, ModInstallResult::Failed, err);
          emit log("  FAILED: " + err);
          QDir(tempDir).removeRecursively();
          ++m_currentIdx;
          installNext();
          return;
        }
        finishExtract(idx, archivePath, tempDir);
      });
}

void CollectionInstaller::finishExtract(int idx,
                                        const QString& /*archivePath*/,
                                        const QString& tempDir)
{
  const CollectionMod& mod = m_manifest.mods[idx];

  // Determine the payload root (handle archives with a single top-level dir).
  QString payloadRoot = tempDir;
  {
    const QFileInfoList entries =
        QDir(tempDir).entryInfoList(QDir::NoDotAndDotDot | QDir::Dirs | QDir::Files);
    if (entries.size() == 1 && entries.first().isDir())
      payloadRoot = entries.first().absoluteFilePath();
  }

  // Apply FOMOD if choices are present.
  const QString sourceDir = applyFomod(idx, payloadRoot);

  // Final mod folder.
  const QString folderName = sanitiseFolderName(mod.folderName.isEmpty() ? mod.name : mod.folderName);
  const QString destDir    = modsDir() + "/" + folderName;
  QDir(destDir).removeRecursively();
  QDir().mkpath(destDir);

  // Copy payload → destDir.
  if (!copyDir(sourceDir, destDir)) {
    const QString err = "Failed copying files to " + destDir;
    m_results[idx].status = ModInstallResult::Failed;
    m_results[idx].error  = err;
    emit modStatus(idx, ModInstallResult::Failed, err);
    emit log("  FAILED: " + err);
    QDir(tempDir).removeRecursively();
    ++m_currentIdx;
    installNext();
    return;
  }

  routeRootFiles(destDir, m_config.gameDomain);

  // Write meta.ini so Fluorine knows the Nexus IDs.
  {
    const auto& src = mod.source;
    if (src.modId > 0 || src.fileId > 0) {
      QSettings meta(destDir + "/meta.ini", QSettings::IniFormat);
      meta.setValue("General/modid",  src.modId);
      meta.setValue("General/fileid", src.fileId);
      meta.setValue("General/version", mod.version);
      meta.setValue("General/installedFiles/1/modId",  src.modId);
      meta.setValue("General/installedFiles/1/fileId", src.fileId);
    }
  }

  QDir(tempDir).removeRecursively();

  m_results[idx].status = ModInstallResult::Done;
  emit modStatus(idx, ModInstallResult::Done, {});
  emit log("  OK: " + folderName);

  ++m_currentIdx;
  installNext();
}

// ─── FOMOD ────────────────────────────────────────────────────────────────

QString CollectionInstaller::applyFomod(int idx, const QString& extractedRoot)
{
  const CollectionMod& mod = m_manifest.mods[idx];
  if (!mod.choices.has_value() || !mod.choices->hasChoices())
    return extractedRoot; // No FOMOD — use the whole root.

  // Look for fomod/ModuleConfig.xml (case-insensitive on Linux).
  QString configPath;
  {
    QDirIterator it(extractedRoot, QDir::Files | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
      const QString p = it.next();
      if (p.endsWith("/fomod/ModuleConfig.xml", Qt::CaseInsensitive)) {
        configPath = p;
        break;
      }
    }
  }
  if (configPath.isEmpty())
    return extractedRoot; // Archive has no FOMOD despite recorded choices.

  const QString fomodDir = QFileInfo(configPath).absolutePath(); // …/fomod/
  const QString dataRoot  = QFileInfo(fomodDir).absolutePath();  // payload root

  // Temp destination for FOMOD-selected files.
  const QString fomodDest = extractedRoot + "/../.fomod_out_" + mod.folderName;
  QDir().mkpath(fomodDest);

  if (executeFomod(dataRoot, fomodDest, *mod.choices)) {
    emit log(QString("  FOMOD applied (%1 choices)").arg(mod.choices->options.size()));
    return fomodDest;
  }

  // Fall back to the full payload if FOMOD execution fails.
  QDir(fomodDest).removeRecursively();
  return extractedRoot;
}

// FOMOD XML parsing and execution.
// Handles: requiredInstallFiles, installSteps / optionalFileGroups,
// conditionalFileInstalls.  Flag evaluation is best-effort.
bool CollectionInstaller::executeFomod(const QString& dataRoot,
                                       const QString& destDir,
                                       const CollectionFomodChoices& choices)
{
  // Helper: copy a file or folder entry from the FOMOD spec.
  auto installEntry = [&](const QString& source, const QString& destination) {
    const QString srcPath = QDir(dataRoot).filePath(
        QString(source).replace('\\', '/'));
    const QString dstPath = QDir(destDir).filePath(
        QString(destination.isEmpty() ? source : destination).replace('\\', '/'));

    const QFileInfo fi(srcPath);
    if (fi.isDir()) {
      copyDir(srcPath, dstPath);
    } else if (fi.isFile()) {
      QFileInfo dstInfo(dstPath);
      QDir().mkpath(dstInfo.absolutePath());
      QFile::copy(srcPath, dstPath);
    }
  };

  // Active flags: name → value (simplified — we treat all conditions as met
  // when a plugin is explicitly chosen by the collection).
  QHash<QString, QString> flags;

  QFile f(QDir(dataRoot).filePath("fomod/ModuleConfig.xml"));
  if (!f.open(QIODevice::ReadOnly))
    return false;

  QXmlStreamReader xml(&f);

  // Parse state.
  struct FlagDep { QString flag; QString value; };
  struct StepCtx {
    QString stepName;
    QString groupName;
    int pluginIdx = 0;
    bool inRequiredFiles    = false;
    bool inInstallSteps     = false;
    bool inConditional      = false;
    bool inPattern          = false;
    bool patternSatisfied   = false;
    bool inPatternDeps      = false;
    QString patternDepsOp;         // "And" or "Or"
    QVector<FlagDep> patternDeps;
    QString currentSource;
    QString currentDest;
  } ctx;

  auto isPluginSelected = [&](const QString& stepName,
                               const QString& groupName,
                               const QString& pluginName,
                               int pluginIdx) -> bool {
    // Try step+group match, then group-only fallback (CLF3 strategy).
    for (const auto& step : choices.options) {
      if (!step.name.compare(stepName, Qt::CaseInsensitive) || stepName.isEmpty()) {
        for (const auto& group : step.groups) {
          if (!group.name.compare(groupName, Qt::CaseInsensitive)) {
            for (const auto& choice : group.choices) {
              if (!choice.name.compare(pluginName, Qt::CaseInsensitive) ||
                  (choice.idx == pluginIdx && !choice.name.isEmpty()))
                return true;
            }
          }
        }
      }
    }
    // Group-only fallback.
    for (const auto& step : choices.options) {
      for (const auto& group : step.groups) {
        if (!group.name.compare(groupName, Qt::CaseInsensitive)) {
          for (const auto& choice : group.choices) {
            if (!choice.name.compare(pluginName, Qt::CaseInsensitive) ||
                (choice.idx == pluginIdx && !choice.name.isEmpty()))
              return true;
          }
        }
      }
    }
    return false;
  };

  bool inSelectedPlugin = false;
  QString currentStepName, currentGroupName, currentPluginName;
  int currentPluginIdx = 0;
  QStringList conditionalFiles_src, conditionalFiles_dst;

  while (!xml.atEnd()) {
    xml.readNext();
    if (xml.isStartElement()) {
      const QString tag = xml.name().toString();
      if (tag == QLatin1String("requiredInstallFiles")) {
        ctx.inRequiredFiles = true;
      } else if (tag == QLatin1String("installSteps")) {
        ctx.inRequiredFiles = false;
        ctx.inInstallSteps = true;
      } else if (tag == QLatin1String("conditionalFileInstalls")) {
        ctx.inInstallSteps = false;
        ctx.inConditional = true;
      } else if (tag == QLatin1String("installStep")) {
        currentStepName  = xml.attributes().value("name").toString();
        currentPluginIdx = 0;
      } else if (tag == QLatin1String("group")) {
        currentGroupName = xml.attributes().value("name").toString();
        currentPluginIdx = 0;
      } else if (tag == QLatin1String("plugin")) {
        currentPluginName  = xml.attributes().value("name").toString();
        inSelectedPlugin   = isPluginSelected(currentStepName, currentGroupName,
                                              currentPluginName, currentPluginIdx);
        ++currentPluginIdx;
      } else if (tag == QLatin1String("flag") && inSelectedPlugin) {
        const QString fname = xml.attributes().value("name").toString();
        const QString fval  = xml.readElementText();
        flags[fname] = fval;
      } else if (tag == QLatin1String("pattern")) {
        ctx.inPattern        = true;
        ctx.patternSatisfied = true; // overwritten by </dependencies> evaluation
        ctx.patternDeps.clear();
        ctx.patternDepsOp.clear();
        conditionalFiles_src.clear();
        conditionalFiles_dst.clear();
      } else if (tag == QLatin1String("dependencies") && ctx.inConditional && ctx.inPattern) {
        ctx.inPatternDeps  = true;
        ctx.patternDepsOp  = xml.attributes().value("operator").toString();
        ctx.patternDeps.clear();
      } else if (tag == QLatin1String("flagDependency") && ctx.inPatternDeps) {
        ctx.patternDeps.append({
            xml.attributes().value("flag").toString(),
            xml.attributes().value("value").toString()
        });
      } else if ((tag == QLatin1String("file") || tag == QLatin1String("folder"))) {
        const QString src = xml.attributes().value("source").toString();
        const QString dst = xml.attributes().value("destination").toString();
        if (ctx.inRequiredFiles) {
          installEntry(src, dst);
        } else if (ctx.inInstallSteps && inSelectedPlugin) {
          installEntry(src, dst);
        } else if (ctx.inConditional && ctx.inPattern) {
          conditionalFiles_src.append(src);
          conditionalFiles_dst.append(dst);
        }
      }
    } else if (xml.isEndElement()) {
      const QString tag = xml.name().toString();
      if (tag == QLatin1String("requiredInstallFiles")) {
        ctx.inRequiredFiles = false;
      } else if (tag == QLatin1String("installSteps")) {
        ctx.inInstallSteps = false;
      } else if (tag == QLatin1String("conditionalFileInstalls")) {
        ctx.inConditional = false;
      } else if (tag == QLatin1String("plugin")) {
        inSelectedPlugin  = false;
        currentPluginName.clear();
      } else if (tag == QLatin1String("dependencies") && ctx.inConditional && ctx.inPattern) {
        ctx.inPatternDeps = false;
        if (!ctx.patternDeps.isEmpty()) {
          const bool isOr = ctx.patternDepsOp.compare("Or", Qt::CaseInsensitive) == 0;
          if (isOr) {
            ctx.patternSatisfied = false;
            for (const auto& dep : ctx.patternDeps) {
              if (flags.value(dep.flag) == dep.value) { ctx.patternSatisfied = true; break; }
            }
          } else { // "And" or absent
            ctx.patternSatisfied = true;
            for (const auto& dep : ctx.patternDeps) {
              if (flags.value(dep.flag) != dep.value) { ctx.patternSatisfied = false; break; }
            }
          }
        }
      } else if (tag == QLatin1String("pattern")) {
        if (ctx.patternSatisfied) {
          for (int i = 0; i < conditionalFiles_src.size(); ++i)
            installEntry(conditionalFiles_src[i], conditionalFiles_dst[i]);
        }
        ctx.inPattern = false;
      }
    }
  }

  return !xml.hasError();
}

// ─── Root file routing ────────────────────────────────────────────────────

// Per-game root file rules (mirrors CLF3 root_files.rs).
bool CollectionInstaller::isRootFile(const QString& filename,
                                     const QString& gameDomain)
{
  const QString lower = filename.toLower();

  if (gameDomain == QLatin1String("skyrimspecialedition") ||
      gameDomain == QLatin1String("skyrim") ||
      gameDomain == QLatin1String("fallout4") ||
      gameDomain == QLatin1String("fallout3") ||
      gameDomain == QLatin1String("newvegas") ||
      gameDomain == QLatin1String("oblivion") ||
      gameDomain == QLatin1String("starfield")) {
    // SKSE / NVSE / F4SE / OBSE versioned binaries.
    if ((lower.startsWith("skse64_") || lower.startsWith("skse_")    ||
         lower.startsWith("nvse_")   || lower.startsWith("f4se_")    ||
         lower.startsWith("obse_"))  &&
        (lower.endsWith(".dll") || lower.endsWith(".exe")))
      return true;

    // ENB files (shader/config).
    if (lower.startsWith("enb") &&
        (lower.endsWith(".fx")  || lower.endsWith(".ini") ||
         lower.endsWith(".bmp") || lower.endsWith(".dds") ||
         lower.endsWith(".tga")))
      return true;

    // DirectX hooks.
    if (lower == "d3d11.dll" || lower == "d3dx9_42.dll"   ||
        lower == "d3dcompiler_47.dll" || lower == "d3dcompiler_46e.dll")
      return true;

    // Engine Fixes TOML, CCC list.
    if (lower == "engine_fixes.toml" || lower == "skyrim.ccc" ||
        lower == "fallout4.ccc")
      return true;
  }
  return false;
}

bool CollectionInstaller::isRootFolder(const QString& folderName,
                                       const QString& gameDomain)
{
  const QString lower = folderName.toLower();
  Q_UNUSED(gameDomain);
  if (lower == "enbseries" || lower == "reshade-shaders" ||
      lower == "reshade-presets")
    return true;
  return false;
}

void CollectionInstaller::routeRootFiles(const QString& modDir,
                                         const QString& gameDomain)
{
  const QString rootDir = modDir + "/Root";
  bool movedAny = false;

  QDirIterator it(modDir, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
  while (it.hasNext()) {
    it.next();
    const QFileInfo fi = it.fileInfo();
    const QString name = fi.fileName();

    // Skip the Root/ directory itself and meta.ini.
    if (name == QLatin1String("Root") || name == QLatin1String("meta.ini"))
      continue;

    bool move = false;
    if (fi.isFile())
      move = isRootFile(name, gameDomain);
    else if (fi.isDir())
      move = isRootFolder(name, gameDomain);

    if (move) {
      if (!movedAny) {
        QDir().mkpath(rootDir);
        movedAny = true;
      }
      QFile::rename(fi.absoluteFilePath(), rootDir + "/" + name);
    }
  }
}

// ─── Finalise ─────────────────────────────────────────────────────────────

void CollectionInstaller::finalise()
{
  emit log("Writing instance configuration...");

  // ── modlist.txt ──────────────────────────────────────────────────────────
  {
    QFile f(profileDir() + "/modlist.txt");
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
      QTextStream ts(&f);
      // Mods are written in reverse priority order (highest first = top of list).
      // We simply reverse the JSON order (collection defines load order).
      for (int i = m_results.size() - 1; i >= 0; --i) {
        const CollectionMod& mod = m_manifest.mods[i];
        const ModInstallResult& res = m_results[i];
        const QString folderName = sanitiseFolderName(mod.folderName.isEmpty() ? mod.name : mod.folderName);
        const bool enabled       = (res.status == ModInstallResult::Done);
        ts << (enabled ? '+' : '-') << folderName << '\n';
      }
      // Overwrite separator (MO2 expects this at the end).
      ts << "*Overwrite\n";
    }
  }

  // ── plugins.txt ──────────────────────────────────────────────────────────
  {
    QFile f(profileDir() + "/plugins.txt");
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
      QTextStream ts(&f);
      for (const auto& plugin : m_manifest.plugins) {
        ts << (plugin.enabled ? '*' : ' ') << plugin.name << '\n';
      }
    }
  }

  // ── loadorder.txt (for games that need it) ───────────────────────────────
  {
    QFile f(profileDir() + "/loadorder.txt");
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
      QTextStream ts(&f);
      for (const auto& plugin : m_manifest.plugins)
        ts << plugin.name << '\n';
    }
  }

  // ── ModOrganizer.ini (portable or global) ────────────────────────────────
  {
    const QString iniPath = m_config.portable
        ? m_config.instanceDir + "/ModOrganizer.ini"
        : m_config.instanceDir + "/ModOrganizer.ini";

    QSettings ini(iniPath, QSettings::IniFormat);

    // [General]
    ini.setValue("General/gameName",         NexusCollections::gameNameForDomain(m_config.gameDomain));
    ini.setValue("General/selectedProfileName", "Default");
    ini.setValue("General/gamePath",         m_config.gamePath);
    ini.setValue("General/firstStart",       false);

    // [Paths]
    ini.setValue("Settings/mod_directory",
                 m_config.instanceDir + "/mods");
    ini.setValue("Settings/download_directory", m_config.downloadsDir);
    ini.setValue("Settings/profiles_directory",
                 m_config.instanceDir + "/profiles");
    ini.setValue("Settings/overwrite_directory",
                 m_config.instanceDir + "/overwrite");

    ini.sync();
  }

  // ── overwrite dir ────────────────────────────────────────────────────────
  QDir().mkpath(m_config.instanceDir + "/overwrite");

  // ── Register with InstanceManager ────────────────────────────────────────
  if (m_config.portable) {
    const auto launcher = portable_launcher_script::create(m_config.instanceDir);
    if (launcher.status == portable_launcher_script::Status::Failed) {
      emit log(QStringLiteral("Warning: %1").arg(launcher.error));
    }

    // Portable instances are discovered by path — just record the path in
    // the registered portables list so the manager shows them.
    InstanceManager::registerPortableInstance(m_config.instanceDir);
  }

  // Count stats.
  int done = 0, skipped = 0, failed = 0;
  for (const auto& r : m_results) {
    if (r.status == ModInstallResult::Done)    ++done;
    else if (r.status == ModInstallResult::Skipped) ++skipped;
    else if (r.status == ModInstallResult::Failed)  ++failed;
  }

  emit log(QString("Collection installed: %1 mods installed, %2 skipped, %3 failed.")
               .arg(done).arg(skipped).arg(failed));
  emit progress(m_results.size(), m_results.size());
  emit finished(m_config.instanceDir);
}

// ─── Utilities ────────────────────────────────────────────────────────────

QString CollectionInstaller::find7z()
{
  const QString bundled = fluorineDataDir() + "/bin/7zz";
  if (QFile::exists(bundled))
    return bundled;
  for (const QString& n : {QStringLiteral("7z"), QStringLiteral("7za"),
                            QStringLiteral("7zz")}) {
    const QString found = QStandardPaths::findExecutable(n);
    if (!found.isEmpty())
      return found;
  }
  return {};
}

bool CollectionInstaller::copyDir(const QString& src, const QString& dst)
{
  QDir srcDir(src);
  if (!srcDir.exists())
    return false;
  QDir().mkpath(dst);
  for (const QFileInfo& fi :
       srcDir.entryInfoList(QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs)) {
    const QString target = dst + "/" + fi.fileName();
    if (fi.isDir()) {
      if (!copyDir(fi.absoluteFilePath(), target))
        return false;
    } else {
      if (QFile::exists(target))
        QFile::remove(target);
      if (!QFile::copy(fi.absoluteFilePath(), target))
        return false;
    }
  }
  return true;
}
