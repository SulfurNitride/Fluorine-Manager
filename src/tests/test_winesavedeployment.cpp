#include "wineprofileinisync.h"
#include "winesavedeployment.h"
#include "winesaverouting.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QUuid>

#include <gtest/gtest.h>

#ifdef Q_OS_UNIX
#include <csignal>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

const QString Owner = QStringLiteral("test-launch-owner");

bool writeBytes(const QString &path, QByteArrayView bytes) {
  if (!QDir().mkpath(QFileInfo(path).absolutePath()))
    return false;
  QFile file(path);
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
         file.write(bytes.data(), bytes.size()) == bytes.size() && file.flush();
}

QByteArray readBytes(const QString &path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

QString iniRetirement(const QString &live) {
  const QByteArray digest =
      QCryptographicHash::hash(Owner.toUtf8(), QCryptographicHash::Sha256)
          .toHex()
          .left(24);
  return live + QStringLiteral(".fluorine-session-") +
         QString::fromLatin1(digest);
}

QString markerPath(const QString &live) {
  const QFileInfo info(live);
  return QDir(info.absolutePath())
      .filePath(
          QStringLiteral(".fluorine-save-owner-%1.json").arg(info.fileName()));
}

bool patchMarker(const QString &live, const QString &phase,
                 const QString &retirement = {}, const QString &intent = {}) {
  const QString path = markerPath(live);
  QJsonParseError error;
  QJsonDocument document = QJsonDocument::fromJson(readBytes(path), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    return false;
  }
  QJsonObject object = document.object();
  object.insert(QStringLiteral("phase"), phase);
  object.insert(QStringLiteral("retirement"), retirement);
  if (!intent.isEmpty())
    object.insert(QStringLiteral("intent"), intent);
  return writeBytes(path, QJsonDocument(object).toJson(QJsonDocument::Compact) +
                              '\n');
}

struct FixturePaths {
  QTemporaryDir temporary;
  QString prefix;
  QString drive;
  QString parent;
  QString profile;
  QString exact;
  QString lower;

  FixturePaths() {
    prefix = temporary.filePath("prefix");
    drive = QDir(prefix).filePath("drive_c");
    parent = QDir(drive).filePath("users/steamuser/Documents/My Games/Game");
    profile = temporary.filePath("profiles/Default/saves");
    exact = QDir(parent).filePath("Saves");
    lower = QDir(parent).filePath("saves");
    QDir().mkpath(parent);
    QDir().mkpath(profile);
  }
};

} // namespace

TEST(WineSaveDeployment, PublishesCompleteTreeBeforeMirroringDeletions) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString source = temporary.filePath("source");
  const QString destination = temporary.filePath("destination");
  const QString nested = QDir(source).filePath("nested/save.dat");
  const QString hidden = QDir(source).filePath(".hidden");
  ASSERT_TRUE(writeBytes(nested, "new"));
  ASSERT_TRUE(writeBytes(hidden, "hidden"));
  ASSERT_TRUE(writeBytes(QDir(destination).filePath("nested/save.dat"), "old"));
  ASSERT_TRUE(writeBytes(QDir(destination).filePath("deleted.dat"), "stale"));

  const QDateTime timestamp =
      QDateTime::fromSecsSinceEpoch(1'700'000'000, QTimeZone::UTC);
  QFile timestampFile(nested);
  ASSERT_TRUE(timestampFile.open(QIODevice::ReadWrite));
  ASSERT_TRUE(
      timestampFile.setFileTime(timestamp, QFileDevice::FileModificationTime));
  timestampFile.close();

  const auto result =
      WineSaveDeployment::publishTree(source, destination, true);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_EQ(readBytes(QDir(destination).filePath("nested/save.dat")), "new");
  EXPECT_EQ(readBytes(QDir(destination).filePath(".hidden")), "hidden");
  EXPECT_FALSE(QFileInfo::exists(QDir(destination).filePath("deleted.dat")));
  EXPECT_EQ(QFileInfo(QDir(destination).filePath("nested/save.dat"))
                .lastModified()
                .toSecsSinceEpoch(),
            timestamp.toSecsSinceEpoch());
}

#ifdef Q_OS_UNIX
TEST(WineSaveDeployment, RejectsSpecialAndLinkedLeavesWithoutFollowing) {
  QTemporaryDir temporary;
  QTemporaryDir external;
  ASSERT_TRUE(temporary.isValid());
  ASSERT_TRUE(external.isValid());
  const QString source = temporary.filePath("source");
  const QString destination = temporary.filePath("destination");
  ASSERT_TRUE(QDir().mkpath(source));
  ASSERT_TRUE(QDir().mkpath(destination));
  const QString outside = external.filePath("outside.dat");
  ASSERT_TRUE(writeBytes(outside, "external"));
  const QString linkedSource = QDir(source).filePath("linked.dat");
  ASSERT_EQ(::symlink(QFile::encodeName(outside).constData(),
                      QFile::encodeName(linkedSource).constData()),
            0);

  auto result = WineSaveDeployment::publishTree(source, destination, false);
  EXPECT_FALSE(result);
  EXPECT_EQ(readBytes(outside), "external");

  ASSERT_TRUE(QFile::remove(linkedSource));
  const QString fifo = QDir(source).filePath("pipe");
  ASSERT_EQ(::mkfifo(QFile::encodeName(fifo).constData(), 0600), 0);
  result = WineSaveDeployment::publishTree(source, destination, false);
  EXPECT_FALSE(result);

  ASSERT_TRUE(QFile::remove(fifo));
  ASSERT_TRUE(writeBytes(QDir(source).filePath("save.dat"), "new"));
  const QString linkedDestination = QDir(destination).filePath("save.dat");
  ASSERT_EQ(::symlink(QFile::encodeName(outside).constData(),
                      QFile::encodeName(linkedDestination).constData()),
            0);
  result = WineSaveDeployment::publishTree(source, destination, false);
  EXPECT_FALSE(result);
  EXPECT_TRUE(QFileInfo(linkedDestination).isSymLink());
  EXPECT_EQ(readBytes(outside), "external");
}

TEST(WineSaveDeployment, ForcedShortWritePreservesSourceAndOldDestination) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString source = temporary.filePath("source");
  const QString destination = temporary.filePath("destination");
  ASSERT_TRUE(writeBytes(QDir(source).filePath("save.dat"),
                         QByteArray(1024 * 1024, 'x')));
  ASSERT_TRUE(writeBytes(QDir(destination).filePath("save.dat"), "old"));

  const pid_t child = ::fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    std::signal(SIGXFSZ, SIG_IGN);
    const struct rlimit limit = {4096, 4096};
    if (::setrlimit(RLIMIT_FSIZE, &limit) != 0)
      _exit(2);
    const auto result =
        WineSaveDeployment::publishTree(source, destination, false);
    _exit(result ? 3 : 0);
  }

  int status = 0;
  ASSERT_EQ(::waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
  EXPECT_EQ(readBytes(QDir(destination).filePath("save.dat")), "old");
  EXPECT_EQ(QFileInfo(QDir(source).filePath("save.dat")).size(), 1024 * 1024);
}
#endif

TEST(WineSaveDeployment, DeployAndRestoreKeepGlobalAndProfileIsolated) {
  FixturePaths paths;
  ASSERT_TRUE(paths.temporary.isValid());
  ASSERT_TRUE(
      writeBytes(QDir(paths.exact).filePath("global-upper.dat"), "upper"));
  ASSERT_TRUE(
      writeBytes(QDir(paths.lower).filePath("global-lower.dat"), "lower"));
  ASSERT_TRUE(
      writeBytes(QDir(paths.profile).filePath("profile.dat"), "profile"));

  auto result = WineSaveDeployment::deployLinks(paths.drive, paths.profile,
                                                paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_TRUE(QFileInfo(paths.exact).isSymLink());
  EXPECT_TRUE(QFileInfo(paths.lower).isSymLink());
  EXPECT_EQ(readBytes(QDir(paths.profile).filePath("profile.dat")), "profile");
  EXPECT_FALSE(
      QFileInfo::exists(QDir(paths.profile).filePath("global-upper.dat")));
  EXPECT_EQ(readBytes(QDir(WineSaveDeployment::backupPathFor(paths.exact))
                          .filePath("global-upper.dat")),
            "upper");
  EXPECT_EQ(readBytes(QDir(WineSaveDeployment::backupPathFor(paths.lower))
                          .filePath("global-lower.dat")),
            "lower");

  result = WineSaveDeployment::synchronizeAndRestore(paths.drive, paths.profile,
                                                     paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_FALSE(QFileInfo(paths.exact).isSymLink());
  EXPECT_FALSE(QFileInfo(paths.lower).isSymLink());
  EXPECT_EQ(readBytes(QDir(paths.exact).filePath("global-upper.dat")), "upper");
  EXPECT_EQ(readBytes(QDir(paths.lower).filePath("global-lower.dat")), "lower");
  EXPECT_FALSE(
      QFileInfo::exists(WineSaveDeployment::backupPathFor(paths.exact)));
  EXPECT_FALSE(
      QFileInfo::exists(WineSaveDeployment::backupPathFor(paths.lower)));
}

TEST(WineSaveDeployment, ManagedDeploymentIsIdempotent) {
  FixturePaths paths;
  ASSERT_TRUE(paths.temporary.isValid());
  ASSERT_TRUE(writeBytes(QDir(paths.exact).filePath("global.dat"), "global"));
  ASSERT_TRUE(
      writeBytes(QDir(paths.profile).filePath("profile.dat"), "profile"));

  auto result = WineSaveDeployment::deployLinks(paths.drive, paths.profile,
                                                paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  result = WineSaveDeployment::deployLinks(paths.drive, paths.profile,
                                           paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_TRUE(QFileInfo(paths.exact).isSymLink());
  EXPECT_TRUE(QFileInfo(paths.lower).isSymLink());
  EXPECT_EQ(readBytes(QDir(WineSaveDeployment::backupPathFor(paths.exact))
                          .filePath("global.dat")),
            "global");
  EXPECT_EQ(readBytes(QDir(paths.profile).filePath("profile.dat")), "profile");
}

TEST(WineSaveDeployment, InterruptedSessionPublishesBeforeRestoringGlobal) {
  FixturePaths paths;
  ASSERT_TRUE(paths.temporary.isValid());
  const QString backup = WineSaveDeployment::backupPathFor(paths.exact);
  ASSERT_TRUE(writeBytes(QDir(paths.exact).filePath("global.dat"), "global"));
  ASSERT_TRUE(writeBytes(QDir(paths.profile).filePath("deleted.dat"), "old"));

  auto result = WineSaveDeployment::deployLinks(paths.drive, paths.profile,
                                                paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  ASSERT_TRUE(QFile::remove(paths.exact));
  ASSERT_TRUE(writeBytes(QDir(paths.exact).filePath("session.dat"), "session"));

  result = WineSaveDeployment::synchronizeAndRestore(paths.drive, paths.profile,
                                                     paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_EQ(readBytes(QDir(paths.exact).filePath("global.dat")), "global");
  EXPECT_EQ(readBytes(QDir(paths.profile).filePath("session.dat")), "session");
  // The lowercase path remained a live link, so the real uppercase tree is
  // only an overlay and cannot prove deletion of profile-only files.
  EXPECT_EQ(readBytes(QDir(paths.profile).filePath("deleted.dat")), "old");
  EXPECT_FALSE(QFileInfo::exists(backup));
}

#ifdef Q_OS_UNIX
TEST(WineSaveDeployment, FailedInterruptedPublicationPreservesEveryGeneration) {
  FixturePaths paths;
  QTemporaryDir external;
  ASSERT_TRUE(paths.temporary.isValid());
  ASSERT_TRUE(external.isValid());
  const QString backup = WineSaveDeployment::backupPathFor(paths.exact);
  ASSERT_TRUE(writeBytes(QDir(paths.exact).filePath("global.dat"), "global"));
  auto deployed = WineSaveDeployment::deployLinks(paths.drive, paths.profile,
                                                  paths.exact, Owner);
  ASSERT_TRUE(deployed) << qPrintable(deployed.error);
  ASSERT_TRUE(QFile::remove(paths.exact));
  ASSERT_TRUE(writeBytes(QDir(paths.exact).filePath("session.dat"), "session"));
  const QString outside = external.filePath("outside.dat");
  ASSERT_TRUE(writeBytes(outside, "outside"));
  const QString destination = QDir(paths.profile).filePath("session.dat");
  ASSERT_EQ(::symlink(QFile::encodeName(outside).constData(),
                      QFile::encodeName(destination).constData()),
            0);

  const auto result = WineSaveDeployment::synchronizeAndRestore(
      paths.drive, paths.profile, paths.exact, Owner);
  EXPECT_FALSE(result);
  EXPECT_FALSE(QFileInfo(paths.exact).isSymLink());
  EXPECT_EQ(readBytes(QDir(paths.exact).filePath("session.dat")), "session");
  EXPECT_EQ(readBytes(QDir(backup).filePath("global.dat")), "global");
  EXPECT_TRUE(QFileInfo(destination).isSymLink());
  EXPECT_EQ(readBytes(outside), "outside");
}
#endif

TEST(WineSaveDeployment, RealTreeWithoutBackupIsNeverClaimedDuringSync) {
  FixturePaths paths;
  ASSERT_TRUE(paths.temporary.isValid());
  ASSERT_TRUE(writeBytes(QDir(paths.exact).filePath("unowned.dat"), "unowned"));

  const auto result = WineSaveDeployment::synchronizeAndRestore(
      paths.drive, paths.profile, paths.exact, Owner);
  EXPECT_FALSE(result);
  EXPECT_EQ(readBytes(QDir(paths.exact).filePath("unowned.dat")), "unowned");
  EXPECT_FALSE(QFileInfo::exists(QDir(paths.profile).filePath("unowned.dat")));
}

TEST(WineSaveDeployment, UnrelatedRetirementDirectoryIsNeverClaimed) {
  FixturePaths paths;
  ASSERT_TRUE(paths.temporary.isValid());
  const QString retirement =
      QDir(paths.parent).filePath(".mo2linux_synced_Saves");
  ASSERT_TRUE(writeBytes(QDir(retirement).filePath("sentinel.dat"), "foreign"));
  ASSERT_TRUE(writeBytes(QDir(paths.exact).filePath("global.dat"), "global"));

  auto result = WineSaveDeployment::deployLinks(paths.drive, paths.profile,
                                                paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  result = WineSaveDeployment::synchronizeAndRestore(paths.drive, paths.profile,
                                                     paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_EQ(readBytes(QDir(retirement).filePath("sentinel.dat")), "foreign");
}

#ifdef Q_OS_UNIX
TEST(WineSaveDeployment, AnotherLaunchCannotAdoptMarkedLinks) {
  FixturePaths paths;
  ASSERT_TRUE(paths.temporary.isValid());
  ASSERT_TRUE(
      writeBytes(QDir(paths.profile).filePath("sentinel.dat"), "profile"));
  ASSERT_TRUE(writeBytes(QDir(paths.exact).filePath("global.dat"), "global"));
  auto result = WineSaveDeployment::deployLinks(
      paths.drive, paths.profile, paths.exact, QStringLiteral("old-owner"));
  ASSERT_TRUE(result) << qPrintable(result.error);
  result = WineSaveDeployment::deployLinks(
      paths.drive, paths.profile, paths.exact, QStringLiteral("new-owner"));
  EXPECT_FALSE(result);
  EXPECT_TRUE(QFileInfo(paths.exact).isSymLink());
  EXPECT_EQ(readBytes(QDir(paths.profile).filePath("sentinel.dat")), "profile");
}

TEST(WineSaveDeployment, ForeignLinkIsPreservedAndRejected) {
  FixturePaths paths;
  QTemporaryDir external;
  ASSERT_TRUE(paths.temporary.isValid());
  ASSERT_TRUE(external.isValid());
  ASSERT_TRUE(
      writeBytes(QDir(external.path()).filePath("sentinel.dat"), "external"));
  ASSERT_EQ(::symlink(QFile::encodeName(external.path()).constData(),
                      QFile::encodeName(paths.exact).constData()),
            0);

  const auto result = WineSaveDeployment::deployLinks(
      paths.drive, paths.profile, paths.exact, Owner);
  EXPECT_FALSE(result);
  EXPECT_TRUE(QFileInfo(paths.exact).isSymLink());
  EXPECT_EQ(readBytes(QDir(external.path()).filePath("sentinel.dat")),
            "external");
}
#endif

TEST(WineSaveDeployment, MissingLiveWithHistoricalBackupIsPreservedThroughRun) {
  FixturePaths paths;
  ASSERT_TRUE(paths.temporary.isValid());
  const QString backup = WineSaveDeployment::backupPathFor(paths.exact);
  ASSERT_TRUE(writeBytes(QDir(backup).filePath("global.dat"), "global"));

  auto result = WineSaveDeployment::deployLinks(paths.drive, paths.profile,
                                                paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  result = WineSaveDeployment::synchronizeAndRestore(paths.drive, paths.profile,
                                                     paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_EQ(readBytes(QDir(paths.exact).filePath("global.dat")), "global");
  EXPECT_FALSE(QFileInfo::exists(backup));
}

TEST(WineSaveDeployment, BindPreparationPreservesAmbiguousLiveAndBackup) {
  FixturePaths paths;
  ASSERT_TRUE(paths.temporary.isValid());
  const QString backup = WineSaveDeployment::backupPathFor(paths.exact);
  ASSERT_TRUE(writeBytes(QDir(paths.exact).filePath("session.dat"), "session"));
  ASSERT_TRUE(writeBytes(QDir(backup).filePath("global.dat"), "global"));

  const auto result = WineSaveDeployment::prepareBindTarget(
      paths.drive, paths.profile, paths.exact, Owner);
  EXPECT_FALSE(result);
  EXPECT_EQ(readBytes(QDir(paths.exact).filePath("session.dat")), "session");
  EXPECT_EQ(readBytes(QDir(backup).filePath("global.dat")), "global");
}

TEST(WineSaveDeployment, BindPreparationCreatesEveryCaseVariantTarget) {
  FixturePaths paths;
  ASSERT_TRUE(paths.temporary.isValid());
  const auto result = WineSaveDeployment::prepareBindTarget(
      paths.drive, paths.profile, paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_TRUE(QFileInfo(paths.exact).isDir());
  EXPECT_FALSE(QFileInfo(paths.exact).isSymLink());
  EXPECT_TRUE(QFileInfo(paths.lower).isDir());
  EXPECT_FALSE(QFileInfo(paths.lower).isSymLink());
  EXPECT_EQ(WineSaveDeployment::managedLivePaths(paths.exact),
            QStringList({paths.exact, paths.lower}));
}

TEST(WineSaveDeployment, UnmarkedLiveAndBackupAreNeverClaimed) {
  FixturePaths paths;
  ASSERT_TRUE(paths.temporary.isValid());
  const QString backup = WineSaveDeployment::backupPathFor(paths.exact);
  ASSERT_TRUE(writeBytes(QDir(paths.exact).filePath("partial.dat"), "partial"));
  ASSERT_TRUE(writeBytes(QDir(backup).filePath("global.dat"), "global"));
  ASSERT_TRUE(
      writeBytes(QDir(paths.profile).filePath("profile.dat"), "profile"));

  const auto result = WineSaveDeployment::deployLinks(
      paths.drive, paths.profile, paths.exact, Owner);
  EXPECT_FALSE(result);
  EXPECT_EQ(readBytes(QDir(paths.exact).filePath("partial.dat")), "partial");
  EXPECT_EQ(readBytes(QDir(backup).filePath("global.dat")), "global");
  EXPECT_EQ(readBytes(QDir(paths.profile).filePath("profile.dat")), "profile");
}

TEST(WineSaveDeployment, PhysicalSaveLeaseExcludesConcurrentOwner) {
  FixturePaths paths;
  ASSERT_TRUE(paths.temporary.isValid());
  const QString leasePath =
      WineSaveDeployment::leasePathFor(paths.prefix, paths.exact);
  QLockFile first(leasePath);
  QLockFile second(leasePath);
  ASSERT_TRUE(first.tryLock(0));
  EXPECT_FALSE(second.tryLock(0));
  first.unlock();
  EXPECT_TRUE(second.tryLock(0));
}

TEST(WineSaveDeployment, DistinctSavePathsShareOnePrefixLease) {
  FixturePaths paths;
  ASSERT_TRUE(paths.temporary.isValid());
  const QString other = QDir(paths.parent).filePath("OtherSaves");
  EXPECT_EQ(WineSaveDeployment::leasePathFor(paths.prefix, paths.exact),
            WineSaveDeployment::leasePathFor(paths.prefix, other));

  QLockFile first(WineSaveDeployment::leasePathFor(paths.prefix, paths.exact));
  QLockFile second(WineSaveDeployment::leasePathFor(paths.prefix, other));
  ASSERT_TRUE(first.tryLock(0));
  EXPECT_FALSE(second.tryLock(0));
  first.unlock();
  EXPECT_TRUE(second.tryLock(0));
}

TEST(WineSaveDeployment, PersistedSessionLeaseCannotBeStolen) {
  FixturePaths paths;
  ASSERT_TRUE(paths.temporary.isValid());
  auto result =
      WineSaveDeployment::beginSessionLease(paths.prefix, paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_TRUE(WineSaveDeployment::hasPersistedSessionLease(paths.prefix));
  result = WineSaveDeployment::beginSessionLease(paths.prefix, paths.exact,
                                                 QStringLiteral("other-owner"));
  EXPECT_FALSE(result);
  result = WineSaveDeployment::endSessionLease(paths.prefix, paths.exact,
                                               QStringLiteral("other-owner"));
  EXPECT_FALSE(result);
  EXPECT_TRUE(QFileInfo::exists(
      WineSaveDeployment::sessionLeasePathFor(paths.prefix, paths.exact)));
  result =
      WineSaveDeployment::endSessionLease(paths.prefix, paths.exact, Owner);
  EXPECT_TRUE(result) << qPrintable(result.error);
  EXPECT_FALSE(QFileInfo::exists(
      WineSaveDeployment::sessionLeasePathFor(paths.prefix, paths.exact)));
  EXPECT_FALSE(WineSaveDeployment::hasPersistedSessionLease(paths.prefix));
}

TEST(WineSaveDeployment, SessionIdentitySurvivesLegacyLinkNormalization) {
  FixturePaths paths;
  ASSERT_TRUE(paths.temporary.isValid());
  ASSERT_TRUE(
      writeBytes(QDir(paths.profile).filePath("profile.dat"), "profile"));
  const QString backup = WineSaveDeployment::backupPathFor(paths.exact);
  ASSERT_TRUE(writeBytes(QDir(backup).filePath("global.dat"), "global"));
  ASSERT_TRUE(QFile::link(paths.profile, paths.exact));

  const QString sessionPath =
      WineSaveDeployment::sessionLeasePathFor(paths.prefix, paths.exact);
  auto result =
      WineSaveDeployment::beginSessionLease(paths.prefix, paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  ASSERT_TRUE(QFileInfo::exists(sessionPath));
  result = WineSaveDeployment::prepareBindTarget(paths.drive, paths.profile,
                                                 paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_TRUE(QFileInfo(paths.exact).isDir());
  EXPECT_EQ(readBytes(QDir(paths.exact).filePath("global.dat")), "global");

  result =
      WineSaveDeployment::endSessionLease(paths.prefix, paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_FALSE(QFileInfo::exists(sessionPath));
  EXPECT_FALSE(WineSaveDeployment::hasPersistedSessionLease(paths.prefix));
}

TEST(WineSaveDeployment, CaseAliasesShareOnePhysicalLeaseIdentity) {
  FixturePaths paths;
  ASSERT_TRUE(paths.temporary.isValid());
  EXPECT_EQ(WineSaveDeployment::leasePathFor(paths.prefix, paths.exact),
            WineSaveDeployment::leasePathFor(paths.prefix, paths.lower));
  EXPECT_EQ(WineSaveDeployment::sessionLeasePathFor(paths.prefix, paths.exact),
            WineSaveDeployment::sessionLeasePathFor(paths.prefix, paths.lower));

  auto result =
      WineSaveDeployment::beginSessionLease(paths.prefix, paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  result = WineSaveDeployment::beginSessionLease(paths.prefix, paths.lower,
                                                 QStringLiteral("other-owner"));
  EXPECT_FALSE(result);
  EXPECT_TRUE(
      WineSaveDeployment::endSessionLease(paths.prefix, paths.lower, Owner));
}

#ifdef Q_OS_UNIX
TEST(WineSaveDeployment, MissingTailPrefixAliasesSharePhysicalLeaseIdentity) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString prefix = temporary.filePath("prefix");
  ASSERT_TRUE(QDir().mkpath(QDir(prefix).filePath("drive_c")));
  const QString alias = temporary.filePath("prefix-alias");
  ASSERT_EQ(::symlink(QFile::encodeName(prefix).constData(),
                      QFile::encodeName(alias).constData()),
            0);
  const QString tail = "drive_c/users/steamuser/Documents/My Games/Game/Saves";
  const QString exact = QDir(prefix).filePath(tail);
  const QString aliased =
      QDir(alias).filePath(QString(tail).replace("Saves", "saves"));

  EXPECT_EQ(WineSaveDeployment::leasePathFor(prefix, exact),
            WineSaveDeployment::leasePathFor(alias, aliased));
  EXPECT_EQ(WineSaveDeployment::sessionLeasePathFor(prefix, exact),
            WineSaveDeployment::sessionLeasePathFor(alias, aliased));
}

TEST(WineSaveDeployment, RetargetedPrefixAliasDoesNotMatchPreparedIdentity) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString first = temporary.filePath("first");
  const QString second = temporary.filePath("second");
  ASSERT_TRUE(QDir().mkpath(first));
  ASSERT_TRUE(QDir().mkpath(second));
  const QString alias = temporary.filePath("prefix");
  ASSERT_EQ(::symlink(QFile::encodeName(first).constData(),
                      QFile::encodeName(alias).constData()),
            0);
  const QString prepared = QFileInfo(alias).canonicalFilePath();
  ASSERT_TRUE(WineSaveDeployment::samePhysicalDirectory(prepared, alias));

  ASSERT_TRUE(QFile::remove(alias));
  ASSERT_EQ(::symlink(QFile::encodeName(second).constData(),
                      QFile::encodeName(alias).constData()),
            0);
  EXPECT_FALSE(WineSaveDeployment::samePhysicalDirectory(prepared, alias));
}
#endif

TEST(WineSaveDeployment, InterruptedPrelaunchRollbackResumesByIntent) {
  FixturePaths paths;
  ASSERT_TRUE(paths.temporary.isValid());
  ASSERT_TRUE(writeBytes(QDir(paths.exact).filePath("upper.dat"), "upper"));
  ASSERT_TRUE(writeBytes(QDir(paths.lower).filePath("lower.dat"), "lower"));
  auto result = WineSaveDeployment::deployLinks(paths.drive, paths.profile,
                                                paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);

  ASSERT_TRUE(QFile::remove(paths.exact));
  ASSERT_TRUE(QDir().rename(WineSaveDeployment::backupPathFor(paths.exact),
                            paths.exact));
  ASSERT_TRUE(patchMarker(paths.exact, QStringLiteral("restored")));

  result = WineSaveDeployment::rollbackLinks(paths.drive, paths.profile,
                                             paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_TRUE(result.topologyComplete);
  EXPECT_EQ(readBytes(QDir(paths.exact).filePath("upper.dat")), "upper");
  EXPECT_EQ(readBytes(QDir(paths.lower).filePath("lower.dat")), "lower");
  EXPECT_FALSE(QFileInfo::exists(markerPath(paths.exact)));
  EXPECT_FALSE(QFileInfo::exists(markerPath(paths.lower)));
}

TEST(WineSaveDeployment, BindNormalizationCleanupResumesPublishIntent) {
  FixturePaths paths;
  ASSERT_TRUE(paths.temporary.isValid());
  ASSERT_TRUE(writeBytes(QDir(paths.exact).filePath("upper.dat"), "upper"));
  ASSERT_TRUE(writeBytes(QDir(paths.lower).filePath("lower.dat"), "lower"));
  auto result = WineSaveDeployment::deployLinks(paths.drive, paths.profile,
                                                paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);

  // Model a bind-preparation normalization that already published the save
  // generation and restored one case slot before cleanup was interrupted.
  ASSERT_TRUE(QFile::remove(paths.exact));
  ASSERT_TRUE(QDir().rename(WineSaveDeployment::backupPathFor(paths.exact),
                            paths.exact));
  ASSERT_TRUE(patchMarker(paths.exact, QStringLiteral("restored"), {},
                          QStringLiteral("publish")));
  ASSERT_TRUE(patchMarker(paths.lower, QStringLiteral("published"), {},
                          QStringLiteral("publish")));

  result = WineSaveDeployment::rollbackLinks(paths.drive, paths.profile,
                                             paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_TRUE(result.topologyComplete);
  EXPECT_EQ(readBytes(QDir(paths.exact).filePath("upper.dat")), "upper");
  EXPECT_EQ(readBytes(QDir(paths.lower).filePath("lower.dat")), "lower");
  EXPECT_FALSE(QFileInfo::exists(markerPath(paths.exact)));
  EXPECT_FALSE(QFileInfo::exists(markerPath(paths.lower)));
}

TEST(WineSaveDeployment, MarkedStateCannotCrossProfiles) {
  FixturePaths paths;
  ASSERT_TRUE(paths.temporary.isValid());
  const QString otherProfile = paths.temporary.filePath("profiles/Other/saves");
  ASSERT_TRUE(QDir().mkpath(otherProfile));
  ASSERT_TRUE(writeBytes(QDir(paths.exact).filePath("global.dat"), "global"));
  ASSERT_TRUE(writeBytes(QDir(otherProfile).filePath("other.dat"), "other"));

  auto result = WineSaveDeployment::deployLinks(paths.drive, paths.profile,
                                                paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  result = WineSaveDeployment::deployLinks(
      paths.drive, otherProfile, paths.exact, QStringLiteral("other-owner"));
  EXPECT_FALSE(result);
  EXPECT_TRUE(QFileInfo(paths.exact).isSymLink());
  EXPECT_EQ(QFileInfo(paths.exact).canonicalFilePath(),
            QFileInfo(paths.profile).canonicalFilePath());
  EXPECT_EQ(readBytes(QDir(otherProfile).filePath("other.dat")), "other");
}

TEST(WineSaveDeployment, WineReplacedLowerLinkWithoutGlobalStillSynchronizes) {
  FixturePaths paths;
  ASSERT_TRUE(paths.temporary.isValid());
  ASSERT_TRUE(
      writeBytes(QDir(paths.profile).filePath("existing.dat"), "existing"));
  auto result = WineSaveDeployment::deployLinks(paths.drive, paths.profile,
                                                paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  ASSERT_TRUE(QFile::remove(paths.lower));
  ASSERT_TRUE(writeBytes(QDir(paths.lower).filePath("session.dat"), "session"));

  result = WineSaveDeployment::synchronizeAndRestore(paths.drive, paths.profile,
                                                     paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_EQ(readBytes(QDir(paths.profile).filePath("session.dat")), "session");
  EXPECT_EQ(readBytes(QDir(paths.profile).filePath("existing.dat")),
            "existing");
  EXPECT_TRUE(QFileInfo(paths.lower).isDir());
  EXPECT_FALSE(QFileInfo(paths.lower).isSymLink());
}

TEST(WineSaveDeployment, AbortedLaunchRestoresGlobalsWithoutPublishing) {
  FixturePaths paths;
  ASSERT_TRUE(paths.temporary.isValid());
  ASSERT_TRUE(writeBytes(QDir(paths.exact).filePath("global.dat"), "global"));
  ASSERT_TRUE(
      writeBytes(QDir(paths.profile).filePath("profile.dat"), "profile"));
  auto result = WineSaveDeployment::deployLinks(paths.drive, paths.profile,
                                                paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);

  result = WineSaveDeployment::rollbackLinks(paths.drive, paths.profile,
                                             paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_EQ(readBytes(QDir(paths.exact).filePath("global.dat")), "global");
  EXPECT_EQ(readBytes(QDir(paths.profile).filePath("profile.dat")), "profile");
}

TEST(WineSaveDeployment,
     DistinctCaseVariantSessionTreesPublishAsOneGeneration) {
  FixturePaths paths;
  ASSERT_TRUE(paths.temporary.isValid());
  ASSERT_TRUE(writeBytes(QDir(paths.profile).filePath("stale.dat"), "stale"));
  auto result = WineSaveDeployment::deployLinks(paths.drive, paths.profile,
                                                paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  ASSERT_TRUE(QFile::remove(paths.exact));
  ASSERT_TRUE(QFile::remove(paths.lower));
  ASSERT_TRUE(writeBytes(QDir(paths.exact).filePath("upper.dat"), "upper"));
  ASSERT_TRUE(writeBytes(QDir(paths.lower).filePath("lower.dat"), "lower"));

  result = WineSaveDeployment::synchronizeAndRestore(paths.drive, paths.profile,
                                                     paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_EQ(readBytes(QDir(paths.profile).filePath("upper.dat")), "upper");
  EXPECT_EQ(readBytes(QDir(paths.profile).filePath("lower.dat")), "lower");
  EXPECT_FALSE(QFileInfo::exists(QDir(paths.profile).filePath("stale.dat")));
}

TEST(WineSaveDeployment, ConflictingCaseVariantSessionTreesRemainRecoverable) {
  FixturePaths paths;
  ASSERT_TRUE(paths.temporary.isValid());
  auto result = WineSaveDeployment::deployLinks(paths.drive, paths.profile,
                                                paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  ASSERT_TRUE(QFile::remove(paths.exact));
  ASSERT_TRUE(QFile::remove(paths.lower));
  ASSERT_TRUE(writeBytes(QDir(paths.exact).filePath("same.dat"), "upper"));
  ASSERT_TRUE(writeBytes(QDir(paths.lower).filePath("same.dat"), "lower"));

  result = WineSaveDeployment::synchronizeAndRestore(paths.drive, paths.profile,
                                                     paths.exact, Owner);
  EXPECT_FALSE(result);
  EXPECT_EQ(readBytes(QDir(paths.exact).filePath("same.dat")), "upper");
  EXPECT_EQ(readBytes(QDir(paths.lower).filePath("same.dat")), "lower");
}

#ifdef Q_OS_UNIX
TEST(WineSaveDeployment, SymlinkedPrefixAncestorWithMissingTailCannotEscape) {
  QTemporaryDir temporary;
  QTemporaryDir external;
  ASSERT_TRUE(temporary.isValid());
  ASSERT_TRUE(external.isValid());
  const QString prefix = temporary.filePath("prefix/drive_c");
  ASSERT_TRUE(QDir().mkpath(prefix));
  const QString users = QDir(prefix).filePath("users");
  ASSERT_EQ(::symlink(QFile::encodeName(external.path()).constData(),
                      QFile::encodeName(users).constData()),
            0);
  const QString profile = temporary.filePath("profile/saves");
  ASSERT_TRUE(QDir().mkpath(profile));
  const QString live =
      QDir(users).filePath("steamuser/Documents/My Games/Game/Saves");

  const auto result =
      WineSaveDeployment::deployLinks(prefix, profile, live, Owner);
  EXPECT_FALSE(result);
  EXPECT_FALSE(QFileInfo::exists(QDir(external.path()).filePath("steamuser")));
}
#endif

TEST(WineSaveDeployment, TeardownRetryNeverRepublishesOneCaseVariant) {
  FixturePaths paths;
  ASSERT_TRUE(paths.temporary.isValid());
  auto result = WineSaveDeployment::deployLinks(paths.drive, paths.profile,
                                                paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);

  // Model a crash after the complete upper+lower generation was published and
  // the upper alias was restored, but before the lower alias was retired.
  ASSERT_TRUE(QFile::remove(paths.exact));
  ASSERT_TRUE(QFile::remove(paths.lower));
  const QString upperBackup = WineSaveDeployment::backupPathFor(paths.exact);
  ASSERT_TRUE(QDir().rename(upperBackup, paths.exact));
  ASSERT_TRUE(writeBytes(QDir(paths.lower).filePath("lower.dat"), "lower"));
  ASSERT_TRUE(writeBytes(QDir(paths.profile).filePath("upper.dat"), "upper"));
  ASSERT_TRUE(writeBytes(QDir(paths.profile).filePath("lower.dat"), "lower"));
  const QString upperRetirement =
      QDir(paths.parent)
          .filePath(
              QStringLiteral(".mo2linux_synced_Saves_%1")
                  .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
  ASSERT_TRUE(writeBytes(QDir(upperRetirement).filePath("upper.dat"), "upper"));
  ASSERT_TRUE(
      patchMarker(paths.exact, QStringLiteral("restored"), upperRetirement));
  ASSERT_TRUE(patchMarker(paths.lower, QStringLiteral("published")));

  result = WineSaveDeployment::synchronizeAndRestore(paths.drive, paths.profile,
                                                     paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_EQ(readBytes(QDir(paths.profile).filePath("upper.dat")), "upper");
  EXPECT_EQ(readBytes(QDir(paths.profile).filePath("lower.dat")), "lower");
  EXPECT_FALSE(QFileInfo::exists(upperRetirement));
}

TEST(WineSaveDeployment, PartialRestoredMarkerRetirementIsIdempotent) {
  FixturePaths paths;
  ASSERT_TRUE(paths.temporary.isValid());
  auto result = WineSaveDeployment::deployLinks(paths.drive, paths.profile,
                                                paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  ASSERT_TRUE(QFile::remove(paths.exact));
  ASSERT_TRUE(QFile::remove(paths.lower));
  ASSERT_TRUE(QDir().rename(WineSaveDeployment::backupPathFor(paths.exact),
                            paths.exact));
  ASSERT_TRUE(QDir().rename(WineSaveDeployment::backupPathFor(paths.lower),
                            paths.lower));
  ASSERT_TRUE(patchMarker(paths.exact, QStringLiteral("restored")));
  ASSERT_TRUE(patchMarker(paths.lower, QStringLiteral("restored")));
  ASSERT_TRUE(QFile::remove(markerPath(paths.exact)));

  result = WineSaveDeployment::synchronizeAndRestore(paths.drive, paths.profile,
                                                     paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_TRUE(result.topologyComplete);
  EXPECT_FALSE(QFileInfo::exists(markerPath(paths.lower)));
}

TEST(WineSaveDeployment, MarkerCannotNameForeignRetirementDirectory) {
  FixturePaths paths;
  ASSERT_TRUE(paths.temporary.isValid());
  QTemporaryDir external;
  ASSERT_TRUE(external.isValid());
  ASSERT_TRUE(writeBytes(external.filePath("sentinel.dat"), "foreign"));
  auto result = WineSaveDeployment::deployLinks(paths.drive, paths.profile,
                                                paths.exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  ASSERT_TRUE(
      patchMarker(paths.exact, QStringLiteral("restored"), external.path()));

  result = WineSaveDeployment::synchronizeAndRestore(paths.drive, paths.profile,
                                                     paths.exact, Owner);
  EXPECT_FALSE(result);
  EXPECT_EQ(readBytes(external.filePath("sentinel.dat")), "foreign");
}

TEST(WineProfileIniSync, PublishesEditThenRestoresGlobalGeneration) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString profile = temporary.filePath("profile/Game.ini");
  const QString prefix = temporary.filePath("prefix/Game.ini");
  const QString backup = prefix + ".mo2linux_backup";
  ASSERT_TRUE(writeBytes(profile, "profile-old"));
  ASSERT_TRUE(writeBytes(prefix, "game-edit"));
  ASSERT_TRUE(writeBytes(backup, "global"));

  WineProfileIniSync::Deployment deployment{
      profile, prefix, {{prefix, backup}}, {prefix}, {}, true};
  QList deployments{deployment};
  auto phase = WineProfileIniSync::CleanupPhase::Prepared;
  const auto result = WineProfileIniSync::finish(
      deployments, Owner, /*publishChanges=*/true, phase);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_EQ(readBytes(profile), "game-edit");
  EXPECT_EQ(readBytes(prefix), "global");
  EXPECT_FALSE(QFileInfo::exists(backup));
}

TEST(WineProfileIniSync, NewPrefixIniPublishesAndIsThenRetired) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString profile = temporary.filePath("profile/Game.ini");
  const QString prefix = temporary.filePath("prefix/Game.ini");
  ASSERT_TRUE(writeBytes(profile, "profile-old"));
  ASSERT_TRUE(writeBytes(prefix, "game-edit"));

  WineProfileIniSync::Deployment deployment{profile,  prefix, {},
                                            {prefix}, {},     true};
  QList deployments{deployment};
  auto phase = WineProfileIniSync::CleanupPhase::Prepared;
  const auto result = WineProfileIniSync::finish(
      deployments, Owner, /*publishChanges=*/true, phase);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_EQ(readBytes(profile), "game-edit");
  EXPECT_FALSE(QFileInfo::exists(prefix));
}

TEST(WineProfileIniSync, AbortedLaunchRestoresWithoutPublishing) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString profile = temporary.filePath("profile/Game.ini");
  const QString prefix = temporary.filePath("prefix/Game.ini");
  const QString backup = prefix + ".mo2linux_backup";
  ASSERT_TRUE(writeBytes(profile, "profile"));
  ASSERT_TRUE(writeBytes(prefix, "deployed"));
  ASSERT_TRUE(writeBytes(backup, "global"));

  WineProfileIniSync::Deployment deployment{
      profile, prefix, {{prefix, backup}}, {prefix}, {}, true};
  QList deployments{deployment};
  auto phase = WineProfileIniSync::CleanupPhase::Prepared;
  const auto result = WineProfileIniSync::finish(
      deployments, Owner, /*publishChanges=*/false, phase);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_EQ(readBytes(profile), "profile");
  EXPECT_EQ(readBytes(prefix), "global");
}

TEST(WineProfileIniSync, DeploymentRejectsUnsafeCaseLeafBeforeMutation) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString profile = temporary.filePath("profile/Game.ini");
  const QString prefix = temporary.filePath("prefix/Game.ini");
  const QString lower = temporary.filePath("prefix/game.ini");
  ASSERT_TRUE(writeBytes(profile, "profile"));
  ASSERT_TRUE(writeBytes(prefix, "global"));
  ASSERT_TRUE(QDir().mkpath(lower));
  ASSERT_TRUE(writeBytes(QDir(lower).filePath("sentinel"), "foreign"));

  WineProfileIniSync::Deployment deployment;
  const auto result =
      WineProfileIniSync::deploy(profile, prefix, Owner, deployment);
  EXPECT_FALSE(result);
  EXPECT_FALSE(deployment.needsCleanup());
  EXPECT_EQ(readBytes(prefix), "global");
  EXPECT_EQ(readBytes(QDir(lower).filePath("sentinel")), "foreign");
  EXPECT_FALSE(QFileInfo::exists(prefix + ".mo2linux_backup"));
}

TEST(WineProfileIniSync, AbortRetiresOnlyLeavesCreatedByDeployment) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString profile = temporary.filePath("profile/Game.ini");
  const QString prefix = temporary.filePath("prefix/Game.ini");
  const QString lower = temporary.filePath("prefix/game.ini");
  const QString backup = prefix + ".mo2linux_backup";
  ASSERT_TRUE(writeBytes(profile, "profile"));
  ASSERT_TRUE(writeBytes(prefix, "deployed"));
  ASSERT_TRUE(writeBytes(lower, "untouched-global-lower"));
  ASSERT_TRUE(writeBytes(backup, "global-exact"));

  QList<WineProfileIniSync::Deployment> deployments{
      {profile, prefix, {{prefix, backup}}, {prefix}, {}, false}};
  auto phase = WineProfileIniSync::CleanupPhase::Prepared;
  const auto result = WineProfileIniSync::finish(
      deployments, Owner, /*publishChanges=*/false, phase);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_EQ(readBytes(profile), "profile");
  EXPECT_EQ(readBytes(prefix), "global-exact");
  EXPECT_EQ(readBytes(lower), "untouched-global-lower");
}

TEST(WineProfileIniSync, FinalRetirementRetryNeverRepublishesGlobals) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString profileOne = temporary.filePath("profile/One.ini");
  const QString prefixOne = temporary.filePath("prefix/One.ini");
  const QString profileTwo = temporary.filePath("profile/Two.ini");
  const QString prefixTwo = temporary.filePath("prefix/Two.ini");
  ASSERT_TRUE(writeBytes(profileOne, "one-session"));
  ASSERT_TRUE(writeBytes(prefixOne, "one-global"));
  ASSERT_TRUE(writeBytes(profileTwo, "two-session"));
  ASSERT_TRUE(writeBytes(prefixTwo, "two-global"));
  ASSERT_TRUE(writeBytes(iniRetirement(prefixOne), "one-retired"));
  ASSERT_TRUE(QDir().mkpath(iniRetirement(prefixTwo)));

  QList<WineProfileIniSync::Deployment> deployments{
      {profileOne, prefixOne, {}, {}, {prefixOne}, true},
      {profileTwo, prefixTwo, {}, {}, {prefixTwo}, true}};
  auto phase = WineProfileIniSync::CleanupPhase::GlobalsRestored;
  auto result = WineProfileIniSync::finish(deployments, Owner,
                                           /*publishChanges=*/true, phase);
  EXPECT_FALSE(result);
  EXPECT_FALSE(QFileInfo::exists(iniRetirement(prefixOne)));
  EXPECT_EQ(readBytes(profileOne), "one-session");
  EXPECT_EQ(readBytes(prefixOne), "one-global");

  ASSERT_TRUE(QDir(iniRetirement(prefixTwo)).removeRecursively());
  ASSERT_TRUE(writeBytes(iniRetirement(prefixTwo), "two-retired"));
  result = WineProfileIniSync::finish(deployments, Owner,
                                      /*publishChanges=*/true, phase);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_EQ(readBytes(profileOne), "one-session");
  EXPECT_EQ(readBytes(profileTwo), "two-session");
  EXPECT_EQ(readBytes(prefixOne), "one-global");
  EXPECT_EQ(readBytes(prefixTwo), "two-global");
}

TEST(WineProfileIniSync, EqualTimeDivergentCaseVariantsRemainRecoverable) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString profile = temporary.filePath("profile/Game.ini");
  const QString exact = temporary.filePath("prefix/Game.ini");
  const QString lower = temporary.filePath("prefix/game.ini");
  ASSERT_TRUE(writeBytes(profile, "profile-old"));
  ASSERT_TRUE(writeBytes(exact, "exact-edit"));
  ASSERT_TRUE(writeBytes(lower, "lower-edit"));
  const QDateTime sameTime =
      QDateTime::fromSecsSinceEpoch(1'700'000'000, QTimeZone::UTC);
  for (const QString &path : {exact, lower}) {
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    ASSERT_TRUE(file.setFileTime(sameTime, QFileDevice::FileModificationTime));
  }

  QList<WineProfileIniSync::Deployment> deployments{
      {profile, exact, {}, {exact, lower}, {}, true}};
  auto phase = WineProfileIniSync::CleanupPhase::Prepared;
  const auto result = WineProfileIniSync::finish(
      deployments, Owner, /*publishChanges=*/true, phase);
  EXPECT_FALSE(result);
  EXPECT_EQ(phase, WineProfileIniSync::CleanupPhase::Prepared);
  EXPECT_EQ(readBytes(profile), "profile-old");
  EXPECT_EQ(readBytes(exact), "exact-edit");
  EXPECT_EQ(readBytes(lower), "lower-edit");
}

TEST(WineProfileIniSync, RoutingSanitizesNewestReplacedCaseVariant) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString profile = temporary.filePath("profile/Game.ini");
  const QString exact = temporary.filePath("prefix/Game.ini");
  const QString lower = temporary.filePath("prefix/game.ini");
  const QByteArray original =
      "[General]\nsLocalSavePath=Saves\\\nbUseMyGamesDirectory=0\n";
  ASSERT_TRUE(writeBytes(profile, "profile-old"));
  ASSERT_TRUE(writeBytes(exact, original));
  auto routing = WineSaveRouting::activate(exact, Owner);
  ASSERT_TRUE(routing) << qPrintable(routing.error);
  ASSERT_TRUE(writeBytes(
      lower, "[General]\nsLocalSavePath=__MO_Saves\\\nbUseMyGamesDirectory=1\n"
             "GameEdit=lower\n"));
  const QDateTime newer = QDateTime::currentDateTimeUtc().addSecs(10);
  QFile lowerFile(lower);
  ASSERT_TRUE(lowerFile.open(QIODevice::ReadOnly));
  ASSERT_TRUE(lowerFile.setFileTime(newer, QFileDevice::FileModificationTime));
  lowerFile.close();

  routing = WineSaveRouting::restore(exact, Owner);
  ASSERT_TRUE(routing) << qPrintable(routing.error);
  WineProfileIniSync::Deployment deployment{profile,        exact, {},
                                            {exact, lower}, {},    true};
  QList deployments{deployment};
  auto phase = WineProfileIniSync::CleanupPhase::Prepared;
  const auto result = WineProfileIniSync::finish(
      deployments, Owner, /*publishChanges=*/true, phase);
  ASSERT_TRUE(result) << qPrintable(result.error);
  const QByteArray published = readBytes(profile);
  EXPECT_NE(published.indexOf("GameEdit=lower"), -1);
  EXPECT_NE(published.indexOf("sLocalSavePath=Saves\\"), -1);
  EXPECT_NE(published.indexOf("bUseMyGamesDirectory=0"), -1);
  EXPECT_EQ(published.indexOf("__MO_Saves"), -1);
}

TEST(WineProfileIniSync, RoutingRestoreAcceptsOwnedCaseAlias) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString profile = temporary.filePath("profile/Game.ini");
  const QString exact = temporary.filePath("prefix/Game.ini");
  const QString lower = temporary.filePath("prefix/game.ini");
  ASSERT_TRUE(writeBytes(
      profile, "[General]\nsLocalSavePath=Saves\\\nbUseMyGamesDirectory=0\n"));
  ASSERT_TRUE(writeBytes(exact, "global"));

  WineProfileIniSync::Deployment deployment;
  auto result = WineProfileIniSync::deploy(profile, exact, Owner, deployment);
  ASSERT_TRUE(result) << qPrintable(result.error);
  ASSERT_TRUE(QFileInfo(lower).isSymLink());
  auto routing = WineSaveRouting::activate(exact, Owner);
  ASSERT_TRUE(routing) << qPrintable(routing.error);
  routing = WineSaveRouting::restore(exact, Owner);
  ASSERT_TRUE(routing) << qPrintable(routing.error);

  QList deployments{deployment};
  auto phase = WineProfileIniSync::CleanupPhase::Prepared;
  result = WineProfileIniSync::finish(deployments, Owner,
                                      /*publishChanges=*/false, phase);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_EQ(readBytes(exact), "global");
  EXPECT_FALSE(QFileInfo::exists(lower));
}

TEST(WineProfileIniSync, DeletedSessionIniDoesNotReplaceTheProfile) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString profile = temporary.filePath("profile/Game.ini");
  const QString exact = temporary.filePath("prefix/Game.ini");
  const QString lower = temporary.filePath("prefix/game.ini");
  ASSERT_TRUE(writeBytes(
      profile, "[General]\nsLocalSavePath=Saves\\\nbUseMyGamesDirectory=0\n"
               "ProfileData=keep\n"));
  ASSERT_TRUE(writeBytes(exact, "global"));

  WineProfileIniSync::Deployment deployment;
  auto result = WineProfileIniSync::deploy(profile, exact, Owner, deployment);
  ASSERT_TRUE(result) << qPrintable(result.error);
  ASSERT_TRUE(QFileInfo(lower).isSymLink());
  auto routing = WineSaveRouting::activate(exact, Owner);
  ASSERT_TRUE(routing) << qPrintable(routing.error);
  ASSERT_TRUE(QFile::remove(exact));
  ASSERT_TRUE(QFileInfo(lower).isSymLink());
  EXPECT_FALSE(QFileInfo(lower).exists());

  routing = WineSaveRouting::restore(exact, Owner);
  ASSERT_TRUE(routing) << qPrintable(routing.error);
  QList deployments{deployment};
  auto phase = WineProfileIniSync::CleanupPhase::Prepared;
  result = WineProfileIniSync::finish(deployments, Owner,
                                      /*publishChanges=*/true, phase);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_NE(readBytes(profile).indexOf("ProfileData=keep"), -1);
  EXPECT_EQ(readBytes(exact), "global");
  EXPECT_FALSE(QFileInfo(lower).isSymLink());
}

TEST(WineProfileIniSync, StartupRecoveryPublishesBeforeRetiringBackup) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString prefixRoot = temporary.filePath("prefix");
  const QString live = QDir(prefixRoot).filePath("drive_c/game/Game.ini");
  const QString backup = live + ".mo2linux_backup";
  ASSERT_TRUE(writeBytes(backup, "recovery-generation"));

  const auto result = WineProfileIniSync::restoreLegacyBackup(live, backup);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_EQ(readBytes(live), "recovery-generation");
  EXPECT_FALSE(QFileInfo::exists(backup));
}

TEST(WineProfileIniSync, AmbiguousLegacyCoexistencePreservesBothGenerations) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString live = temporary.filePath("prefix/drive_c/game/Game.ini");
  const QString backup = live + ".mo2linux_backup";
  ASSERT_TRUE(writeBytes(live, "possible-game-edit"));
  ASSERT_TRUE(writeBytes(backup, "pre-launch-global"));

  const auto result = WineProfileIniSync::restoreLegacyBackup(live, backup);
  EXPECT_FALSE(result);
  EXPECT_EQ(readBytes(live), "possible-game-edit");
  EXPECT_EQ(readBytes(backup), "pre-launch-global");
}

#ifdef Q_OS_UNIX
TEST(WineProfileIniSync, LegacyCaseAliasNeverCollapsesDistinctBackups) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString exact = temporary.filePath("prefix/drive_c/game/Game.ini");
  const QString lower = temporary.filePath("prefix/drive_c/game/game.ini");
  const QString exactBackup = exact + ".mo2linux_backup";
  const QString lowerBackup = lower + ".mo2linux_backup";
  ASSERT_TRUE(writeBytes(exact, "live-global"));
  ASSERT_EQ(::symlink("Game.ini", QFile::encodeName(lower).constData()), 0);
  ASSERT_TRUE(writeBytes(exactBackup, "exact-backup"));
  ASSERT_TRUE(writeBytes(lowerBackup, "lower-backup"));

  EXPECT_FALSE(WineProfileIniSync::restoreLegacyBackup(exact, exactBackup));
  EXPECT_FALSE(WineProfileIniSync::restoreLegacyBackup(lower, lowerBackup));
  EXPECT_EQ(readBytes(exact), "live-global");
  EXPECT_EQ(readBytes(exactBackup), "exact-backup");
  EXPECT_EQ(readBytes(lowerBackup), "lower-backup");
}
#endif

TEST(WineProfileIniSync, FailedStartupRecoveryPreservesLiveAndBackup) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString prefixRoot = temporary.filePath("prefix");
  const QString live = QDir(prefixRoot).filePath("drive_c/game/Game.ini");
  const QString backup = live + ".mo2linux_backup";
  ASSERT_TRUE(QDir().mkpath(live));
  ASSERT_TRUE(writeBytes(QDir(live).filePath("sentinel"), "old-live"));
  ASSERT_TRUE(writeBytes(backup, "recovery-generation"));

  const auto result = WineProfileIniSync::restoreLegacyBackup(live, backup);
  EXPECT_FALSE(result);
  EXPECT_EQ(readBytes(QDir(live).filePath("sentinel")), "old-live");
  EXPECT_EQ(readBytes(backup), "recovery-generation");
}

#ifdef Q_OS_UNIX
TEST(WineProfileIniSync, FailedLaterPublicationKeepsEveryLiveSourceForRetry) {
  QTemporaryDir temporary;
  QTemporaryDir external;
  ASSERT_TRUE(temporary.isValid());
  ASSERT_TRUE(external.isValid());
  const QString profileOne = temporary.filePath("profile/One.ini");
  const QString prefixOne = temporary.filePath("prefix/One.ini");
  const QString backupOne = prefixOne + ".mo2linux_backup";
  const QString profileTwo = temporary.filePath("profile/Two.ini");
  const QString prefixTwo = temporary.filePath("prefix/Two.ini");
  const QString backupTwo = prefixTwo + ".mo2linux_backup";
  const QString outside = external.filePath("outside.ini");
  ASSERT_TRUE(writeBytes(profileOne, "one-old"));
  ASSERT_TRUE(writeBytes(prefixOne, "one-edit"));
  ASSERT_TRUE(writeBytes(backupOne, "one-global"));
  ASSERT_TRUE(writeBytes(outside, "foreign"));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(profileTwo).absolutePath()));
  ASSERT_EQ(::symlink(QFile::encodeName(outside).constData(),
                      QFile::encodeName(profileTwo).constData()),
            0);
  ASSERT_TRUE(writeBytes(prefixTwo, "two-edit"));
  ASSERT_TRUE(writeBytes(backupTwo, "two-global"));

  QList<WineProfileIniSync::Deployment> deployments{
      {profileOne, prefixOne, {{prefixOne, backupOne}}, {prefixOne}, {}, true},
      {profileTwo, prefixTwo, {{prefixTwo, backupTwo}}, {prefixTwo}, {}, true}};
  auto phase = WineProfileIniSync::CleanupPhase::Prepared;
  auto result = WineProfileIniSync::finish(deployments, Owner,
                                           /*publishChanges=*/true, phase);
  EXPECT_FALSE(result);
  EXPECT_EQ(readBytes(prefixOne), "one-edit");
  EXPECT_EQ(readBytes(backupOne), "one-global");
  EXPECT_EQ(readBytes(prefixTwo), "two-edit");
  EXPECT_EQ(readBytes(backupTwo), "two-global");
  EXPECT_EQ(readBytes(outside), "foreign");

  ASSERT_TRUE(QFile::remove(profileTwo));
  ASSERT_TRUE(writeBytes(profileTwo, "two-old"));
  result = WineProfileIniSync::finish(deployments, Owner,
                                      /*publishChanges=*/true, phase);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_EQ(readBytes(profileOne), "one-edit");
  EXPECT_EQ(readBytes(profileTwo), "two-edit");
  EXPECT_EQ(readBytes(prefixOne), "one-global");
  EXPECT_EQ(readBytes(prefixTwo), "two-global");
}
#endif

TEST(WineSaveRouting, PreservesTrailingBackslashAndAdjacentKeys) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString ini = temporary.filePath("Game.ini");
  const QByteArray original =
      "[General]\r\nAdjacent=keep\r\nsLocalSavePath=Saves\\\r\n"
      "bUseMyGamesDirectory=0\r\n[Display]\r\nValue=1\r\n";
  ASSERT_TRUE(writeBytes(ini, original));

  WineSaveRouting::Value value;
  auto result =
      WineSaveRouting::readValue(ini, "general", "SLOCALSAVEPATH", value);
  ASSERT_TRUE(result) << qPrintable(result.error);
  ASSERT_TRUE(value.present);
  EXPECT_EQ(value.bytes, "Saves\\");

  result = WineSaveRouting::activate(ini, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  ASSERT_TRUE(result.recoveryRequired);
  result = WineSaveRouting::readValue(ini, "General", "sLocalSavePath", value);
  ASSERT_TRUE(result);
  EXPECT_EQ(value.bytes, "__MO_Saves\\");
  EXPECT_NE(readBytes(ini).indexOf("Adjacent=keep\r\n"), -1);

  QString pendingOwner;
  bool pending = false;
  result = WineSaveRouting::pendingOwner(ini, pendingOwner, pending);
  ASSERT_TRUE(result);
  EXPECT_TRUE(pending);
  EXPECT_EQ(pendingOwner, Owner);

  result = WineSaveRouting::restore(ini, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_EQ(readBytes(ini), original);
  EXPECT_FALSE(QFileInfo::exists(WineSaveRouting::receiptPathFor(ini)));
}

TEST(WineSaveRouting, DistinctCaseVariantsRestoreTheirOwnValues) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString exact = temporary.filePath("Game.ini");
  const QString lower = temporary.filePath("game.ini");
  const QByteArray exactOriginal =
      "[General]\nsLocalSavePath=Exact\\\nbUseMyGamesDirectory=0\nExact=1\n";
  const QByteArray lowerOriginal =
      "[General]\nsLocalSavePath=Lower\\\nbUseMyGamesDirectory=7\nLower=1\n";
  ASSERT_TRUE(writeBytes(exact, exactOriginal));
  ASSERT_TRUE(writeBytes(lower, lowerOriginal));
  const QDateTime exactTime =
      QDateTime::fromSecsSinceEpoch(1'700'000'000, QTimeZone::UTC);
  const QDateTime lowerTime = exactTime.addSecs(120);
  QFile exactFile(exact);
  ASSERT_TRUE(exactFile.open(QIODevice::ReadWrite));
  ASSERT_TRUE(exactFile.setFileTime(exactTime,
                                    QFileDevice::FileModificationTime));
  exactFile.close();
  QFile lowerFile(lower);
  ASSERT_TRUE(lowerFile.open(QIODevice::ReadWrite));
  ASSERT_TRUE(lowerFile.setFileTime(lowerTime,
                                    QFileDevice::FileModificationTime));
  lowerFile.close();

  auto result = WineSaveRouting::activate(exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_NE(readBytes(exact).indexOf("sLocalSavePath=__MO_Saves\\"), -1);
  EXPECT_NE(readBytes(lower).indexOf("sLocalSavePath=__MO_Saves\\"), -1);
  EXPECT_EQ(QFileInfo(exact).lastModified().toSecsSinceEpoch(),
            exactTime.toSecsSinceEpoch());
  EXPECT_EQ(QFileInfo(lower).lastModified().toSecsSinceEpoch(),
            lowerTime.toSecsSinceEpoch());

  result = WineSaveRouting::restore(exact, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_EQ(readBytes(exact), exactOriginal);
  EXPECT_EQ(readBytes(lower), lowerOriginal);
  EXPECT_EQ(QFileInfo(exact).lastModified().toSecsSinceEpoch(),
            exactTime.toSecsSinceEpoch());
  EXPECT_EQ(QFileInfo(lower).lastModified().toSecsSinceEpoch(),
            lowerTime.toSecsSinceEpoch());
}

TEST(WineSaveRouting, MissingKeysRemainMissingAfterRestore) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString ini = temporary.filePath("Game.ini");
  ASSERT_TRUE(writeBytes(ini, "[General]\nAdjacent=keep\n"));
  auto result = WineSaveRouting::activate(ini, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  result = WineSaveRouting::restore(ini, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);

  WineSaveRouting::Value value;
  ASSERT_TRUE(
      WineSaveRouting::readValue(ini, "General", "sLocalSavePath", value));
  EXPECT_FALSE(value.present);
  ASSERT_TRUE(WineSaveRouting::readValue(ini, "General", "bUseMyGamesDirectory",
                                         value));
  EXPECT_FALSE(value.present);
  EXPECT_NE(readBytes(ini).indexOf("Adjacent=keep\n"), -1);
}

TEST(WineSaveRouting, PreservesBomAndRecognizesFirstSection) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString ini = temporary.filePath("Game.ini");
  const QByteArray original =
      "\xEF\xBB\xBF[General]\r\nsLocalSavePath=Custom\\\r\n";
  ASSERT_TRUE(writeBytes(ini, original));

  auto result = WineSaveRouting::activate(ini, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_EQ(readBytes(ini).count("[General]"), 1);
  result = WineSaveRouting::restore(ini, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_EQ(readBytes(ini), original);
}

TEST(WineSaveRouting, TerminatesExistingSectionBeforeInsertingKeys) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString ini = temporary.filePath("Game.ini");
  const QByteArray original = "[General]";
  ASSERT_TRUE(writeBytes(ini, original));

  auto result = WineSaveRouting::activate(ini, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  const QByteArray active = readBytes(ini);
  EXPECT_TRUE(active.startsWith("[General]\n"));
  EXPECT_EQ(active.count("[General]"), 1);
  result = WineSaveRouting::restore(ini, Owner);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_EQ(readBytes(ini), original);
}

TEST(WineSaveRouting, WrongOwnerCannotRestoreReceipt) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString ini = temporary.filePath("Game.ini");
  ASSERT_TRUE(writeBytes(ini, "[General]\nsLocalSavePath=Saves\\\n"));
  ASSERT_TRUE(WineSaveRouting::activate(ini, Owner));
  const auto result =
      WineSaveRouting::restore(ini, QStringLiteral("another-owner"));
  EXPECT_FALSE(result);
  EXPECT_TRUE(QFileInfo::exists(WineSaveRouting::receiptPathFor(ini)));
}

#ifdef Q_OS_UNIX
TEST(WineSaveRouting, RejectsLinkedAndSpecialIniLeavesWithoutFollowing) {
  QTemporaryDir temporary;
  QTemporaryDir external;
  ASSERT_TRUE(temporary.isValid());
  ASSERT_TRUE(external.isValid());
  const QString outside = external.filePath("outside.ini");
  ASSERT_TRUE(writeBytes(outside, "[General]\nsLocalSavePath=outside\\\n"));
  const QString linked = temporary.filePath("linked.ini");
  ASSERT_EQ(::symlink(QFile::encodeName(outside).constData(),
                      QFile::encodeName(linked).constData()),
            0);
  auto result = WineSaveRouting::activate(linked, Owner);
  EXPECT_FALSE(result);
  EXPECT_EQ(readBytes(outside), "[General]\nsLocalSavePath=outside\\\n");

  const QString fifo = temporary.filePath("routing.ini");
  ASSERT_EQ(::mkfifo(QFile::encodeName(fifo).constData(), 0600), 0);
  result = WineSaveRouting::activate(fifo, Owner);
  EXPECT_FALSE(result);
  struct stat status;
  ASSERT_EQ(::lstat(QFile::encodeName(fifo).constData(), &status), 0);
  EXPECT_TRUE(S_ISFIFO(status.st_mode));
}
#endif
