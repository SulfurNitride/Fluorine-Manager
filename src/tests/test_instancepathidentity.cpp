#include "instancepathidentity.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace {

QString makeDirectory(const QTemporaryDir &temporary, const QString &name) {
  const QString path = temporary.filePath(name);
  EXPECT_TRUE(QDir().mkpath(path));
  return path;
}

bool makeDirectoryLink(const QString &target, const QString &link) {
  return QFile::link(target, link);
}

} // namespace

TEST(InstancePathIdentity, ResolvesDirectoryAliasesByPhysicalIdentity) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString real = makeDirectory(temporary, "real");
  const QString other = makeDirectory(temporary, "other");
  const QString alias = temporary.filePath("alias");
  const QString chain = temporary.filePath("chain");
  ASSERT_TRUE(makeDirectoryLink(real, alias));
  ASSERT_TRUE(makeDirectoryLink(alias, chain));

  EXPECT_EQ(instance_path::relation(real, alias),
            instance_path::DirectoryRelation::Same);
  EXPECT_EQ(instance_path::relation(real, chain),
            instance_path::DirectoryRelation::Same);
  EXPECT_EQ(instance_path::relation(real, other),
            instance_path::DirectoryRelation::Different);
  EXPECT_TRUE(instance_path::sameDirectoryOrPath(real, alias));
}

TEST(InstancePathIdentity, MissingPathsAreNeverPhysicallyEquivalent) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString missing = temporary.filePath("missing");
  const QString other = temporary.filePath("other-missing");

  EXPECT_EQ(instance_path::relation(missing, missing),
            instance_path::DirectoryRelation::Indeterminate);
  EXPECT_EQ(instance_path::relation(missing, other),
            instance_path::DirectoryRelation::Indeterminate);
  EXPECT_TRUE(instance_path::sameDirectoryOrPath(missing, missing));
  EXPECT_FALSE(instance_path::sameDirectoryOrPath(missing, other));
}

TEST(InstancePathIdentity, DanglingLinksRemainIndeterminate) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString missing = temporary.filePath("missing");
  const QString link = temporary.filePath("dangling");
  ASSERT_TRUE(makeDirectoryLink(missing, link));

  EXPECT_FALSE(instance_path::captureDirectory(link).valid);
  EXPECT_EQ(instance_path::relation(link, missing),
            instance_path::DirectoryRelation::Indeterminate);
}

TEST(InstancePathIdentity, SnapshotRejectsSymlinkRetargeting) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString first = makeDirectory(temporary, "first");
  const QString second = makeDirectory(temporary, "second");
  const QString alias = temporary.filePath("alias");
  ASSERT_TRUE(makeDirectoryLink(first, alias));

  const auto captured = instance_path::captureDirectory(alias);
  ASSERT_TRUE(captured.valid);
  ASSERT_TRUE(instance_path::stillNames(captured, alias));

  ASSERT_TRUE(QFile::remove(alias));
  ASSERT_TRUE(makeDirectoryLink(second, alias));
  EXPECT_FALSE(instance_path::stillNames(captured, alias));
  EXPECT_EQ(instance_path::relation(alias, second),
            instance_path::DirectoryRelation::Same);
}

TEST(InstancePathIdentity, MutationPolicyRejectsActiveAliasesAndRetargeting) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString active = makeDirectory(temporary, "active");
  const QString inactive = makeDirectory(temporary, "inactive");
  const QString selected = temporary.filePath("selected");
  ASSERT_TRUE(makeDirectoryLink(inactive, selected));
  const auto captured = instance_path::captureDirectory(selected);

  EXPECT_EQ(instance_path::mutationStatus(captured, selected, active),
            instance_path::MutationStatus::Safe);
  EXPECT_EQ(instance_path::mutationStatus(
                instance_path::captureDirectory(active), active, active),
            instance_path::MutationStatus::Active);
  const QString activeAlias = temporary.filePath("active-alias");
  ASSERT_TRUE(makeDirectoryLink(active, activeAlias));
  EXPECT_EQ(instance_path::mutationStatus(
                instance_path::captureDirectory(activeAlias), activeAlias, active),
            instance_path::MutationStatus::Active);

  ASSERT_TRUE(QFile::remove(selected));
  ASSERT_TRUE(makeDirectoryLink(active, selected));
  EXPECT_EQ(instance_path::mutationStatus(captured, selected, active),
            instance_path::MutationStatus::TargetChanged);
}

TEST(InstancePathIdentity, SnapshotRejectsSamePathDirectoryReplacement) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = makeDirectory(temporary, "instance");
  const auto captured = instance_path::captureDirectory(path);
  ASSERT_TRUE(captured.valid);

  ASSERT_TRUE(QDir().rename(path, temporary.filePath("old-instance")));
  ASSERT_TRUE(QDir().mkpath(path));
  EXPECT_FALSE(instance_path::stillNames(captured, path));
}

TEST(InstancePathIdentity, IdentityRequiresDirectories) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString file = temporary.filePath("ModOrganizer.ini");
  QFile output(file);
  ASSERT_TRUE(output.open(QIODevice::WriteOnly));
  output.close();

  EXPECT_FALSE(instance_path::captureDirectory(file).valid);
}

TEST(InstancePathIdentity, DistinctFilesDoNotOverlap) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString first = temporary.filePath("first.ini");
  const QString second = temporary.filePath("second.ini");
  for (const QString &path : {first, second}) {
    QFile output(path);
    ASSERT_TRUE(output.open(QIODevice::WriteOnly));
  }

  EXPECT_EQ(instance_path::overlap(first, second),
            instance_path::PathOverlap::Separate);
  EXPECT_EQ(instance_path::overlap(first, first),
            instance_path::PathOverlap::Overlaps);
}

TEST(InstancePathIdentity, DetectsDeletionTreeOverlap) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString active = makeDirectory(temporary, "active");
  const QString nested = makeDirectory(temporary, "active/mods");
  const QString other = makeDirectory(temporary, "other");
  const QString alias = temporary.filePath("active-alias");
  ASSERT_TRUE(makeDirectoryLink(active, alias));

  EXPECT_EQ(instance_path::overlap(active, nested),
            instance_path::PathOverlap::Overlaps);
  EXPECT_EQ(instance_path::overlap(nested, active),
            instance_path::PathOverlap::Overlaps);
  EXPECT_EQ(instance_path::overlap(alias, active),
            instance_path::PathOverlap::Overlaps);
  EXPECT_EQ(instance_path::overlap(active, other),
            instance_path::PathOverlap::Separate);
  EXPECT_EQ(instance_path::overlap(temporary.filePath("missing"), active),
            instance_path::PathOverlap::Indeterminate);

  EXPECT_EQ(instance_path::overlap(QDir::rootPath(), active),
            instance_path::PathOverlap::Overlaps);
  EXPECT_EQ(instance_path::overlap(active, QDir::rootPath()),
            instance_path::PathOverlap::Overlaps);

  const auto protectedResult = instance_path::firstOverlap({other, active}, {active});
  EXPECT_EQ(protectedResult.status, instance_path::PathOverlap::Overlaps);
  EXPECT_EQ(protectedResult.selectedPath, active);

  const auto safeResult = instance_path::firstOverlap({other}, {active});
  EXPECT_EQ(safeResult.status, instance_path::PathOverlap::Separate);

  const auto ancestorResult =
      instance_path::firstOverlap({temporary.path()}, {active});
  EXPECT_EQ(ancestorResult.status, instance_path::PathOverlap::Overlaps);
  const auto childResult = instance_path::firstOverlap({nested}, {active});
  EXPECT_EQ(childResult.status, instance_path::PathOverlap::Overlaps);
}

TEST(InstancePathIdentity, KeepsCaseDistinctDirectoriesSeparate) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString upper = makeDirectory(temporary, "Case");
  const QString lower = makeDirectory(temporary, "case");

  EXPECT_EQ(instance_path::relation(upper, lower),
            instance_path::DirectoryRelation::Different);
  EXPECT_EQ(instance_path::overlap(upper, lower),
            instance_path::PathOverlap::Separate);
}

TEST(InstancePathIdentity, ContainmentResolvesSymlinksAndComponents) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString root = makeDirectory(temporary, "root");
  const QString child = makeDirectory(temporary, "root/child");
  const QString outside = makeDirectory(temporary, "outside");
  const QString escape = temporary.filePath("root/escape");
  ASSERT_TRUE(makeDirectoryLink(outside, escape));

  EXPECT_EQ(instance_path::contains(root, child),
            instance_path::PathContainment::Contained);
  EXPECT_EQ(instance_path::contains(root, outside),
            instance_path::PathContainment::Separate);
  EXPECT_EQ(instance_path::contains(root, escape),
            instance_path::PathContainment::Separate);
  EXPECT_EQ(instance_path::contains(QDir::rootPath(), child),
            instance_path::PathContainment::Contained);
}
