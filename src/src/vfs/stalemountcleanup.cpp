#include "stalemountcleanup.h"

#include <QDir>

#include <algorithm>
#include <cerrno>

namespace stale_mount_cleanup {
namespace {

QString decodeMountField(const QByteArray &input) {
  QByteArray output;
  output.reserve(input.size());

  for (qsizetype i = 0; i < input.size();) {
    if (input[i] == '\\' && i + 3 < input.size() && input[i + 1] >= '0' &&
        input[i + 1] <= '7' && input[i + 2] >= '0' && input[i + 2] <= '7' &&
        input[i + 3] >= '0' && input[i + 3] <= '7') {
      const int value = ((input[i + 1] - '0') << 6) |
                        ((input[i + 2] - '0') << 3) | (input[i + 3] - '0');
      output.append(static_cast<char>(value));
      i += 4;
      continue;
    }

    output.append(input[i]);
    ++i;
  }

  return QString::fromUtf8(output);
}

} // namespace

QVector<MountEntry> parseMountInfo(const QByteArray &contents) {
  QVector<MountEntry> result;

  for (const QByteArray &rawLine : contents.split('\n')) {
    const QByteArray line = rawLine.trimmed();
    if (line.isEmpty()) {
      continue;
    }

    const QList<QByteArray> fields = line.split(' ');
    const qsizetype separator = fields.indexOf(QByteArrayLiteral("-"));
    if (separator < 6 || separator + 3 >= fields.size()) {
      continue;
    }

    bool idValid = false;
    const quint64 id = fields[0].toULongLong(&idValid);
    if (!idValid || id == 0 || fields[4].isEmpty() ||
        fields[separator + 1].isEmpty() || fields[separator + 2].isEmpty()) {
      continue;
    }

    result.push_back({id, QDir::cleanPath(decodeMountField(fields[4])),
                      decodeMountField(fields[separator + 1]),
                      decodeMountField(fields[separator + 2])});
  }

  return result;
}

bool isFluorineFuseMount(const MountEntry &entry) {
  return entry.source == QStringLiteral("mo2linux") &&
         (entry.fsType == QStringLiteral("fuse") ||
          entry.fsType == QStringLiteral("fuse.mo2linux"));
}

bool isDisconnectedProbeError(int error) { return error == ENOTCONN; }

std::optional<MountEntry>
uniqueContainingFluorineMount(const QVector<MountEntry> &entries,
                              const QString &path) {
  const QString cleanPath = QDir::cleanPath(path);
  if (cleanPath.isEmpty() || cleanPath == QStringLiteral(".") ||
      cleanPath == QDir::rootPath()) {
    return std::nullopt;
  }

  qsizetype deepestLength = -1;
  for (const MountEntry &entry : entries) {
    const bool containsPath =
        cleanPath == entry.mountPoint ||
        cleanPath.startsWith(entry.mountPoint + QDir::separator());
    if (!containsPath) {
      continue;
    }

    deepestLength = std::max(deepestLength, entry.mountPoint.size());
  }

  if (deepestLength < 0) {
    return std::nullopt;
  }

  std::optional<MountEntry> match;
  for (const MountEntry &entry : entries) {
    if (entry.mountPoint.size() != deepestLength ||
        (cleanPath != entry.mountPoint &&
         !cleanPath.startsWith(entry.mountPoint + QDir::separator()))) {
      continue;
    }

    // More than one deepest entry means the top layer is stacked. Never peel a
    // mount stack based on an ambiguous path probe.
    if (match.has_value()) {
      return std::nullopt;
    }
    match = entry;
  }

  if (!match.has_value() || !isFluorineFuseMount(*match)) {
    return std::nullopt;
  }
  if (match->mountPoint == QDir::rootPath()) {
    return std::nullopt;
  }

  const QString descendantPrefix = match->mountPoint + QDir::separator();
  for (const MountEntry &entry : entries) {
    if (entry.mountPoint.startsWith(descendantPrefix)) {
      return std::nullopt;
    }
  }
  return match;
}

bool containsSameMount(const QVector<MountEntry> &entries,
                       const MountEntry &expected) {
  for (const MountEntry &entry : entries) {
    if (entry == expected) {
      return true;
    }
  }
  return false;
}

} // namespace stale_mount_cleanup
