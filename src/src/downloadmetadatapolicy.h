#ifndef DOWNLOADMETADATAPOLICY_H
#define DOWNLOADMETADATAPOLICY_H

#include <QSettings>
#include <QStringList>
#include <QVariantMap>

namespace DownloadMetadataPolicy {
enum class CapabilityRetention {
  Resumable,
  Retire,
};

// Mirrors the ordered download phases without depending on DownloadManager's
// large UI/network header. The production adapter statically verifies every
// corresponding enum value before converting to this type.
enum class DownloadPhase {
  Started = 0,
  Downloading,
  Canceling,
  Pausing,
  Canceled,
  Paused,
  Error,
  FetchingModInfo,
  FetchingFileInfo,
  FetchingModInfoMd5,
  NoFetch,
  Ready,
  Installed,
  Uninstalled,
};

struct LoadedCapabilities {
  QStringList urls;
  QVariantMap userData;
  bool changed{false};
  QSettings::Status status{QSettings::NoError};
};

CapabilityRetention retentionForPhase(DownloadPhase phase);

// The download inventory reserves ".unfinished" for partial files. Rewrite a
// remote basename using that suffix before it enters the path namespace.
QString unambiguousFinalBaseName(QString baseName);

// Missing or regular metadata leaves are admissible. Symlinks and other file
// types are rejected before QSettings can open or follow them.
bool isSafeMetadataLeaf(const QString &path);

// A completed byte stream becomes non-resumable only after its archive exists
// at a different path from the exact partial output captured before
// publication.
CapabilityRetention retentionAfterPublication(const QString &path,
                                              const QString &partialPath);

// Removes a retired metadata leaf without following symlinks. If unlinking a
// regular file fails, converge its capability fields in place as a fallback.
bool retireFile(const QString &path);

// Loads capability-bearing fields. Terminal metadata is converged in place by
// removing exact resume URLs and the duplicated Nexus download map while
// preserving every other setting and user-data entry.
LoadedCapabilities loadAndConverge(QSettings &settings,
                                   CapabilityRetention retention);

// Writes capability-bearing fields according to the archive's current phase.
// Callers own synchronization together with their remaining metadata fields.
void write(QSettings &settings, CapabilityRetention retention,
           const QStringList &urls, const QVariantMap &userData);
} // namespace DownloadMetadataPolicy

#endif // DOWNLOADMETADATAPOLICY_H
