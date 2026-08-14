#include "legacyflatpakmigration.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QMap>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QSharedMemory>
#include <QStandardPaths>
#include <QTemporaryFile>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/fs.h>
#include <optional>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace LegacyFlatpakMigration
{
namespace
{
  constexpr auto CompletionName   = ".legacy-flatpak-migration-v2.json";
  constexpr auto IntentName       = ".fluorine-native-migration-v2.intent.json";
  constexpr auto LockName         = ".fluorine-native-migration-v2.lock";
  constexpr auto AttentionName    = "legacy-flatpak-migration-attention.txt";
  constexpr char AttentionMagic[] = "Fluorine legacy Flatpak migration report v2\n";
  constexpr auto LegacyMovedName  = "MOVED.txt";
  constexpr auto DefaultLegacyProcessKey = "mo-43d1a3ad-eeb0-4818-97c9-eda5216c29b5";

  struct Identity
  {
    quint64 device = 0;
    quint64 inode  = 0;
  };

  struct Move
  {
    QString kind;
    QString source;
    QString destination;
    Identity identity;
    Identity destinationParent;
  };

  struct Plan
  {
    QString legacyRoot;
    QString dataRoot;
    QList<Move> moves;
    QStringList attention;
    bool blockingAttention = false;
  };

  struct Inventory
  {
    QStringList prefixSources;
    QStringList instanceParents;
    QStringList configCandidates;
    QStringList settingsCandidates;
    QStringList retainedRuntimeCandidates;
  };

  QString cleanAbsolute(const QString& path)
  {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
  }

  bool pathExists(const QString& path)
  {
    const QFileInfo info(path);
    return info.exists() || info.isSymLink();
  }

  std::optional<Identity> identityOf(const QString& path, bool follow = false)
  {
    struct stat value{};
    const QByteArray encoded = QFile::encodeName(path);
    const int result         = follow ? ::stat(encoded.constData(), &value)
                                      : ::lstat(encoded.constData(), &value);
    if (result != 0) {
      return std::nullopt;
    }
    return Identity{static_cast<quint64>(value.st_dev),
                    static_cast<quint64>(value.st_ino)};
  }

  bool sameIdentity(const Identity& left, const Identity& right)
  {
    return left.device == right.device && left.inode == right.inode;
  }

  bool isRealDirectory(const QString& path)
  {
    const QFileInfo info(path);
    return info.exists() && info.isDir() && !info.isSymLink();
  }

  bool isRegularFile(const QString& path)
  {
    const QFileInfo info(path);
    return info.exists() && info.isFile() && !info.isSymLink();
  }

  bool isAtOrBelow(const QString& path, const QString& root)
  {
    const QString cleanPath = cleanAbsolute(path);
    const QString cleanRoot = cleanAbsolute(root);
    return cleanPath == cleanRoot || cleanPath.startsWith(cleanRoot + QLatin1Char('/'));
  }

  bool hasSymlinkInAbsolutePath(const QString& path)
  {
    const QString cleanPath = cleanAbsolute(path);
    if (!QDir::isAbsolutePath(cleanPath)) {
      return true;
    }
    QString current = QStringLiteral("/");
    for (const QString& component :
         cleanPath.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
      current = QDir(current).filePath(component);
      const QFileInfo info(current);
      if (info.isSymLink()) {
        return true;
      }
      if (info.exists() && !info.isDir() && current != cleanPath) {
        return true;
      }
    }
    return false;
  }

  bool hasSymlinkComponent(const QString& path, const QString& root)
  {
    const QString cleanPath = cleanAbsolute(path);
    const QString cleanRoot = cleanAbsolute(root);
    if (!isAtOrBelow(cleanPath, cleanRoot)) {
      return true;
    }
    if (hasSymlinkInAbsolutePath(cleanRoot)) {
      return true;
    }

    QString current = cleanRoot;
    if (QFileInfo(current).isSymLink()) {
      return true;
    }
    const QString relative = QDir(cleanRoot).relativeFilePath(cleanPath);
    for (const QString& component :
         relative.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
      if (component == QStringLiteral(".")) {
        continue;
      }
      if (component == QStringLiteral("..")) {
        return true;
      }
      current = QDir(current).filePath(component);
      if (QFileInfo(current).isSymLink()) {
        return true;
      }
    }
    return false;
  }

  bool hasUnsafeSourceComponent(const QString& path, const Paths& paths)
  {
    if (isAtOrBelow(path, paths.legacyRoot)) {
      return hasSymlinkComponent(path, paths.legacyRoot);
    }
    if (isAtOrBelow(path, paths.dataRoot)) {
      return hasSymlinkComponent(path, paths.dataRoot);
    }
    return true;
  }

  QString remapBelow(const QString& path, const QString& source,
                     const QString& destination)
  {
    const QString cleanPath   = QDir::cleanPath(path);
    const QString cleanSource = QDir::cleanPath(source);
    const QString cleanDest   = QDir::cleanPath(destination);
    if (cleanPath == cleanSource) {
      return cleanDest;
    }
    if (cleanPath.startsWith(cleanSource + QLatin1Char('/'))) {
      return cleanDest + cleanPath.mid(cleanSource.size());
    }
    return path;
  }

  bool fsyncDirectory(const QString& path)
  {
    const QByteArray encoded = QFile::encodeName(path);
    const int fd = ::open(encoded.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
      return false;
    }
    const bool ok = ::fsync(fd) == 0;
    ::close(fd);
    return ok;
  }

  bool writeAtomically(const QString& path, const QByteArray& payload, QString& error)
  {
    const QFileInfo target(path);
    if (target.isSymLink()) {
      error = QStringLiteral("refusing symlink destination %1").arg(path);
      return false;
    }
    if (hasSymlinkInAbsolutePath(target.dir().absolutePath())) {
      error =
          QStringLiteral("refusing symlinked destination ancestry for %1").arg(path);
      return false;
    }
    if (!QDir().mkpath(target.dir().absolutePath())) {
      error = QStringLiteral("cannot create directory for %1").arg(path);
      return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
      error = QStringLiteral("cannot open %1: %2").arg(path, file.errorString());
      return false;
    }
    if (file.write(payload) != payload.size() || !file.commit()) {
      error = QStringLiteral("cannot commit %1: %2").arg(path, file.errorString());
      return false;
    }
    if (!fsyncDirectory(target.dir().absolutePath())) {
      error = QStringLiteral("cannot make %1 durable").arg(path);
      return false;
    }
    return true;
  }

  bool removeFileDurably(const QString& path, QString& error)
  {
    if (!pathExists(path)) {
      return true;
    }
    if (QFileInfo(path).isSymLink() || !QFileInfo(path).isFile()) {
      error = QStringLiteral("refusing unexpected transaction object %1").arg(path);
      return false;
    }
    const QString parent = QFileInfo(path).dir().absolutePath();
    if (!QFile::remove(path) || !fsyncDirectory(parent)) {
      error = QStringLiteral("cannot retire transaction receipt %1").arg(path);
      return false;
    }
    return true;
  }

  QJsonObject identityJson(const Identity& identity)
  {
    return {{QStringLiteral("device"), QString::number(identity.device)},
            {QStringLiteral("inode"), QString::number(identity.inode)}};
  }

  std::optional<Identity> parseIdentity(const QJsonValue& value)
  {
    if (!value.isObject()) {
      return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    bool deviceOk            = false;
    bool inodeOk             = false;
    const quint64 device =
        object.value(QStringLiteral("device")).toString().toULongLong(&deviceOk);
    const quint64 inode =
        object.value(QStringLiteral("inode")).toString().toULongLong(&inodeOk);
    if (!deviceOk || !inodeOk || device == 0 || inode == 0) {
      return std::nullopt;
    }
    return Identity{device, inode};
  }

  QByteArray encodePlan(const Plan& plan)
  {
    QJsonArray moves;
    for (const Move& move : plan.moves) {
      moves.append(QJsonObject{
          {QStringLiteral("kind"), move.kind},
          {QStringLiteral("source"), move.source},
          {QStringLiteral("destination"), move.destination},
          {QStringLiteral("identity"), identityJson(move.identity)},
          {QStringLiteral("destination_parent"), identityJson(move.destinationParent)},
      });
    }
    return QJsonDocument(
               QJsonObject{
                   {QStringLiteral("version"), 2},
                   {QStringLiteral("legacy_root"), plan.legacyRoot},
                   {QStringLiteral("data_root"), plan.dataRoot},
                   {QStringLiteral("moves"), moves},
                   {QStringLiteral("attention"),
                    QJsonArray::fromStringList(plan.attention)},
                   {QStringLiteral("blocking_attention"), plan.blockingAttention},
               })
        .toJson(QJsonDocument::Indented);
  }

  std::optional<Plan> decodePlan(const QString& path, const Paths& paths,
                                 const QStringList& allowedPrefixSources,
                                 const QStringList& allowedInstanceParents,
                                 QString& error)
  {
    if (!isRegularFile(path)) {
      error = QStringLiteral("invalid migration receipt %1").arg(path);
      return std::nullopt;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      error = QStringLiteral("cannot read migration receipt %1").arg(path);
      return std::nullopt;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
      error = QStringLiteral("invalid migration receipt JSON in %1").arg(path);
      return std::nullopt;
    }
    const QJsonObject object = document.object();
    Plan plan;
    plan.legacyRoot =
        cleanAbsolute(object.value(QStringLiteral("legacy_root")).toString());
    plan.dataRoot = cleanAbsolute(object.value(QStringLiteral("data_root")).toString());
    if (object.value(QStringLiteral("version")).toInt() != 2 ||
        plan.legacyRoot != cleanAbsolute(paths.legacyRoot) ||
        plan.dataRoot != cleanAbsolute(paths.dataRoot) ||
        !object.value(QStringLiteral("moves")).isArray()) {
      error = QStringLiteral("migration receipt does not match the configured roots");
      return std::nullopt;
    }
    if (!object.value(QStringLiteral("attention")).isArray() ||
        !object.value(QStringLiteral("blocking_attention")).isBool()) {
      error = QStringLiteral("migration receipt is missing policy state");
      return std::nullopt;
    }
    for (const QJsonValue& value :
         object.value(QStringLiteral("attention")).toArray()) {
      if (!value.isString()) {
        error = QStringLiteral("migration receipt contains an invalid diagnostic");
        return std::nullopt;
      }
      plan.attention.append(value.toString());
    }
    plan.blockingAttention =
        object.value(QStringLiteral("blocking_attention")).toBool();

    const QString expectedPrefix =
        QDir(plan.dataRoot).filePath(QStringLiteral("Prefix"));
    for (const QJsonValue& value : object.value(QStringLiteral("moves")).toArray()) {
      if (!value.isObject()) {
        error = QStringLiteral("migration receipt contains a non-object move");
        return std::nullopt;
      }
      const QJsonObject moveObject = value.toObject();
      Move move;
      move.kind = moveObject.value(QStringLiteral("kind")).toString();
      move.source =
          cleanAbsolute(moveObject.value(QStringLiteral("source")).toString());
      move.destination =
          cleanAbsolute(moveObject.value(QStringLiteral("destination")).toString());
      const auto identity = parseIdentity(moveObject.value(QStringLiteral("identity")));
      const auto parentIdentity =
          parseIdentity(moveObject.value(QStringLiteral("destination_parent")));
      if (!identity || !parentIdentity) {
        error = QStringLiteral("migration receipt contains an invalid identity");
        return std::nullopt;
      }
      move.identity          = *identity;
      move.destinationParent = *parentIdentity;

      const bool validPrefix = move.kind == QStringLiteral("prefix") &&
                               allowedPrefixSources.contains(move.source) &&
                               move.destination == cleanAbsolute(expectedPrefix);
      bool validInstance = false;
      if (move.kind == QStringLiteral("instance")) {
        for (const QString& parent : allowedInstanceParents) {
          if (QFileInfo(move.source).dir().absolutePath() == cleanAbsolute(parent)) {
            validInstance = true;
            break;
          }
        }
        validInstance =
            validInstance &&
            QFileInfo(move.destination).dir().absolutePath() ==
                cleanAbsolute(plan.dataRoot) &&
            QFileInfo(move.source).fileName() == QFileInfo(move.destination).fileName();
      }
      if (!validPrefix && !validInstance) {
        error = QStringLiteral("migration receipt contains an unsupported move");
        return std::nullopt;
      }
      plan.moves.append(std::move(move));
    }
    return plan;
  }

  QStringList uniqueCleanPaths(QStringList paths)
  {
    for (QString& path : paths) {
      path = cleanAbsolute(path);
    }
    paths.removeDuplicates();
    return paths;
  }

  Inventory inventoryFor(const Paths& paths)
  {
    const QString oldRoot  = cleanAbsolute(paths.legacyRoot);
    const QString dataRoot = cleanAbsolute(paths.dataRoot);
    const QString nestedRoot =
        QDir(oldRoot).filePath(QStringLiteral(".var/app/com.fluorine.manager"));

    QStringList dataLayouts{
        oldRoot,
        QDir(oldRoot).filePath(QStringLiteral("data/fluorine")),
        QDir(oldRoot).filePath(QStringLiteral(".local/share/fluorine")),
        nestedRoot,
        QDir(nestedRoot).filePath(QStringLiteral("data/fluorine")),
        QDir(nestedRoot).filePath(QStringLiteral(".local/share/fluorine")),
    };
    dataLayouts = uniqueCleanPaths(std::move(dataLayouts));

    Inventory inventory;
    for (const QString& root : dataLayouts) {
      inventory.prefixSources.append(QDir(root).filePath(QStringLiteral("Prefix")));
      inventory.instanceParents.append(root);
      inventory.retainedRuntimeCandidates.append(
          QDir(root).filePath(QStringLiteral("bin")));
      inventory.retainedRuntimeCandidates.append(
          QDir(root).filePath(QStringLiteral("plugins")));
      inventory.retainedRuntimeCandidates.append(
          QDir(root).filePath(QStringLiteral("dlls")));
      inventory.retainedRuntimeCandidates.append(
          QDir(root).filePath(QStringLiteral("lib")));
      inventory.retainedRuntimeCandidates.append(
          QDir(root).filePath(QStringLiteral("logs")));
    }
    inventory.instanceParents.append(
        QDir(oldRoot).filePath(QStringLiteral("data/ModOrganizer")));
    inventory.instanceParents.append(
        QDir(nestedRoot).filePath(QStringLiteral("data/ModOrganizer")));

    inventory.configCandidates = {
        QDir(oldRoot).filePath(QStringLiteral("config/fluorine/config.json")),
        QDir(nestedRoot).filePath(QStringLiteral("config/fluorine/config.json")),
        QDir(dataRoot).filePath(QStringLiteral("config/fluorine/config.json")),
    };
    inventory.settingsCandidates = {
        QDir(oldRoot).filePath(
            QStringLiteral("config/Mod Organizer Team/Mod Organizer.conf")),
        QDir(nestedRoot)
            .filePath(QStringLiteral("config/Mod Organizer Team/Mod Organizer.conf")),
        QDir(dataRoot).filePath(
            QStringLiteral("config/Mod Organizer Team/Mod Organizer.conf")),
    };
    // The retired v1 migrator could move these package/runtime directories
    // into the native root before writing its unversioned marker. Their exact
    // provenance can no longer be proven, so retain and report them rather
    // than certifying a possibly mixed executable tree as clean.
    if (isRegularFile(QDir(oldRoot).filePath(QString::fromLatin1(LegacyMovedName)))) {
      inventory.retainedRuntimeCandidates.append(
          QDir(dataRoot).filePath(QStringLiteral("bin")));
      inventory.retainedRuntimeCandidates.append(
          QDir(dataRoot).filePath(QStringLiteral("logs")));
    }

    inventory.prefixSources   = uniqueCleanPaths(std::move(inventory.prefixSources));
    inventory.instanceParents = uniqueCleanPaths(std::move(inventory.instanceParents));
    inventory.configCandidates =
        uniqueCleanPaths(std::move(inventory.configCandidates));
    inventory.settingsCandidates =
        uniqueCleanPaths(std::move(inventory.settingsCandidates));
    inventory.retainedRuntimeCandidates =
        uniqueCleanPaths(std::move(inventory.retainedRuntimeCandidates));
    return inventory;
  }

  QList<Move> discoverMoves(const Paths& paths, const Inventory& inventory,
                            QStringList& attention, bool& blocking, bool& dangerous)
  {
    QList<Move> moves;
    const QString dataRoot       = cleanAbsolute(paths.dataRoot);
    const auto destinationParent = identityOf(dataRoot);
    if (!destinationParent) {
      blocking  = true;
      dangerous = true;
      attention.append(
          QStringLiteral("cannot identify native data root %1").arg(dataRoot));
      return moves;
    }

    QStringList prefixSources;
    for (const QString& source : inventory.prefixSources) {
      if (pathExists(source)) {
        const QString instanceIni =
            QDir(source).filePath(QStringLiteral("ModOrganizer.ini"));
        if (isRegularFile(instanceIni)) {
          blocking  = true;
          dangerous = true;
          attention.append(
              QStringLiteral("legacy path is ambiguous between the managed Prefix "
                             "and an instance: %1")
                  .arg(source));
        } else if (isRealDirectory(source) &&
                   !hasUnsafeSourceComponent(source, paths)) {
          prefixSources.append(source);
        } else {
          blocking  = true;
          dangerous = true;
          attention.append(
              QStringLiteral("legacy Prefix is not a real directory: %1").arg(source));
        }
      }
    }
    const QString prefixDestination = QDir(dataRoot).filePath(QStringLiteral("Prefix"));
    if (prefixSources.size() > 1) {
      blocking  = true;
      dangerous = true;
      attention.append(
          QStringLiteral(
              "multiple legacy Prefix directories require manual selection: %1")
              .arg(prefixSources.join(QStringLiteral(", "))));
    } else if (prefixSources.size() == 1) {
      const QString source = prefixSources.front();
      if (pathExists(prefixDestination)) {
        blocking  = true;
        dangerous = true;
        attention.append(
            QStringLiteral("both legacy and native Prefix directories exist; "
                           "neither was changed: %1 and %2")
                .arg(source, prefixDestination));
      } else if (const auto sourceIdentity = identityOf(source)) {
        if (sourceIdentity->device != destinationParent->device) {
          blocking  = true;
          dangerous = true;
          attention.append(
              QStringLiteral("legacy Prefix is on another filesystem and must be "
                             "moved manually: %1")
                  .arg(source));
        } else {
          moves.append(Move{QStringLiteral("prefix"), source,
                            cleanAbsolute(prefixDestination), *sourceIdentity,
                            *destinationParent});
        }
      } else {
        blocking  = true;
        dangerous = true;
        attention.append(
            QStringLiteral("cannot identify legacy Prefix %1").arg(source));
      }
    }

    QHash<QString, QStringList> instancesByName;
    for (const QString& parent : inventory.instanceParents) {
      if (!pathExists(parent)) {
        continue;
      }
      if (!isRealDirectory(parent) || hasUnsafeSourceComponent(parent, paths)) {
        blocking = true;
        attention.append(
            QStringLiteral("legacy instance root is not a real directory: %1")
                .arg(parent));
        continue;
      }
      const QFileInfoList children = QDir(parent).entryInfoList(
          QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
      for (const QFileInfo& child : children) {
        const QString ini =
            QDir(child.absoluteFilePath()).filePath(QStringLiteral("ModOrganizer.ini"));
        if (child.isSymLink() || !child.isDir()) {
          if (pathExists(ini)) {
            blocking = true;
            attention.append(
                QStringLiteral("legacy instance is not a real directory: %1")
                    .arg(child.absoluteFilePath()));
          }
          continue;
        }
        if (!isRegularFile(ini)) {
          if (pathExists(ini)) {
            blocking = true;
            attention.append(
                QStringLiteral("legacy instance settings are not a regular file: %1")
                    .arg(ini));
          }
          continue;
        }
        QSettings settings(ini, QSettings::IniFormat);
        const bool hasRootPortable = settings.contains(QStringLiteral("portable"));
        const bool hasGroupPortable =
            settings.contains(QStringLiteral("General/portable"));
        const bool rootPortable =
            settings.value(QStringLiteral("portable"), false).toBool();
        const bool groupPortable =
            settings.value(QStringLiteral("General/portable"), false).toBool();
        if (settings.status() != QSettings::NoError) {
          blocking = true;
          attention.append(
              QStringLiteral("cannot read legacy instance settings %1").arg(ini));
          continue;
        }
        if (hasRootPortable && hasGroupPortable && rootPortable != groupPortable) {
          blocking = true;
          attention.append(
              QStringLiteral("legacy instance has conflicting portable flags: %1")
                  .arg(ini));
          continue;
        }
        const bool portable = hasRootPortable ? rootPortable : groupPortable;
        if (!portable) {
          instancesByName[child.fileName()].append(
              cleanAbsolute(child.absoluteFilePath()));
        }
      }
    }

    QStringList names = instancesByName.keys();
    std::sort(names.begin(), names.end());
    for (const QString& name : names) {
      QStringList sources = instancesByName.value(name);
      sources.removeDuplicates();
      const QString destination = QDir(dataRoot).filePath(name);
      if (name == QStringLiteral("Prefix")) {
        blocking  = true;
        dangerous = true;
        attention.append(
            QStringLiteral("legacy global instance 'Prefix' conflicts with the "
                           "managed Wine prefix directory and must be renamed: %1")
                .arg(sources.join(QStringLiteral(", "))));
        continue;
      }
      if (sources.size() != 1) {
        blocking = true;
        attention.append(QStringLiteral("multiple legacy global instances named "
                                        "'%1' require manual selection: %2")
                             .arg(name, sources.join(QStringLiteral(", "))));
        continue;
      }
      if (pathExists(destination)) {
        blocking = true;
        attention.append(QStringLiteral("both legacy and native global instance "
                                        "'%1' exist; neither was changed")
                             .arg(name));
        continue;
      }
      const auto sourceIdentity = identityOf(sources.front());
      if (!sourceIdentity || sourceIdentity->device != destinationParent->device) {
        blocking = true;
        attention.append(
            QStringLiteral("legacy global instance '%1' must be moved manually from %2")
                .arg(name, sources.front()));
        continue;
      }
      moves.append(Move{QStringLiteral("instance"), sources.front(),
                        cleanAbsolute(destination), *sourceIdentity,
                        *destinationParent});
    }
    return moves;
  }

  void appendRetainedRuntimeAttention(const Inventory& inventory,
                                      QStringList& attention)
  {
    for (const QString& path : inventory.retainedRuntimeCandidates) {
      if (pathExists(path)) {
        attention.append(
            QStringLiteral("legacy runtime/plugin/log data was deliberately left in "
                           "place and was not mixed with the native bundle: %1")
                .arg(path));
      }
    }
  }

  std::optional<QString> planTopologyError(const QList<Move>& moves)
  {
    auto overlaps = [](const QString& left, const QString& right) {
      return isAtOrBelow(left, right) || isAtOrBelow(right, left);
    };
    for (qsizetype i = 0; i < moves.size(); ++i) {
      for (qsizetype j = i + 1; j < moves.size(); ++j) {
        const Move& left  = moves.at(i);
        const Move& right = moves.at(j);
        if (overlaps(left.source, right.source) ||
            overlaps(left.destination, right.destination) ||
            overlaps(left.source, right.destination) ||
            overlaps(left.destination, right.source)) {
          return QStringLiteral("migration objects overlap and require manual "
                                "resolution: %1 -> %2; %3 -> %4")
              .arg(left.source, left.destination, right.source, right.destination);
        }
      }
    }
    return std::nullopt;
  }

  bool renameNoReplace(const QString& source, const QString& destination,
                       QString& error)
  {
    if (hasSymlinkInAbsolutePath(QFileInfo(source).dir().absolutePath()) ||
        hasSymlinkInAbsolutePath(QFileInfo(destination).dir().absolutePath())) {
      error = QStringLiteral("refusing symlinked move ancestry from %1 to %2")
                  .arg(source, destination);
      return false;
    }
#if defined(SYS_renameat2) && defined(RENAME_NOREPLACE)
    const QByteArray encodedSource      = QFile::encodeName(source);
    const QByteArray encodedDestination = QFile::encodeName(destination);
    if (::syscall(SYS_renameat2, AT_FDCWD, encodedSource.constData(), AT_FDCWD,
                  encodedDestination.constData(), RENAME_NOREPLACE) != 0) {
      error =
          QStringLiteral("cannot move %1 to %2: %3")
              .arg(source, destination, QString::fromLocal8Bit(std::strerror(errno)));
      return false;
    }
#else
    Q_UNUSED(source);
    Q_UNUSED(destination);
    error = QStringLiteral("atomic no-replace rename is unavailable");
    return false;
#endif
    if (!fsyncDirectory(QFileInfo(source).dir().absolutePath()) ||
        !fsyncDirectory(QFileInfo(destination).dir().absolutePath())) {
      error = QStringLiteral("cannot make move durable from %1 to %2")
                  .arg(source, destination);
      return false;
    }
    return true;
  }

  bool applyPlan(const Plan& plan, QString& prefixSource, QString& prefixDestination,
                 QString& error)
  {
    const auto currentParent = identityOf(plan.dataRoot);
    if (!currentParent) {
      error = QStringLiteral("native data root disappeared during migration");
      return false;
    }
    for (const Move& move : plan.moves) {
      if (!sameIdentity(*currentParent, move.destinationParent)) {
        error = QStringLiteral("native data root identity changed during migration");
        return false;
      }
      const auto sourceIdentity      = identityOf(move.source);
      const auto destinationIdentity = identityOf(move.destination);
      if (sourceIdentity && destinationIdentity) {
        error = QStringLiteral("both migration source and destination exist: %1 and %2")
                    .arg(move.source, move.destination);
        return false;
      }
      if (sourceIdentity) {
        if (!sameIdentity(*sourceIdentity, move.identity) ||
            !isRealDirectory(move.source)) {
          error =
              QStringLiteral("migration source identity changed: %1").arg(move.source);
          return false;
        }
        if (!renameNoReplace(move.source, move.destination, error)) {
          return false;
        }
      } else if (!destinationIdentity ||
                 !sameIdentity(*destinationIdentity, move.identity) ||
                 !isRealDirectory(move.destination)) {
        error =
            QStringLiteral("recorded migration object is at neither expected path: %1")
                .arg(move.source);
        return false;
      }

      const auto committedIdentity = identityOf(move.destination);
      if (!committedIdentity || !sameIdentity(*committedIdentity, move.identity)) {
        error = QStringLiteral("moved object failed identity verification: %1")
                    .arg(move.destination);
        return false;
      }
      if (move.kind == QStringLiteral("prefix")) {
        prefixSource      = move.source;
        prefixDestination = move.destination;
      }
    }
    return true;
  }

  QStringList existingRegularCandidates(const QStringList& candidates,
                                        const Paths& paths, QStringList& invalid)
  {
    QStringList existing;
    for (const QString& candidate : candidates) {
      if (!pathExists(candidate)) {
        continue;
      }
      if (isRegularFile(candidate) && !hasUnsafeSourceComponent(candidate, paths)) {
        existing.append(candidate);
      } else {
        invalid.append(candidate);
      }
    }
    return existing;
  }

  QStringList excludeInstanceOwnedCandidates(const QStringList& candidates,
                                             const QString& description,
                                             bool nativeTargetAbsent,
                                             QStringList& attention,
                                             bool& blockingAttention)
  {
    QStringList usable;
    for (const QString& candidate : candidates) {
      if (!pathExists(candidate)) {
        usable.append(candidate);
        continue;
      }
      QDir container = QFileInfo(candidate).dir();
      container.cdUp();
      const QString ini = container.filePath(QStringLiteral("ModOrganizer.ini"));
      if (pathExists(ini)) {
        blockingAttention = blockingAttention || nativeTargetAbsent;
        attention.append(
            QStringLiteral("legacy %1 was not imported because it belongs to a "
                           "global or portable instance: %2")
                .arg(description, candidate));
      } else {
        usable.append(candidate);
      }
    }
    return usable;
  }

  std::optional<QJsonObject> readJsonObject(const QString& path, QString& error)
  {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      error = QStringLiteral("cannot read %1: %2").arg(path, file.errorString());
      return std::nullopt;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
      error = QStringLiteral("invalid JSON object in %1").arg(path);
      return std::nullopt;
    }
    return document.object();
  }

  bool migrateConfig(const Paths& paths, const Inventory& inventory,
                     const QString& prefixSource, const QString& prefixDestination,
                     QStringList& attention, bool& blockingAttention, bool& dangerous,
                     QString& error)
  {
    const QString target = QDir(cleanAbsolute(paths.configRoot))
                               .filePath(QStringLiteral("fluorine/config.json"));
    QStringList invalid;
    const QStringList candidates = excludeInstanceOwnedCandidates(
        inventory.configCandidates, QStringLiteral("Fluorine config"),
        !pathExists(target), attention, blockingAttention);
    const QStringList sources = existingRegularCandidates(candidates, paths, invalid);
    for (const QString& path : invalid) {
      attention.append(
          QStringLiteral("legacy Fluorine config is not a regular file: %1").arg(path));
    }

    QString selected;
    if (!pathExists(target)) {
      if (!invalid.isEmpty()) {
        blockingAttention = true;
        return true;
      }
      if (sources.size() > 1) {
        QByteArray reference;
        bool identical = true;
        for (const QString& source : sources) {
          QFile file(source);
          if (!file.open(QIODevice::ReadOnly)) {
            identical = false;
            break;
          }
          const QByteArray bytes = file.readAll();
          if (reference.isNull()) {
            reference = bytes;
          } else if (bytes != reference) {
            identical = false;
          }
        }
        if (!identical) {
          blockingAttention = true;
          attention.append(QStringLiteral("multiple different legacy Fluorine configs "
                                          "require manual selection: %1")
                               .arg(sources.join(QStringLiteral(", "))));
          return true;
        }
      }
      if (!sources.isEmpty()) {
        selected = sources.front();
      }
    } else if (!isRegularFile(target)) {
      error = QStringLiteral("native Fluorine config is not a regular file: %1")
                  .arg(target);
      return false;
    }

    const QString input = pathExists(target) ? target : selected;
    if (input.isEmpty()) {
      if (!invalid.isEmpty()) {
        blockingAttention = true;
      }
      return true;
    }
    auto object = readJsonObject(input, error);
    if (!object) {
      return false;
    }

    bool rewrite = input != target;
    if (!prefixSource.isEmpty()) {
      const QString oldPrefix = object->value(QStringLiteral("prefix_path")).toString();
      const QString newPrefix = remapBelow(oldPrefix, prefixSource, prefixDestination);
      if (newPrefix != oldPrefix) {
        object->insert(QStringLiteral("prefix_path"), newPrefix);
        rewrite = true;
      }
    } else {
      const QString configured =
          object->value(QStringLiteral("prefix_path")).toString();
      const QString nativePrefix =
          QDir(paths.dataRoot).filePath(QStringLiteral("Prefix"));
      for (const QString& legacyPrefix : inventory.prefixSources) {
        if (remapBelow(configured, legacyPrefix, nativePrefix) != configured &&
            !pathExists(legacyPrefix) && pathExists(nativePrefix)) {
          blockingAttention = true;
          dangerous         = true;
          attention.append(
              QStringLiteral("cannot prove whether the legacy Prefix was moved "
                             "before interruption; config was left unchanged: %1")
                  .arg(configured));
          break;
        }
      }
    }

    if (dangerous) {
      // In particular, never publish a copied v1 config that still names an
      // unproven/missing old Prefix. The caller will stop startup.
      return true;
    }

    if (rewrite) {
      if (!writeAtomically(
              target, QJsonDocument(*object).toJson(QJsonDocument::Indented), error)) {
        return false;
      }
      auto verified = readJsonObject(target, error);
      if (!verified || *verified != *object) {
        error =
            QStringLiteral("Fluorine config verification failed for %1").arg(target);
        return false;
      }
    }
    if (pathExists(target) && !sources.isEmpty() && selected.isEmpty()) {
      attention.append(
          QStringLiteral(
              "native Fluorine config was preserved; legacy config remains at %1")
              .arg(sources.join(QStringLiteral(", "))));
    }
    return true;
  }

  bool loadSettingsValues(const QString& path, QMap<QString, QVariant>& values,
                          QString& error)
  {
    QSettings settings(path, QSettings::IniFormat);
    for (const QString& key : settings.allKeys()) {
      values.insert(key, settings.value(key));
    }
    if (settings.status() != QSettings::NoError) {
      error = QStringLiteral("cannot read legacy global settings %1").arg(path);
      return false;
    }
    return true;
  }

  bool encodeSettings(const QString& target, const QMap<QString, QVariant>& values,
                      QByteArray& payload, QString& error)
  {
    const QFileInfo targetInfo(target);
    if (targetInfo.isSymLink() ||
        hasSymlinkInAbsolutePath(targetInfo.dir().absolutePath())) {
      error = QStringLiteral("refusing symlinked global settings destination %1")
                  .arg(target);
      return false;
    }
    if (!QDir().mkpath(targetInfo.dir().absolutePath())) {
      error =
          QStringLiteral("cannot create global settings directory for %1").arg(target);
      return false;
    }
    QTemporaryFile temporary(
        targetInfo.dir().filePath(QStringLiteral(".fluorine-settings-XXXXXX")));
    if (!temporary.open()) {
      error = QStringLiteral("cannot create temporary global settings file");
      return false;
    }
    const QString temporaryPath = temporary.fileName();
    temporary.close();
    {
      QSettings settings(temporaryPath, QSettings::IniFormat);
      settings.clear();
      for (auto it = values.cbegin(); it != values.cend(); ++it) {
        settings.setValue(it.key(), it.value());
      }
      settings.sync();
      if (settings.status() != QSettings::NoError) {
        error = QStringLiteral("cannot serialize migrated global settings");
        return false;
      }
    }
    QFile file(temporaryPath);
    if (!file.open(QIODevice::ReadOnly)) {
      error = QStringLiteral("cannot read serialized global settings");
      return false;
    }
    payload = file.readAll();
    return true;
  }

  bool migrateGlobalSettings(const Paths& paths, const Inventory& inventory,
                             const QSet<QString>& movedInstanceNames,
                             QStringList& attention, bool& blockingAttention,
                             QString& error)
  {
    const QString target =
        QDir(cleanAbsolute(paths.configRoot))
            .filePath(QStringLiteral("Mod Organizer Team/Mod Organizer.conf"));
    QStringList invalid;
    const QStringList candidates = excludeInstanceOwnedCandidates(
        inventory.settingsCandidates, QStringLiteral("global settings"),
        !pathExists(target), attention, blockingAttention);
    const QStringList sources = existingRegularCandidates(candidates, paths, invalid);
    for (const QString& path : invalid) {
      attention.append(
          QStringLiteral("legacy global settings are not a regular file: %1")
              .arg(path));
    }
    if (pathExists(target) && !isRegularFile(target)) {
      error = QStringLiteral("native global settings are not a regular file: %1")
                  .arg(target);
      return false;
    }
    if (pathExists(target)) {
      if (!sources.isEmpty() || !invalid.isEmpty()) {
        attention.append(
            QStringLiteral("native global settings were preserved unchanged; "
                           "legacy settings remain at %1")
                .arg((sources + invalid).join(QStringLiteral(", "))));
      }
      return true;
    }
    if (!invalid.isEmpty()) {
      blockingAttention = true;
      return true;
    }
    if (sources.isEmpty()) {
      return true;
    }

    std::optional<QMap<QString, QVariant>> selected;
    for (const QString& source : sources) {
      QMap<QString, QVariant> values;
      if (!loadSettingsValues(source, values, error)) {
        return false;
      }
      if (selected && *selected != values) {
        blockingAttention = true;
        attention.append(
            QStringLiteral("multiple different legacy global settings files "
                           "require manual selection: %1")
                .arg(sources.join(QStringLiteral(", "))));
        return true;
      }
      selected = std::move(values);
    }

    QMap<QString, QVariant> migrated = *selected;
    const QString currentInstance =
        migrated.value(QStringLiteral("CurrentInstance")).toString();
    if (!currentInstance.isEmpty() && !QDir::isAbsolutePath(currentInstance) &&
        !movedInstanceNames.contains(currentInstance)) {
      migrated.remove(QStringLiteral("CurrentInstance"));
      blockingAttention = true;
      attention.append(
          QStringLiteral("legacy current instance '%1' was not imported because "
                         "that exact instance was not proven moved")
              .arg(currentInstance));
    }

    QByteArray payload;
    if (!encodeSettings(target, migrated, payload, error) ||
        !writeAtomically(target, payload, error)) {
      return false;
    }
    QMap<QString, QVariant> verified;
    if (!loadSettingsValues(target, verified, error) || verified != migrated) {
      error = QStringLiteral("global settings verification failed for %1").arg(target);
      return false;
    }
    return true;
  }

  bool hasRecognizedLegacyData(const Paths& paths, const Inventory& inventory)
  {
    if (pathExists(
            QDir(paths.legacyRoot).filePath(QString::fromLatin1(LegacyMovedName))) ||
        pathExists(prefixReceiptPath(paths))) {
      return true;
    }
    for (const QString& path : inventory.prefixSources + inventory.configCandidates +
                                   inventory.settingsCandidates +
                                   inventory.retainedRuntimeCandidates) {
      if (pathExists(path)) {
        return true;
      }
    }
    for (const QString& parent : inventory.instanceParents) {
      if (pathExists(parent) && !isRealDirectory(parent)) {
        return true;
      }
      if (!isRealDirectory(parent)) {
        continue;
      }
      const QFileInfoList children = QDir(parent).entryInfoList(
          QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
      for (const QFileInfo& child : children) {
        if (pathExists(QDir(child.absoluteFilePath())
                           .filePath(QStringLiteral("ModOrganizer.ini")))) {
          return true;
        }
      }
    }
    return false;
  }

  void appendDiagnostic(Result& result, const QString& message)
  {
    if (!message.isEmpty()) {
      result.diagnostics.append(message);
    }
  }

  bool acquireLegacyProcessGuard(QSharedMemory& guard, QString& error)
  {
    if (guard.create(1)) {
      return true;
    }

    // Historical Qt builds could leave their SysV segment behind after a hard
    // exit. Attaching and detaching removes a zero-owner segment, but cannot
    // reclaim one still attached by a live old process. A few bounded retries
    // also cover the creator disappearing between create() and attach().
    for (int attempt = 0; attempt < 3; ++attempt) {
      if (guard.error() == QSharedMemory::AlreadyExists &&
          guard.attach(QSharedMemory::ReadOnly)) {
        guard.detach();
      }
      if (guard.create(1)) {
        return true;
      }
      if (guard.error() != QSharedMemory::AlreadyExists &&
          guard.error() != QSharedMemory::NotFound &&
          guard.error() != QSharedMemory::UnknownError) {
        break;
      }
      ::usleep(100000);
    }

    error = guard.errorString();
    return false;
  }

  bool writeAttentionReport(const Paths& paths, const QStringList& diagnostics,
                            QString& reportPath, QString& error)
  {
    reportPath = QDir(paths.dataRoot).filePath(QString::fromLatin1(AttentionName));
    QByteArray payload(AttentionMagic);
    payload += QByteArray("Fluorine preserved ambiguous legacy Flatpak data.\n"
                          "Review the paths below; Fluorine will not modify them.\n\n");
    for (const QString& diagnostic : diagnostics) {
      payload += "- " + diagnostic.toUtf8() + '\n';
    }
    if (pathExists(reportPath)) {
      QFile existing(reportPath);
      const bool owned =
          isRegularFile(reportPath) && existing.open(QIODevice::ReadOnly) &&
          existing.read(sizeof(AttentionMagic) - 1) == QByteArray(AttentionMagic);
      if (!owned) {
        QTemporaryFile unique(
            QDir(paths.dataRoot)
                .filePath(QStringLiteral("legacy-flatpak-migration-attention-"
                                         "XXXXXX.txt")));
        unique.setAutoRemove(false);
        if (!unique.open() || unique.write(payload) != payload.size() ||
            !unique.flush() || ::fsync(unique.handle()) != 0) {
          error = QStringLiteral("cannot write a migration attention report in %1")
                      .arg(paths.dataRoot);
          return false;
        }
        reportPath = unique.fileName();
        unique.close();
        if (!fsyncDirectory(paths.dataRoot)) {
          error = QStringLiteral("cannot make migration report durable: %1")
                      .arg(reportPath);
          return false;
        }
        return true;
      }
    }
    return writeAtomically(reportPath, payload, error);
  }

  void removeOwnedAttentionReport(const Paths& paths)
  {
    const QString path =
        QDir(paths.dataRoot).filePath(QString::fromLatin1(AttentionName));
    QFile file(path);
    if (!isRegularFile(path) || !file.open(QIODevice::ReadOnly) ||
        file.read(sizeof(AttentionMagic) - 1) != QByteArray(AttentionMagic)) {
      return;
    }
    file.close();
    if (QFile::remove(path)) {
      fsyncDirectory(paths.dataRoot);
    }
  }

  bool writeCompletion(const Paths& paths, bool withAttention, QString& error)
  {
    const QString legacyMarker =
        QDir(paths.legacyRoot).filePath(QString::fromLatin1(LegacyMovedName));
    if (!pathExists(legacyMarker)) {
      if (!writeAtomically(
              legacyMarker,
              QStringLiteral("Data migrated to %1\n").arg(paths.dataRoot).toUtf8(),
              error)) {
        return false;
      }
    } else if (QFileInfo(legacyMarker).isSymLink() ||
               !QFileInfo(legacyMarker).isFile()) {
      error = QStringLiteral("legacy completion marker has an unsafe type: %1")
                  .arg(legacyMarker);
      return false;
    }

    const QJsonObject marker{
        {QStringLiteral("version"), 2},
        {QStringLiteral("legacy_root"), cleanAbsolute(paths.legacyRoot)},
        {QStringLiteral("data_root"), cleanAbsolute(paths.dataRoot)},
        {QStringLiteral("completed_with_attention"), withAttention},
    };
    return writeAtomically(completionMarkerPath(paths),
                           QJsonDocument(marker).toJson(QJsonDocument::Indented),
                           error);
  }

  bool validCompletion(const Paths& paths, QString& error)
  {
    const QString markerPath = completionMarkerPath(paths);
    if (!pathExists(markerPath)) {
      return false;
    }
    if (!isRegularFile(markerPath)) {
      error =
          QStringLiteral("v2 completion marker has an unsafe type: %1").arg(markerPath);
      return false;
    }
    const auto marker = readJsonObject(markerPath, error);
    if (!marker || marker->value(QStringLiteral("version")).toInt() != 2 ||
        cleanAbsolute(marker->value(QStringLiteral("legacy_root")).toString()) !=
            cleanAbsolute(paths.legacyRoot) ||
        cleanAbsolute(marker->value(QStringLiteral("data_root")).toString()) !=
            cleanAbsolute(paths.dataRoot) ||
        !marker->value(QStringLiteral("completed_with_attention")).isBool()) {
      error = QStringLiteral("v2 completion marker does not match this migration");
      return false;
    }
    return true;
  }

}  // namespace

QString completionMarkerPath(const Paths& paths)
{
  return QDir(paths.dataRoot).filePath(QString::fromLatin1(CompletionName));
}

QString prefixReceiptPath(const Paths& paths)
{
  return QDir(paths.legacyRoot).filePath(QString::fromLatin1(IntentName));
}

Paths defaultPaths()
{
  QString configRoot = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
  if (configRoot.isEmpty()) {
    configRoot = QDir::homePath() + QStringLiteral("/.config");
  }
  const QString legacyRoot =
      QDir::homePath() + QStringLiteral("/.var/app/com.fluorine.manager");
  const QString dataRoot = QDir::homePath() + QStringLiteral("/.local/share/fluorine");
  return {legacyRoot, dataRoot, configRoot, {}, 30000};
}

Result migrate(const Paths& rawPaths)
{
  Result result;
  if (rawPaths.legacyRoot.isEmpty() || rawPaths.dataRoot.isEmpty() ||
      rawPaths.configRoot.isEmpty()) {
    result.status = Status::Failed;
    appendDiagnostic(result, QStringLiteral("migration roots must not be empty"));
    return result;
  }
  Paths paths{cleanAbsolute(rawPaths.legacyRoot), cleanAbsolute(rawPaths.dataRoot),
              cleanAbsolute(rawPaths.configRoot), rawPaths.legacyProcessKey,
              rawPaths.lockTimeoutMs};
  if (!pathExists(paths.legacyRoot)) {
    return result;
  }
  if (hasSymlinkInAbsolutePath(paths.legacyRoot)) {
    result.status = Status::Attention;
    appendDiagnostic(
        result,
        QStringLiteral("legacy Flatpak root has symlinked ancestry and was left "
                       "untouched: %1")
            .arg(paths.legacyRoot));
    return result;
  }
  if (!isRealDirectory(paths.legacyRoot)) {
    result.status = Status::Attention;
    appendDiagnostic(result,
                     QStringLiteral("legacy Flatpak root is not a real directory: %1")
                         .arg(paths.legacyRoot));
    return result;
  }
  QString error;
  if (pathExists(completionMarkerPath(paths))) {
    QString markerError;
    if (!validCompletion(paths, markerError)) {
      result.status = Status::Failed;
      appendDiagnostic(result, markerError);
      return result;
    }
    result.status = Status::Complete;
    return result;
  }

  const Inventory inventory = inventoryFor(paths);
  if (!hasRecognizedLegacyData(paths, inventory)) {
    return result;
  }

  const auto rootsOverlap = [](const QString& left, const QString& right) {
    return isAtOrBelow(left, right) || isAtOrBelow(right, left);
  };
  if (rootsOverlap(paths.legacyRoot, paths.dataRoot) ||
      rootsOverlap(paths.legacyRoot, paths.configRoot) ||
      rootsOverlap(paths.dataRoot, paths.configRoot)) {
    result.status = Status::Failed;
    appendDiagnostic(result, QStringLiteral("migration roots overlap and are unsafe"));
    return result;
  }

  if (hasSymlinkInAbsolutePath(paths.dataRoot) ||
      hasSymlinkInAbsolutePath(paths.configRoot)) {
    result.status = Status::Attention;
    appendDiagnostic(
        result, QStringLiteral("native migration destination has symlinked ancestry; "
                               "legacy data was left untouched (%1, %2)")
                    .arg(paths.dataRoot, paths.configRoot));
    return result;
  }

  QLockFile lock(QDir(paths.legacyRoot).filePath(QString::fromLatin1(LockName)));
  if (!lock.tryLock(std::max(0, paths.lockTimeoutMs))) {
    result.status = Status::Failed;
    appendDiagnostic(result,
                     QStringLiteral("another Flatpak migration is active; close the "
                                    "other Fluorine process (lock error %1)")
                         .arg(static_cast<int>(lock.error())));
    return result;
  }
  // A process may have completed the work while this caller waited.
  if (pathExists(completionMarkerPath(paths))) {
    QString markerError;
    if (validCompletion(paths, markerError)) {
      result.status = Status::Complete;
      return result;
    }
    result.status = Status::Failed;
    appendDiagnostic(result, markerError);
    return result;
  }

  const QString legacyProcessKey = paths.legacyProcessKey.isEmpty()
                                       ? QString::fromLatin1(DefaultLegacyProcessKey)
                                       : paths.legacyProcessKey;
  // Historical --multiple processes could ignore this key, so it is only a
  // best-effort pre-mutation admission check. QLockFile serializes repaired
  // versions, and this RAII object removes its IPC segment on every normal exit.
  QSharedMemory legacyProcess(legacyProcessKey);
  QString legacyProcessError;
  if (!acquireLegacyProcessGuard(legacyProcess, legacyProcessError)) {
    result.status = Status::Failed;
    appendDiagnostic(result,
                     QStringLiteral("an older Fluorine process may still be using "
                                    "Flatpak data; close it before migrating (%1)")
                         .arg(legacyProcessError));
    return result;
  }

  if (!pathExists(paths.dataRoot) && !QDir().mkpath(paths.dataRoot)) {
    result.status = Status::Failed;
    appendDiagnostic(
        result,
        QStringLiteral("cannot create native data root %1").arg(paths.dataRoot));
    return result;
  }
  if (!isRealDirectory(paths.dataRoot)) {
    result.status = Status::Failed;
    appendDiagnostic(result,
                     QStringLiteral("native data root is not a real directory: %1")
                         .arg(paths.dataRoot));
    return result;
  }

  QStringList attention;
  bool blockingAttention = false;
  bool dangerous         = false;
  std::optional<Plan> plan;
  if (pathExists(prefixReceiptPath(paths))) {
    plan = decodePlan(prefixReceiptPath(paths), paths, inventory.prefixSources,
                      inventory.instanceParents, error);
    if (!plan) {
      result.status = Status::Failed;
      appendDiagnostic(result, error);
      return result;
    }
    if (const auto topologyError = planTopologyError(plan->moves)) {
      result.status = Status::Failed;
      appendDiagnostic(result, *topologyError);
      return result;
    }
    attention         = plan->attention;
    blockingAttention = plan->blockingAttention;
  } else {
    Plan discovered;
    discovered.legacyRoot = paths.legacyRoot;
    discovered.dataRoot   = paths.dataRoot;
    appendRetainedRuntimeAttention(inventory, attention);
    discovered.moves =
        discoverMoves(paths, inventory, attention, blockingAttention, dangerous);
    if (const auto topologyError = planTopologyError(discovered.moves)) {
      blockingAttention = true;
      dangerous         = true;
      attention.append(*topologyError);
      discovered.moves.clear();
    }
    if (dangerous) {
      // Discovery has not mutated anything. Keep the native installation
      // usable while requiring the user to resolve the retained legacy data.
      result.status      = Status::Attention;
      result.diagnostics = attention;
      QString reportError;
      if (!writeAttentionReport(paths, attention, result.attentionReport,
                                reportError)) {
        appendDiagnostic(result, reportError);
      }
      return result;
    }
    discovered.attention         = attention;
    discovered.blockingAttention = blockingAttention;
    if (!discovered.moves.isEmpty()) {
      if (!writeAtomically(prefixReceiptPath(paths), encodePlan(discovered), error)) {
        result.status = Status::Failed;
        appendDiagnostic(result, error);
        return result;
      }
      plan = std::move(discovered);
    }
  }

  QString prefixSource;
  QString prefixDestination;
  if (plan && !applyPlan(*plan, prefixSource, prefixDestination, error)) {
    result.status = Status::Failed;
    appendDiagnostic(result, error);
    return result;
  }

  QSet<QString> movedInstanceNames;
  if (plan) {
    for (const Move& move : plan->moves) {
      if (move.kind == QStringLiteral("instance")) {
        movedInstanceNames.insert(QFileInfo(move.destination).fileName());
      }
    }
  }

  if (!migrateConfig(paths, inventory, prefixSource, prefixDestination, attention,
                     blockingAttention, dangerous, error)) {
    result.status = Status::Failed;
    appendDiagnostic(result, error);
    return result;
  }
  if (dangerous) {
    result.status      = Status::Failed;
    result.diagnostics = attention;
    QString reportError;
    if (!writeAttentionReport(paths, attention, result.attentionReport, reportError)) {
      appendDiagnostic(result, reportError);
    }
    return result;
  }
  if (!migrateGlobalSettings(paths, inventory, movedInstanceNames, attention,
                             blockingAttention, error)) {
    result.status = Status::Failed;
    appendDiagnostic(result, error);
    return result;
  }

  if (plan && !removeFileDurably(prefixReceiptPath(paths), error)) {
    result.status = Status::Failed;
    appendDiagnostic(result, error);
    return result;
  }

  if (blockingAttention) {
    result.status      = Status::Attention;
    result.diagnostics = attention;
    if (!writeAttentionReport(paths, attention, result.attentionReport, error)) {
      appendDiagnostic(result, error);
    }
    return result;
  }

  const bool withAttention = !attention.isEmpty();
  if (withAttention &&
      !writeAttentionReport(paths, attention, result.attentionReport, error)) {
    result.status      = Status::Attention;
    result.diagnostics = attention;
    appendDiagnostic(result, error);
    return result;
  }
  if (!writeCompletion(paths, withAttention, error)) {
    result.status = Status::Failed;
    appendDiagnostic(result, error);
    return result;
  }
  if (!withAttention) {
    removeOwnedAttentionReport(paths);
  }

  result.status      = Status::Complete;
  result.diagnostics = attention;
  return result;
}

}  // namespace LegacyFlatpakMigration
