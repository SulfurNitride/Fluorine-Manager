#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

struct CuratedGuidePageSource
{
  QString path;
  QString sha256;
};

struct CuratedGuideArtifact
{
  enum class SourceType { Nexus, Direct, Manual };

  QString id;
  QString name;
  SourceType sourceType{SourceType::Manual};
  QString domain;
  int modId{0};
  int fileId{0};
  QString fileLabel;
  QString fileCategory;
  QString filename;
  QString url;
  QString sourceUrl;
  QString version;
  QString sha256;
  qint64 size{0};
  bool latestCompatible{false};
  QString minimumVersion;
};

struct CuratedGuideAction
{
  QString id;
  QString type;
  QString name;
  QStringList dependsOn;
  QString artifact;
  QJsonObject condition;
  QJsonObject parameters;
  QJsonObject validation;
  bool required{true};
};

struct CuratedGuideRecipe
{
  static constexpr int CurrentSchemaVersion = 1;

  int schemaVersion{0};
  QString id;
  QString displayName;
  QString version;
  QString description;
  QString gamePlugin;
  QString guideUrl;
  QString sourceRepository;
  QString sourceCommit;
  QString updatedAt;
  QString artworkUrl;
  QString artworkSha256;
  qint64 estimatedDownloadSize{0};
  qint64 estimatedInstallSize{0};
  QString sizeEstimateNote;
  QVector<CuratedGuidePageSource> pages;
  QStringList supportedStores;
  QStringList requiredGames;
  QVector<CuratedGuideArtifact> artifacts;
  QVector<CuratedGuideAction> actions;
  QJsonObject profile;
  // SHA-256 of the exact reviewed manifest plus its optional artifact lock.
  // It deliberately changes when install behavior changes, even if the guide
  // source revision itself did not.
  QString contentDigest;

  bool isValid() const;
  const CuratedGuideArtifact* artifact(const QString& artifactId) const;
  const CuratedGuideAction* action(const QString& actionId) const;
  QString digest() const;
  QString legacyDigest() const;
  bool matchesDigest(const QString& candidate) const;

  static CuratedGuideRecipe fromJson(const QByteArray& json, QString* error = nullptr);
};

class CuratedGuideCatalog
{
public:
  static QVector<CuratedGuideRecipe> bundled(QStringList* errors = nullptr);
};
