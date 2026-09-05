#pragma once

#include <QString>
#include <QVector>
#include <QSettings>
#include <memory>

namespace Clf3InstallUtils
{
// Explicit application-owned store; independent of QApplication organization metadata.
std::unique_ptr<QSettings> openSettings(const QString& configRoot = {});

struct SpaceRequirement
{
  QString volume;
  QString purpose;
  qint64 available{-1};
  qint64 required{0};
};

// Aggregate paths sharing a device before comparing with user-available bytes.
QVector<SpaceRequirement> combineSpaceRequirements(
    const QVector<SpaceRequirement>& requirements);
qint64 temporarySpaceEstimate(qint64 archives, qint64 installed);
QString prepareWritableDirectory(const QString& path);
bool pathsOverlap(const QString& first, const QString& second);
QString redactLog(QString text);
bool rootDeploymentMatches(const QString& checkpoint, const QString& jobId,
                           const QString& source, const QString& destination);
bool saveRootDeployment(const QString& checkpoint, const QString& jobId,
                        const QString& source, const QString& destination);
}
