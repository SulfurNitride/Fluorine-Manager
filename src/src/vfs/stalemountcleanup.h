#ifndef VFS_STALEMOUNTCLEANUP_H
#define VFS_STALEMOUNTCLEANUP_H

#include <QByteArray>
#include <QString>
#include <QVector>

#include <optional>

namespace stale_mount_cleanup {

struct MountEntry {
  quint64 mountId = 0;
  QString mountPoint;
  QString fsType;
  QString source;

  bool operator==(const MountEntry &) const = default;
};

QVector<MountEntry> parseMountInfo(const QByteArray &contents);

bool isFluorineFuseMount(const MountEntry &entry);
bool isDisconnectedProbeError(int error);

// Returns the deepest mount containing the requested path only when that layer
// has one unambiguous mount-table entry, Fluorine's FUSE identity, and no
// mounted descendants. Stacked and nested mounts deliberately fail closed:
// lazy-unmounting the parent would also detach a healthy child filesystem.
std::optional<MountEntry>
uniqueContainingFluorineMount(const QVector<MountEntry> &entries,
                              const QString &path);

bool containsSameMount(const QVector<MountEntry> &entries,
                       const MountEntry &expected);

} // namespace stale_mount_cleanup

#endif // VFS_STALEMOUNTCLEANUP_H
