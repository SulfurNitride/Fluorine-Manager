#ifndef CATEGORYPERSISTENCE_H
#define CATEGORYPERSISTENCE_H

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <optional>
#include <utility>

namespace CategoryPersistence
{
enum class WriteResult
{
  Success,
  Conflict,
  CategoriesFailed,
  NexusMapFailed
};

struct Snapshot
{
  bool existed{false};
  QByteArray data;
};

enum class JournalReadStatus
{
  Success,
  IoError,
  Invalid
};

struct JournalReadResult
{
  JournalReadStatus status{JournalReadStatus::IoError};
  std::optional<std::pair<Snapshot, Snapshot>> snapshots;
};

inline constexpr quint32 JournalMagic = 0x464d4341;  // "FMCA"
inline constexpr quint16 JournalVersion = 1;
inline constexpr qint64 MaximumCategoryFileSize = 4 * 1024 * 1024;
inline constexpr qint64 MaximumJournalSize = 16 * 1024 * 1024;
inline constexpr int TransactionLockTimeoutMs = 5000;
inline constexpr int TransactionLockStaleMs = 30000;

QByteArray storageVersion(const Snapshot& categories, const Snapshot& nexusMap);
QString journalPath(const QString& categoriesPath);
QString lockPath(const QString& categoriesPath);
bool writeAtomic(const QString& path, const QByteArray& data);
std::optional<Snapshot> snapshot(const QString& path);
bool writeJournal(const QString& path, const Snapshot& categories,
                  const Snapshot& nexusMap);
JournalReadResult readJournal(const QString& path);
std::optional<QString> quarantinedJournalPath(const QString& path);
bool pathPresent(const QString& path);
bool resetFiles(const QString& categoriesPath, const QString& nexusMapPath,
                QStringList* backupPaths = nullptr);
bool recoverFiles(const QString& categoriesPath, const QString& nexusMapPath,
                  QString* quarantinedJournal = nullptr);
std::optional<std::pair<Snapshot, Snapshot>> readFiles(
    const QString& categoriesPath, const QString& nexusMapPath,
    QString* quarantinedJournal = nullptr);
WriteResult writeFiles(const QString& categoriesPath,
                       const QByteArray& categoriesData,
                       const QString& nexusMapPath,
                       const QByteArray& nexusMapData,
                       const QByteArray& expectedVersion = {});
}  // namespace CategoryPersistence

#endif  // CATEGORYPERSISTENCE_H
