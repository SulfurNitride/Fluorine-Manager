#include "curatedguideinstallstate.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <blake3.h>

#include <array>
#include <algorithm>

namespace
{
QString statusName(CuratedActionStatus status)
{
  switch (status) {
  case CuratedActionStatus::Pending: return "pending";
  case CuratedActionStatus::Running: return "running";
  case CuratedActionStatus::WaitingForUser: return "waiting_for_user";
  case CuratedActionStatus::Complete: return "complete";
  case CuratedActionStatus::Failed: return "failed";
  case CuratedActionStatus::Skipped: return "skipped";
  }
  return "pending";
}

CuratedActionStatus parseStatus(const QString& value)
{
  if (value == "running") return CuratedActionStatus::Running;
  if (value == "waiting_for_user") return CuratedActionStatus::WaitingForUser;
  if (value == "complete") return CuratedActionStatus::Complete;
  if (value == "failed") return CuratedActionStatus::Failed;
  if (value == "skipped") return CuratedActionStatus::Skipped;
  return CuratedActionStatus::Pending;
}

void hashBytes(blake3_hasher& hasher, const QByteArray& value)
{
  blake3_hasher_update(&hasher, value.constData(), static_cast<size_t>(value.size()));
}
}

CuratedActionRecord* CuratedGuideInstallState::action(const QString& id)
{
  for (auto& record : actions) if (record.id == id) return &record;
  return nullptr;
}

const CuratedActionRecord* CuratedGuideInstallState::action(const QString& id) const
{
  for (const auto& record : actions) if (record.id == id) return &record;
  return nullptr;
}

CuratedArtifactRecord* CuratedGuideInstallState::artifact(const QString& id)
{
  for (auto& record : artifacts) if (record.id == id) return &record;
  return nullptr;
}

const CuratedArtifactRecord* CuratedGuideInstallState::artifact(const QString& id) const
{
  for (const auto& record : artifacts) if (record.id == id) return &record;
  return nullptr;
}

QJsonObject CuratedGuideInstallState::toJson() const
{
  QJsonObject root{{"formatVersion", formatVersion},
                   {"jobId", jobId},
                   {"recipeId", recipeId},
                   {"recipeVersion", recipeVersion},
                   {"recipeDigest", recipeDigest},
                   {"guideSourceCommit", guideSourceCommit},
                   {"instanceName", instanceName},
                   {"instancePath", instancePath},
                   {"downloadsPath", downloadsPath},
                   {"createdAt", createdAt},
                   {"updatedAt", updatedAt},
                   {"overallStatus", overallStatus},
                   {"options", options}};
  QJsonArray artifactArray;
  for (const auto& record : artifacts) {
    artifactArray.push_back(QJsonObject{{"id", record.id},
                                        {"path", record.path},
                                        {"version", record.version},
                                        {"sha256", record.sha256},
                                        {"size", record.size}});
  }
  root["artifacts"] = artifactArray;
  QJsonArray actionArray;
  for (const auto& record : actions) {
    actionArray.push_back(QJsonObject{{"id", record.id},
                                      {"status", statusName(record.status)},
                                      {"error", record.error},
                                      {"outputDigest", record.outputDigest},
                                      {"startedAt", record.startedAt},
                                      {"completedAt", record.completedAt},
                                      {"provenance", record.provenance}});
  }
  root["actions"] = actionArray;
  return root;
}

CuratedGuideInstallState
CuratedGuideInstallState::fromJson(const QJsonObject& root, QString* error)
{
  CuratedGuideInstallState state;
  state.formatVersion     = root.value("formatVersion").toInt();
  state.jobId             = root.value("jobId").toString();
  state.recipeId          = root.value("recipeId").toString();
  state.recipeVersion     = root.value("recipeVersion").toString();
  state.recipeDigest      = root.value("recipeDigest").toString();
  state.guideSourceCommit = root.value("guideSourceCommit").toString();
  state.instanceName      = root.value("instanceName").toString();
  state.instancePath      = root.value("instancePath").toString();
  state.downloadsPath     = root.value("downloadsPath").toString();
  state.createdAt         = root.value("createdAt").toString();
  state.updatedAt         = root.value("updatedAt").toString();
  state.overallStatus     = root.value("overallStatus").toString("pending");
  state.options           = root.value("options").toObject();
  if (state.formatVersion != CurrentFormatVersion || state.jobId.isEmpty()
      || state.recipeId.isEmpty() || state.instancePath.isEmpty()) {
    if (error) *error = "Invalid or unsupported curated install state";
    return {};
  }
  for (const auto& value : root.value("artifacts").toArray()) {
    const auto object = value.toObject();
    state.artifacts.push_back({object.value("id").toString(),
                               object.value("path").toString(),
                               object.value("version").toString(),
                               object.value("sha256").toString(),
                               object.value("size").toVariant().toLongLong()});
  }
  for (const auto& value : root.value("actions").toArray()) {
    const auto object = value.toObject();
    state.actions.push_back({object.value("id").toString(),
                             parseStatus(object.value("status").toString()),
                             object.value("error").toString(),
                             object.value("outputDigest").toString(),
                             object.value("startedAt").toString(),
                             object.value("completedAt").toString(),
                             object.value("provenance").toObject()});
  }
  if (error) error->clear();
  return state;
}

bool CuratedGuideInstallState::save(const QString& path, QString* error)
{
  updatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    if (error) *error = file.errorString();
    return false;
  }
  if (file.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented)) < 0
      || !file.commit()) {
    if (error) *error = file.errorString();
    return false;
  }
  if (error) error->clear();
  return true;
}

CuratedGuideInstallState CuratedGuideInstallState::load(const QString& path,
                                                        QString* error)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error) *error = file.errorString();
    return {};
  }
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    if (error) *error = parseError.errorString();
    return {};
  }
  return fromJson(document.object(), error);
}

QString curatedSha256File(const QString& path, QString* error)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error) *error = file.errorString();
    return {};
  }
  QCryptographicHash hasher(QCryptographicHash::Sha256);
  if (!hasher.addData(&file)) {
    if (error) *error = file.errorString();
    return {};
  }
  if (error) error->clear();
  return QString::fromLatin1(hasher.result().toHex());
}

QString curatedBlake3Tree(const QString& path, QString* error)
{
  QDir root(path);
  if (!root.exists()) {
    if (error) *error = QString("Directory does not exist: %1").arg(path);
    return {};
  }
  QStringList files;
  QDirIterator iterator(path, QDir::Files | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    files.push_back(root.relativeFilePath(iterator.next()).replace('\\', '/'));
  }
  std::sort(files.begin(), files.end(), [](const QString& left, const QString& right) {
    return left.toUtf8() < right.toUtf8();
  });

  blake3_hasher tree;
  blake3_hasher_init(&tree);
  std::array<char, 1024 * 1024> buffer{};
  for (const auto& relative : files) {
    const QByteArray name = relative.toUtf8();
    hashBytes(tree, QByteArray::number(name.size()));
    hashBytes(tree, name);
    QFile file(root.filePath(relative));
    if (!file.open(QIODevice::ReadOnly)) {
      if (error) *error = file.errorString();
      return {};
    }
    while (!file.atEnd()) {
      const qint64 count = file.read(buffer.data(), buffer.size());
      if (count < 0) {
        if (error) *error = file.errorString();
        return {};
      }
      blake3_hasher_update(&tree, buffer.data(), static_cast<size_t>(count));
    }
  }
  std::array<unsigned char, BLAKE3_OUT_LEN> digest{};
  blake3_hasher_finalize(&tree, digest.data(), digest.size());
  if (error) error->clear();
  return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(digest.data()),
                                        static_cast<qsizetype>(digest.size()))
                                 .toHex());
}
