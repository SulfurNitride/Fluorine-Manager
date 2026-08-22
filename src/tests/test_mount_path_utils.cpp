#include "mountpathutils.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

TEST(MountPathUtils, MatchesCanonicalMountAlias)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());

  const QString realRoot = QDir(temporary.path()).filePath(QStringLiteral("real-home"));
  const QString aliasRoot = QDir(temporary.path()).filePath(QStringLiteral("home"));
  ASSERT_TRUE(QDir().mkpath(QDir(realRoot).filePath(QStringLiteral("game/Data"))));
  ASSERT_TRUE(QFile::link(realRoot, aliasRoot));

  EXPECT_TRUE(mountPathsEquivalent(
      QDir(aliasRoot).filePath(QStringLiteral("game/Data")),
      QDir(realRoot).filePath(QStringLiteral("game/Data"))));
}

TEST(MountPathUtils, ResolvesParentWhenMountTargetIsUnavailable)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());

  const QString realRoot = QDir(temporary.path()).filePath(QStringLiteral("real-home"));
  const QString aliasRoot = QDir(temporary.path()).filePath(QStringLiteral("home"));
  ASSERT_TRUE(QDir().mkpath(QDir(realRoot).filePath(QStringLiteral("game"))));
  ASSERT_TRUE(QFile::link(realRoot, aliasRoot));

  // Data intentionally does not exist, matching canonicalFilePath() failure
  // on an inaccessible stale mount while its parent remains resolvable.
  EXPECT_TRUE(mountPathsEquivalent(
      QDir(aliasRoot).filePath(QStringLiteral("game/Data")),
      QDir(realRoot).filePath(QStringLiteral("game/Data"))));
}

TEST(MountPathUtils, KeepsDifferentMountTargetsDistinct)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());

  EXPECT_FALSE(mountPathsEquivalent(
      QDir(temporary.path()).filePath(QStringLiteral("first/Data")),
      QDir(temporary.path()).filePath(QStringLiteral("second/Data"))));
}

TEST(MountPathUtils, FindsAliasedPathInEncodedMountTable)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());

  const QString realRoot = QDir(temporary.path()).filePath(QStringLiteral("real home"));
  const QString aliasRoot = QDir(temporary.path()).filePath(QStringLiteral("home"));
  ASSERT_TRUE(QDir().mkpath(QDir(realRoot).filePath(QStringLiteral("game/Data"))));
  ASSERT_TRUE(QFile::link(realRoot, aliasRoot));

  QString encoded = QDir(realRoot).filePath(QStringLiteral("game/Data"));
  encoded.replace(QLatin1Char(' '), QStringLiteral("\\040"));
  const QString mounts = QStringLiteral("mo2linux %1 fuse rw 0 0\n").arg(encoded);

  EXPECT_TRUE(mountTableContainsPath(
      mounts, QDir(aliasRoot).filePath(QStringLiteral("game/Data"))));
}
