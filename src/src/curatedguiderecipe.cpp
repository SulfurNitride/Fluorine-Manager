#include "curatedguiderecipe.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QUrl>

namespace
{
QStringList stringList(const QJsonValue& value)
{
  QStringList result;
  for (const auto& entry : value.toArray()) {
    if (entry.isString()) {
      result.push_back(entry.toString());
    }
  }
  return result;
}

CuratedGuideArtifact::SourceType sourceType(const QString& value, bool* ok)
{
  *ok = true;
  if (value == "nexus") {
    return CuratedGuideArtifact::SourceType::Nexus;
  }
  if (value == "direct") {
    return CuratedGuideArtifact::SourceType::Direct;
  }
  if (value == "manual") {
    return CuratedGuideArtifact::SourceType::Manual;
  }
  *ok = false;
  return CuratedGuideArtifact::SourceType::Manual;
}

bool safeRelativePath(const QString& path)
{
  if (path.isEmpty() || path.startsWith('/') || path.startsWith('\\')) {
    return false;
  }
  QString normalized = path;
  normalized.replace('\\', '/');
  const auto parts = normalized.split('/', Qt::SkipEmptyParts);
  return !parts.contains("..");
}
}

bool CuratedGuideRecipe::isValid() const
{
  return schemaVersion == CurrentSchemaVersion && !id.isEmpty() && !displayName.isEmpty()
         && !version.isEmpty() && !actions.isEmpty();
}

const CuratedGuideArtifact*
CuratedGuideRecipe::artifact(const QString& artifactId) const
{
  for (const auto& candidate : artifacts) {
    if (candidate.id == artifactId) {
      return &candidate;
    }
  }
  return nullptr;
}

const CuratedGuideAction* CuratedGuideRecipe::action(const QString& actionId) const
{
  for (const auto& candidate : actions) {
    if (candidate.id == actionId) {
      return &candidate;
    }
  }
  return nullptr;
}

QString CuratedGuideRecipe::digest() const
{
  return contentDigest;
}

QString CuratedGuideRecipe::legacyDigest() const
{
  QJsonObject identity;
  identity["schemaVersion"] = schemaVersion;
  identity["id"]            = id;
  identity["version"]       = version;
  identity["sourceCommit"]  = sourceCommit;
  QJsonArray sourcePages;
  for (const auto& page : pages)
    sourcePages.push_back(QJsonObject{{"path", page.path}, {"sha256", page.sha256}});
  identity["pages"] = sourcePages;
  return QString::fromLatin1(QCryptographicHash::hash(
      QJsonDocument(identity).toJson(QJsonDocument::Compact),
      QCryptographicHash::Sha256).toHex());
}

bool CuratedGuideRecipe::matchesDigest(const QString& candidate) const
{
  // Accept pre-catalog jobs once, then reconcileStateWithRecipe() persists the
  // full manifest+lock digest. New jobs never write the legacy digest.
  return candidate == digest() || candidate == legacyDigest();
}

CuratedGuideRecipe CuratedGuideRecipe::fromJson(const QByteArray& json, QString* error)
{
  auto fail = [error](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return CuratedGuideRecipe{};
  };

  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    return fail(QString("Invalid recipe JSON: %1").arg(parseError.errorString()));
  }

  const QJsonObject root = document.object();
  CuratedGuideRecipe recipe;
  recipe.contentDigest = QString::fromLatin1(
      QCryptographicHash::hash(json, QCryptographicHash::Sha256).toHex());
  recipe.schemaVersion    = root.value("schemaVersion").toInt();
  recipe.id               = root.value("id").toString();
  recipe.displayName      = root.value("displayName").toString();
  recipe.version          = root.value("version").toString();
  recipe.description      = root.value("description").toString();
  recipe.gamePlugin       = root.value("gamePlugin").toString();
  recipe.guideUrl         = root.value("guideUrl").toString();
  recipe.sourceRepository = root.value("sourceRepository").toString();
  recipe.sourceCommit     = root.value("sourceCommit").toString();
  const QJsonObject metadata = root.value("metadata").toObject();
  recipe.updatedAt        = metadata.value("updatedAt").toString();
  const QJsonObject artwork = metadata.value("artwork").toObject();
  recipe.artworkUrl       = artwork.value("url").toString();
  recipe.artworkSha256    = artwork.value("sha256").toString().toLower();
  const QJsonObject estimates = metadata.value("sizeEstimates").toObject();
  recipe.estimatedDownloadSize =
      estimates.value("downloadBytes").toVariant().toLongLong();
  recipe.estimatedInstallSize =
      estimates.value("installedBytes").toVariant().toLongLong();
  recipe.sizeEstimateNote = estimates.value("note").toString();
  recipe.supportedStores  = stringList(root.value("supportedStores"));
  recipe.requiredGames    = stringList(root.value("requiredGames"));
  recipe.profile          = root.value("profile").toObject();

  if (recipe.schemaVersion != CurrentSchemaVersion) {
    return fail(QString("Unsupported recipe schema %1").arg(recipe.schemaVersion));
  }
  if (recipe.id.isEmpty() || recipe.displayName.isEmpty() || recipe.version.isEmpty()) {
    return fail("Recipe id, displayName, and version are required");
  }
  if ((!recipe.artworkUrl.isEmpty()
       && (!QUrl(recipe.artworkUrl).isValid() || !recipe.artworkUrl.startsWith("https://")
           || recipe.artworkSha256.size() != 64))
      || recipe.estimatedDownloadSize < 0 || recipe.estimatedInstallSize < 0) {
    return fail(QString("Recipe %1 contains invalid catalog metadata").arg(recipe.id));
  }

  auto validModlist = [](const QStringList& modlist) {
    QSet<QString> separatorNames;
    for (const auto& line : modlist) {
      const QString entry =
          (!line.isEmpty() && (line.front() == '+' || line.front() == '-'
                               || line.front() == '*'))
              ? line.mid(1).trimmed()
              : line.trimmed();
      // Overwrite is MO2's reserved, automatically managed pseudo-mod. It must
      // never be serialized by a curated recipe as a regular profile entry.
      if (entry.compare(QStringLiteral("overwrite"), Qt::CaseInsensitive) == 0)
        return false;
      if (!line.endsWith("_separator")) continue;
      const QString name =
          line.mid(1, line.size() - 1 - QString("_separator").size()).trimmed();
      const QString key = name.toCaseFolded();
      if ((line.front() != '+' && line.front() != '-') || name.isEmpty()
          || key == QStringLiteral("create separator")
          || separatorNames.contains(key))
        return false;
      separatorNames.insert(key);
    }
    return true;
  };
  if (!validModlist(stringList(recipe.profile.value("modlist"))))
    return fail(QString("Invalid or duplicate profile separator in recipe %1")
                    .arg(recipe.id));
  QSet<QString> profileNames;
  for (const auto& value : recipe.profile.value("profiles").toArray()) {
    const QJsonObject profile = value.toObject();
    const QString name = profile.value("name").toString();
    const QString key = name.toCaseFolded();
    if (!safeRelativePath(name) || profileNames.contains(key)
        || !validModlist(stringList(profile.value("modlist"))))
      return fail(QString("Invalid or duplicate generated profile in recipe %1")
                      .arg(recipe.id));
    profileNames.insert(key);
  }

  for (const auto& value : root.value("pages").toArray()) {
    const auto object = value.toObject();
    const CuratedGuidePageSource page{object.value("path").toString(),
                                      object.value("sha256").toString()};
    if (!safeRelativePath(page.path) || page.sha256.size() != 64) {
      return fail(QString("Invalid source page in recipe %1").arg(recipe.id));
    }
    recipe.pages.push_back(page);
  }

  QSet<QString> artifactIds;
  for (const auto& value : root.value("artifacts").toArray()) {
    const auto object = value.toObject();
    CuratedGuideArtifact artifact;
    artifact.id               = object.value("id").toString();
    artifact.name             = object.value("name").toString();
    artifact.domain           = object.value("domain").toString();
    artifact.modId            = object.value("modId").toInt();
    artifact.fileId           = object.value("fileId").toInt();
    artifact.fileLabel        = object.value("fileLabel").toString();
    artifact.fileCategory     = object.value("fileCategory").toString();
    artifact.filename         = object.value("filename").toString();
    artifact.url              = object.value("url").toString();
    artifact.sourceUrl        = object.value("sourceUrl").toString();
    artifact.version          = object.value("version").toString();
    artifact.sha256           = object.value("sha256").toString().toLower();
    artifact.size             = object.value("size").toVariant().toLongLong();
    artifact.latestCompatible = object.value("latestCompatible").toBool(false);
    artifact.minimumVersion   = object.value("minimumVersion").toString();
    bool validSource          = false;
    artifact.sourceType = sourceType(object.value("source").toString(), &validSource);
    if (artifact.id.isEmpty() || artifactIds.contains(artifact.id) || !validSource
        || !safeRelativePath(artifact.filename)) {
      return fail(QString("Invalid or duplicate artifact in recipe %1").arg(recipe.id));
    }
    if (!artifact.sha256.isEmpty() && artifact.sha256.size() != 64) {
      return fail(QString("Artifact %1 has an invalid SHA-256").arg(artifact.id));
    }
    if (artifact.sourceType == CuratedGuideArtifact::SourceType::Nexus
        && (artifact.domain.isEmpty() || artifact.modId <= 0
            || (artifact.fileId <= 0 && artifact.fileLabel.isEmpty()
                && !artifact.latestCompatible))) {
      return fail(QString("Nexus artifact %1 is incomplete").arg(artifact.id));
    }
    if (artifact.sourceType == CuratedGuideArtifact::SourceType::Nexus
        && artifact.fileId <= 0 && !artifact.fileLabel.isEmpty()
        && artifact.fileCategory.isEmpty() && !artifact.latestCompatible) {
      return fail(QString("Nexus artifact %1 has no guide file category")
                      .arg(artifact.id));
    }
    artifactIds.insert(artifact.id);
    recipe.artifacts.push_back(artifact);
  }

  static const QSet<QString> allowedActions = {
      "acquire",       "copy_game",      "extract",       "install_mod",
      "install_root",  "run_native",     "run_proton",    "assisted_tool",
      "edit_ini",      "write_profile",   "validate_profile", "first_launch"};
  QSet<QString> actionIds;
  for (const auto& value : root.value("actions").toArray()) {
    const auto object = value.toObject();
    CuratedGuideAction action;
    action.id         = object.value("id").toString();
    action.type       = object.value("type").toString();
    action.name       = object.value("name").toString(action.id);
    action.dependsOn  = stringList(object.value("dependsOn"));
    action.artifact   = object.value("artifact").toString();
    action.condition  = object.value("condition").toObject();
    action.parameters = object.value("parameters").toObject();
    action.validation = object.value("validation").toObject();
    action.required   = object.value("required").toBool(true);
    if (action.id.isEmpty() || actionIds.contains(action.id)
        || !allowedActions.contains(action.type)) {
      return fail(QString("Invalid or duplicate action in recipe %1").arg(recipe.id));
    }
    if (!action.artifact.isEmpty() && !artifactIds.contains(action.artifact)) {
      return fail(QString("Action %1 references unknown artifact %2")
                      .arg(action.id, action.artifact));
    }
    if ((action.type == "install_mod" || action.type == "install_root")
        && !safeRelativePath(action.parameters.value("folder").toString())) {
      return fail(QString("Action %1 has an unsafe mod folder").arg(action.id));
    }
    if (action.type == "copy_game"
        && !safeRelativePath(action.parameters.value("destination").toString())) {
      return fail(QString("Action %1 has an unsafe game destination").arg(action.id));
    }
    if (action.type == "edit_ini"
        && (!safeRelativePath(action.parameters.value("path").toString())
            || action.parameters.value("values").toObject().isEmpty())) {
      return fail(QString("Action %1 has invalid INI edit parameters").arg(action.id));
    }
    if (action.type == "write_profile"
        && !safeRelativePath(action.parameters.value("profileName").toString("Default"))) {
      return fail(QString("Action %1 has an unsafe profile name").arg(action.id));
    }
    actionIds.insert(action.id);
    recipe.actions.push_back(action);
  }
  for (const auto& action : recipe.actions) {
    for (const auto& dependency : action.dependsOn) {
      if (!actionIds.contains(dependency) || dependency == action.id) {
        return fail(QString("Action %1 has invalid dependency %2")
                        .arg(action.id, dependency));
      }
    }
  }

  // Kahn's algorithm also rejects indirect cycles.
  QSet<QString> resolved;
  while (resolved.size() < recipe.actions.size()) {
    bool changed = false;
    for (const auto& action : recipe.actions) {
      if (resolved.contains(action.id)) {
        continue;
      }
      bool ready = true;
      for (const auto& dependency : action.dependsOn) {
        if (!resolved.contains(dependency)) {
          ready = false;
          break;
        }
      }
      if (ready) {
        resolved.insert(action.id);
        changed = true;
      }
    }
    if (!changed) {
      return fail(QString("Recipe %1 contains an action dependency cycle").arg(recipe.id));
    }
  }

  if (!recipe.isValid()) {
    return fail(QString("Recipe %1 contains no install actions").arg(recipe.id));
  }
  if (error != nullptr) {
    error->clear();
  }
  return recipe;
}

QVector<CuratedGuideRecipe> CuratedGuideCatalog::bundled(QStringList* errors)
{
  const QString base = ":/fluorine/curated-guides/";
  QVector<CuratedGuideRecipe> recipes;
  QFile catalogFile(base + "catalog.json");
  if (!catalogFile.open(QIODevice::ReadOnly)) {
    if (errors) errors->push_back("Cannot open bundled curated-guide catalog");
    return recipes;
  }
  QJsonParseError parseError;
  const auto catalog = QJsonDocument::fromJson(catalogFile.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !catalog.isObject()
      || catalog.object().value("schemaVersion").toInt() != 1) {
    if (errors) errors->push_back("Bundled curated-guide catalog is invalid");
    return recipes;
  }
  QSet<QString> catalogIds;
  for (const auto& value : catalog.object().value("recipes").toArray()) {
    const QJsonObject entry = value.toObject();
    const QString expectedId = entry.value("id").toString();
    const QString relativePath = entry.value("manifest").toString();
    const QString expectedHash = entry.value("manifestSha256").toString().toLower();
    if (expectedId.isEmpty() || catalogIds.contains(expectedId)
        || !safeRelativePath(relativePath) || expectedHash.size() != 64) {
      if (errors) errors->push_back("Bundled curated-guide catalog entry is invalid");
      continue;
    }
    catalogIds.insert(expectedId);
    const QString path = base + relativePath;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      if (errors != nullptr) {
        errors->push_back(QString("Cannot open bundled recipe %1").arg(path));
      }
      continue;
    }
    const QByteArray manifestBytes = file.readAll();
    const QString actualHash = QString::fromLatin1(
        QCryptographicHash::hash(manifestBytes, QCryptographicHash::Sha256).toHex());
    if (actualHash != expectedHash) {
      if (errors) errors->push_back(QString("%1: manifest SHA-256 does not match catalog").arg(path));
      continue;
    }
    QString error;
    auto recipe = CuratedGuideRecipe::fromJson(manifestBytes, &error);
    if (!recipe.isValid()) {
      if (errors != nullptr) {
        errors->push_back(QString("%1: %2").arg(path, error));
      }
      continue;
    }
    if (recipe.id != expectedId) {
      if (errors) errors->push_back(QString("%1: manifest id does not match catalog").arg(path));
      continue;
    }

    const QString lockRelativePath = entry.value("lock").toString();
    if (!lockRelativePath.isEmpty()) {
      const QString expectedLockHash = entry.value("lockSha256").toString().toLower();
      if (!safeRelativePath(lockRelativePath) || expectedLockHash.size() != 64) {
        if (errors) errors->push_back(QString("%1: artifact lock entry is invalid").arg(path));
        continue;
      }
      QFile lockFile(base + lockRelativePath);
      if (!lockFile.open(QIODevice::ReadOnly)) {
        if (errors) errors->push_back(QString("Cannot open bundled artifact lock %1").arg(lockRelativePath));
        continue;
      }
      const QByteArray lockBytes = lockFile.readAll();
      const QString actualLockHash = QString::fromLatin1(
          QCryptographicHash::hash(lockBytes, QCryptographicHash::Sha256).toHex());
      QJsonParseError lockParseError;
      const auto lockDocument = QJsonDocument::fromJson(lockBytes, &lockParseError);
      const QJsonObject lock = lockDocument.object();
      if (actualLockHash != expectedLockHash
          || lockParseError.error != QJsonParseError::NoError
          || !lockDocument.isObject() || lock.value("schemaVersion").toInt() != 1
          || lock.value("recipeId").toString() != recipe.id
          || lock.value("recipeVersion").toString() != recipe.version
          || lock.value("sourceCommit").toString() != recipe.sourceCommit) {
        if (errors) errors->push_back(QString("%1: artifact lock is invalid or stale").arg(lockRelativePath));
        continue;
      }
      bool lockValid = true;
      const QJsonObject lockedArtifacts = lock.value("artifacts").toObject();
      for (auto it = lockedArtifacts.begin(); it != lockedArtifacts.end(); ++it) {
        const QJsonObject pinned = it.value().toObject();
        const QString pinnedFilename = pinned.value("filename").toString();
        const QString pinnedSha256 = pinned.value("sha256").toString().toLower();
        CuratedGuideArtifact* artifact = nullptr;
        for (auto& candidate : recipe.artifacts) {
          if (candidate.id == it.key()) {
            artifact = &candidate;
            break;
          }
        }
        if (!artifact || artifact->sourceType != CuratedGuideArtifact::SourceType::Nexus
            || pinned.value("domain").toString() != artifact->domain
            || pinned.value("modId").toInt() != artifact->modId
            || pinned.value("fileId").toInt() <= 0
            || (!pinnedFilename.isEmpty() && !safeRelativePath(pinnedFilename))
            || (!pinnedSha256.isEmpty() && pinnedSha256.size() != 64)) {
          lockValid = false;
          break;
        }
        artifact->fileId = pinned.value("fileId").toInt();
        if (!pinnedFilename.isEmpty()) artifact->filename = pinnedFilename;
        if (!pinned.value("version").toString().isEmpty())
          artifact->version = pinned.value("version").toString();
        if (pinned.value("size").toVariant().toLongLong() > 0)
          artifact->size = pinned.value("size").toVariant().toLongLong();
        if (!pinnedSha256.isEmpty()) artifact->sha256 = pinnedSha256;
      }
      if (!lockValid) {
        if (errors) errors->push_back(QString("%1: artifact lock contains an invalid pin").arg(lockRelativePath));
        continue;
      }
      recipe.contentDigest = QString::fromLatin1(QCryptographicHash::hash(
          manifestBytes + QByteArray(1, '\0') + lockBytes,
          QCryptographicHash::Sha256).toHex());
    }
    recipes.push_back(std::move(recipe));
  }
  return recipes;
}
