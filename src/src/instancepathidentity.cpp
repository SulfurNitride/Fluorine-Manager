#include "instancepathidentity.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <sys/stat.h>

namespace instance_path {

DirectoryIdentity captureDirectory(const QString &path) {
  DirectoryIdentity result;
  if (path.isEmpty()) {
    return result;
  }

  result.absolutePath = QDir::cleanPath(QFileInfo(path).absoluteFilePath());

  struct stat info{};
  const QByteArray encoded = QFile::encodeName(result.absolutePath);
  if (::stat(encoded.constData(), &info) != 0 || !S_ISDIR(info.st_mode)) {
    return result;
  }

  result.device = static_cast<std::uintmax_t>(info.st_dev);
  result.inode = static_cast<std::uintmax_t>(info.st_ino);
  result.valid = true;
  return result;
}

bool equivalent(const DirectoryIdentity &lhs, const DirectoryIdentity &rhs) {
  return lhs.valid && rhs.valid && lhs.device == rhs.device &&
         lhs.inode == rhs.inode;
}

bool stillNames(const DirectoryIdentity &identity, const QString &path) {
  return equivalent(identity, captureDirectory(path));
}

MutationStatus mutationStatus(const DirectoryIdentity &selectedIdentity,
                              const QString &selectedPath,
                              const QString &activePath) {
  if (!selectedIdentity.valid ||
      !stillNames(selectedIdentity, selectedPath)) {
    return MutationStatus::TargetChanged;
  }
  if (activePath.isEmpty()) {
    return MutationStatus::Safe;
  }

  switch (relation(selectedPath, activePath)) {
  case DirectoryRelation::Same:
    return MutationStatus::Active;
  case DirectoryRelation::Different:
    return MutationStatus::Safe;
  case DirectoryRelation::Indeterminate:
    return MutationStatus::Indeterminate;
  }

  return MutationStatus::Indeterminate;
}

DirectoryRelation relation(const QString &lhs, const QString &rhs) {
  const auto left = captureDirectory(lhs);
  const auto right = captureDirectory(rhs);
  if (!left.valid || !right.valid) {
    return DirectoryRelation::Indeterminate;
  }

  return equivalent(left, right) ? DirectoryRelation::Same
                                 : DirectoryRelation::Different;
}

bool sameDirectoryOrPath(const QString &lhs, const QString &rhs) {
  const auto left = captureDirectory(lhs);
  const auto right = captureDirectory(rhs);

  if (left.valid && right.valid) {
    return equivalent(left, right);
  }

  return !left.absolutePath.isEmpty() &&
         left.absolutePath == right.absolutePath;
}

PathContainment contains(const QString &directory, const QString &path) {
  const auto directoryIdentity = captureDirectory(directory);
  const auto pathIdentity = captureDirectory(path);
  if (equivalent(directoryIdentity, pathIdentity)) {
    return PathContainment::Contained;
  }

  const QString directoryCanonical = QFileInfo(directory).canonicalFilePath();
  const QString pathCanonical = QFileInfo(path).canonicalFilePath();
  if (!directoryIdentity.valid || directoryCanonical.isEmpty() ||
      pathCanonical.isEmpty()) {
    return PathContainment::Indeterminate;
  }

  if (directoryCanonical == pathCanonical) {
    return PathContainment::Contained;
  }

  const QString relative =
      QDir::fromNativeSeparators(QDir(directoryCanonical).relativeFilePath(pathCanonical));
  if (relative == QStringLiteral("..") ||
      relative.startsWith(QStringLiteral("../")) ||
      QDir::isAbsolutePath(relative)) {
    return PathContainment::Separate;
  }

  return PathContainment::Contained;
}

PathOverlap overlap(const QString &lhs, const QString &rhs) {
  const QString leftCanonical = QFileInfo(lhs).canonicalFilePath();
  const QString rightCanonical = QFileInfo(rhs).canonicalFilePath();
  if (leftCanonical.isEmpty() || rightCanonical.isEmpty()) {
    return PathOverlap::Indeterminate;
  }
  if (leftCanonical == rightCanonical) {
    return PathOverlap::Overlaps;
  }

  const auto leftDirectory = captureDirectory(lhs);
  const auto rightDirectory = captureDirectory(rhs);
  const auto leftContainsRight =
      leftDirectory.valid ? contains(lhs, rhs) : PathContainment::Separate;
  const auto rightContainsLeft =
      rightDirectory.valid ? contains(rhs, lhs) : PathContainment::Separate;
  if (leftContainsRight == PathContainment::Contained ||
      rightContainsLeft == PathContainment::Contained) {
    return PathOverlap::Overlaps;
  }
  if (leftContainsRight == PathContainment::Indeterminate ||
      rightContainsLeft == PathContainment::Indeterminate) {
    return PathOverlap::Indeterminate;
  }

  return PathOverlap::Separate;
}

PathOverlapResult firstOverlap(const QStringList &selectedPaths,
                               const QStringList &protectedPaths) {
  for (const QString &selected : selectedPaths) {
    for (const QString &protectedPath : protectedPaths) {
      const PathOverlap status = overlap(selected, protectedPath);
      if (status != PathOverlap::Separate) {
        return {status, selected, protectedPath};
      }
    }
  }

  return {};
}

} // namespace instance_path
