#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

enum class CuratedActionStatus { Pending, Running, WaitingForUser, Complete, Failed, Skipped };

struct CuratedArtifactRecord
{
  QString id;
  QString path;
  QString version;
  QString sha256;
  qint64 size{0};
};

struct CuratedActionRecord
{
  QString id;
  CuratedActionStatus status{CuratedActionStatus::Pending};
  QString error;
  QString outputDigest;
  QString startedAt;
  QString completedAt;
  QJsonObject provenance;
};

struct CuratedGuideInstallState
{
  static constexpr int CurrentFormatVersion = 1;

  int formatVersion{CurrentFormatVersion};
  QString jobId;
  QString recipeId;
  QString recipeVersion;
  QString recipeDigest;
  QString guideSourceCommit;
  QString instanceName;
  QString instancePath;
  QString downloadsPath;
  QString createdAt;
  QString updatedAt;
  QString overallStatus{"pending"};
  QJsonObject options;
  QVector<CuratedArtifactRecord> artifacts;
  QVector<CuratedActionRecord> actions;

  CuratedActionRecord* action(const QString& id);
  const CuratedActionRecord* action(const QString& id) const;
  CuratedArtifactRecord* artifact(const QString& id);
  const CuratedArtifactRecord* artifact(const QString& id) const;

  QJsonObject toJson() const;
  static CuratedGuideInstallState fromJson(const QJsonObject& object,
                                           QString* error = nullptr);
  bool save(const QString& path, QString* error = nullptr);
  static CuratedGuideInstallState load(const QString& path, QString* error = nullptr);
};

QString curatedSha256File(const QString& path, QString* error = nullptr);
QString curatedBlake3Tree(const QString& path, QString* error = nullptr);
