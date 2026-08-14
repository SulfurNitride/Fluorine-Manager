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

[[nodiscard]] ReadResult read(const QString& path);
[[nodiscard]] int countStarred(QByteArrayView contents);
[[nodiscard]] bool isSuspiciousActiveDrop(int profileCount, int candidateCount);
[[nodiscard]] bool isSameFile(const QString& path, const Snapshot& snapshot);
[[nodiscard]] bool publish(MOBase::TransactionalWriteFile& transaction,
                           const Snapshot& snapshot, QString& error);

}  // namespace WinePluginListSync

#endif  // WINEPLUGINLISTSYNC_H
