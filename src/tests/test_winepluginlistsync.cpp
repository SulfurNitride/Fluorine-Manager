#include "winepluginlistsync.h"
#include "winepluginprojectionsync.h"

#include <uibase/transactionalwritefile.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{

bool writeBytes(const QString& path, QByteArrayView bytes)
{
  QFile file(path);
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
         file.write(bytes.data(), bytes.size()) == bytes.size() && file.flush();
}

QByteArray readBytes(const QString& path)
{
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

QString projectionRetirementPath(const QString& path, const QString& ownerId)
{
  const QByteArray digest =
      QCryptographicHash::hash(ownerId.toUtf8(), QCryptographicHash::Sha256)
          .toHex()
          .left(24);
  return path + QStringLiteral(".fluorine-plugin-session-") +
         QString::fromLatin1(digest);
}

}  // namespace

TEST(WinePluginListSync, ReadsAndCountsARegularSnapshot)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("Plugins.txt");
  ASSERT_TRUE(writeBytes(path, "# header\r\n *One.esm\r\nTwo.esp\n\t*Three.esm\n"));

  const auto result = WinePluginListSync::read(path);
  ASSERT_TRUE(result.snapshot) << qPrintable(result.error);
  EXPECT_EQ(result.snapshot->contents,
            "# header\r\n *One.esm\r\nTwo.esp\n\t*Three.esm\n");
  EXPECT_EQ(WinePluginListSync::countStarred(result.snapshot->contents), 2);
  EXPECT_TRUE(result.snapshot->modificationTime.isValid());
}

TEST(WinePluginListSync, RejectsAnInPlaceGenerationChange)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("Plugins.txt");
  ASSERT_TRUE(writeBytes(path, "first-data\n"));
  const auto snapshot = WinePluginListSync::read(path);
  ASSERT_TRUE(snapshot.snapshot) << qPrintable(snapshot.error);

  ASSERT_TRUE(writeBytes(path, "other-data\n"));
  EXPECT_FALSE(WinePluginListSync::isSameFile(path, *snapshot.snapshot));
}

TEST(WinePluginListSync, SuspiciousActiveDropPolicyIsBounded)
{
  EXPECT_FALSE(WinePluginListSync::isSuspiciousActiveDrop(10, 0));
  EXPECT_FALSE(WinePluginListSync::isSuspiciousActiveDrop(100, -1));
  EXPECT_FALSE(WinePluginListSync::isSuspiciousActiveDrop(100, 90));
  EXPECT_FALSE(WinePluginListSync::isSuspiciousActiveDrop(100, 70));
  EXPECT_TRUE(WinePluginListSync::isSuspiciousActiveDrop(100, 69));
  EXPECT_TRUE(WinePluginListSync::isSuspiciousActiveDrop(40, 20));
}

#ifdef Q_OS_UNIX
TEST(WinePluginListSync, AcceptsStrictCaseAliasAndRecognizesSameLeaf)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString target  = temporary.filePath("Plugins.txt");
  const QString alias   = temporary.filePath("plugins.txt");
  const QString sibling = temporary.filePath("PLUGINS.TXT");
  ASSERT_TRUE(writeBytes(target, "*One.esm\n"));
  ASSERT_TRUE(writeBytes(sibling, "stale\n"));
  ASSERT_EQ(::symlink("Plugins.txt", QFile::encodeName(alias).constData()), 0);

  const auto result = WinePluginListSync::read(alias);
  ASSERT_TRUE(result.snapshot) << qPrintable(result.error);
  EXPECT_TRUE(WinePluginListSync::isSameFile(target, *result.snapshot));
  EXPECT_TRUE(WinePluginListSync::isSameFile(alias, *result.snapshot));
  EXPECT_FALSE(WinePluginListSync::isSameFile(sibling, *result.snapshot));

  MOBase::TransactionalWriteFile siblingTransaction(sibling);
  QString error;
  ASSERT_TRUE(WinePluginListSync::publish(siblingTransaction, *result.snapshot, error))
      << qPrintable(error);
  EXPECT_EQ(readBytes(target), "*One.esm\n");
  EXPECT_EQ(readBytes(alias), "*One.esm\n");
  EXPECT_EQ(readBytes(sibling), "*One.esm\n");
}

TEST(WinePluginListSync, RefusesExternalSymlinkAndFifoWithoutBlocking)
{
  QTemporaryDir temporary;
  QTemporaryDir external;
  ASSERT_TRUE(temporary.isValid());
  ASSERT_TRUE(external.isValid());
  const QString target = external.filePath("outside.txt");
  const QString link   = temporary.filePath("plugins.txt");
  ASSERT_TRUE(writeBytes(target, "secret"));
  ASSERT_EQ(::symlink(QFile::encodeName(target).constData(),
                      QFile::encodeName(link).constData()),
            0);
  EXPECT_FALSE(WinePluginListSync::read(link).snapshot);
  EXPECT_EQ(readBytes(target), "secret");

  ASSERT_TRUE(QFile::remove(link));
  ASSERT_EQ(::mkfifo(QFile::encodeName(link).constData(), 0600), 0);
  EXPECT_FALSE(WinePluginListSync::read(link).snapshot);
  struct stat status;
  ASSERT_EQ(::lstat(QFile::encodeName(link).constData(), &status), 0);
  EXPECT_TRUE(S_ISFIFO(status.st_mode));
}
#endif

TEST(WinePluginListSync, PublishesContentsAndTimestampBeforeCommit)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString source = temporary.filePath("source.txt");
  const QString target = temporary.filePath("target.txt");
  ASSERT_TRUE(writeBytes(source, "*New.esm\n"));
  ASSERT_TRUE(writeBytes(target, "old"));
  const QDateTime wanted = QDateTime::fromMSecsSinceEpoch(1'700'000'000'000LL);
  QFile sourceFile(source);
  ASSERT_TRUE(sourceFile.open(QIODevice::ReadWrite));
  ASSERT_TRUE(sourceFile.setFileTime(wanted, QFileDevice::FileModificationTime));
  sourceFile.close();

  const auto snapshot = WinePluginListSync::read(source);
  ASSERT_TRUE(snapshot.snapshot) << qPrintable(snapshot.error);
  MOBase::TransactionalWriteFile transaction(target);
  QString error;
  ASSERT_TRUE(WinePluginListSync::publish(transaction, *snapshot.snapshot, error))
      << qPrintable(error);

  EXPECT_EQ(readBytes(target), "*New.esm\n");
  EXPECT_EQ(QFileInfo(target).lastModified().toMSecsSinceEpoch(),
            snapshot.snapshot->modificationTime.toMSecsSinceEpoch());
}

TEST(WinePluginListSync, PublishesOneCaseFamilyWithoutDeleteFirst)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString existing = temporary.filePath("Plugins.txt");
  const QString requested = temporary.filePath("plugins.txt");
  ASSERT_TRUE(writeBytes(existing, "old-generation\n"));

  QString error;
  ASSERT_TRUE(WinePluginListSync::publishUniqueFamily(
      requested, "*New.esm\r\n", error)) << qPrintable(error);
  EXPECT_EQ(readBytes(existing), "*New.esm\r\n");
  EXPECT_FALSE(QFileInfo::exists(requested));
}

TEST(WinePluginListSync, RefusesIndependentCaseVariantsWithoutMutation)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString upper = temporary.filePath("Plugins.txt");
  const QString lower = temporary.filePath("plugins.txt");
  ASSERT_TRUE(writeBytes(upper, "upper-generation\n"));
  ASSERT_TRUE(writeBytes(lower, "lower-generation\n"));

  const auto family = WinePluginListSync::readUniqueFamily(lower);
  EXPECT_FALSE(family.snapshot);
  EXPECT_FALSE(family.error.isEmpty());
  QString error;
  EXPECT_FALSE(WinePluginListSync::publishUniqueFamily(
      lower, "replacement\n", error));
  EXPECT_EQ(readBytes(upper), "upper-generation\n");
  EXPECT_EQ(readBytes(lower), "lower-generation\n");
}

#ifdef Q_OS_UNIX
TEST(WinePluginListSync, PublishesThroughStrictCaseAliasOnly)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString target = temporary.filePath("Plugins.txt");
  const QString alias = temporary.filePath("plugins.txt");
  ASSERT_TRUE(writeBytes(target, "old\n"));
  ASSERT_EQ(::symlink("Plugins.txt", QFile::encodeName(alias).constData()), 0);

  QString error;
  ASSERT_TRUE(WinePluginListSync::publishUniqueFamily(
      alias, "new\n", error)) << qPrintable(error);
  EXPECT_EQ(readBytes(target), "new\n");
  EXPECT_TRUE(QFileInfo(alias).isSymLink());
}
#endif

TEST(WinePluginListSync, FailedDestinationPreservesExistingLeaf)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString source = temporary.filePath("source.txt");
  const QString target = temporary.filePath("target");
  ASSERT_TRUE(writeBytes(source, "*New.esm\n"));
  ASSERT_TRUE(QDir().mkdir(target));
  const auto snapshot = WinePluginListSync::read(source);
  ASSERT_TRUE(snapshot.snapshot);

  MOBase::TransactionalWriteFile transaction(target);
  QString error;
  EXPECT_FALSE(WinePluginListSync::publish(transaction, *snapshot.snapshot, error));
  EXPECT_TRUE(QFileInfo(target).isDir());
}

TEST(WinePluginProjectionSync, EmptyProfileIsAnAuthoritativeOverlay)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString profile = temporary.filePath("profile/plugins.txt");
  const QString target = temporary.filePath("prefix/Plugins.txt");
  const QString loadOrder = temporary.filePath("prefix/loadorder.txt");
  ASSERT_TRUE(QDir().mkpath(QFileInfo(profile).absolutePath()));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(target).absolutePath()));
  ASSERT_TRUE(writeBytes(profile, QByteArrayView{}));
  ASSERT_TRUE(writeBytes(target, "*OtherProfile.esp\n"));
  ASSERT_TRUE(writeBytes(loadOrder, "unowned-load-order\n"));

  WinePluginProjectionSync::Deployment deployment;
  const auto prepared = WinePluginProjectionSync::prepare(
      profile, {target}, QStringLiteral("launch-empty"), deployment);
  ASSERT_TRUE(prepared) << qPrintable(prepared.error);
  EXPECT_TRUE(QFileInfo::exists(target));
  EXPECT_TRUE(readBytes(target).isEmpty());

  const auto finished = WinePluginProjectionSync::finish(
      deployment, /*publishChanges=*/true);
  ASSERT_TRUE(finished) << qPrintable(finished.error);
  EXPECT_TRUE(readBytes(profile).isEmpty());
  EXPECT_EQ(readBytes(target), "*OtherProfile.esp\n");
  EXPECT_EQ(readBytes(loadOrder), "unowned-load-order\n");
  EXPECT_FALSE(deployment.needsCleanup());
}

TEST(WinePluginProjectionSync, PublishesOneEditAndRestoresEveryGlobalTarget)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString profile = temporary.filePath("profile/plugins.txt");
  const QString first = temporary.filePath("prefix/FalloutNV/plugins.txt");
  const QString second =
      temporary.filePath("prefix/FalloutNV_Epic/plugins.txt");
  ASSERT_TRUE(QDir().mkpath(QFileInfo(profile).absolutePath()));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(first).absolutePath()));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(second).absolutePath()));
  ASSERT_TRUE(writeBytes(profile, "baseline\r\n"));
  ASSERT_TRUE(writeBytes(first, "first-global\n"));
  ASSERT_TRUE(writeBytes(second, "second-global\n"));

  WinePluginProjectionSync::Deployment deployment;
  const auto prepared = WinePluginProjectionSync::prepare(
      profile, {first, second}, QStringLiteral("launch-two"), deployment);
  ASSERT_TRUE(prepared) << qPrintable(prepared.error);
  EXPECT_EQ(readBytes(first), "baseline\r\n");
  EXPECT_EQ(readBytes(second), "baseline\r\n");
  ASSERT_TRUE(writeBytes(second, "game-edit\r\n"));

  const auto finished = WinePluginProjectionSync::finish(
      deployment, /*publishChanges=*/true);
  ASSERT_TRUE(finished) << qPrintable(finished.error);
  EXPECT_EQ(readBytes(profile), "game-edit\r\n");
  EXPECT_EQ(readBytes(first), "first-global\n");
  EXPECT_EQ(readBytes(second), "second-global\n");
}

TEST(WinePluginProjectionSync, AbnormalEditGetsRecoveryCopyAndGlobalsReturn)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString profile = temporary.filePath("profile/plugins.txt");
  const QString target = temporary.filePath("prefix/Plugins.txt");
  ASSERT_TRUE(QDir().mkpath(QFileInfo(profile).absolutePath()));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(target).absolutePath()));
  ASSERT_TRUE(writeBytes(profile, "baseline\n"));
  ASSERT_TRUE(writeBytes(target, "global\n"));

  WinePluginProjectionSync::Deployment deployment;
  ASSERT_TRUE(WinePluginProjectionSync::prepare(
      profile, {target}, QStringLiteral("launch-nonzero"), deployment));
  ASSERT_TRUE(writeBytes(target, "tool-edit\n"));
  const auto finished = WinePluginProjectionSync::finish(
      deployment, /*publishChanges=*/false);
  ASSERT_TRUE(finished) << qPrintable(finished.error);
  ASSERT_EQ(finished.recoveryFiles.size(), 1);
  EXPECT_EQ(readBytes(finished.recoveryFiles.front()), "tool-edit\n");
  EXPECT_EQ(readBytes(profile), "baseline\n");
  EXPECT_EQ(readBytes(target), "global\n");
}

TEST(WinePluginProjectionSync, ConcurrentProfileEditIsNeverOverwritten)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString profile = temporary.filePath("profile/plugins.txt");
  const QString target = temporary.filePath("prefix/Plugins.txt");
  ASSERT_TRUE(QDir().mkpath(QFileInfo(profile).absolutePath()));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(target).absolutePath()));
  ASSERT_TRUE(writeBytes(profile, "baseline\n"));
  ASSERT_TRUE(writeBytes(target, "global\n"));

  WinePluginProjectionSync::Deployment deployment;
  ASSERT_TRUE(WinePluginProjectionSync::prepare(
      profile, {target}, QStringLiteral("launch-conflict"), deployment));
  ASSERT_TRUE(writeBytes(target, "game-edit\n"));
  ASSERT_TRUE(writeBytes(profile, "external-profile-edit\n"));
  const auto finished = WinePluginProjectionSync::finish(
      deployment, /*publishChanges=*/true);
  ASSERT_TRUE(finished) << qPrintable(finished.error);
  ASSERT_EQ(finished.recoveryFiles.size(), 1);
  EXPECT_EQ(readBytes(finished.recoveryFiles.front()), "game-edit\n");
  EXPECT_EQ(readBytes(profile), "external-profile-edit\n");
  EXPECT_EQ(readBytes(target), "global\n");
}

TEST(WinePluginProjectionSync, LaterPreflightFailureMutatesNoEarlierTarget)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString profile = temporary.filePath("profile/plugins.txt");
  const QString first = temporary.filePath("prefix/First/Plugins.txt");
  const QString unsafe = temporary.filePath("prefix/Second/Plugins.txt");
  ASSERT_TRUE(QDir().mkpath(QFileInfo(profile).absolutePath()));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(first).absolutePath()));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(unsafe).absolutePath()));
  ASSERT_TRUE(writeBytes(profile, "baseline\n"));
  ASSERT_TRUE(writeBytes(first, "first-global\n"));
  ASSERT_TRUE(QDir().mkdir(unsafe));

  WinePluginProjectionSync::Deployment deployment;
  const auto prepared = WinePluginProjectionSync::prepare(
      profile, {first, unsafe}, QStringLiteral("launch-preflight"), deployment);
  EXPECT_FALSE(prepared);
  EXPECT_EQ(readBytes(first), "first-global\n");
  EXPECT_TRUE(QFileInfo(unsafe).isDir());
  EXPECT_FALSE(deployment.needsCleanup());
}

TEST(WinePluginProjectionSync, AbortRestoresAbsence)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString profile = temporary.filePath("profile/plugins.txt");
  const QString target = temporary.filePath("prefix/Plugins.txt");
  ASSERT_TRUE(QDir().mkpath(QFileInfo(profile).absolutePath()));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(target).absolutePath()));
  ASSERT_TRUE(writeBytes(profile, "baseline\n"));

  WinePluginProjectionSync::Deployment deployment;
  ASSERT_TRUE(WinePluginProjectionSync::prepare(
      profile, {target}, QStringLiteral("launch-abort"), deployment));
  ASSERT_TRUE(QFileInfo::exists(target));
  const auto finished = WinePluginProjectionSync::finish(
      deployment, /*publishChanges=*/false);
  ASSERT_TRUE(finished) << qPrintable(finished.error);
  EXPECT_FALSE(QFileInfo::exists(target));
  EXPECT_EQ(readBytes(profile), "baseline\n");
}

TEST(WinePluginProjectionSync, MissingUnchangedProfileDoesNotBlockRestoration)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString profile = temporary.filePath("profile/plugins.txt");
  const QString target = temporary.filePath("prefix/Plugins.txt");
  ASSERT_TRUE(QDir().mkpath(QFileInfo(profile).absolutePath()));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(target).absolutePath()));
  ASSERT_TRUE(writeBytes(profile, "baseline\n"));
  ASSERT_TRUE(writeBytes(target, "global\n"));

  WinePluginProjectionSync::Deployment deployment;
  ASSERT_TRUE(WinePluginProjectionSync::prepare(
      profile, {target}, QStringLiteral("launch-missing-unchanged"), deployment));
  ASSERT_TRUE(QFile::remove(profile));

  const auto finished = WinePluginProjectionSync::finish(
      deployment, /*publishChanges=*/true);
  ASSERT_TRUE(finished) << qPrintable(finished.error);
  EXPECT_FALSE(QFileInfo::exists(profile));
  EXPECT_EQ(readBytes(target), "global\n");
  EXPECT_FALSE(deployment.needsCleanup());
}

TEST(WinePluginProjectionSync, MissingProfileAbnormalEditIsPreserved)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString profile = temporary.filePath("profile/plugins.txt");
  const QString target = temporary.filePath("prefix/Plugins.txt");
  ASSERT_TRUE(QDir().mkpath(QFileInfo(profile).absolutePath()));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(target).absolutePath()));
  ASSERT_TRUE(writeBytes(profile, "baseline\n"));
  ASSERT_TRUE(writeBytes(target, "global\n"));

  WinePluginProjectionSync::Deployment deployment;
  ASSERT_TRUE(WinePluginProjectionSync::prepare(
      profile, {target}, QStringLiteral("launch-missing-edited"), deployment));
  ASSERT_TRUE(writeBytes(target, "tool-edit\n"));
  ASSERT_TRUE(QFile::remove(profile));

  const auto finished = WinePluginProjectionSync::finish(
      deployment, /*publishChanges=*/false);
  ASSERT_TRUE(finished) << qPrintable(finished.error);
  ASSERT_EQ(finished.recoveryFiles.size(), 1);
  EXPECT_EQ(readBytes(finished.recoveryFiles.front()), "tool-edit\n");
  EXPECT_FALSE(QFileInfo::exists(profile));
  EXPECT_EQ(readBytes(target), "global\n");
}

TEST(WinePluginProjectionSync, RetirementCollisionIsPreservedAndRetryable)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString profile = temporary.filePath("profile/plugins.txt");
  const QString target = temporary.filePath("prefix/Plugins.txt");
  const QString owner = QStringLiteral("launch-retirement-collision");
  const QString retirement = projectionRetirementPath(target, owner);
  ASSERT_TRUE(QDir().mkpath(QFileInfo(profile).absolutePath()));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(target).absolutePath()));
  ASSERT_TRUE(writeBytes(profile, "baseline\n"));
  ASSERT_TRUE(writeBytes(target, "global\n"));

  WinePluginProjectionSync::Deployment deployment;
  ASSERT_TRUE(WinePluginProjectionSync::prepare(
      profile, {target}, owner, deployment));
  ASSERT_TRUE(writeBytes(retirement, "foreign\n"));

  const auto obstructed = WinePluginProjectionSync::finish(
      deployment, /*publishChanges=*/false);
  EXPECT_FALSE(obstructed);
  EXPECT_EQ(deployment.phase,
            WinePluginProjectionSync::CleanupPhase::Reconciled);
  EXPECT_EQ(readBytes(target), "baseline\n");
  EXPECT_EQ(readBytes(retirement), "foreign\n");

  ASSERT_TRUE(QFile::remove(retirement));
  const auto retried = WinePluginProjectionSync::finish(
      deployment, /*publishChanges=*/false);
  ASSERT_TRUE(retried) << qPrintable(retried.error);
  EXPECT_EQ(readBytes(target), "global\n");
  EXPECT_FALSE(QFileInfo::exists(retirement));
  EXPECT_FALSE(deployment.needsCleanup());
  EXPECT_TRUE(WinePluginProjectionSync::finish(
      deployment, /*publishChanges=*/false));
}

TEST(WinePluginProjectionSync, PartialRetirementJournalResumesExactly)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString profile = temporary.filePath("profile/plugins.txt");
  const QString first = temporary.filePath("prefix/First/Plugins.txt");
  const QString second = temporary.filePath("prefix/Second/Plugins.txt");
  const QString owner = QStringLiteral("launch-partial-retirement");
  const QString firstRetirement = projectionRetirementPath(first, owner);
  const QString secondRetirement = projectionRetirementPath(second, owner);
  ASSERT_TRUE(QDir().mkpath(QFileInfo(profile).absolutePath()));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(first).absolutePath()));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(second).absolutePath()));
  ASSERT_TRUE(writeBytes(profile, "baseline\n"));
  ASSERT_TRUE(writeBytes(first, "first-global\n"));
  ASSERT_TRUE(writeBytes(second, "second-global\n"));

  WinePluginProjectionSync::Deployment deployment;
  ASSERT_TRUE(WinePluginProjectionSync::prepare(
      profile, {first, second}, owner, deployment));
  ASSERT_TRUE(writeBytes(secondRetirement, "foreign\n"));

  const auto obstructed = WinePluginProjectionSync::finish(
      deployment, /*publishChanges=*/false);
  EXPECT_FALSE(obstructed);
  EXPECT_EQ(deployment.phase,
            WinePluginProjectionSync::CleanupPhase::Reconciled);
  ASSERT_EQ(deployment.targets.size(), 2);
  ASSERT_EQ(deployment.targets.at(0).retiredLeaves.size(), 1);
  EXPECT_EQ(deployment.targets.at(0).retiredLeaves.front().first, first);
  EXPECT_EQ(deployment.targets.at(0).retiredLeaves.front().second,
            firstRetirement);
  EXPECT_FALSE(QFileInfo::exists(first));
  EXPECT_EQ(readBytes(firstRetirement), "baseline\n");
  EXPECT_EQ(readBytes(second), "baseline\n");
  EXPECT_EQ(readBytes(secondRetirement), "foreign\n");

  ASSERT_TRUE(QFile::remove(secondRetirement));
  const auto retried = WinePluginProjectionSync::finish(
      deployment, /*publishChanges=*/false);
  ASSERT_TRUE(retried) << qPrintable(retried.error);
  EXPECT_EQ(readBytes(first), "first-global\n");
  EXPECT_EQ(readBytes(second), "second-global\n");
  EXPECT_FALSE(QFileInfo::exists(firstRetirement));
  EXPECT_FALSE(QFileInfo::exists(secondRetirement));
  EXPECT_FALSE(deployment.needsCleanup());
}

#ifdef Q_OS_UNIX
TEST(WinePluginProjectionSync, ResolvesThePhysicalOwnerOfAnAppDataBridge)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString selected = temporary.filePath("selected/pfx");
  const QString external = temporary.filePath("external/pfx");
  const QString selectedLocal =
      QDir(selected).filePath("drive_c/users/steamuser/AppData/Local");
  const QString externalGame =
      QDir(external).filePath("drive_c/users/alice/AppData/Local/Game");
  ASSERT_TRUE(QDir().mkpath(selectedLocal));
  ASSERT_TRUE(QDir().mkpath(externalGame));
  const QString bridge = QDir(selectedLocal).filePath("Game");
  ASSERT_EQ(::symlink(QFile::encodeName(externalGame).constData(),
                      QFile::encodeName(bridge).constData()),
            0);

  EXPECT_EQ(WinePluginProjectionSync::physicalPrefixRoot(
                QDir(bridge).filePath("Plugins.txt")),
            QFileInfo(external).canonicalFilePath());
  EXPECT_EQ(WinePluginProjectionSync::physicalPrefixRoot(
                QDir(selectedLocal).filePath("Missing/Plugins.txt")),
            QFileInfo(selected).canonicalFilePath());
  EXPECT_TRUE(WinePluginProjectionSync::physicalPrefixRoot(
                  temporary.filePath("outside/Plugins.txt"))
                  .isEmpty());
}

TEST(WinePluginProjectionSync, DeduplicatesAliasesToOnePhysicalTargetFamily)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString profile = temporary.filePath("profile/plugins.txt");
  const QString physical = temporary.filePath("physical/Game");
  const QString firstAlias = temporary.filePath("prefix/First");
  const QString secondAlias = temporary.filePath("prefix/Second");
  ASSERT_TRUE(QDir().mkpath(QFileInfo(profile).absolutePath()));
  ASSERT_TRUE(QDir().mkpath(physical));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(firstAlias).absolutePath()));
  ASSERT_EQ(::symlink(QFile::encodeName(physical).constData(),
                      QFile::encodeName(firstAlias).constData()),
            0);
  ASSERT_EQ(::symlink(QFile::encodeName(physical).constData(),
                      QFile::encodeName(secondAlias).constData()),
            0);
  ASSERT_TRUE(writeBytes(profile, "baseline\n"));
  ASSERT_TRUE(writeBytes(QDir(physical).filePath("Plugins.txt"), "global\n"));

  WinePluginProjectionSync::Deployment deployment;
  const auto prepared = WinePluginProjectionSync::prepare(
      profile,
      {QDir(firstAlias).filePath("Plugins.txt"),
       QDir(secondAlias).filePath("plugins.txt")},
      QStringLiteral("launch-aliases"), deployment);
  ASSERT_TRUE(prepared) << qPrintable(prepared.error);
  EXPECT_EQ(deployment.targets.size(), 1);
  ASSERT_TRUE(WinePluginProjectionSync::finish(
      deployment, /*publishChanges=*/false));
  EXPECT_EQ(readBytes(QDir(physical).filePath("Plugins.txt")), "global\n");
}
#endif
