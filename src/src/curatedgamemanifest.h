#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

struct CuratedManifestChunk
{
  qint64 size{0};
  QByteArray md5;
};

struct CuratedManifestFileVariant
{
  qint64 size{0};
  QByteArray digest;
  QString digestAlgorithm;
  QVector<CuratedManifestChunk> chunks;
};

struct CuratedManifestFile
{
  QString path;
  QVector<CuratedManifestFileVariant> variants;
  bool executable{false};
};

struct CuratedGameManifest
{
  QString store;
  QString gameId;
  QString buildId;
  QStringList manifestIds;
  QVector<CuratedManifestFile> files;

  bool isValid() const { return !store.isEmpty() && !gameId.isEmpty() && !files.isEmpty(); }
  QJsonObject provenance() const;
};

struct CuratedManifestResolution
{
  bool success{false};
  CuratedGameManifest manifest;
  QString error;
};

struct CuratedVerifiedCopyResult
{
  bool success{false};
  QString error;
  QStringList unexpectedFiles;
  QJsonObject provenance;
};

CuratedManifestResolution resolveCuratedGameManifest(const QString& source,
                                                      const QString& requestedStore);
CuratedVerifiedCopyResult copyCuratedGameFromManifest(
    const QString& source, const QString& destination,
    const CuratedGameManifest& manifest,
    bool excludeGuideManagedNewVegasFiles = true);

// Exposed for deterministic parser tests and for future store helper integrations.
CuratedManifestResolution parseSteamDepotManifestData(const QByteArray& data,
                                                       const QString& depotId);
CuratedManifestResolution parseEpicBuildPatchManifestData(const QByteArray& data,
                                                           const QString& appName,
                                                           const QString& version);
