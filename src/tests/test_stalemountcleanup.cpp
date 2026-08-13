#include "vfs/stalemountcleanup.h"

#include <gtest/gtest.h>

#include <cerrno>

using namespace stale_mount_cleanup;

namespace {

QByteArray mountLine(quint64 id, const QByteArray &target,
                     const QByteArray &type = "fuse.mo2linux",
                     const QByteArray &source = "mo2linux") {
  return QByteArray::number(id) + " 1 0:42 / " + target +
         " rw,nosuid,nodev shared:1 - " + type + " " + source + " rw\n";
}

} // namespace

TEST(StaleMountCleanup, ParsesMountInfoAndDecodesEscapedFields) {
  const auto entries = parseMountInfo(
      mountLine(42, "/games/Nerevar\\040Moon\\011Star\\134Data"));

  ASSERT_EQ(entries.size(), 1);
  EXPECT_EQ(entries[0].mountId, 42u);
  EXPECT_EQ(entries[0].mountPoint,
            QString::fromUtf8("/games/Nerevar Moon\tStar\\Data"));
  EXPECT_EQ(entries[0].fsType, QStringLiteral("fuse.mo2linux"));
  EXPECT_EQ(entries[0].source, QStringLiteral("mo2linux"));
}

TEST(StaleMountCleanup, IgnoresMalformedAndIncompleteRows) {
  const QByteArray input = "not mountinfo\n"
                           "0 1 0:42 / /zero rw - fuse.mo2linux mo2linux rw\n"
                           "7 1 0:42 / /missing-source rw - fuse.mo2linux\n"
                           "8 1 0:42 / /valid rw - fuse.mo2linux mo2linux rw\n";

  const auto entries = parseMountInfo(input);
  ASSERT_EQ(entries.size(), 1);
  EXPECT_EQ(entries[0].mountId, 8u);
}

TEST(StaleMountCleanup, RecognizesCurrentAndLegacyFluorineFuseIdentity) {
  EXPECT_TRUE(isFluorineFuseMount({1, QStringLiteral("/data"),
                                   QStringLiteral("fuse.mo2linux"),
                                   QStringLiteral("mo2linux")}));
  EXPECT_TRUE(
      isFluorineFuseMount({1, QStringLiteral("/data"), QStringLiteral("fuse"),
                           QStringLiteral("mo2linux")}));

  EXPECT_FALSE(isFluorineFuseMount({1, QStringLiteral("/data"),
                                    QStringLiteral("fuse.sshfs"),
                                    QStringLiteral("mo2linux")}));
  EXPECT_FALSE(isFluorineFuseMount({1, QStringLiteral("/data"),
                                    QStringLiteral("fuse.mo2linux"),
                                    QStringLiteral("other")}));
  EXPECT_FALSE(
      isFluorineFuseMount({1, QStringLiteral("/data"), QStringLiteral("ext4"),
                           QStringLiteral("mo2linux")}));
}

TEST(StaleMountCleanup, OnlyEnotconnProvesDisconnection) {
  EXPECT_TRUE(isDisconnectedProbeError(ENOTCONN));
  EXPECT_FALSE(isDisconnectedProbeError(0));
  EXPECT_FALSE(isDisconnectedProbeError(ENOENT));
  EXPECT_FALSE(isDisconnectedProbeError(EACCES));
  EXPECT_FALSE(isDisconnectedProbeError(EIO));
}

TEST(StaleMountCleanup, SelectsOwnedMountContainingRequestedPath) {
  const auto entries = parseMountInfo(
      mountLine(10, "/games/data") + mountLine(11, "/games/other") +
      mountLine(12, "/native", "ext4", "/dev/sda1"));

  const auto match =
      uniqueContainingFluorineMount(entries, QStringLiteral("/games/data"));
  ASSERT_TRUE(match.has_value());
  EXPECT_EQ(match->mountId, 10u);
  const auto descendant = uniqueContainingFluorineMount(
      entries, QStringLiteral("/games/data/subdir/file.txt"));
  ASSERT_TRUE(descendant.has_value());
  EXPECT_EQ(descendant->mountId, 10u);
  EXPECT_FALSE(uniqueContainingFluorineMount(entries, QStringLiteral("/native"))
                   .has_value());
  EXPECT_FALSE(
      uniqueContainingFluorineMount(entries, QStringLiteral("/missing"))
          .has_value());
  EXPECT_FALSE(uniqueContainingFluorineMount(entries, QString()).has_value());
  EXPECT_FALSE(
      uniqueContainingFluorineMount(entries, QStringLiteral("/")).has_value());
  EXPECT_FALSE(uniqueContainingFluorineMount(
                   parseMountInfo(mountLine(13, "/")),
                   QStringLiteral("/games/data"))
                   .has_value());
}

TEST(StaleMountCleanup, RejectsAnyStackedMountAtRequestedPath) {
  const auto ownedStack = parseMountInfo(mountLine(20, "/games/data") +
                                         mountLine(21, "/games/data"));
  EXPECT_FALSE(
      uniqueContainingFluorineMount(ownedStack, QStringLiteral("/games/data"))
          .has_value());

  const auto mixedStack =
      parseMountInfo(mountLine(22, "/games/data", "ext4", "/dev/sda1") +
                     mountLine(23, "/games/data"));
  EXPECT_FALSE(
      uniqueContainingFluorineMount(mixedStack, QStringLiteral("/games/data"))
          .has_value());
}

TEST(StaleMountCleanup, RejectsNestedUnrelatedTopMount) {
  const auto entries = parseMountInfo(
      mountLine(24, "/games/data") +
      mountLine(25, "/games/data/subdir", "fuse.sshfs", "sshfs"));
  EXPECT_FALSE(
      uniqueContainingFluorineMount(entries, QStringLiteral("/games/data"))
          .has_value());
  EXPECT_FALSE(uniqueContainingFluorineMount(
                   entries, QStringLiteral("/games/data/subdir/file.txt"))
                   .has_value());
}

TEST(StaleMountCleanup, RevalidationRequiresTheExactSameMount) {
  const MountEntry expected{30, QStringLiteral("/games/data"),
                            QStringLiteral("fuse.mo2linux"),
                            QStringLiteral("mo2linux")};
  EXPECT_TRUE(containsSameMount({expected}, expected));
  EXPECT_FALSE(containsSameMount(
      {{31, expected.mountPoint, expected.fsType, expected.source}}, expected));
  EXPECT_FALSE(
      containsSameMount({{expected.mountId, expected.mountPoint,
                          QStringLiteral("fuse.sshfs"), expected.source}},
                        expected));
}
