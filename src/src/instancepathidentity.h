#pragma once

#include <QString>
#include <QStringList>
#include <cstdint>

namespace instance_path {

// A followed directory identity captured at a point in time. Device and inode
// identify the actual directory object on Linux, including through symlink and
// bind-mount aliases.
struct DirectoryIdentity {
  QString absolutePath;
  std::uintmax_t device{0};
  std::uintmax_t inode{0};
  bool valid{false};
};

enum class DirectoryRelation {
  Same,
  Different,
  Indeterminate,
};

DirectoryIdentity captureDirectory(const QString &path);

// Compares two captured directory objects. Invalid identities never match.
bool equivalent(const DirectoryIdentity &lhs, const DirectoryIdentity &rhs);

// Verifies that path still names the directory captured in identity.
bool stillNames(const DirectoryIdentity &identity, const QString &path);

enum class MutationStatus {
  Safe,
  TargetChanged,
  Active,
  Indeterminate,
};

// Authorizes a mutation against the selected directory snapshot. An empty
// active path means no instance is active; otherwise active identity must be
// proven different.
MutationStatus mutationStatus(const DirectoryIdentity &selectedIdentity,
                              const QString &selectedPath,
                              const QString &activePath);

// Returns Indeterminate unless both paths currently resolve to directories.
DirectoryRelation relation(const QString &lhs, const QString &rhs);

// Runtime instance-path comparison. Existing directories compare by physical
// identity; missing paths compare only by cleaned absolute spelling.
bool sameDirectoryOrPath(const QString &lhs, const QString &rhs);

enum class PathOverlap {
  Separate,
  Overlaps,
  Indeterminate,
};

enum class PathContainment {
  Contained,
  Separate,
  Indeterminate,
};

// Determines whether an existing path is equal to or below an existing
// directory after symlinks are resolved.
PathContainment contains(const QString &directory, const QString &path);

// Determines whether two existing filesystem objects overlap as deletion
// trees. Equal objects, and a directory containing the other path, overlap.
PathOverlap overlap(const QString &lhs, const QString &rhs);

struct PathOverlapResult {
  PathOverlap status{PathOverlap::Separate};
  QString selectedPath;
  QString protectedPath;
};

PathOverlapResult firstOverlap(const QStringList &selectedPaths,
                               const QStringList &protectedPaths);

} // namespace instance_path
