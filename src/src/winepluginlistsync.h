#ifndef WINEPLUGINLISTSYNC_H
#define WINEPLUGINLISTSYNC_H

#include <QByteArray>
#include <QByteArrayView>
#include <QDateTime>
#include <QString>

#include <optional>

namespace MOBase
{
class TransactionalWriteFile;
}

namespace WinePluginListSync
{

struct Snapshot
{
  QByteArray contents;
  QDateTime modificationTime;
  QString identity;
};

struct ReadResult
{
  std::optional<Snapshot> snapshot;
  QString error;
};

struct FamilyReadResult
{
  std::optional<Snapshot> snapshot;
  QString effectivePath;
  QString error;
};

[[nodiscard]] ReadResult read(const QString& path);
// Resolve one Windows-case-insensitive plugin-list family. Same-directory
// case aliases to one effective leaf are accepted; multiple independent
// leaves are an ambiguity and are never guessed or partially rewritten.
[[nodiscard]] FamilyReadResult readUniqueFamily(const QString& requestedPath);
[[nodiscard]] bool publishUniqueFamily(const QString& requestedPath,
                                       QByteArrayView contents, QString& error);
[[nodiscard]] int countStarred(QByteArrayView contents);
[[nodiscard]] bool isSuspiciousActiveDrop(int profileCount, int candidateCount);
[[nodiscard]] bool isSameFile(const QString& path, const Snapshot& snapshot);
[[nodiscard]] bool publish(MOBase::TransactionalWriteFile& transaction,
                           const Snapshot& snapshot, QString& error);

}  // namespace WinePluginListSync

#endif  // WINEPLUGINLISTSYNC_H
