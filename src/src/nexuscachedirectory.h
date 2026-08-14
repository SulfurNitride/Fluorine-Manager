#ifndef NEXUSCACHEDIRECTORY_H
#define NEXUSCACHEDIRECTORY_H

#include <QString>

class QNetworkAccessManager;

namespace NexusCacheDirectory
{
enum class Status
{
  Ready,
  InvalidRoot,
  Collision,
  IoError,
  UnexpectedCache
};

struct Result
{
  Status status{Status::InvalidRoot};
  QString path;
  QString errorPath;

  explicit operator bool() const noexcept { return status == Status::Ready; }
};

// Prepares the application-owned Nexus network-cache directory below an
// existing configured cache root. The configured root itself is never used as
// a QNetworkDiskCache directory.
Result prepare(const QString& configuredRoot);

// Configures the manager with a disk cache at the prepared child. On failure,
// an existing accepted cache is retained so in-flight replies cannot outlive
// the cache devices they are using.
Result configure(QNetworkAccessManager& manager, const QString& configuredRoot,
                 const QString& quarantineTemplate = {});

// Clears only an attached Nexus disk cache. Other cache implementations are
// deliberately left alone.
bool clear(QNetworkAccessManager& manager);
}

#endif  // NEXUSCACHEDIRECTORY_H
