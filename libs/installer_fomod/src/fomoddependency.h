#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace FomodDependency
{

enum class FileState
{
  Missing,
  Inactive,
  Active
};

using FileStateResolver = std::function<FileState(const QString&)>;

inline constexpr auto SnapshotKey = "dependencySnapshotV1";
inline constexpr auto ReviewReasonKey = "dependencyReviewReason";

bool conditionUsesFile(const QJsonObject& condition);

// Returns a user-facing list of meaningful changes since the snapshot baseline.
QStringList reviewReasons(const QByteArray& snapshot, const FileStateResolver& resolver);

// A new snapshot is deliberately stored unbaselined. The first post-refresh pass
// resolves it against the final installed plugin/file state and must not warn.
bool isBaselined(const QByteArray& snapshot);
QByteArray rebaseline(const QByteArray& snapshot, const FileStateResolver& resolver);

}  // namespace FomodDependency
