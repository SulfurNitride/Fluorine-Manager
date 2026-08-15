#include "prefixsymlinktransaction.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QStringList>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <memory>
#include <optional>

#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace PrefixSymlinkTransaction {
namespace {

constexpr auto SteamUser = "steamuser";
constexpr auto SetupLockFile = ".fluorine-prefix-symlinks.lock";

bool isSystemUser(const QString &name) {
  static const QStringList systemUsers = {
      QStringLiteral("Public"), QStringLiteral("root"),
      QStringLiteral("Default"), QStringLiteral("Default User"),
      QStringLiteral("All Users")};
  return std::ranges::any_of(systemUsers, [&](const QString &systemUser) {
    return name.compare(systemUser, Qt::CaseInsensitive) == 0;
  });
}

const QStringList SkipDirectories = {
    QStringLiteral("Temp"),         QStringLiteral("Microsoft"),
    QStringLiteral("wine"),         QStringLiteral("Public"),
    QStringLiteral("root"),         QStringLiteral("Application Data"),
    QStringLiteral("Cookies"),      QStringLiteral("Local Settings"),
    QStringLiteral("NetHood"),      QStringLiteral("PrintHood"),
    QStringLiteral("Recent"),       QStringLiteral("SendTo"),
    QStringLiteral("Start Menu"),   QStringLiteral("Templates"),
    QStringLiteral("My Documents"), QStringLiteral("My Music"),
    QStringLiteral("My Pictures"),  QStringLiteral("My Videos"),
    QStringLiteral("Desktop"),      QStringLiteral("Downloads"),
    QStringLiteral("Favorites"),    QStringLiteral("Links"),
    QStringLiteral("Searches"),     QStringLiteral("Contacts"),
    QStringLiteral("3D Objects"),
};

struct ObjectIdentity {
  dev_t device{};
  ino_t inode{};
  mode_t mode{};
};

class ScopedFd {
public:
  ScopedFd() = default;
  explicit ScopedFd(int fd) : m_fd(fd) {}
  ~ScopedFd() {
    if (m_fd >= 0) {
      ::close(m_fd);
    }
  }
  ScopedFd(const ScopedFd &) = delete;
  ScopedFd &operator=(const ScopedFd &) = delete;
  ScopedFd(ScopedFd &&other) noexcept : m_fd(other.m_fd) { other.m_fd = -1; }
  ScopedFd &operator=(ScopedFd &&other) noexcept {
    if (this != &other) {
      if (m_fd >= 0) {
        ::close(m_fd);
      }
      m_fd = other.m_fd;
      other.m_fd = -1;
    }
    return *this;
  }
  [[nodiscard]] int get() const { return m_fd; }
  [[nodiscard]] bool valid() const { return m_fd >= 0; }

private:
  int m_fd{-1};
};

ScopedFd acquireSetupLock(int rootFd, const QString &root, QString &error) {
  const int fd = ::openat(rootFd, SetupLockFile,
                          O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (fd < 0) {
    error = QStringLiteral("Could not open the prefix-setup lock in '%1': %2")
                .arg(root, QString::fromLocal8Bit(std::strerror(errno)));
    return {};
  }
  ScopedFd lock(fd);
  struct stat state{};
  if (::fstat(fd, &state) != 0 || !S_ISREG(state.st_mode)) {
    error = QStringLiteral("The prefix-setup lock is not a safe regular file "
                           "under %1")
                .arg(root);
    return {};
  }
  if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
    error = QStringLiteral("Another process is preparing this Wine prefix: %1")
                .arg(root);
    return {};
  }
  return lock;
}

struct PlannedLink {
  enum class ExistingPolicy { Strict, PreserveAny };

  QString path;
  QString target;
  QString symlinkTarget;
  QString source;
  QString sourceRoot;
  ObjectIdentity targetIdentity;
  ObjectIdentity sourceRootIdentity;
  std::shared_ptr<ScopedFd> targetHandle;
  std::shared_ptr<ScopedFd> sourceRootHandle;
  bool targetWillBeCreated{false};
  ExistingPolicy existingPolicy{ExistingPolicy::Strict};
};

enum class FamilyState { Missing, Occupied, Conflict };

struct CreatedLink {
  PlannedLink link;
  ObjectIdentity identity;
  std::shared_ptr<ScopedFd> handle;
  std::shared_ptr<ScopedFd> parentHandle;
  QByteArray leaf;
};

struct CreatedDirectory {
  QString path;
  ObjectIdentity identity;
  std::shared_ptr<ScopedFd> handle;
  std::shared_ptr<ScopedFd> parentHandle;
  QByteArray leaf;
};

ObjectIdentity identityFrom(const struct stat &state) {
  return {state.st_dev, state.st_ino, state.st_mode};
}

bool sameIdentity(const ObjectIdentity &left, const ObjectIdentity &right) {
  return left.device == right.device && left.inode == right.inode &&
         (left.mode & S_IFMT) == (right.mode & S_IFMT);
}

bool pathIdentity(const QString &path, bool follow, ObjectIdentity &identity) {
  struct stat state{};
  const QByteArray encoded = QFile::encodeName(path);
  const int status = follow ? ::stat(encoded.constData(), &state)
                            : ::lstat(encoded.constData(), &state);
  if (status != 0) {
    return false;
  }
  identity = identityFrom(state);
  return true;
}

bool descriptorIdentity(int fd, ObjectIdentity &identity) {
  struct stat state{};
  if (::fstat(fd, &state) != 0) {
    return false;
  }
  identity = identityFrom(state);
  return true;
}

std::shared_ptr<ScopedFd> retainDirectory(const QString &path,
                                          ObjectIdentity &identity) {
  const QByteArray encoded = QFile::encodeName(path);
  const int fd = ::open(encoded.constData(),
                        O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) {
    return {};
  }
  auto handle = std::make_shared<ScopedFd>(fd);
  if (!descriptorIdentity(handle->get(), identity)) {
    return {};
  }
  return handle;
}

std::shared_ptr<ScopedFd> retainDirectoryAt(int parentFd,
                                            const QByteArray &name,
                                            ObjectIdentity &identity) {
  const int fd = ::openat(parentFd, name.constData(),
                          O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) {
    return {};
  }
  auto handle = std::make_shared<ScopedFd>(fd);
  if (!descriptorIdentity(handle->get(), identity)) {
    return {};
  }
  return handle;
}

bool retainedIdentityMatches(const std::shared_ptr<ScopedFd> &handle,
                             const ObjectIdentity &expected,
                             const ObjectIdentity &current) {
  ObjectIdentity retained;
  return handle && handle->valid() &&
         descriptorIdentity(handle->get(), retained) &&
         sameIdentity(retained, expected) && sameIdentity(current, retained);
}

bool rootGenerationMatches(const QString &root,
                           const ObjectIdentity &expected) {
  ObjectIdentity current;
  return pathIdentity(root, true, current) && sameIdentity(current, expected);
}

bool shouldSkip(const QString &name) {
  return std::ranges::any_of(SkipDirectories, [&](const QString &skipped) {
    return name.compare(skipped, Qt::CaseInsensitive) == 0;
  });
}

QString absoluteClean(const QString &path) {
  return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString physicalRoot(const QString &prefixPath, QString &error) {
  const QFileInfo info(prefixPath);
  const QString canonical = info.canonicalFilePath();
  if (canonical.isEmpty() || !QFileInfo(canonical).isDir()) {
    error = QStringLiteral(
                "The managed Wine prefix is not a readable directory: %1")
                .arg(prefixPath);
    return {};
  }
  return QDir::cleanPath(canonical);
}

bool withinRoot(const QString &root, const QString &path) {
  const QString relative = QDir(root).relativeFilePath(path);
  return relative != QStringLiteral("..") &&
         !relative.startsWith(QStringLiteral("../")) &&
         !QDir::isAbsolutePath(relative);
}

bool validateParent(const QString &root, const QString &parent,
                    QString &error) {
  const QString cleanParent = absoluteClean(parent);
  if (!withinRoot(root, cleanParent)) {
    error =
        QStringLiteral("A prefix link parent escapes the managed prefix: %1")
            .arg(cleanParent);
    return false;
  }

  const QString relative = QDir(root).relativeFilePath(cleanParent);
  QString current = root;
  for (const QString &component : relative.split('/', Qt::SkipEmptyParts)) {
    current = QDir(current).filePath(component);
    const QFileInfo info(current);
    if (info.isSymLink()) {
      error =
          QStringLiteral("Refusing to create prefix links through symlinked "
                         "directory '%1' -> '%2'.")
              .arg(current, info.symLinkTarget());
      return false;
    }
    if (info.exists() && !info.isDir()) {
      error = QStringLiteral("A prefix link parent is not a directory: %1")
                  .arg(current);
      return false;
    }
  }
  return true;
}

QString prefixUsername(const QString &usersPath, QString &error) {
  QStringList steamUsers;
  QStringList otherUsers;
  const QFileInfoList entries = QDir(usersPath).entryInfoList(
      QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
      QDir::Name | QDir::IgnoreCase);
  for (const QFileInfo &entry : entries) {
    if (isSystemUser(entry.fileName())) {
      continue;
    }
    if (entry.isSymLink()) {
      error = QStringLiteral("A Wine user directory is a symlink and cannot be "
                             "used for prefix links: %1")
                  .arg(entry.absoluteFilePath());
      return {};
    }
    if (entry.fileName().compare(QString::fromLatin1(SteamUser),
                                 Qt::CaseInsensitive) == 0) {
      steamUsers.push_back(entry.fileName());
    } else {
      otherUsers.push_back(entry.fileName());
    }
  }

  if (steamUsers.size() == 1) {
    return steamUsers.front();
  }
  if (steamUsers.size() > 1 || otherUsers.size() > 1) {
    error = QStringLiteral("The Wine prefix has an ambiguous user directory "
                           "set under %1: %2")
                .arg(usersPath, (steamUsers + otherUsers).join(", "));
    return {};
  }
  if (otherUsers.size() == 1) {
    return otherUsers.front();
  }

  error = QStringLiteral("The Wine prefix has no usable user directory under "
                         "%1")
              .arg(usersPath);
  return {};
}

bool samePhysicalDirectory(const QString &left, const QString &right) {
  struct stat leftStat{};
  struct stat rightStat{};
  const QByteArray encodedLeft = QFile::encodeName(left);
  const QByteArray encodedRight = QFile::encodeName(right);
  return ::stat(encodedLeft.constData(), &leftStat) == 0 &&
         ::stat(encodedRight.constData(), &rightStat) == 0 &&
         S_ISDIR(leftStat.st_mode) && S_ISDIR(rightStat.st_mode) &&
         leftStat.st_dev == rightStat.st_dev &&
         leftStat.st_ino == rightStat.st_ino;
}

bool validateSourceDirectory(const QString &sourceRoot, const QString &path,
                             QString &error) {
  const QFileInfo info(path);
  if (info.isSymLink()) {
    error = QStringLiteral("Refusing to expose a symlinked source directory "
                           "from a detected prefix: '%1' -> '%2'")
                .arg(path, info.symLinkTarget());
    return false;
  }
  if (!validateParent(sourceRoot, path, error) || !info.isDir()) {
    if (error.isEmpty()) {
      error = QStringLiteral("A detected prefix source is not a real "
                             "directory: %1")
                  .arg(path);
    }
    return false;
  }
  const QString canonical = info.canonicalFilePath();
  if (canonical.isEmpty() || !withinRoot(sourceRoot, canonical)) {
    error = QStringLiteral("A detected prefix source escapes its authenticated "
                           "prefix: %1")
                .arg(path);
    return false;
  }
  return true;
}

bool sourceGenerationMatches(const PlannedLink &link) {
  ObjectIdentity target;
  if (!pathIdentity(link.target, true, target) ||
      !retainedIdentityMatches(link.targetHandle, link.targetIdentity,
                               target)) {
    return false;
  }
  if (link.targetWillBeCreated) {
    return true;
  }
  ObjectIdentity sourceRoot;
  return pathIdentity(link.sourceRoot, true, sourceRoot) &&
         retainedIdentityMatches(link.sourceRootHandle, link.sourceRootIdentity,
                                 sourceRoot);
}

bool plannedSourceGenerationMatches(const PlannedLink &link) {
  ObjectIdentity target;
  ObjectIdentity sourceRoot;
  return pathIdentity(link.target, true, target) &&
         sameIdentity(target, link.targetIdentity) &&
         pathIdentity(link.sourceRoot, true, sourceRoot) &&
         retainedIdentityMatches(link.sourceRootHandle, link.sourceRootIdentity,
                                 sourceRoot);
}

bool retainedSymlinkHasTarget(const std::shared_ptr<ScopedFd> &handle,
                              const QByteArray &expected) {
  if (!handle || !handle->valid()) {
    return false;
  }
  QByteArray target(4096, '\0');
  const ssize_t size =
      ::readlinkat(handle->get(), "", target.data(), target.size());
  if (size < 0) {
    return false;
  }
  target.resize(size);
  return target == expected;
}

ScopedFd openRootDirectory(const QString &root, QString &error) {
  const QByteArray encoded = QFile::encodeName(root);
  const int fd = ::open(encoded.constData(),
                        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) {
    error =
        QStringLiteral("Could not authenticate the managed prefix root '%1': "
                       "%2")
            .arg(root, QString::fromLocal8Bit(std::strerror(errno)));
  }
  return ScopedFd(fd);
}

QStringList caseMatchesAt(int directoryFd, const QString &requested,
                          QString &error) {
  const int independent = ::openat(
      directoryFd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (independent < 0) {
    error = QStringLiteral("Could not inspect a prefix directory: %1")
                .arg(QString::fromLocal8Bit(std::strerror(errno)));
    return {};
  }
  DIR *directory = ::fdopendir(independent);
  if (directory == nullptr) {
    ::close(independent);
    error = QStringLiteral("Could not enumerate a prefix directory: %1")
                .arg(QString::fromLocal8Bit(std::strerror(errno)));
    return {};
  }

  QStringList matches;
  errno = 0;
  while (dirent *entry = ::readdir(directory)) {
    const QString name = QFile::decodeName(entry->d_name);
    if (name != QStringLiteral(".") && name != QStringLiteral("..") &&
        name.compare(requested, Qt::CaseInsensitive) == 0) {
      matches.push_back(name);
    }
  }
  const int readError = errno;
  ::closedir(directory);
  if (readError != 0) {
    error = QStringLiteral("Could not finish enumerating a prefix directory: "
                           "%1")
                .arg(QString::fromLocal8Bit(std::strerror(readError)));
    return {};
  }
  std::ranges::sort(matches);
  return matches;
}

ScopedFd openDirectoryBelowRoot(int rootFd, const QString &root,
                                const QString &path, bool create,
                                QVector<CreatedDirectory> *createdDirectories,
                                QString &error, QString *resolvedPath = nullptr,
                                bool missingIsOkay = false) {
  const QString clean = absoluteClean(path);
  if (!withinRoot(root, clean)) {
    error = QStringLiteral("A prefix directory escapes the managed root: %1")
                .arg(clean);
    return {};
  }

  int current = ::dup(rootFd);
  if (current < 0) {
    error = QStringLiteral("Could not duplicate the managed-prefix descriptor: "
                           "%1")
                .arg(QString::fromLocal8Bit(std::strerror(errno)));
    return {};
  }
  ScopedFd directory(current);
  QString currentPath = root;
  const QString relative = QDir(root).relativeFilePath(clean);
  for (const QString &component : relative.split('/', Qt::SkipEmptyParts)) {
    if (component == QStringLiteral(".") || component == QStringLiteral("..")) {
      error =
          QStringLiteral("Invalid prefix directory component in %1").arg(path);
      return {};
    }

    const QStringList matches =
        caseMatchesAt(directory.get(), component, error);
    if (!error.isEmpty()) {
      return {};
    }
    if (matches.size() > 1) {
      error =
          QStringLiteral("A prefix directory has ambiguous "
                         "case-insensitive entries for '%1' under %2: %3")
              .arg(component, currentPath, matches.join(QStringLiteral(", ")));
      return {};
    }
    const QString actual = matches.isEmpty() ? component : matches.front();
    const QByteArray name = QFile::encodeName(actual);
    const QString nextPath = QDir(currentPath).filePath(actual);
    int next = ::openat(directory.get(), name.constData(),
                        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (next < 0 && errno == ENOENT && matches.isEmpty() && create) {
      if (::mkdirat(directory.get(), name.constData(), 0777) == 0) {
        if (createdDirectories != nullptr) {
          ObjectIdentity createdIdentity;
          auto retained =
              retainDirectoryAt(directory.get(), name, createdIdentity);
          ObjectIdentity parentIdentity;
          auto retainedParent = retainDirectoryAt(
              directory.get(), QByteArrayLiteral("."), parentIdentity);
          if (!retained || !retainedParent || !S_ISDIR(createdIdentity.mode)) {
            const int savedError = errno;
            ::unlinkat(directory.get(), name.constData(), AT_REMOVEDIR);
            error = QStringLiteral("Could not authenticate newly created "
                                   "prefix directory '%1': %2")
                        .arg(nextPath,
                             QString::fromLocal8Bit(std::strerror(savedError)));
            return {};
          }
          createdDirectories->push_back({nextPath, createdIdentity,
                                         std::move(retained),
                                         std::move(retainedParent), name});
        }
      } else if (errno != EEXIST) {
        error =
            QStringLiteral("Could not create prefix directory '%1': %2")
                .arg(nextPath, QString::fromLocal8Bit(std::strerror(errno)));
        return {};
      }
      next = ::openat(directory.get(), name.constData(),
                      O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    }
    if (next < 0 && errno == ENOENT && matches.isEmpty() && missingIsOkay) {
      error.clear();
      return {};
    }
    if (next < 0) {
      error = QStringLiteral("Could not open prefix directory '%1' without "
                             "following links: %2")
                  .arg(nextPath, QString::fromLocal8Bit(std::strerror(errno)));
      return {};
    }
    currentPath = nextPath;
    directory = ScopedFd(next);
  }
  if (resolvedPath != nullptr) {
    *resolvedPath = currentPath;
  }
  return directory;
}

bool resolveDirectoryBelowRoot(int rootFd, const QString &root,
                               const QString &requested, bool create,
                               bool missingIsOkay,
                               QVector<CreatedDirectory> *createdDirectories,
                               QString &resolved, QString &error) {
  resolved.clear();
  ScopedFd opened = openDirectoryBelowRoot(rootFd, root, requested, create,
                                           createdDirectories, error, &resolved,
                                           missingIsOkay);
  if (opened.valid()) {
    return true;
  }
  return missingIsOkay && error.isEmpty();
}

bool liveParentMatches(int rootFd, const QString &root, const QString &path,
                       const std::shared_ptr<ScopedFd> &retainedParent) {
  QString error;
  ScopedFd live =
      openDirectoryBelowRoot(rootFd, root, path, false, nullptr, error);
  ObjectIdentity retainedIdentity;
  ObjectIdentity liveIdentity;
  return live.valid() && retainedParent && retainedParent->valid() &&
         descriptorIdentity(retainedParent->get(), retainedIdentity) &&
         descriptorIdentity(live.get(), liveIdentity) &&
         sameIdentity(retainedIdentity, liveIdentity);
}

bool removeCreatedLink(const CreatedLink &created, QString &error) {
  const PlannedLink &link = created.link;
  if (!created.parentHandle || !created.parentHandle->valid()) {
    error = QStringLiteral("The parent of a newly created prefix link is no "
                           "longer authenticated: %1")
                .arg(link.path);
    return false;
  }
  struct stat state{};
  if (::fstatat(created.parentHandle->get(), created.leaf.constData(), &state,
                AT_SYMLINK_NOFOLLOW) != 0) {
    if (errno == ENOENT) {
      return true;
    }
    error =
        QStringLiteral("Could not inspect a newly created prefix link '%1': "
                       "%2")
            .arg(link.path, QString::fromLocal8Bit(std::strerror(errno)));
    return false;
  }
  if (!S_ISLNK(state.st_mode)) {
    error = QStringLiteral("A newly created prefix link changed type before "
                           "rollback: %1")
                .arg(link.path);
    return false;
  }
  if (!retainedIdentityMatches(created.handle, created.identity,
                               identityFrom(state))) {
    error = QStringLiteral("A newly created prefix link was replaced before "
                           "rollback and will be preserved: %1")
                .arg(link.path);
    return false;
  }

  QByteArray target(4096, '\0');
  const ssize_t size =
      ::readlinkat(created.parentHandle->get(), created.leaf.constData(),
                   target.data(), target.size());
  if (size < 0) {
    error = QStringLiteral("Could not authenticate a newly created prefix link "
                           "'%1': %2")
                .arg(link.path, QString::fromLocal8Bit(std::strerror(errno)));
    return false;
  }
  target.resize(size);
  if (target != QFile::encodeName(link.symlinkTarget)) {
    error = QStringLiteral("A newly created prefix link changed target before "
                           "rollback: %1")
                .arg(link.path);
    return false;
  }
  if (::unlinkat(created.parentHandle->get(), created.leaf.constData(), 0) !=
      0) {
    error =
        QStringLiteral("Could not roll back newly created prefix link '%1': "
                       "%2")
            .arg(link.path, QString::fromLocal8Bit(std::strerror(errno)));
    return false;
  }
  return true;
}

bool removeCreatedDirectory(const CreatedDirectory &created, QString &error) {
  if (!created.parentHandle || !created.parentHandle->valid()) {
    error = QStringLiteral("The parent of a newly created prefix directory is "
                           "no longer authenticated: %1")
                .arg(created.path);
    return false;
  }
  struct stat state{};
  if (::fstatat(created.parentHandle->get(), created.leaf.constData(), &state,
                AT_SYMLINK_NOFOLLOW) != 0) {
    if (errno == ENOENT) {
      return true;
    }
    error =
        QStringLiteral("Could not inspect newly created prefix directory "
                       "'%1': %2")
            .arg(created.path, QString::fromLocal8Bit(std::strerror(errno)));
    return false;
  }
  if (!retainedIdentityMatches(created.handle, created.identity,
                               identityFrom(state))) {
    error = QStringLiteral("A newly created prefix directory was replaced "
                           "before rollback and will be preserved: %1")
                .arg(created.path);
    return false;
  }
  if (::unlinkat(created.parentHandle->get(), created.leaf.constData(),
                 AT_REMOVEDIR) != 0 &&
      errno != ENOENT) {
    error =
        QStringLiteral("Could not roll back newly created prefix directory "
                       "'%1': %2")
            .arg(created.path, QString::fromLocal8Bit(std::strerror(errno)));
    return false;
  }
  return true;
}

QString resolvedLinkTarget(const QString &linkPath) {
  QString target = QFileInfo(linkPath).symLinkTarget();
  if (target.isEmpty()) {
    return {};
  }
  if (QDir::isRelativePath(target)) {
    target = QDir(QFileInfo(linkPath).absolutePath()).absoluteFilePath(target);
  }
  return absoluteClean(target);
}

bool sameTarget(const PlannedLink &link) {
  const QString existing = resolvedLinkTarget(link.path);
  if (existing.isEmpty()) {
    return false;
  }
  ObjectIdentity identity;
  return pathIdentity(existing, true, identity) &&
         sameIdentity(identity, link.targetIdentity);
}

QStringList caseFamily(const QString &path) {
  const QFileInfo requested(path);
  const QFileInfoList entries =
      QDir(requested.absolutePath())
          .entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System |
                             QDir::NoDotAndDotDot,
                         QDir::NoSort);
  QStringList family;
  for (const QFileInfo &entry : entries) {
    if (entry.fileName().compare(requested.fileName(), Qt::CaseInsensitive) ==
        0) {
      family.push_back(entry.absoluteFilePath());
    }
  }
  std::ranges::sort(family);
  return family;
}

QString familyDescription(const QStringList &family) {
  QStringList descriptions;
  for (const QString &path : family) {
    const QFileInfo info(path);
    descriptions.push_back(
        info.isSymLink() ? QStringLiteral("'%1' -> '%2'")
                               .arg(path, info.symLinkTarget().isEmpty()
                                              ? QStringLiteral("<dangling>")
                                              : info.symLinkTarget())
                         : QStringLiteral("'%1'").arg(path));
  }
  return descriptions.join(QStringLiteral(", "));
}

FamilyState classifyFamily(const PlannedLink &link, Result &result,
                           bool count) {
  const QStringList family = caseFamily(link.path);
  if (family.isEmpty()) {
    return FamilyState::Missing;
  }

  if (link.existingPolicy == PlannedLink::ExistingPolicy::PreserveAny) {
    if (family.size() > 1) {
      result.error =
          QStringLiteral("Refusing to preserve an ambiguous "
                         "case-insensitive compatibility-link family: %1")
              .arg(familyDescription(family));
      return FamilyState::Conflict;
    }
    if (count) {
      result.preserved += family.size();
    }
    return FamilyState::Occupied;
  }

  bool allEquivalentLinks = true;
  for (const QString &path : family) {
    PlannedLink member = link;
    member.path = path;
    if (!QFileInfo(path).isSymLink() || !sameTarget(member)) {
      allEquivalentLinks = false;
      break;
    }
  }
  if (allEquivalentLinks) {
    if (count) {
      result.adopted += family.size();
    }
    return FamilyState::Occupied;
  }

  if (family.size() == 1 && !QFileInfo(family.front()).isSymLink()) {
    if (count) {
      ++result.preserved;
    }
    return FamilyState::Occupied;
  }

  result.error =
      QStringLiteral("Refusing to change the existing case-insensitive prefix "
                     "leaf family %1; the proposed target is '%2' (%3). Remove "
                     "the conflicting link or resolve the ambiguous case "
                     "variants, then retry.")
          .arg(familyDescription(family), link.symlinkTarget, link.source);
  return FamilyState::Conflict;
}

bool addTree(QMap<QString, PlannedLink> &plan, const QString &destinationBase,
             const QString &sourceRoot, const QString &sourceBase,
             const QString &candidateName,
             const std::shared_ptr<ScopedFd> &sourceRootHandle,
             const ObjectIdentity &sourceRootIdentity, bool skipMyGames,
             QString &error) {
  const QFileInfo baseInfo(sourceBase);
  if (!baseInfo.exists() && !baseInfo.isSymLink()) {
    return true;
  }
  if (!validateSourceDirectory(sourceRoot, sourceBase, error)) {
    return false;
  }

  QMap<QString, QFileInfoList> families;
  const QFileInfoList entries =
      QDir(sourceBase)
          .entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System |
                             QDir::NoDotAndDotDot,
                         QDir::NoSort);
  for (const QFileInfo &entry : entries) {
    if (shouldSkip(entry.fileName()) ||
        (skipMyGames && entry.fileName().compare(QStringLiteral("My Games"),
                                                 Qt::CaseInsensitive) == 0)) {
      continue;
    }
    if (entry.isSymLink()) {
      if (entry.isDir() || !entry.exists()) {
        error = QStringLiteral("Refusing to expose a symlinked source leaf "
                               "from a detected prefix: '%1' -> '%2'")
                    .arg(entry.absoluteFilePath(), entry.symLinkTarget());
        return false;
      }
      continue;
    }
    if (entry.isDir()) {
      families[entry.fileName().toCaseFolded()].push_back(entry);
    }
  }

  for (auto family = families.cbegin(); family != families.cend(); ++family) {
    const QFileInfoList &members = family.value();
    const QFileInfo *selected = &members.front();
    for (const QFileInfo &member : members) {
      if (!samePhysicalDirectory(selected->absoluteFilePath(),
                                 member.absoluteFilePath())) {
        QStringList paths;
        for (const QFileInfo &listed : members) {
          paths.push_back(listed.absoluteFilePath());
        }
        error = QStringLiteral("A detected prefix has ambiguous "
                               "case-insensitive source directories: %1")
                    .arg(paths.join(QStringLiteral(", ")));
        return false;
      }
      if (member.fileName() < selected->fileName()) {
        selected = &member;
      }
    }

    const QString folder = selected->fileName();
    const QString destination = QDir(destinationBase).filePath(folder);
    const QString key = absoluteClean(destination).toCaseFolded();
    if (plan.contains(key)) {
      continue;
    }
    const QString target = absoluteClean(QDir(sourceBase).filePath(folder));
    PlannedLink link;
    link.path = absoluteClean(destination);
    link.target = target;
    link.symlinkTarget = target;
    link.source = candidateName;
    link.sourceRoot = sourceRoot;
    link.sourceRootHandle = sourceRootHandle;
    link.sourceRootIdentity = sourceRootIdentity;
    if (!pathIdentity(target, true, link.targetIdentity) ||
        !link.sourceRootHandle) {
      error = QStringLiteral("Could not capture the source generation for %1")
                  .arg(target);
      return false;
    }
    plan.insert(key, link);
  }
  return true;
}

bool preflightLink(const PlannedLink &link, Result &result) {
  if (!link.targetWillBeCreated) {
    if (!validateSourceDirectory(link.sourceRoot, link.target, result.error)) {
      return false;
    }
    if (!plannedSourceGenerationMatches(link)) {
      result.error = QStringLiteral("A detected prefix source changed during "
                                    "link preparation: %1")
                         .arg(link.target);
      return false;
    }
  }

  return classifyFamily(link, result, true) != FamilyState::Conflict;
}

bool createLink(int rootFd, const QString &root, PlannedLink &link,
                const Options &options,
                QMap<QString, std::shared_ptr<ScopedFd>> &retainedParents,
                std::optional<CreatedLink> &created, Result &result) {
  created.reset();
  ObjectIdentity currentTargetIdentity;
  link.targetHandle = retainDirectory(link.target, currentTargetIdentity);
  if (!link.targetHandle ||
      !sameIdentity(currentTargetIdentity, link.targetIdentity)) {
    result.error = QStringLiteral("The prefix-link source changed before "
                                  "publication: %1")
                       .arg(link.target);
    return false;
  }
  if (!link.targetWillBeCreated) {
    if (!validateSourceDirectory(link.sourceRoot, link.target, result.error)) {
      return false;
    }
    if (!sourceGenerationMatches(link)) {
      result.error = QStringLiteral("A detected prefix source changed before "
                                    "link publication: %1")
                         .arg(link.target);
      return false;
    }
  } else {
    if (!validateSourceDirectory(root, link.target, result.error) ||
        !sourceGenerationMatches(link)) {
      if (!result.error.isEmpty()) {
        return false;
      }
      result.error = QStringLiteral("The prefix Documents directory changed "
                                    "before compatibility-link publication: %1")
                         .arg(link.target);
      return false;
    }
  }

  const FamilyState family = classifyFamily(link, result, false);
  if (family == FamilyState::Conflict) {
    return false;
  }
  if (family == FamilyState::Occupied) {
    return true;
  }

  ScopedFd parent =
      openDirectoryBelowRoot(rootFd, root, QFileInfo(link.path).absolutePath(),
                             false, nullptr, result.error);
  if (!parent.valid()) {
    return false;
  }
  const QByteArray target = QFile::encodeName(link.symlinkTarget);
  const QByteArray leaf = QFile::encodeName(QFileInfo(link.path).fileName());
  const QString parentKey =
      absoluteClean(QFileInfo(link.path).absolutePath()).toCaseFolded();
  auto retainedParent = retainedParents.value(parentKey);
  if (!retainedParent) {
    ObjectIdentity parentIdentity;
    retainedParent =
        retainDirectoryAt(parent.get(), QByteArrayLiteral("."), parentIdentity);
    if (retainedParent) {
      retainedParents.insert(parentKey, retainedParent);
    }
  } else {
    ObjectIdentity retainedIdentity;
    ObjectIdentity currentIdentity;
    if (!descriptorIdentity(retainedParent->get(), retainedIdentity) ||
        !descriptorIdentity(parent.get(), currentIdentity) ||
        !sameIdentity(retainedIdentity, currentIdentity)) {
      result.error = QStringLiteral("The prefix-link parent changed before "
                                    "publication: %1")
                         .arg(QFileInfo(link.path).absolutePath());
      return false;
    }
  }
  if (!retainedParent) {
    result.error = QStringLiteral("Could not retain the prefix-link parent "
                                  "generation: %1")
                       .arg(QFileInfo(link.path).absolutePath());
    return false;
  }
  if (::symlinkat(target.constData(), parent.get(), leaf.constData()) != 0) {
    if (errno == EEXIST &&
        classifyFamily(link, result, false) == FamilyState::Occupied) {
      return true;
    }
    result.error = QStringLiteral("Could not create symlink '%1' -> '%2': %3")
                       .arg(link.path, link.symlinkTarget,
                            QString::fromLocal8Bit(std::strerror(errno)));
    return false;
  }
  if (options.afterSymlinkCallForTesting) {
    options.afterSymlinkCallForTesting(link.path);
  }
  const int retainedFd =
      ::openat(parent.get(), leaf.constData(), O_PATH | O_CLOEXEC | O_NOFOLLOW);
  auto retained = retainedFd >= 0 ? std::make_shared<ScopedFd>(retainedFd)
                                  : std::shared_ptr<ScopedFd>{};
  ObjectIdentity createdIdentity;
  if (!retained || !descriptorIdentity(retained->get(), createdIdentity) ||
      !S_ISLNK(createdIdentity.mode) ||
      !retainedSymlinkHasTarget(retained, target)) {
    const int savedError = errno;
    result.error =
        QStringLiteral("Could not authenticate newly created prefix "
                       "link '%1': %2. The leaf may have been published; "
                       "inspect it before retrying.")
            .arg(link.path, QString::fromLocal8Bit(std::strerror(savedError)));
    return false;
  }
  CreatedLink publication{link, createdIdentity, std::move(retained),
                          std::move(retainedParent), leaf};
  if (options.afterSymlinkPublicationForTesting) {
    options.afterSymlinkPublicationForTesting(link.path);
  }
  struct stat currentState{};
  if (::fstatat(parent.get(), leaf.constData(), &currentState,
                AT_SYMLINK_NOFOLLOW) != 0 ||
      !retainedIdentityMatches(publication.handle, publication.identity,
                               identityFrom(currentState)) ||
      !retainedSymlinkHasTarget(publication.handle, target) ||
      !liveParentMatches(rootFd, root, QFileInfo(link.path).absolutePath(),
                         publication.parentHandle)) {
    QString cleanupError;
    const bool cleaned = removeCreatedLink(publication, cleanupError);
    result.error = QStringLiteral("The newly published prefix link or its "
                                  "parent changed before validation: %1")
                       .arg(link.path);
    if (!cleaned) {
      result.error += QStringLiteral(" Cleanup was incomplete and the changed "
                                     "leaf was preserved: %1")
                          .arg(cleanupError);
    }
    return false;
  }
  QString postValidationError;
  const QString validationRoot =
      link.targetWillBeCreated ? root : link.sourceRoot;
  const bool sourceValid = validateSourceDirectory(validationRoot, link.target,
                                                   postValidationError) &&
                           sourceGenerationMatches(link);
  if (!sourceValid) {
    QString cleanupError;
    const bool cleaned = removeCreatedLink(publication, cleanupError);
    result.error = postValidationError.isEmpty()
                       ? QStringLiteral("The prefix-link source changed during "
                                        "publication: %1")
                             .arg(link.target)
                       : postValidationError;
    if (!cleaned) {
      result.error +=
          QStringLiteral(" Cleanup was incomplete: %1").arg(cleanupError);
    }
    return false;
  }
  if (classifyFamily(link, result, false) == FamilyState::Conflict) {
    const QString conflictError = result.error;
    QString cleanupError;
    const bool cleaned = removeCreatedLink(publication, cleanupError);
    result.error = conflictError;
    if (!cleaned) {
      result.error +=
          QStringLiteral(" Cleanup was incomplete: %1").arg(cleanupError);
    }
    return false;
  }
  publication.link.targetHandle.reset();
  publication.link.sourceRootHandle.reset();
  created = std::move(publication);
  ++result.created;
  return true;
}

} // namespace

Result apply(const QString &prefixPath,
             const QVector<Candidate> &rankedCandidates,
             const Options &options) {
  Result result;
  const QString root = physicalRoot(prefixPath, result.error);
  if (root.isEmpty()) {
    return result;
  }

  ScopedFd rootDirectory = openRootDirectory(root, result.error);
  if (!rootDirectory.valid()) {
    return result;
  }
  ObjectIdentity rootGeneration;
  if (!descriptorIdentity(rootDirectory.get(), rootGeneration)) {
    result.error = QStringLiteral("Could not capture the managed prefix root "
                                  "generation: %1")
                       .arg(root);
    return result;
  }

  ScopedFd lock = acquireSetupLock(rootDirectory.get(), root, result.error);
  if (!lock.valid()) {
    return result;
  }
  if (!rootGenerationMatches(root, rootGeneration)) {
    result.error = QStringLiteral("The managed prefix root changed while setup "
                                  "was acquiring it: %1")
                       .arg(root);
    return result;
  }

  QVector<CreatedDirectory> createdDirectories;
  QVector<CreatedLink> createdLinks;
  auto rollBack = [&] {
    QStringList cleanupErrors;
    for (auto link = createdLinks.crbegin(); link != createdLinks.crend();
         ++link) {
      QString cleanupError;
      if (!removeCreatedLink(*link, cleanupError)) {
        cleanupErrors.push_back(cleanupError);
      }
    }
    for (auto directory = createdDirectories.crbegin();
         directory != createdDirectories.crend(); ++directory) {
      QString cleanupError;
      if (!removeCreatedDirectory(*directory, cleanupError)) {
        cleanupErrors.push_back(cleanupError);
      }
    }
    if (cleanupErrors.isEmpty()) {
      result.created = 0;
    } else {
      result.error += QStringLiteral(" Cleanup was incomplete: %1")
                          .arg(cleanupErrors.join(QStringLiteral("; ")));
    }
  };

  QString usersPath;
  if (!resolveDirectoryBelowRoot(
          rootDirectory.get(), root,
          QDir(root).filePath(QStringLiteral("drive_c/users")), false, false,
          nullptr, usersPath, result.error)) {
    return result;
  }
  const QString username = prefixUsername(usersPath, result.error);
  if (username.isEmpty()) {
    return result;
  }

  QString userDir;
  QString documents;
  QString myGames;
  QString appdata;
  QString appdataLocal;
  QString appdataRoaming;
  if (!resolveDirectoryBelowRoot(rootDirectory.get(), root,
                                 QDir(usersPath).filePath(username), false,
                                 false, nullptr, userDir, result.error) ||
      !resolveDirectoryBelowRoot(
          rootDirectory.get(), root,
          QDir(userDir).filePath(QStringLiteral("Documents")), true, false,
          &createdDirectories, documents, result.error) ||
      !resolveDirectoryBelowRoot(
          rootDirectory.get(), root,
          QDir(documents).filePath(QStringLiteral("My Games")), true, false,
          &createdDirectories, myGames, result.error) ||
      !resolveDirectoryBelowRoot(
          rootDirectory.get(), root,
          QDir(userDir).filePath(QStringLiteral("AppData")), true, false,
          &createdDirectories, appdata, result.error) ||
      !resolveDirectoryBelowRoot(
          rootDirectory.get(), root,
          QDir(appdata).filePath(QStringLiteral("Local")), true, false,
          &createdDirectories, appdataLocal, result.error) ||
      !resolveDirectoryBelowRoot(
          rootDirectory.get(), root,
          QDir(appdata).filePath(QStringLiteral("Roaming")), true, false,
          &createdDirectories, appdataRoaming, result.error)) {
    rollBack();
    return result;
  }

  QMap<QString, PlannedLink> plan;
  for (const Candidate &candidate : rankedCandidates) {
    if (candidate.prefixPath.isEmpty()) {
      continue;
    }
    QString candidateError;
    const QString candidateRoot =
        physicalRoot(candidate.prefixPath, candidateError);
    if (candidateRoot.isEmpty()) {
      continue;
    }
    if (candidateRoot == root) {
      continue;
    }
    ScopedFd candidateDirectory =
        openRootDirectory(candidateRoot, candidateError);
    QString candidateUsers;
    if (!candidateDirectory.valid() ||
        !resolveDirectoryBelowRoot(
            candidateDirectory.get(), candidateRoot,
            QDir(candidateRoot).filePath(QStringLiteral("drive_c/users")),
            false, false, nullptr, candidateUsers, candidateError)) {
      result.error =
          QStringLiteral("Cannot inspect detected prefix '%1' (%2): "
                         "%3")
              .arg(candidate.name, candidate.prefixPath, candidateError);
      rollBack();
      return result;
    }
    const QString candidateUsername =
        prefixUsername(candidateUsers, candidateError);
    if (candidateUsername.isEmpty()) {
      result.error =
          QStringLiteral("Cannot inspect detected prefix '%1' (%2): "
                         "%3")
              .arg(candidate.name, candidate.prefixPath, candidateError);
      rollBack();
      return result;
    }
    QString sourceUser;
    QString sourceDocuments;
    QString sourceMyGames;
    QString sourceAppdata;
    QString sourceLocal;
    QString sourceRoaming;
    if (!resolveDirectoryBelowRoot(
            candidateDirectory.get(), candidateRoot,
            QDir(candidateUsers).filePath(candidateUsername), false, false,
            nullptr, sourceUser, candidateError) ||
        !resolveDirectoryBelowRoot(
            candidateDirectory.get(), candidateRoot,
            QDir(sourceUser).filePath(QStringLiteral("Documents")), false, true,
            nullptr, sourceDocuments, candidateError) ||
        (!sourceDocuments.isEmpty() &&
         !resolveDirectoryBelowRoot(
             candidateDirectory.get(), candidateRoot,
             QDir(sourceDocuments).filePath(QStringLiteral("My Games")), false,
             true, nullptr, sourceMyGames, candidateError)) ||
        !resolveDirectoryBelowRoot(
            candidateDirectory.get(), candidateRoot,
            QDir(sourceUser).filePath(QStringLiteral("AppData")), false, true,
            nullptr, sourceAppdata, candidateError) ||
        (!sourceAppdata.isEmpty() &&
         (!resolveDirectoryBelowRoot(
              candidateDirectory.get(), candidateRoot,
              QDir(sourceAppdata).filePath(QStringLiteral("Local")), false,
              true, nullptr, sourceLocal, candidateError) ||
          !resolveDirectoryBelowRoot(
              candidateDirectory.get(), candidateRoot,
              QDir(sourceAppdata).filePath(QStringLiteral("Roaming")), false,
              true, nullptr, sourceRoaming, candidateError)))) {
      result.error =
          QStringLiteral("Cannot inspect detected prefix '%1' (%2): "
                         "%3")
              .arg(candidate.name, candidate.prefixPath, candidateError);
      rollBack();
      return result;
    }

    ObjectIdentity candidateRootIdentity;
    auto candidateRootHandle =
        retainDirectoryAt(candidateDirectory.get(), QByteArrayLiteral("."),
                          candidateRootIdentity);
    if (!candidateRootHandle) {
      result.error = QStringLiteral("Could not retain detected prefix '%1': %2")
                         .arg(candidate.name, candidateRoot);
      rollBack();
      return result;
    }

    if ((!sourceMyGames.isEmpty() &&
         !addTree(plan, myGames, candidateRoot, sourceMyGames, candidate.name,
                  candidateRootHandle, candidateRootIdentity, false,
                  result.error)) ||
        (!sourceDocuments.isEmpty() &&
         !addTree(plan, documents, candidateRoot, sourceDocuments,
                  candidate.name, candidateRootHandle, candidateRootIdentity,
                  true, result.error)) ||
        (!sourceLocal.isEmpty() &&
         !addTree(plan, appdataLocal, candidateRoot, sourceLocal,
                  candidate.name, candidateRootHandle, candidateRootIdentity,
                  false, result.error)) ||
        (!sourceRoaming.isEmpty() &&
         !addTree(plan, appdataRoaming, candidateRoot, sourceRoaming,
                  candidate.name, candidateRootHandle, candidateRootIdentity,
                  false, result.error))) {
      rollBack();
      return result;
    }
  }

  const QString myDocuments =
      QDir(userDir).filePath(QStringLiteral("My Documents"));
  PlannedLink compatibility;
  compatibility.path = absoluteClean(myDocuments);
  compatibility.target = absoluteClean(documents);
  compatibility.symlinkTarget = QFileInfo(documents).fileName();
  compatibility.source = QStringLiteral("compatibility link");
  compatibility.targetWillBeCreated = true;
  compatibility.existingPolicy = PlannedLink::ExistingPolicy::PreserveAny;
  if (!pathIdentity(compatibility.target, true, compatibility.targetIdentity)) {
    result.error = QStringLiteral("Could not authenticate the prefix Documents "
                                  "directory: %1")
                       .arg(compatibility.target);
    rollBack();
    return result;
  }
  plan.insert(absoluteClean(myDocuments).toCaseFolded(), compatibility);

  QMap<QString, std::shared_ptr<ScopedFd>> retainedParents;
  for (const PlannedLink &link : plan) {
    if (!rootGenerationMatches(root, rootGeneration) ||
        !validateParent(root, QFileInfo(link.path).absolutePath(),
                        result.error) ||
        !preflightLink(link, result)) {
      if (result.error.isEmpty()) {
        result.error = QStringLiteral("The managed prefix root changed during "
                                      "link preflight: %1")
                           .arg(root);
      }
      rollBack();
      return result;
    }
  }

  if (options.beforePublicationForTesting) {
    options.beforePublicationForTesting();
  }

  for (PlannedLink &link : plan) {
    if (options.failAfterCreations >= 0 &&
        result.created >= options.failAfterCreations) {
      result.error = QStringLiteral("Injected prefix-link commit failure");
      rollBack();
      return result;
    }
    if (!rootGenerationMatches(root, rootGeneration) ||
        !validateParent(root, QFileInfo(link.path).absolutePath(),
                        result.error)) {
      if (result.error.isEmpty()) {
        result.error = QStringLiteral("The managed prefix root changed before "
                                      "link publication: %1")
                           .arg(root);
      }
      rollBack();
      return result;
    }
    std::optional<CreatedLink> created;
    if (!createLink(rootDirectory.get(), root, link, options, retainedParents,
                    created, result)) {
      rollBack();
      return result;
    }
    if (created.has_value()) {
      createdLinks.push_back(*created);
      if (options.afterCreationForTesting) {
        options.afterCreationForTesting(link.path);
      }
    }
    link.targetHandle.reset();
    link.sourceRootHandle.reset();
    if (!rootGenerationMatches(root, rootGeneration)) {
      result.error = QStringLiteral("The managed prefix root changed during "
                                    "link publication: %1")
                         .arg(root);
      rollBack();
      return result;
    }
  }

  result.success = true;
  return result;
}

bool ensureTempDirectory(const QString &prefixPath, QString &error) {
  return ensureTempDirectory(prefixPath, error, {});
}

bool ensureTempDirectory(const QString &prefixPath, QString &error,
                         const Options &options) {
  error.clear();
  const QString root = physicalRoot(prefixPath, error);
  if (root.isEmpty()) {
    return false;
  }
  ScopedFd rootDirectory = openRootDirectory(root, error);
  if (!rootDirectory.valid()) {
    return false;
  }
  ObjectIdentity rootGeneration;
  if (!descriptorIdentity(rootDirectory.get(), rootGeneration)) {
    error = QStringLiteral("Could not capture the managed prefix root "
                           "generation: %1")
                .arg(root);
    return false;
  }
  ScopedFd lock = acquireSetupLock(rootDirectory.get(), root, error);
  if (!lock.valid()) {
    return false;
  }
  if (!rootGenerationMatches(root, rootGeneration)) {
    error = QStringLiteral("The managed prefix root changed while setup was "
                           "acquiring it: %1")
                .arg(root);
    return false;
  }
  QString usersPath;
  if (!resolveDirectoryBelowRoot(
          rootDirectory.get(), root,
          QDir(root).filePath(QStringLiteral("drive_c/users")), false, false,
          nullptr, usersPath, error)) {
    return false;
  }
  const QString username = prefixUsername(usersPath, error);
  if (username.isEmpty()) {
    return false;
  }
  QVector<CreatedDirectory> createdDirectories;
  auto rollBack = [&] {
    QStringList cleanupErrors;
    for (auto directory = createdDirectories.crbegin();
         directory != createdDirectories.crend(); ++directory) {
      QString cleanupError;
      if (!removeCreatedDirectory(*directory, cleanupError)) {
        cleanupErrors.push_back(cleanupError);
      }
    }
    if (!cleanupErrors.isEmpty()) {
      error += QStringLiteral(" Cleanup was incomplete: %1")
                   .arg(cleanupErrors.join(QStringLiteral("; ")));
    }
  };
  if (options.beforePublicationForTesting) {
    options.beforePublicationForTesting();
  }
  if (!rootGenerationMatches(root, rootGeneration)) {
    error = QStringLiteral("The managed prefix root changed before temporary "
                           "directory publication: %1")
                .arg(root);
    return false;
  }
  QString userDir;
  QString appdata;
  QString local;
  QString temp;
  if (!resolveDirectoryBelowRoot(rootDirectory.get(), root,
                                 QDir(usersPath).filePath(username), false,
                                 false, nullptr, userDir, error) ||
      !resolveDirectoryBelowRoot(
          rootDirectory.get(), root,
          QDir(userDir).filePath(QStringLiteral("AppData")), true, false,
          &createdDirectories, appdata, error) ||
      !resolveDirectoryBelowRoot(
          rootDirectory.get(), root,
          QDir(appdata).filePath(QStringLiteral("Local")), true, false,
          &createdDirectories, local, error) ||
      !resolveDirectoryBelowRoot(rootDirectory.get(), root,
                                 QDir(local).filePath(QStringLiteral("Temp")),
                                 true, false, &createdDirectories, temp,
                                 error)) {
    rollBack();
    return false;
  }
  if (!rootGenerationMatches(root, rootGeneration)) {
    error = QStringLiteral("The managed prefix root changed during temporary "
                           "directory publication: %1")
                .arg(root);
    rollBack();
    return false;
  }
  return true;
}

} // namespace PrefixSymlinkTransaction
