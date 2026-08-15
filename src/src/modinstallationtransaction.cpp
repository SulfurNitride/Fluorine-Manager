#include "modinstallationtransaction.h"

#include <algorithm>
#include <cerrno>
#include <cstring>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QTemporaryDir>

#include <uibase/utility.h>

#if defined(Q_OS_LINUX)
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#if defined(Q_OS_LINUX) && defined(SYS_renameat2) &&                           \
    defined(RENAME_NOREPLACE) && defined(RENAME_EXCHANGE)
#define FLUORINE_HAS_ATOMIC_MOD_RENAME 1
#endif

struct ModInstallationTransaction::Identity {
  bool exists{false};
  bool directory{false};
  bool symlink{false};
#ifdef Q_OS_LINUX
  dev_t device{};
  ino_t inode{};
#else
  QString canonicalPath;
#endif
};

namespace {

using Identity = ModInstallationTransaction::Identity;

bool validLeafName(const QString &name) {
  return !name.isEmpty() && name != QStringLiteral(".") &&
         name != QStringLiteral("..") && !QDir::isAbsolutePath(name) &&
         !name.contains('/') && !name.contains('\\') &&
         !name.contains(QChar::Null);
}

bool inspect(const QString &path, Identity &identity, QString &error) {
  identity = {};
#ifdef Q_OS_LINUX
  struct stat status{};
  const QByteArray encoded = QFile::encodeName(path);
  if (::lstat(encoded.constData(), &status) != 0) {
    if (errno == ENOENT) {
      return true;
    }
    error = QStringLiteral("Could not inspect '%1': %2")
                .arg(path, QString::fromLocal8Bit(std::strerror(errno)));
    return false;
  }
  identity.exists = true;
  identity.directory = S_ISDIR(status.st_mode);
  identity.symlink = S_ISLNK(status.st_mode);
  identity.device = status.st_dev;
  identity.inode = status.st_ino;
#else
  const QFileInfo info(path);
  identity.exists = info.exists() || info.isSymLink();
  identity.directory = info.isDir();
  identity.symlink = info.isSymLink();
  identity.canonicalPath = info.canonicalFilePath();
#endif
  return true;
}

bool sameIdentity(const Identity &left, const Identity &right) {
  if (left.exists != right.exists || left.directory != right.directory ||
      left.symlink != right.symlink) {
    return false;
  }
  if (!left.exists) {
    return true;
  }
#ifdef Q_OS_LINUX
  return left.device == right.device && left.inode == right.inode;
#else
  return left.canonicalPath == right.canonicalPath;
#endif
}

QString identityToken(const Identity &identity) {
  if (!identity.exists) {
    return {};
  }
#ifdef Q_OS_LINUX
  return QStringLiteral("%1:%2")
      .arg(static_cast<qulonglong>(identity.device))
      .arg(static_cast<qulonglong>(identity.inode));
#else
  return identity.canonicalPath;
#endif
}

bool findCaseFamily(const QString &parent, const QString &leaf,
                    QStringList &matches, QString &error) {
  matches.clear();
  const QDir directory(parent);
  if (!directory.exists()) {
    error =
        QStringLiteral("Mod directory root '%1' does not exist.").arg(parent);
    return false;
  }

  const QFileInfoList entries = directory.entryInfoList(
      QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System,
      QDir::Name);
  for (const QFileInfo &entry : entries) {
    if (entry.fileName().compare(leaf, Qt::CaseInsensitive) == 0) {
      matches.append(entry.fileName());
    }
  }
  if (matches.size() > 1) {
    error = QStringLiteral("Mod name '%1' has multiple case variants in '%2'.")
                .arg(leaf, parent);
    return false;
  }
  return true;
}

bool copyReplacementMetadata(const QString &sourceRoot,
                             const QString &stageRoot, QString &error) {
  QStringList family;
  if (!findCaseFamily(sourceRoot, QStringLiteral("meta.ini"), family, error)) {
    return false;
  }
  if (family.isEmpty()) {
    return true;
  }

  const QString source = QDir(sourceRoot).filePath(family.front());
  const QFileInfo info(source);
  if (!info.isFile() || info.isSymLink()) {
    error =
        QStringLiteral("Existing mod metadata '%1' is not an ordinary file.")
            .arg(source);
    return false;
  }

  const QString destination =
      QDir(stageRoot).filePath(QStringLiteral("meta.ini"));
  if (!QFile::copy(source, destination)) {
    error = QStringLiteral("Could not preserve existing mod metadata '%1'.")
                .arg(source);
    return false;
  }
  return true;
}

bool normalizeStagedMetadata(const QString &stageRoot, QString &error) {
  QStringList family;
  if (!findCaseFamily(stageRoot, QStringLiteral("meta.ini"), family, error)) {
    return false;
  }
  if (family.isEmpty() || family.front() == QStringLiteral("meta.ini")) {
    return true;
  }

  const QString existing = QDir(stageRoot).filePath(family.front());
  const QFileInfo info(existing);
  if (!info.isFile() || info.isSymLink()) {
    error = QStringLiteral("Staged mod metadata '%1' is not an ordinary file.")
                .arg(existing);
    return false;
  }
  if (!QDir(stageRoot).rename(family.front(), QStringLiteral("meta.ini"))) {
    error = QStringLiteral("Could not normalize staged mod metadata '%1'.")
                .arg(existing);
    return false;
  }
  return true;
}

bool metadataGeneration(const QString &targetRoot, QString &generation,
                        QString &error) {
  QStringList family;
  if (!findCaseFamily(targetRoot, QStringLiteral("meta.ini"), family, error)) {
    return false;
  }
  if (family.isEmpty()) {
    generation = QStringLiteral("absent");
    return true;
  }

  const QString path = QDir(targetRoot).filePath(family.front());
  Identity before;
  Identity after;
  if (!inspect(path, before, error) || !before.exists || before.symlink ||
      before.directory) {
    if (error.isEmpty()) {
      error = QStringLiteral("Mod metadata '%1' is not an ordinary file.")
                  .arg(path);
    }
    return false;
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    error = QStringLiteral("Could not read mod metadata '%1'.").arg(path);
    return false;
  }
  const QByteArray digest =
      QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
  if (file.error() != QFileDevice::NoError || !inspect(path, after, error) ||
      !sameIdentity(before, after)) {
    if (error.isEmpty()) {
      error = QStringLiteral("Mod metadata '%1' changed while it was read.")
                  .arg(path);
    }
    return false;
  }
  generation = family.front() + QLatin1Char(':') + identityToken(before) +
               QLatin1Char(':') + QString::fromLatin1(digest.toHex());
  return true;
}

#ifdef Q_OS_LINUX
bool syncDirectory(const QString &path, QString &error) {
  const QByteArray encoded = QFile::encodeName(path);
  const int descriptor =
      ::open(encoded.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (descriptor < 0) {
    error = QStringLiteral(
                "Could not open mod directory '%1' for synchronization: %2")
                .arg(path, QString::fromLocal8Bit(std::strerror(errno)));
    return false;
  }
  const int result = ::fsync(descriptor);
  const int savedError = errno;
  ::close(descriptor);
  if (result != 0) {
    error = QStringLiteral("Could not synchronize mod directory '%1': %2")
                .arg(path, QString::fromLocal8Bit(std::strerror(savedError)));
    return false;
  }
  return true;
}
#endif

} // namespace

bool ModInstallationTransaction::inspectTarget(const QString &modsRoot,
                                               const QString &requestedName,
                                               Target &target, QString &error) {
  target = {};
  error.clear();
  if (!validLeafName(requestedName)) {
    error =
        QStringLiteral("Invalid mod directory name '%1'.").arg(requestedName);
    return false;
  }

  const QFileInfo rootInfo(modsRoot);
  if (!rootInfo.exists() || !rootInfo.isDir()) {
    error = QStringLiteral("Mod directory root '%1' is not a real directory.")
                .arg(modsRoot);
    return false;
  }
  const QString root = rootInfo.canonicalFilePath();
  if (root.isEmpty()) {
    error = QStringLiteral("Mod directory root '%1' cannot be authenticated.")
                .arg(modsRoot);
    return false;
  }

  QStringList family;
  if (!findCaseFamily(root, requestedName, family, error)) {
    return false;
  }

  target.name = family.isEmpty() ? requestedName : family.front();
  target.path = QDir(root).filePath(target.name);
  target.exists = !family.isEmpty();
  if (target.exists) {
    Identity identity;
    if (!inspect(target.path, identity, error)) {
      return false;
    }
    if (!identity.exists || !identity.directory || identity.symlink) {
      error = QStringLiteral("Mod target '%1' is not a real directory.")
                  .arg(target.path);
      return false;
    }
    target.generation = identityToken(identity);
  }
  return true;
}

bool ModInstallationTransaction::prepareStagedFile(const QString &stageRoot,
                                                   const QString &relativePath,
                                                   bool createParents,
                                                   QString &absolutePath,
                                                   QString &error) {
  absolutePath.clear();
  error.clear();
  const QString normalized = QString(relativePath).replace('\\', '/');
  const QStringList components = normalized.split('/', Qt::KeepEmptyParts);
  if (normalized.isEmpty() || QDir::isAbsolutePath(normalized) ||
      MOBase::isWindowsDrivePath(normalized) ||
      normalized.contains(QChar::Null) ||
      std::any_of(
          components.cbegin(), components.cend(), [](const QString &component) {
            return component.isEmpty() || component == QStringLiteral(".") ||
                   component == QStringLiteral("..");
          })) {
    error = QStringLiteral("Unsafe staged mod path '%1'.").arg(relativePath);
    return false;
  }

  const QFileInfo rootInfo(stageRoot);
  if (!rootInfo.exists() || !rootInfo.isDir() || rootInfo.isSymLink()) {
    error =
        QStringLiteral("Mod installation stage '%1' is not a real directory.")
            .arg(stageRoot);
    return false;
  }

  QString current = rootInfo.absoluteFilePath();
  for (qsizetype index = 0; index + 1 < components.size(); ++index) {
    current = QDir(current).filePath(components[index]);
    QFileInfo info(current);
    if (!info.exists() && !info.isSymLink() && createParents) {
      if (!QDir().mkdir(current)) {
        error = QStringLiteral("Could not create staged mod directory '%1'.")
                    .arg(current);
        return false;
      }
      info.setFile(current);
    }
    if (!info.exists() || !info.isDir() || info.isSymLink()) {
      error = QStringLiteral("Staged mod path has an unsafe parent '%1'.")
                  .arg(current);
      return false;
    }
  }

  absolutePath = QDir(stageRoot).filePath(components.join('/'));
  const QFileInfo finalInfo(absolutePath);
  if ((finalInfo.exists() || finalInfo.isSymLink()) &&
      (!finalInfo.isFile() || finalInfo.isSymLink())) {
    error = QStringLiteral("Staged mod output '%1' is not an ordinary file.")
                .arg(absolutePath);
    absolutePath.clear();
    return false;
  }
  return true;
}

bool ModInstallationTransaction::prepareStagedMetadata(const QString &stageRoot,
                                                       QString &absolutePath,
                                                       QString &error) {
  if (!normalizeStagedMetadata(stageRoot, error)) {
    absolutePath.clear();
    return false;
  }
  return prepareStagedFile(stageRoot, QStringLiteral("meta.ini"), false,
                           absolutePath, error);
}

std::unique_ptr<ModInstallationTransaction> ModInstallationTransaction::begin(
    const QString &modsRoot, const QString &targetName, Mode mode,
    QString &error, const QString &expectedGeneration,
    const std::function<void()> &afterSourceSnapshotForTesting) {
  error.clear();
  const QFileInfo rootInfo(modsRoot);
  if (!rootInfo.exists() || !rootInfo.isDir()) {
    error = QStringLiteral("Mod directory root '%1' is not a real directory.")
                .arg(modsRoot);
    return {};
  }
  const QString root = rootInfo.canonicalFilePath();
  if (root.isEmpty()) {
    error = QStringLiteral("Mod directory root '%1' cannot be authenticated.")
                .arg(modsRoot);
    return {};
  }

  auto lock = std::make_unique<QLockFile>(
      QDir(root).filePath(QStringLiteral(".fluorine-mod-install.lock")));
  lock->setStaleLockTime(0);
  if (!lock->tryLock()) {
    error = QStringLiteral("Another mod installation is using '%1'.").arg(root);
    return {};
  }

  Identity rootIdentity;
  if (!inspect(root, rootIdentity, error) || !rootIdentity.exists ||
      !rootIdentity.directory || rootIdentity.symlink) {
    if (error.isEmpty()) {
      error = QStringLiteral("Mod directory root '%1' changed.").arg(root);
    }
    return {};
  }

  Target target;
  if (!inspectTarget(root, targetName, target, error)) {
    return {};
  }
  if ((mode == Mode::New && target.exists) ||
      (mode != Mode::New && !target.exists)) {
    error =
        QStringLiteral("Mod target '%1' changed while preparing installation.")
            .arg(target.path);
    return {};
  }
  if (!expectedGeneration.isNull() && target.generation != expectedGeneration) {
    error =
        QStringLiteral("Mod target '%1' changed after overwrite confirmation.")
            .arg(target.path);
    return {};
  }

  Identity targetIdentity;
  if (!inspect(target.path, targetIdentity, error)) {
    return {};
  }

  QString sourceMetadataGeneration;
  if (target.exists &&
      !metadataGeneration(target.path, sourceMetadataGeneration, error)) {
    return {};
  }
  if (afterSourceSnapshotForTesting) {
    afterSourceSnapshotForTesting();
  }

  auto stage = std::make_unique<QTemporaryDir>(
      QDir(root).filePath(QStringLiteral(".fluorine-mod-install-XXXXXX")));
  if (!stage->isValid()) {
    error = QStringLiteral(
                "Could not create a private mod installation stage in '%1'.")
                .arg(root);
    return {};
  }

  const QString stagePath = QDir(stage->path()).filePath(target.name);
  if (!QDir().mkdir(stagePath)) {
    error = QStringLiteral("Could not create mod installation candidate '%1'.")
                .arg(stagePath);
    return {};
  }

  QFile::Permissions permissions =
      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner;
  if (target.exists) {
    permissions = QFileInfo(target.path).permissions();
  }
  if (!QFile::setPermissions(stagePath, QFile::ReadOwner | QFile::WriteOwner |
                                            QFile::ExeOwner)) {
    error = QStringLiteral(
                "Could not set permissions on mod installation stage '%1'.")
                .arg(stagePath);
    return {};
  }

  if (mode == Mode::Merge && !MOBase::copyDir(target.path, stagePath, true)) {
    error = QStringLiteral("Could not stage the existing mod '%1' for merging.")
                .arg(target.path);
    return {};
  }
  if (mode == Mode::Replace &&
      !copyReplacementMetadata(target.path, stagePath, error)) {
    return {};
  }
  if (!normalizeStagedMetadata(stagePath, error)) {
    return {};
  }

  if (target.exists) {
    QString metadataAfterStaging;
    if (!metadataGeneration(target.path, metadataAfterStaging, error) ||
        metadataAfterStaging != sourceMetadataGeneration) {
      if (error.isEmpty()) {
        error = QStringLiteral("Mod metadata changed while it was staged.");
      }
      return {};
    }
  }

  Identity stageIdentity;
  if (!inspect(stagePath, stageIdentity, error) || !stageIdentity.exists ||
      !stageIdentity.directory || stageIdentity.symlink) {
    if (error.isEmpty()) {
      error =
          QStringLiteral("Mod installation stage '%1' changed.").arg(stagePath);
    }
    return {};
  }

  return std::unique_ptr<ModInstallationTransaction>(
      new ModInstallationTransaction(root, target, mode, std::move(lock),
                                     std::move(stage), stagePath, rootIdentity,
                                     targetIdentity, stageIdentity, permissions,
                                     sourceMetadataGeneration));
}

ModInstallationTransaction::ModInstallationTransaction(
    QString root, Target target, Mode mode, std::unique_ptr<QLockFile> lock,
    std::unique_ptr<QTemporaryDir> stage, QString stagePath,
    Identity rootIdentity, Identity targetIdentity, Identity stageIdentity,
    QFile::Permissions targetPermissions, QString metadataGeneration)
    : m_Root(std::move(root)), m_Target(std::move(target)), m_Mode(mode),
      m_Lock(std::move(lock)), m_Stage(std::move(stage)),
      m_StagePath(std::move(stagePath)),
      m_RootIdentity(std::make_unique<Identity>(std::move(rootIdentity))),
      m_TargetIdentity(std::make_unique<Identity>(std::move(targetIdentity))),
      m_StageIdentity(std::make_unique<Identity>(std::move(stageIdentity))),
      m_TargetPermissions(targetPermissions),
      m_MetadataGeneration(std::move(metadataGeneration)) {}

ModInstallationTransaction::~ModInstallationTransaction() = default;

QString ModInstallationTransaction::stagePath() const {
  return m_Stage ? m_StagePath : QString{};
}

QString ModInstallationTransaction::targetPath() const { return m_Target.path; }

QString ModInstallationTransaction::targetName() const { return m_Target.name; }

ModInstallationTransaction::Mode ModInstallationTransaction::mode() const {
  return m_Mode;
}

ModInstallationTransaction::PublishResult
ModInstallationTransaction::publish() {
  if (m_Published || !m_Stage) {
    return {
        PublishStatus::Failure,
        QStringLiteral("Mod installation transaction was already published."),
        {}};
  }

  QString error;
  Identity currentRoot;
  Identity currentStage;
  Identity currentTarget;
  if (!inspect(m_Root, currentRoot, error) ||
      !sameIdentity(*m_RootIdentity, currentRoot) ||
      !inspect(m_StagePath, currentStage, error) ||
      !sameIdentity(*m_StageIdentity, currentStage) ||
      !inspect(m_Target.path, currentTarget, error) ||
      !sameIdentity(*m_TargetIdentity, currentTarget)) {
    if (error.isEmpty()) {
      error = QStringLiteral(
          "Mod target or staging generation changed before publication.");
    }
    return {PublishStatus::Failure, error, {}};
  }

  if (m_Target.exists) {
    QString currentMetadataGeneration;
    if (!metadataGeneration(m_Target.path, currentMetadataGeneration, error) ||
        currentMetadataGeneration != m_MetadataGeneration) {
      if (error.isEmpty()) {
        error = QStringLiteral(
            "Mod metadata changed while the replacement was staged.");
      }
      return {PublishStatus::Failure, error, {}};
    }
  }

  if (!QFile::setPermissions(m_StagePath, m_TargetPermissions)) {
    return {PublishStatus::Failure,
            QStringLiteral("Could not prepare final permissions for mod '%1'.")
                .arg(m_Target.path),
            {}};
  }

#ifdef FLUORINE_HAS_ATOMIC_MOD_RENAME
  const QByteArray stage = QFile::encodeName(m_StagePath);
  const QByteArray target = QFile::encodeName(m_Target.path);
  const unsigned int flags =
      m_Mode == Mode::New ? RENAME_NOREPLACE : RENAME_EXCHANGE;
  if (::syscall(SYS_renameat2, AT_FDCWD, stage.constData(), AT_FDCWD,
                target.constData(), flags) != 0) {
    const int renameError = errno;
    QFile::setPermissions(m_StagePath, QFile::ReadOwner | QFile::WriteOwner |
                                           QFile::ExeOwner);
    return {PublishStatus::Failure,
            QStringLiteral("Could not atomically publish mod '%1': %2")
                .arg(m_Target.path,
                     QString::fromLocal8Bit(std::strerror(renameError))),
            {}};
  }
#else
  if (m_Mode != Mode::New || !QDir().rename(m_StagePath, m_Target.path)) {
    QFile::setPermissions(m_StagePath, QFile::ReadOwner | QFile::WriteOwner |
                                           QFile::ExeOwner);
    return {PublishStatus::Failure,
            QStringLiteral(
                "Atomic mod replacement is unavailable on this platform."),
            {}};
  }
#endif

  m_Published = true;

  Identity publishedTarget;
  Identity retiredTarget;
  const bool targetValid = inspect(m_Target.path, publishedTarget, error) &&
                           sameIdentity(*m_StageIdentity, publishedTarget);
  const bool retiredValid =
      m_Mode == Mode::New || (inspect(m_StagePath, retiredTarget, error) &&
                              sameIdentity(*m_TargetIdentity, retiredTarget));
  Identity rootAfter;
  const bool rootValid = inspect(m_Root, rootAfter, error) &&
                         sameIdentity(*m_RootIdentity, rootAfter);
  if (!targetValid || !retiredValid || !rootValid) {
    m_Stage->setAutoRemove(false);
    return {
        PublishStatus::PublicationUncertain,
        QStringLiteral("Published mod generation could not be authenticated; "
                       "the installation stage was preserved."),
        m_Mode == Mode::New ? m_Target.path : m_StagePath};
  }

#ifdef Q_OS_LINUX
  if (!syncDirectory(m_Root, error)) {
    QString residue;
    if (m_Mode != Mode::New) {
      m_Stage->setAutoRemove(false);
      residue = m_StagePath;
    }
    return {PublishStatus::Success, error, residue};
  }
#endif

  QString residue;
  if (m_Mode != Mode::New) {
    const QString retiredPath = m_StagePath;
    if (!QDir(retiredPath).removeRecursively()) {
      m_Stage->setAutoRemove(false);
      residue = retiredPath;
    }
  }
  return {PublishStatus::Success, {}, residue};
}
