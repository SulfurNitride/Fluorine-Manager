#include "wineruntimeconfig.h"

#include "fluorineconfig.h"
#include "settingsmigration.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSettings>
#include <QVariant>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

namespace WineRuntimeConfig {
namespace {

struct Candidate {
  bool present{false};
  QString value;
  QString anchorFile;
  Source source{Source::None};
};

std::atomic<std::uint64_t> g_generation{0};
std::mutex g_snapshotMutex;
Snapshot g_snapshot;

FileIdentity identityFor(const QString &path) {
  FileIdentity result;
  const QFileInfo info(path);
  result.canonicalPath = info.canonicalFilePath();
  if (result.canonicalPath.isEmpty()) {
    return result;
  }
#ifdef Q_OS_UNIX
  struct stat status = {};
  const QByteArray encoded = QFile::encodeName(result.canonicalPath);
  if (::stat(encoded.constData(), &status) != 0) {
    return {};
  }
  result.device = static_cast<std::uint64_t>(status.st_dev);
  result.inode = static_cast<std::uint64_t>(status.st_ino);
  result.size = static_cast<std::uint64_t>(status.st_size);
#ifdef __APPLE__
  result.modificationNanoseconds =
      static_cast<std::int64_t>(status.st_mtimespec.tv_sec) * 1000000000LL +
      status.st_mtimespec.tv_nsec;
  result.changeNanoseconds =
      static_cast<std::int64_t>(status.st_ctimespec.tv_sec) * 1000000000LL +
      status.st_ctimespec.tv_nsec;
#else
  result.modificationNanoseconds =
      static_cast<std::int64_t>(status.st_mtim.tv_sec) * 1000000000LL +
      status.st_mtim.tv_nsec;
  result.changeNanoseconds =
      static_cast<std::int64_t>(status.st_ctim.tv_sec) * 1000000000LL +
      status.st_ctim.tv_nsec;
#endif
#else
  result.size = static_cast<std::uint64_t>(info.size());
  result.modificationNanoseconds =
      info.lastModified().toMSecsSinceEpoch() * 1000000LL;
#endif
  result.present = true;
  return result;
}

bool sameDirectoryIdentity(const FileIdentity &expected, const QString &path) {
  const FileIdentity current = identityFor(path);
  if (!expected.present || !current.present) {
    return false;
  }
#ifdef Q_OS_UNIX
  return expected.device == current.device && expected.inode == current.inode;
#else
  return expected.canonicalPath == current.canonicalPath;
#endif
}

bool sameFileIdentity(const FileIdentity &expected, const QString &path) {
  const FileIdentity current = identityFor(path);
  if (!expected.present || !current.present) {
    return false;
  }
#ifdef Q_OS_UNIX
  return expected.device == current.device && expected.inode == current.inode &&
         expected.size == current.size &&
         expected.modificationNanoseconds == current.modificationNanoseconds &&
         expected.changeNanoseconds == current.changeNanoseconds;
#else
  return expected.canonicalPath == current.canonicalPath &&
         expected.size == current.size &&
         expected.modificationNanoseconds == current.modificationNanoseconds;
#endif
}

QString anchoredPath(const QString &value, const QString &anchorFile) {
  const QString trimmed = value.trimmed();
  if (trimmed.isEmpty()) {
    return {};
  }

  const QFileInfo info(trimmed);
  if (info.isAbsolute()) {
    return QDir::cleanPath(info.absoluteFilePath());
  }

  return QDir::cleanPath(
      QDir(QFileInfo(anchorFile).absolutePath()).absoluteFilePath(trimmed));
}

QString physicalDirectory(const QString &path) {
  const QFileInfo info(path);
  if (!info.isDir()) {
    return {};
  }
  return info.canonicalFilePath();
}

QString selectWineUser(const QString &prefixPath, QString &error) {
  const QString usersPath = QDir(prefixPath).filePath("drive_c/users");
  if (QFileInfo(usersPath).isSymLink()) {
    error = QStringLiteral("Wine prefix '%1' has a symlinked users directory")
                .arg(prefixPath);
    return {};
  }
  const QDir users(usersPath);
  if (!users.exists()) {
    error = QStringLiteral("Wine prefix '%1' has no users directory")
                .arg(prefixPath);
    return {};
  }

  const QFileInfoList entries = users.entryInfoList(
      QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
      QDir::Name);
  QStringList steamUsers;
  QStringList ordinaryUsers;
  const QStringList systemUsers{"Public",       "root",      "Default",
                                "Default User", "All Users", "defaultuser0"};
  for (const QFileInfo &entryInfo : entries) {
    const QString entry = entryInfo.fileName();
    if (entryInfo.isSymLink()) {
      error = QStringLiteral("Wine prefix '%1' has a symlinked user profile '%2'")
                  .arg(prefixPath, entry);
      return {};
    }
    if (entry.compare("steamuser", Qt::CaseInsensitive) == 0) {
      steamUsers.append(entry);
      continue;
    }

    bool system = false;
    for (const QString &name : systemUsers) {
      if (entry.compare(name, Qt::CaseInsensitive) == 0) {
        system = true;
        break;
      }
    }
    if (!system) {
      ordinaryUsers.append(entry);
    }
  }

  if (steamUsers.size() > 1) {
    error =
        QStringLiteral("Wine prefix '%1' has ambiguous steamuser case variants")
            .arg(prefixPath);
    return {};
  }
  if (steamUsers.size() == 1) {
    return physicalDirectory(users.filePath(steamUsers.front()));
  }
  if (ordinaryUsers.size() > 1) {
    error = QStringLiteral(
                "Wine prefix '%1' has multiple possible user profiles: %2")
                .arg(prefixPath, ordinaryUsers.join(QStringLiteral(", ")));
    return {};
  }
  if (ordinaryUsers.size() == 1) {
    return physicalDirectory(users.filePath(ordinaryUsers.front()));
  }
  error = QStringLiteral("Wine prefix '%1' has no usable user profile")
              .arg(prefixPath);
  return {};
}

void normalizePrefix(const Candidate &candidate, Snapshot &result) {
  if (!candidate.present) {
    return;
  }
  result.prefixSource = candidate.source;

  const QString selected = anchoredPath(candidate.value, candidate.anchorFile);
  if (selected.isEmpty()) {
    result.prefixError = QStringLiteral("The selected %1 Wine prefix is empty")
                             .arg(sourceName(candidate.source));
    return;
  }

  const bool direct = QFileInfo(QDir(selected).filePath("drive_c")).isDir();
  const bool nested = QFileInfo(QDir(selected).filePath("pfx/drive_c")).isDir();
  if (direct == nested) {
    result.prefixError =
        direct ? QStringLiteral("Wine prefix '%1' contains both drive_c and "
                                "pfx/drive_c; choose one layout explicitly")
                     .arg(selected)
               : QStringLiteral("Wine prefix '%1' contains neither drive_c nor "
                                "pfx/drive_c")
                     .arg(selected);
    return;
  }

  const QFileInfo structuralPrefix(nested ? QDir(selected).filePath("pfx")
                                          : selected);
  const QFileInfo drive(QDir(structuralPrefix.filePath()).filePath("drive_c"));
  if ((nested && structuralPrefix.isSymLink()) || drive.isSymLink()) {
    result.prefixError =
        QStringLiteral("Wine prefix '%1' uses a symlink for its pfx/drive_c "
                       "structure")
            .arg(selected);
    return;
  }

  const QString directPrefix =
      nested ? QDir(selected).filePath("pfx") : selected;
  result.prefixPath = physicalDirectory(directPrefix);
  if (result.prefixPath.isEmpty()) {
    result.prefixError =
        QStringLiteral("Wine prefix '%1' has no stable physical identity")
            .arg(directPrefix);
    return;
  }

  result.compatDataPath =
      nested
          ? physicalDirectory(selected)
          : (QFileInfo(result.prefixPath)
                         .fileName()
                         .compare("pfx", Qt::CaseSensitive) == 0
                 ? physicalDirectory(QFileInfo(result.prefixPath).dir().path())
                 : QString());
  result.prefixIdentity = identityFor(result.prefixPath);
  result.driveIdentity =
      identityFor(QDir(result.prefixPath).filePath("drive_c"));
  if (!result.compatDataPath.isEmpty()) {
    result.compatDataIdentity = identityFor(result.compatDataPath);
  }
  if (!result.prefixIdentity.present || !result.driveIdentity.present ||
      (!result.compatDataPath.isEmpty() &&
       !result.compatDataIdentity.present)) {
    result.prefixError =
        QStringLiteral("Wine prefix '%1' changed while it was being resolved")
            .arg(selected);
    return;
  }
  result.userProfilePath =
      selectWineUser(result.prefixPath, result.prefixError);
  if (!result.userProfilePath.isEmpty()) {
    result.userProfileIdentity = identityFor(result.userProfilePath);
    if (!result.userProfileIdentity.present) {
      result.prefixError =
          QStringLiteral("Wine user profile '%1' changed while it was resolved")
              .arg(result.userProfilePath);
    }
  }
}

void normalizeProton(const Candidate &candidate, Snapshot &result) {
  if (!candidate.present) {
    return;
  }
  result.protonSource = candidate.source;

  const QString selected = anchoredPath(candidate.value, candidate.anchorFile);
  if (selected.isEmpty()) {
    result.protonError = QStringLiteral("The selected %1 Proton path is empty")
                             .arg(sourceName(candidate.source));
    return;
  }

  QFileInfo info(selected);
  if (info.isDir()) {
    const QFileInfo script(QDir(selected).filePath("proton"));
    if (!script.isFile()) {
      result.protonError =
          QStringLiteral(
              "Proton directory '%1' does not contain a proton script")
              .arg(selected);
      return;
    }
    if (!script.isExecutable()) {
      result.protonError =
          QStringLiteral("Proton script '%1' is not executable")
              .arg(script.filePath());
      return;
    }
    const QFileInfo wineFiles(QDir(selected).filePath("files/bin/wine"));
    const QFileInfo wineDist(QDir(selected).filePath("dist/bin/wine"));
    if ((!wineFiles.isFile() || !wineFiles.isExecutable()) &&
        (!wineDist.isFile() || !wineDist.isExecutable())) {
      result.protonError =
          QStringLiteral("Proton directory '%1' has no executable Wine runtime")
              .arg(selected);
      return;
    }
    result.protonPath = info.canonicalFilePath();
    result.protonWinePath =
        wineFiles.isFile() && wineFiles.isExecutable()
            ? wineFiles.canonicalFilePath()
            : wineDist.canonicalFilePath();
    result.protonRootIdentity = identityFor(result.protonPath);
    result.protonWineIdentity = identityFor(result.protonWinePath);
    result.protonIdentity = identityFor(script.filePath());
    return;
  }

  if (!info.isFile()) {
    result.protonError =
        QStringLiteral(
            "Proton path '%1' does not exist or is not a regular file")
            .arg(selected);
    return;
  }
  if (!info.isExecutable()) {
    result.protonError =
        QStringLiteral("Proton script '%1' is not executable").arg(selected);
    return;
  }
  result.protonPath = info.canonicalFilePath();
  result.protonIdentity = identityFor(result.protonPath);
}

Candidate firstCandidate(bool explicitPresent, const QString &explicitValue,
                         const QString &instanceIniPath,
                         bool defaultConfigPresent, bool defaultConfigInvalid,
                         const QString &defaultValue,
                         const QString &defaultConfigPath,
                         const QString &legacyValue) {
  if (explicitPresent) {
    return {true, explicitValue, instanceIniPath, Source::InstanceExplicit};
  }
  if (defaultConfigInvalid) {
    return {true, {}, defaultConfigPath, Source::ApplicationDefault};
  }
  if (defaultConfigPresent || !defaultValue.trimmed().isEmpty()) {
    return {true, defaultValue, defaultConfigPath, Source::ApplicationDefault};
  }
  if (!legacyValue.trimmed().isEmpty()) {
    return {true, legacyValue, instanceIniPath, Source::InstanceLegacy};
  }
  return {};
}

QString firstValue(const QSettings &settings, const QStringList &keys) {
  for (const QString &key : keys) {
    const QString value = settings.value(key).toString().trimmed();
    if (!value.isEmpty()) {
      return value;
    }
  }
  return {};
}

} // namespace

QString sourceName(Source source) {
  switch (source) {
  case Source::InstanceExplicit:
    return QStringLiteral("instance override");
  case Source::ApplicationDefault:
    return QStringLiteral("application default");
  case Source::InstanceLegacy:
    return QStringLiteral("legacy instance setting");
  case Source::None:
  default:
    return QStringLiteral("unconfigured");
  }
}

Snapshot resolve(const Inputs &inputs) {
  Snapshot result;
  if (inputs.instanceConfigInvalid) {
    result.prefixSource = Source::InstanceExplicit;
    result.protonSource = Source::InstanceExplicit;
    result.prefixError =
        QStringLiteral("The selected instance configuration is unreadable or malformed");
    result.protonError = result.prefixError;
    return result;
  }
  normalizePrefix(firstCandidate(inputs.explicitPrefixPresent,
                                 inputs.explicitPrefix, inputs.instanceIniPath,
                                 inputs.defaultConfigPresent,
                                 inputs.defaultConfigInvalid,
                                 inputs.defaultPrefix, inputs.defaultConfigPath,
                                 inputs.legacyPrefix),
                  result);
  normalizeProton(firstCandidate(inputs.explicitProtonPresent,
                                 inputs.explicitProton, inputs.instanceIniPath,
                                 inputs.defaultConfigPresent,
                                 inputs.defaultConfigInvalid,
                                 inputs.defaultProton, inputs.defaultConfigPath,
                                 inputs.legacyProton),
                  result);
  return result;
}

Snapshot resolveInstance(const QString &iniPath) {
  QSettings instance(iniPath, QSettings::IniFormat);
  Inputs inputs;
  inputs.instanceIniPath = iniPath;
  inputs.explicitPrefixPresent = instance.contains("fluorine/prefix_path");
  inputs.explicitPrefix = instance.value("fluorine/prefix_path").toString();
  inputs.explicitProtonPresent = instance.contains("fluorine/proton_path");
  inputs.explicitProton = instance.value("fluorine/proton_path").toString();
  inputs.legacyPrefix =
      firstValue(instance, {"Settings/proton_prefix_path",
                            "Settings/prefix_path", "Proton/prefix_path"});
  inputs.legacyProton =
      firstValue(instance, {"Settings/proton_path", "Proton/path"});
  instance.sync();
  inputs.instanceConfigInvalid = instance.status() != QSettings::NoError;

  inputs.defaultConfigPath = FluorineConfig::configFilePath();
  // A complete explicit pair does not consult or wait for its lower-authority
  // application default. Otherwise serialize the read with create/delete/
  // recreate and Winetricks so a launch cannot capture a half-transitioned
  // global runtime.
  std::unique_ptr<QLockFile> configLock;
  if (!inputs.instanceConfigInvalid &&
      (!inputs.explicitPrefixPresent || !inputs.explicitProtonPresent)) {
    const QFileInfo configInfo(inputs.defaultConfigPath);
    if (configInfo.dir().exists()) {
      configLock = std::make_unique<QLockFile>(
          SettingsMigration::settingsLockPath(inputs.defaultConfigPath));
      configLock->setStaleLockTime(SettingsMigration::SettingsLockStaleMs);
      if (!configLock->tryLock(SettingsMigration::SettingsLockTimeoutMs)) {
        inputs.defaultConfigInvalid = true;
        return resolve(inputs);
      }
    }

    const auto global = FluorineConfig::load();
    if (global) {
      inputs.defaultConfigPresent = true;
      inputs.defaultPrefix = global->prefix_path;
      inputs.defaultProton = global->proton_path;
    } else if (QFileInfo::exists(inputs.defaultConfigPath) ||
               QFileInfo(inputs.defaultConfigPath).isSymLink()) {
      inputs.defaultConfigInvalid = true;
    }
  }
  return resolve(inputs);
}

bool publish(Snapshot snapshot) {
  auto *application = QCoreApplication::instance();
  if (application == nullptr) {
    return false;
  }
  snapshot.generation =
      g_generation.fetch_add(1, std::memory_order_relaxed) + 1;
  {
    const std::scoped_lock lock(g_snapshotMutex);
    g_snapshot = snapshot;
  }
  application->setProperty(PrefixProperty, snapshot.prefixPath);
  application->setProperty(CompatDataProperty, snapshot.compatDataPath);
  application->setProperty(UserProfileProperty, snapshot.userProfilePath);
  application->setProperty(ProtonProperty, snapshot.protonPath);
  application->setProperty(PrefixErrorProperty, snapshot.prefixError);
  application->setProperty(ProtonErrorProperty, snapshot.protonError);
  application->setProperty(PrefixSourceProperty,
                           static_cast<int>(snapshot.prefixSource));
  application->setProperty(ProtonSourceProperty,
                           static_cast<int>(snapshot.protonSource));
  application->setProperty(
      GenerationProperty, QVariant::fromValue<qulonglong>(snapshot.generation));
  return true;
}

Snapshot current() {
  const std::scoped_lock lock(g_snapshotMutex);
  return g_snapshot;
}

void clear() {
  auto *application = QCoreApplication::instance();
  {
    const std::scoped_lock lock(g_snapshotMutex);
    g_snapshot = {};
  }
  if (application == nullptr) {
    return;
  }
  for (const char *property :
       {PrefixProperty, CompatDataProperty, UserProfileProperty, ProtonProperty,
        PrefixErrorProperty, ProtonErrorProperty, PrefixSourceProperty,
        ProtonSourceProperty, GenerationProperty}) {
    application->setProperty(property, {});
  }
}

bool revalidatePrefix(const Snapshot &snapshot, QString *error) {
  const auto fail = [&](const QString &message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };

  if (!snapshot.prefixError.isEmpty()) {
    return fail(snapshot.prefixError);
  }
  if (!snapshot.prefixPath.isEmpty()) {
    if (!sameDirectoryIdentity(snapshot.prefixIdentity, snapshot.prefixPath) ||
        !sameDirectoryIdentity(snapshot.driveIdentity,
                               QDir(snapshot.prefixPath).filePath("drive_c"))) {
      return fail(
          QStringLiteral("The selected Wine prefix changed after setup"));
    }
    if (!snapshot.compatDataPath.isEmpty() &&
        !sameDirectoryIdentity(snapshot.compatDataIdentity,
                               snapshot.compatDataPath)) {
      return fail(QStringLiteral(
          "The selected Proton compatdata root changed after setup"));
    }
    if (snapshot.userProfilePath.isEmpty() ||
        !sameDirectoryIdentity(snapshot.userProfileIdentity,
                               snapshot.userProfilePath)) {
      return fail(
          QStringLiteral("The selected Wine user profile changed after setup"));
    }
  }
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

bool revalidateProton(const Snapshot &snapshot, QString *error) {
  const auto fail = [&](const QString &message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  if (!snapshot.protonError.isEmpty()) {
    return fail(snapshot.protonError);
  }
  if (!snapshot.protonPath.isEmpty()) {
    const QFileInfo proton(snapshot.protonPath);
    const QString script = proton.isDir()
                               ? QDir(snapshot.protonPath).filePath("proton")
                               : snapshot.protonPath;
    if ((proton.isDir() &&
         !sameDirectoryIdentity(snapshot.protonRootIdentity,
                                snapshot.protonPath)) ||
        (!snapshot.protonWinePath.isEmpty() &&
         !sameFileIdentity(snapshot.protonWineIdentity,
                           snapshot.protonWinePath)) ||
        !sameFileIdentity(snapshot.protonIdentity, script)) {
      return fail(
          QStringLiteral("The selected Proton installation changed after "
                         "setup; restart Fluorine Manager"));
    }
  }
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

bool revalidate(const Snapshot &snapshot, QString *error) {
  return revalidatePrefix(snapshot, error) && revalidateProton(snapshot, error);
}

} // namespace WineRuntimeConfig
