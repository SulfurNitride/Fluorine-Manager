#include "profilelegacymigration.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>

#include <cerrno>
#include <cstring>
#include <optional>
#include <string>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef Q_OS_LINUX
#include <linux/fs.h>
#include <sys/syscall.h>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace ProfileLegacyMigration {
namespace {

constexpr auto IntentVersion = 1;

struct Identity {
  quint64 device = 0;
  quint64 inode = 0;
};

struct Intent {
  QString operation;
  Identity identity;
};

enum class EntryState {
  Missing,
  Valid,
  Unsafe,
  Error,
};

struct Inspection {
  EntryState state{EntryState::Error};
  Identity identity;
  QString error;
};

bool sameIdentity(const Identity &left, const Identity &right) {
  return left.device == right.device && left.inode == right.inode;
}

bool validLeaf(const QString &leaf) {
  return !leaf.isEmpty() && leaf != QStringLiteral(".") &&
         leaf != QStringLiteral("..") && !leaf.contains(u'/') &&
         !leaf.contains(u'\\');
}

#ifdef Q_OS_UNIX
Inspection inspectAt(int root, const QString &leaf, EntryKind kind) {
  const QByteArray encoded = QFile::encodeName(leaf);
  struct stat value{};
  if (::fstatat(root, encoded.constData(), &value, AT_SYMLINK_NOFOLLOW) != 0) {
    if (errno == ENOENT) {
      return {EntryState::Missing, {}, {}};
    }
    return {EntryState::Error,
            {},
            QObject::tr("Cannot inspect %1: %2")
                .arg(leaf, QString::fromLocal8Bit(std::strerror(errno)))};
  }

  const bool expected = kind == EntryKind::Directory ? S_ISDIR(value.st_mode)
                                                     : S_ISREG(value.st_mode);
  if (!expected) {
    return {EntryState::Unsafe,
            {},
            QObject::tr("Legacy migration entry is not an ordinary %1: %2")
                .arg(kind == EntryKind::Directory ? QObject::tr("directory")
                                                  : QObject::tr("file"),
                     leaf)};
  }
  return {
      EntryState::Valid,
      {static_cast<quint64>(value.st_dev), static_cast<quint64>(value.st_ino)},
      {}};
}
#elif defined(Q_OS_WIN)
Inspection inspectPath(const QString &path, EntryKind kind) {
  const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
  const DWORD attributes = ::GetFileAttributesW(native.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    if (::GetLastError() == ERROR_FILE_NOT_FOUND ||
        ::GetLastError() == ERROR_PATH_NOT_FOUND) {
      return {EntryState::Missing, {}, {}};
    }
    return {EntryState::Error, {}, QObject::tr("Cannot inspect %1").arg(path)};
  }
  const bool directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
      directory != (kind == EntryKind::Directory)) {
    return {EntryState::Unsafe,
            {},
            QObject::tr("Legacy migration entry is unsafe: %1").arg(path)};
  }

  const DWORD flags = directory ? FILE_FLAG_BACKUP_SEMANTICS : 0;
  HANDLE handle =
      ::CreateFileW(native.c_str(), FILE_READ_ATTRIBUTES,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, flags, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return {EntryState::Error, {}, QObject::tr("Cannot open %1").arg(path)};
  }
  BY_HANDLE_FILE_INFORMATION value{};
  const bool read = ::GetFileInformationByHandle(handle, &value) != FALSE;
  ::CloseHandle(handle);
  if (!read) {
    return {EntryState::Error, {}, QObject::tr("Cannot identify %1").arg(path)};
  }
  const quint64 inode = (static_cast<quint64>(value.nFileIndexHigh) << 32) |
                        static_cast<quint64>(value.nFileIndexLow);
  return {EntryState::Valid,
          {static_cast<quint64>(value.dwVolumeSerialNumber), inode},
          {}};
}
#else
Inspection inspectPath(const QString &path, EntryKind kind) {
  const QFileInfo info(path);
  if (!info.exists() && !info.isSymLink()) {
    return {EntryState::Missing, {}, {}};
  }
  const bool expected =
      kind == EntryKind::Directory ? info.isDir() : info.isFile();
  if (info.isSymLink() || !expected) {
    return {EntryState::Unsafe,
            {},
            QObject::tr("Legacy migration entry is unsafe: %1").arg(path)};
  }
  // Other supported platforms lack a portable stable file identifier in Qt.
  // The size/time tuple is used only to authenticate crash recovery; the live
  // no-replace move is still verified independently.
  return {EntryState::Valid,
          {static_cast<quint64>(info.size()),
           static_cast<quint64>(info.lastModified().toMSecsSinceEpoch())},
          {}};
}
#endif

QByteArray serializeIntent(const Intent &intent) {
  QJsonObject object;
  object.insert(QStringLiteral("version"), IntentVersion);
  object.insert(QStringLiteral("operation"), intent.operation);
  object.insert(QStringLiteral("device"),
                QString::number(intent.identity.device));
  object.insert(QStringLiteral("inode"),
                QString::number(intent.identity.inode));
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

std::optional<Intent> parseIntent(const QVariant &value) {
  QJsonParseError parseError{};
  const QJsonDocument document =
      QJsonDocument::fromJson(value.toByteArray(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    return std::nullopt;
  }
  const QJsonObject object = document.object();
  if (object.value(QStringLiteral("version")).toInt() != IntentVersion) {
    return std::nullopt;
  }
  bool deviceOk = false;
  bool inodeOk = false;
  const quint64 device =
      object.value(QStringLiteral("device")).toString().toULongLong(&deviceOk);
  const quint64 inode =
      object.value(QStringLiteral("inode")).toString().toULongLong(&inodeOk);
  const QString operation =
      object.value(QStringLiteral("operation")).toString();
  if (!deviceOk || !inodeOk || operation.isEmpty()) {
    return std::nullopt;
  }
  return Intent{operation, {device, inode}};
}

bool syncSettings(QSettings &settings, QString &error) {
  settings.sync();
  if (settings.status() != QSettings::NoError) {
    error = QObject::tr("The profile settings file could not be synchronized.");
    return false;
  }
  return true;
}

Result failure(Status status, const QString &error) { return {status, error}; }

} // namespace

QString pendingOperation(const QSettings &settings, const QString &intentKey) {
  if (!settings.contains(intentKey)) {
    return {};
  }
  const auto intent = parseIntent(settings.value(intentKey));
  return intent ? intent->operation : QStringLiteral("<invalid>");
}

Result migrate(QSettings &settings, const QString &profileRoot,
               const QString &intentKey, const QString &operation,
               const QString &sourceLeaf, const QString &destinationLeaf,
               EntryKind kind, const QString &finalSettingKey,
               const QVariant &finalSettingValue, const Hooks *hooks) {
  if (!validLeaf(sourceLeaf) || !validLeaf(destinationLeaf) ||
      sourceLeaf == destinationLeaf || intentKey.isEmpty() ||
      operation.isEmpty() || finalSettingKey.isEmpty()) {
    return failure(Status::InvalidRequest,
                   QObject::tr("Invalid legacy profile migration request."));
  }

  QString error;
  if (!syncSettings(settings, error)) {
    return failure(Status::SettingsUnavailable, error);
  }

#ifdef Q_OS_UNIX
  const QByteArray encodedRoot =
      QFile::encodeName(QDir::cleanPath(profileRoot));
  const int root = ::open(encodedRoot.constData(),
                          O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (root < 0) {
    return failure(Status::UnsafeEntry,
                   QObject::tr("Cannot open the profile directory safely: %1")
                       .arg(QString::fromLocal8Bit(std::strerror(errno))));
  }
  struct stat rootIdentity{};
  if (::fstat(root, &rootIdentity) != 0) {
    const QString detail = QString::fromLocal8Bit(std::strerror(errno));
    ::close(root);
    return failure(
        Status::UnsafeEntry,
        QObject::tr("Cannot identify the profile directory: %1").arg(detail));
  }
  auto inspectSource = [&] { return inspectAt(root, sourceLeaf, kind); };
  auto inspectDestination = [&] {
    return inspectAt(root, destinationLeaf, kind);
  };
#else
  const QString sourcePath = QDir(profileRoot).absoluteFilePath(sourceLeaf);
  const QString destinationPath =
      QDir(profileRoot).absoluteFilePath(destinationLeaf);
  auto inspectSource = [&] { return inspectPath(sourcePath, kind); };
  auto inspectDestination = [&] { return inspectPath(destinationPath, kind); };
#endif

  const QVariant rawIntent = settings.value(intentKey);
  std::optional<Intent> intent;
  if (settings.contains(intentKey)) {
    intent = parseIntent(rawIntent);
    if (!intent || intent->operation != operation) {
#ifdef Q_OS_UNIX
      ::close(root);
#endif
      return failure(
          Status::InvalidRequest,
          QObject::tr("A different or malformed profile migration is "
                      "already pending."));
    }
  }

  Inspection source = inspectSource();
  Inspection destination = inspectDestination();
  auto closeAndFail = [&](Status status, const QString &detail) {
#ifdef Q_OS_UNIX
    ::close(root);
#endif
    return failure(status, detail);
  };
  if (source.state == EntryState::Error ||
      destination.state == EntryState::Error) {
    return closeAndFail(Status::SourceUnavailable, !source.error.isEmpty()
                                                       ? source.error
                                                       : destination.error);
  }
  if (source.state == EntryState::Unsafe ||
      destination.state == EntryState::Unsafe) {
    return closeAndFail(Status::UnsafeEntry, !source.error.isEmpty()
                                                 ? source.error
                                                 : destination.error);
  }

  if (!intent) {
    if (source.state != EntryState::Valid) {
      return closeAndFail(Status::SourceUnavailable,
                          QObject::tr("Legacy migration source is missing: %1")
                              .arg(sourceLeaf));
    }
    intent = Intent{operation, source.identity};
    settings.setValue(intentKey, serializeIntent(*intent));
    if (!syncSettings(settings, error) ||
        settings.value(intentKey).toByteArray() != serializeIntent(*intent)) {
      return closeAndFail(Status::SettingsUnavailable,
                          error.isEmpty() ? QObject::tr("The migration "
                                                        "intent was not "
                                                        "persisted.")
                                          : error);
    }
    if (hooks && hooks->afterIntentPersisted) {
      hooks->afterIntentPersisted();
    }
  }

  if (source.state == EntryState::Valid &&
      !sameIdentity(source.identity, intent->identity)) {
    return closeAndFail(Status::UnsafeEntry,
                        QObject::tr("The legacy migration source changed."));
  }
  if (destination.state == EntryState::Valid &&
      !sameIdentity(destination.identity, intent->identity)) {
    return closeAndFail(
        Status::DestinationExists,
        QObject::tr("The migration destination is occupied by another "
                    "entry."));
  }
  if (source.state == EntryState::Valid &&
      destination.state == EntryState::Valid) {
    return closeAndFail(
        Status::DestinationExists,
        QObject::tr("Both legacy migration paths are occupied."));
  }
  if (source.state == EntryState::Missing &&
      destination.state == EntryState::Missing) {
    return closeAndFail(
        Status::SourceUnavailable,
        QObject::tr("The pending migration entry disappeared."));
  }

  if (source.state == EntryState::Valid) {
#ifdef Q_OS_LINUX
#if defined(SYS_renameat2) && defined(RENAME_NOREPLACE)
    const QByteArray encodedSource = QFile::encodeName(sourceLeaf);
    const QByteArray encodedDestination = QFile::encodeName(destinationLeaf);
    if (::syscall(SYS_renameat2, root, encodedSource.constData(), root,
                  encodedDestination.constData(), RENAME_NOREPLACE) != 0) {
      const int renameError = errno;
      return closeAndFail(
          renameError == EEXIST ? Status::DestinationExists
                                : Status::RenameFailed,
          QObject::tr("Cannot move %1 to %2: %3")
              .arg(sourceLeaf, destinationLeaf,
                   QString::fromLocal8Bit(std::strerror(renameError))));
    }
#else
    return closeAndFail(
        Status::RenameFailed,
        QObject::tr("Atomic no-replace rename is unavailable."));
#endif
#elif defined(Q_OS_WIN)
    const std::wstring nativeSource =
        QDir::toNativeSeparators(sourcePath).toStdWString();
    const std::wstring nativeDestination =
        QDir::toNativeSeparators(destinationPath).toStdWString();
    if (::MoveFileExW(nativeSource.c_str(), nativeDestination.c_str(),
                      MOVEFILE_WRITE_THROUGH) == FALSE) {
      return closeAndFail(Status::RenameFailed,
                          QObject::tr("Cannot move the legacy profile entry."));
    }
#else
    if (!QDir(profileRoot).rename(sourceLeaf, destinationLeaf)) {
      return closeAndFail(Status::RenameFailed,
                          QObject::tr("Cannot move the legacy profile entry."));
    }
#endif
    source = inspectSource();
    destination = inspectDestination();
    if (source.state != EntryState::Missing ||
        destination.state != EntryState::Valid ||
        !sameIdentity(destination.identity, intent->identity)) {
      return closeAndFail(Status::PublicationUncertain,
                          QObject::tr("The moved profile entry could not be "
                                      "authenticated."));
    }
#ifdef Q_OS_UNIX
    if (::fsync(root) != 0) {
      const QString detail = QString::fromLocal8Bit(std::strerror(errno));
      return closeAndFail(
          Status::PublicationUncertain,
          QObject::tr("The profile migration could not be made durable: "
                      "%1")
              .arg(detail));
    }
#endif
    if (hooks && hooks->afterRenamePublished) {
      hooks->afterRenamePublished();
    }
  }

#ifdef Q_OS_UNIX
  struct stat liveRoot{};
  const int reopened = ::open(encodedRoot.constData(),
                              O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  const bool rootMatches = reopened >= 0 && ::fstat(reopened, &liveRoot) == 0 &&
                           liveRoot.st_dev == rootIdentity.st_dev &&
                           liveRoot.st_ino == rootIdentity.st_ino;
  if (reopened >= 0) {
    ::close(reopened);
  }
  if (!rootMatches) {
    return closeAndFail(
        Status::PublicationUncertain,
        QObject::tr("The profile directory changed during migration."));
  }
  ::close(root);
#endif

  settings.setValue(finalSettingKey, finalSettingValue);
  settings.remove(intentKey);
  if (!syncSettings(settings, error) || settings.contains(intentKey) ||
      settings.value(finalSettingKey) != finalSettingValue) {
    return failure(
        Status::SettingsUnavailable,
        error.isEmpty()
            ? QObject::tr("The migrated profile setting was not persisted.")
            : error);
  }
  return {Status::Completed, {}};
}

} // namespace ProfileLegacyMigration
