#ifndef CATEGORYPERSISTENCE_H
#define CATEGORYPERSISTENCE_H

#include <QByteArray>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QLockFile>
#include <QSaveFile>
#include <QString>
#include <QStringList>

#include <algorithm>
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

inline QByteArray storageVersion(const Snapshot& categories,
                                 const Snapshot& nexusMap)
{
  QByteArray identity;
  QDataStream stream(&identity, QIODevice::WriteOnly);
  stream.setVersion(QDataStream::Qt_6_0);
  stream << categories.existed << categories.data << nexusMap.existed
         << nexusMap.data;
  if (stream.status() != QDataStream::Ok) {
    return {};
  }
  return QCryptographicHash::hash(identity, QCryptographicHash::Sha256);
}

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

inline QString journalPath(const QString& categoriesPath)
{
  return categoriesPath + QStringLiteral(".transaction");
}

inline QString lockPath(const QString& categoriesPath)
{
  return journalPath(categoriesPath) + QStringLiteral(".lock");
}

inline bool writeAtomic(const QString& path, const QByteArray& data)
{
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  return file.write(data) == data.size() && file.commit();
}

inline std::optional<QByteArray> readBounded(QFile& file, qint64 maximumSize)
{
  if (file.size() < 0 || file.size() > maximumSize) {
    return std::nullopt;
  }

  QByteArray data;
  for (;;) {
    const qint64 remaining = maximumSize - data.size();
    const qint64 requestSize =
        std::min<qint64>(64 * 1024, remaining + 1);
    const QByteArray chunk = file.read(requestSize);
    if (chunk.isEmpty()) {
      if (file.error() != QFileDevice::NoError || !file.atEnd()) {
        return std::nullopt;
      }
      break;
    }
    data.append(chunk);
    if (data.size() > maximumSize) {
      return std::nullopt;
    }
    if (file.atEnd()) {
      break;
    }
  }
  if (file.error() != QFileDevice::NoError) {
    return std::nullopt;
  }
  return data;
}

inline std::optional<Snapshot> snapshot(const QString& path)
{
  if (!QFileInfo::exists(path)) {
    return Snapshot{};
  }

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return std::nullopt;
  }
  const auto data = readBounded(file, MaximumCategoryFileSize);
  if (!data) {
    return std::nullopt;
  }
  return Snapshot{true, *data};
}

inline bool restore(const QString& path, const Snapshot& state)
{
  if (state.existed) {
    return writeAtomic(path, state.data);
  }
  return !QFileInfo::exists(path) || QFile::remove(path);
}

inline bool writeJournal(const QString& path, const Snapshot& categories,
                         const Snapshot& nexusMap)
{
  QByteArray payload;
  QBuffer payloadBuffer(&payload);
  if (!payloadBuffer.open(QIODevice::WriteOnly)) {
    return false;
  }
  QDataStream payloadStream(&payloadBuffer);
  payloadStream.setVersion(QDataStream::Qt_6_0);
  payloadStream << categories.existed << categories.data << nexusMap.existed
                << nexusMap.data;
  payloadBuffer.close();
  if (payloadStream.status() != QDataStream::Ok) {
    return false;
  }

  const QByteArray checksum =
      QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
  QByteArray journal;
  QBuffer journalBuffer(&journal);
  if (!journalBuffer.open(QIODevice::WriteOnly)) {
    return false;
  }
  QDataStream stream(&journalBuffer);
  stream.setVersion(QDataStream::Qt_6_0);
  stream << JournalMagic << JournalVersion << payload << checksum;
  journalBuffer.close();
  return stream.status() == QDataStream::Ok &&
         journal.size() <= MaximumJournalSize && writeAtomic(path, journal);
}

inline JournalReadResult readJournal(const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {JournalReadStatus::IoError, std::nullopt};
  }
  if (file.size() > MaximumJournalSize) {
    return {JournalReadStatus::Invalid, std::nullopt};
  }
  const auto journalData = readBounded(file, MaximumJournalSize);
  if (!journalData) {
    return {JournalReadStatus::IoError, std::nullopt};
  }
  QByteArray journal = *journalData;

  QBuffer journalBuffer(&journal);
  if (!journalBuffer.open(QIODevice::ReadOnly)) {
    return {JournalReadStatus::IoError, std::nullopt};
  }
  QDataStream stream(&journalBuffer);
  stream.setVersion(QDataStream::Qt_6_0);
  quint32 magic = 0;
  quint16 version = 0;
  QByteArray payload;
  QByteArray checksum;
  stream >> magic >> version >> payload >> checksum;
  if (stream.status() != QDataStream::Ok || !journalBuffer.atEnd() ||
      magic != JournalMagic || version != JournalVersion ||
      checksum !=
          QCryptographicHash::hash(payload, QCryptographicHash::Sha256)) {
    return {JournalReadStatus::Invalid, std::nullopt};
  }

  QBuffer payloadBuffer(&payload);
  if (!payloadBuffer.open(QIODevice::ReadOnly)) {
    return {JournalReadStatus::IoError, std::nullopt};
  }
  QDataStream payloadStream(&payloadBuffer);
  payloadStream.setVersion(QDataStream::Qt_6_0);
  Snapshot categories;
  Snapshot nexusMap;
  payloadStream >> categories.existed >> categories.data >> nexusMap.existed >>
      nexusMap.data;
  if (payloadStream.status() != QDataStream::Ok || !payloadBuffer.atEnd() ||
      categories.data.size() > MaximumCategoryFileSize ||
      nexusMap.data.size() > MaximumCategoryFileSize) {
    return {JournalReadStatus::Invalid, std::nullopt};
  }
  return {JournalReadStatus::Success,
          std::pair{std::move(categories), std::move(nexusMap)}};
}

inline std::optional<QString> quarantineJournal(const QString& path)
{
  QString quarantinePath = path + QStringLiteral(".corrupt");
  for (int suffix = 1; QFileInfo::exists(quarantinePath); ++suffix) {
    quarantinePath = path + QStringLiteral(".corrupt.%1").arg(suffix);
  }
  if (!QFile::rename(path, quarantinePath)) {
    return std::nullopt;
  }
  return quarantinePath;
}

inline std::optional<QString> quarantinedJournalPath(const QString& path)
{
  const QFileInfo journal(path);
  const QStringList matches = journal.dir().entryList(
      {journal.fileName() + QStringLiteral(".corrupt*")}, QDir::Files,
      QDir::Name);
  if (matches.isEmpty()) {
    return std::nullopt;
  }
  return journal.dir().filePath(matches.front());
}

inline bool pathPresent(const QString& path)
{
  const QFileInfo info(path);
  return info.exists() || info.isSymLink();
}

inline QString nextRecoveryPath(const QString& path)
{
  QString backup = path + QStringLiteral(".recovery");
  for (int suffix = 1; pathPresent(backup); ++suffix) {
    backup = path + QStringLiteral(".recovery.%1").arg(suffix);
  }
  return backup;
}

inline bool renamePath(const QString& source, const QString& destination)
{
  const QFileInfo info(source);
  if (info.isDir() && !info.isSymLink()) {
    return QDir().rename(source, destination);
  }
  return QFile::rename(source, destination);
}

// Explicit recovery is backup-first: move every live category/transaction
// artifact aside under the same lock, rolling moves back if any one fails.
// A crash part-way through remains fail-closed because either a transaction
// marker or an incomplete established pair is left behind.
inline bool resetFiles(const QString& categoriesPath,
                       const QString& nexusMapPath,
                       QStringList* backupPaths = nullptr)
{
  if (backupPaths) {
    backupPaths->clear();
  }

  QLockFile lock(lockPath(categoriesPath));
  lock.setStaleLockTime(TransactionLockStaleMs);
  if (!lock.tryLock(TransactionLockTimeoutMs)) {
    return false;
  }

  const QString transactionPath = journalPath(categoriesPath);
  QStringList sources{categoriesPath, nexusMapPath, transactionPath};
  const QFileInfo transactionInfo(transactionPath);
  const QStringList quarantined = transactionInfo.dir().entryList(
      {transactionInfo.fileName() + QStringLiteral(".corrupt*")},
      QDir::Files, QDir::Name);
  for (const auto& fileName : quarantined) {
    sources.push_back(transactionInfo.dir().filePath(fileName));
  }

  std::vector<std::pair<QString, QString>> moved;
  for (const auto& source : sources) {
    if (!pathPresent(source)) {
      continue;
    }
    // A preserved corrupt journal must leave the *.transaction.corrupt*
    // blocker namespace, otherwise the recovery backup would itself keep the
    // instance blocked.
    const QString backupBase =
        source.startsWith(transactionPath + QStringLiteral(".corrupt"))
            ? transactionPath + QStringLiteral(".quarantined")
            : source;
    const QString backup = nextRecoveryPath(backupBase);
    if (!renamePath(source, backup)) {
      for (auto it = moved.rbegin(); it != moved.rend(); ++it) {
        renamePath(it->second, it->first);
      }
      return false;
    }
    moved.emplace_back(source, backup);
  }

  if (backupPaths) {
    for (const auto& [unused, backup] : moved) {
      (void)unused;
      backupPaths->push_back(backup);
    }
  }
  return true;
}

inline bool recoverFilesUnlocked(const QString& categoriesPath,
                                 const QString& nexusMapPath,
                                 QString* quarantinedJournal = nullptr)
{
  if (quarantinedJournal) {
    quarantinedJournal->clear();
  }
  const QString transactionPath = journalPath(categoriesPath);
  if (const auto existing = quarantinedJournalPath(transactionPath)) {
    if (quarantinedJournal) {
      *quarantinedJournal = *existing;
    }
    return false;
  }
  if (!QFileInfo::exists(transactionPath)) {
    return true;
  }

  const auto journal = readJournal(transactionPath);
  if (journal.status == JournalReadStatus::IoError) {
    return false;
  }
  if (journal.status == JournalReadStatus::Invalid) {
    const auto quarantinePath = quarantineJournal(transactionPath);
    if (!quarantinePath) {
      return false;
    }
    if (quarantinedJournal) {
      *quarantinedJournal = *quarantinePath;
    }
    return false;
  }

  const auto& snapshots = *journal.snapshots;
  if (!restore(categoriesPath, snapshots.first) ||
      !restore(nexusMapPath, snapshots.second)) {
    return false;
  }
  return QFile::remove(transactionPath);
}

inline bool recoverFiles(const QString& categoriesPath,
                         const QString& nexusMapPath,
                         QString* quarantinedJournal = nullptr)
{
  QLockFile lock(lockPath(categoriesPath));
  lock.setStaleLockTime(TransactionLockStaleMs);
  if (!lock.tryLock(TransactionLockTimeoutMs)) {
    return false;
  }
  return recoverFilesUnlocked(categoriesPath, nexusMapPath,
                              quarantinedJournal);
}

inline std::optional<std::pair<Snapshot, Snapshot>> readFiles(
    const QString& categoriesPath, const QString& nexusMapPath,
    QString* quarantinedJournal = nullptr)
{
  QLockFile lock(lockPath(categoriesPath));
  lock.setStaleLockTime(TransactionLockStaleMs);
  if (!lock.tryLock(TransactionLockTimeoutMs) ||
      !recoverFilesUnlocked(categoriesPath, nexusMapPath,
                            quarantinedJournal)) {
    return std::nullopt;
  }

  const auto categories = snapshot(categoriesPath);
  const auto nexusMap   = snapshot(nexusMapPath);
  if (!categories || !nexusMap) {
    return std::nullopt;
  }
  return std::pair{*categories, *nexusMap};
}

inline WriteResult writeFiles(const QString& categoriesPath,
                              const QByteArray& categoriesData,
                              const QString& nexusMapPath,
                              const QByteArray& nexusMapData,
                              const QByteArray& expectedVersion = {})
{
  if (categoriesData.size() > MaximumCategoryFileSize) {
    return WriteResult::CategoriesFailed;
  }
  if (nexusMapData.size() > MaximumCategoryFileSize) {
    return WriteResult::NexusMapFailed;
  }

  QLockFile lock(lockPath(categoriesPath));
  lock.setStaleLockTime(TransactionLockStaleMs);
  if (!lock.tryLock(TransactionLockTimeoutMs) ||
      !recoverFilesUnlocked(categoriesPath, nexusMapPath)) {
    return WriteResult::CategoriesFailed;
  }

  const auto oldCategories = snapshot(categoriesPath);
  if (!oldCategories) {
    return WriteResult::CategoriesFailed;
  }
  const auto oldNexusMap = snapshot(nexusMapPath);
  if (!oldNexusMap) {
    return WriteResult::NexusMapFailed;
  }
  if (!expectedVersion.isEmpty() &&
      storageVersion(*oldCategories, *oldNexusMap) != expectedVersion) {
    return WriteResult::Conflict;
  }

  const QString transactionPath = journalPath(categoriesPath);
  if (!writeJournal(transactionPath, *oldCategories, *oldNexusMap)) {
    return WriteResult::CategoriesFailed;
  }
  if (!writeAtomic(categoriesPath, categoriesData)) {
    recoverFilesUnlocked(categoriesPath, nexusMapPath);
    return WriteResult::CategoriesFailed;
  }
  if (!writeAtomic(nexusMapPath, nexusMapData)) {
    recoverFilesUnlocked(categoriesPath, nexusMapPath);
    return WriteResult::NexusMapFailed;
  }
  if (!QFile::remove(transactionPath)) {
    recoverFilesUnlocked(categoriesPath, nexusMapPath);
    return WriteResult::NexusMapFailed;
  }
  return WriteResult::Success;
}
}  // namespace CategoryPersistence

#endif  // CATEGORYPERSISTENCE_H
