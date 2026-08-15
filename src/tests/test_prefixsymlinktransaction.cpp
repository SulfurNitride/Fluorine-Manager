#include "prefixsymlinktransaction.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

bool makeLink(const QString &target, const QString &path) {
  const QByteArray encodedTarget = QFile::encodeName(target);
  const QByteArray encodedPath = QFile::encodeName(path);
  return ::symlink(encodedTarget.constData(), encodedPath.constData()) == 0;
}

bool sameFile(const QString &left, const QString &right) {
  const QString leftCanonical = QFileInfo(left).canonicalFilePath();
  const QString rightCanonical = QFileInfo(right).canonicalFilePath();
  return !leftCanonical.isEmpty() && leftCanonical == rightCanonical;
}

class PrefixSymlinkTransactionTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(m_temp.isValid());
    m_prefix = QDir(m_temp.path()).filePath(QStringLiteral("managed"));
    m_gameA = QDir(m_temp.path()).filePath(QStringLiteral("game-a"));
    m_gameB = QDir(m_temp.path()).filePath(QStringLiteral("game-b"));
    ASSERT_TRUE(QDir().mkpath(m_prefix));
    ASSERT_TRUE(QDir().mkpath(m_gameA));
    ASSERT_TRUE(QDir().mkpath(m_gameB));
    ASSERT_TRUE(QDir().mkpath(destination(QString{})));
  }

  QString source(const QString &gamePrefix, const QString &relative) const {
    return QDir(gamePrefix)
        .filePath(QStringLiteral("drive_c/users/steamuser/%1").arg(relative));
  }

  QString destination(const QString &relative) const {
    return QDir(m_prefix).filePath(
        QStringLiteral("drive_c/users/steamuser/%1").arg(relative));
  }

  QTemporaryDir m_temp;
  QString m_prefix;
  QString m_gameA;
  QString m_gameB;
};

TEST_F(PrefixSymlinkTransactionTest, CreatesMissingLinksAndAdoptsThemOnRetry) {
  const QString myGame =
      source(m_gameA, QStringLiteral("Documents/My Games/Game"));
  const QString config = source(m_gameA, QStringLiteral("Documents/Config"));
  const QString cache = source(m_gameA, QStringLiteral("AppData/Local/Cache"));
  const QString state =
      source(m_gameA, QStringLiteral("AppData/Roaming/State"));
  ASSERT_TRUE(QDir().mkpath(myGame));
  ASSERT_TRUE(QDir().mkpath(config));
  ASSERT_TRUE(QDir().mkpath(cache));
  ASSERT_TRUE(QDir().mkpath(state));

  const auto first = PrefixSymlinkTransaction::apply(
      m_prefix, {{m_gameA, QStringLiteral("Game A")}});
  ASSERT_TRUE(first.success) << first.error.toStdString();
  EXPECT_EQ(first.created, 5);
  EXPECT_TRUE(
      sameFile(destination(QStringLiteral("Documents/My Games/Game")), myGame));
  EXPECT_TRUE(
      sameFile(destination(QStringLiteral("Documents/Config")), config));
  EXPECT_TRUE(
      sameFile(destination(QStringLiteral("AppData/Local/Cache")), cache));
  EXPECT_TRUE(
      sameFile(destination(QStringLiteral("AppData/Roaming/State")), state));
  EXPECT_TRUE(sameFile(destination(QStringLiteral("My Documents")),
                       destination(QStringLiteral("Documents"))));

  const auto retry = PrefixSymlinkTransaction::apply(
      m_prefix, {{m_gameA, QStringLiteral("Game A")}});
  ASSERT_TRUE(retry.success) << retry.error.toStdString();
  EXPECT_EQ(retry.created, 0);
  EXPECT_EQ(retry.adopted, 4);
  EXPECT_GE(retry.preserved, 1);
}

TEST_F(PrefixSymlinkTransactionTest, PreservesRealDestinationLeaves) {
  const QString sourceGame =
      source(m_gameA, QStringLiteral("Documents/My Games/Game"));
  const QString destinationGame =
      destination(QStringLiteral("Documents/My Games/Game"));
  ASSERT_TRUE(QDir().mkpath(sourceGame));
  ASSERT_TRUE(QDir().mkpath(destinationGame));
  QFile sentinel(QDir(destinationGame).filePath(QStringLiteral("sentinel")));
  ASSERT_TRUE(sentinel.open(QIODevice::WriteOnly));
  ASSERT_EQ(sentinel.write("global"), 6);
  sentinel.close();

  const auto result = PrefixSymlinkTransaction::apply(
      m_prefix, {{m_gameA, QStringLiteral("Game A")}});
  ASSERT_TRUE(result.success) << result.error.toStdString();
  EXPECT_GE(result.preserved, 1);
  EXPECT_FALSE(QFileInfo(destinationGame).isSymLink());
  EXPECT_TRUE(QFileInfo::exists(sentinel.fileName()));
}

TEST_F(PrefixSymlinkTransactionTest, PreservesCaseVariantRealLeaf) {
  const QString sourceGame =
      source(m_gameA, QStringLiteral("Documents/My Games/Game"));
  const QString lowerDestination =
      destination(QStringLiteral("Documents/My Games/game"));
  ASSERT_TRUE(QDir().mkpath(sourceGame));
  ASSERT_TRUE(QDir().mkpath(lowerDestination));

  const auto result = PrefixSymlinkTransaction::apply(
      m_prefix, {{m_gameA, QStringLiteral("Game A")}});
  ASSERT_TRUE(result.success) << result.error.toStdString();
  EXPECT_FALSE(QFileInfo(lowerDestination).isSymLink());
  EXPECT_FALSE(QFileInfo(destination(QStringLiteral("Documents/My Games/Game")))
                   .exists());
}

TEST_F(PrefixSymlinkTransactionTest, RejectsCaseVariantForeignLink) {
  const QString sourceGame =
      source(m_gameA, QStringLiteral("Documents/My Games/Game"));
  const QString lowerDestination =
      destination(QStringLiteral("Documents/My Games/game"));
  const QString foreign =
      QDir(m_temp.path()).filePath(QStringLiteral("foreign-case"));
  ASSERT_TRUE(QDir().mkpath(sourceGame));
  ASSERT_TRUE(QDir().mkpath(foreign));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(lowerDestination).absolutePath()));
  ASSERT_TRUE(makeLink(foreign, lowerDestination));

  const auto result = PrefixSymlinkTransaction::apply(
      m_prefix, {{m_gameA, QStringLiteral("Game A")}});
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(sameFile(lowerDestination, foreign));
  EXPECT_FALSE(QFileInfo(destination(QStringLiteral("Documents/My Games/Game")))
                   .exists());
}

TEST_F(PrefixSymlinkTransactionTest, AdoptsEquivalentCaseVariantLinks) {
  const QString sourceGame =
      source(m_gameA, QStringLiteral("Documents/My Games/Game"));
  const QString exact = destination(QStringLiteral("Documents/My Games/Game"));
  const QString lower = destination(QStringLiteral("Documents/My Games/game"));
  ASSERT_TRUE(QDir().mkpath(sourceGame));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(exact).absolutePath()));
  ASSERT_TRUE(makeLink(sourceGame, exact));
  ASSERT_TRUE(makeLink(sourceGame, lower));

  const auto result = PrefixSymlinkTransaction::apply(
      m_prefix, {{m_gameA, QStringLiteral("Game A")}});
  ASSERT_TRUE(result.success) << result.error.toStdString();
  EXPECT_GE(result.adopted, 2);
  EXPECT_TRUE(sameFile(exact, sourceGame));
  EXPECT_TRUE(sameFile(lower, sourceGame));
}

TEST_F(PrefixSymlinkTransactionTest, AdoptsCaseVariantDestinationStructure) {
  const QString sourceGame =
      source(m_gameA, QStringLiteral("Documents/My Games/Game"));
  const QString lowerTree = destination(QStringLiteral("documents/my games"));
  ASSERT_TRUE(QDir().mkpath(sourceGame));
  ASSERT_TRUE(QDir().mkpath(lowerTree));

  const auto result = PrefixSymlinkTransaction::apply(
      m_prefix, {{m_gameA, QStringLiteral("Game A")}});
  ASSERT_TRUE(result.success) << result.error.toStdString();
  EXPECT_TRUE(
      sameFile(QDir(lowerTree).filePath(QStringLiteral("Game")), sourceGame));
  EXPECT_FALSE(QFileInfo::exists(destination(QStringLiteral("Documents"))));
  EXPECT_TRUE(sameFile(destination(QStringLiteral("My Documents")),
                       destination(QStringLiteral("documents"))));
}

TEST_F(PrefixSymlinkTransactionTest, RejectsAmbiguousDestinationStructure) {
  const QString user = destination(QString{});
  ASSERT_TRUE(QDir().mkpath(QDir(user).filePath(QStringLiteral("Documents"))));
  ASSERT_TRUE(QDir().mkpath(QDir(user).filePath(QStringLiteral("documents"))));
  const auto result = PrefixSymlinkTransaction::apply(m_prefix, {});
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.error.contains(QStringLiteral("ambiguous")));
  EXPECT_FALSE(
      QFileInfo::exists(QDir(user).filePath(QStringLiteral("My Documents"))));
}

TEST_F(PrefixSymlinkTransactionTest,
       ConflictingLinkFailsBeforeCreatingAnyPlannedLink) {
  const QString sourceA =
      source(m_gameA, QStringLiteral("Documents/My Games/A"));
  const QString sourceB =
      source(m_gameA, QStringLiteral("Documents/My Games/B"));
  ASSERT_TRUE(QDir().mkpath(sourceA));
  ASSERT_TRUE(QDir().mkpath(sourceB));

  const QString destinationA =
      destination(QStringLiteral("Documents/My Games/A"));
  const QString destinationB =
      destination(QStringLiteral("Documents/My Games/B"));
  const QString foreign =
      QDir(m_temp.path()).filePath(QStringLiteral("foreign"));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(destinationA).absolutePath()));
  ASSERT_TRUE(QDir().mkpath(foreign));
  ASSERT_TRUE(makeLink(foreign, destinationA));

  const auto result = PrefixSymlinkTransaction::apply(
      m_prefix, {{m_gameA, QStringLiteral("Game A")}});
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.error.contains(destinationA));
  EXPECT_TRUE(result.error.contains(foreign));
  EXPECT_TRUE(sameFile(destinationA, foreign));
  EXPECT_FALSE(QFileInfo(destinationB).exists());
  EXPECT_FALSE(
      QFileInfo(destination(QStringLiteral("My Documents"))).isSymLink());
}

TEST_F(PrefixSymlinkTransactionTest, PreservesDanglingConflictingLink) {
  const QString sourceGame =
      source(m_gameA, QStringLiteral("Documents/My Games/Game"));
  ASSERT_TRUE(QDir().mkpath(sourceGame));
  const QString destinationGame =
      destination(QStringLiteral("Documents/My Games/Game"));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(destinationGame).absolutePath()));
  const QString missing =
      QDir(m_temp.path()).filePath(QStringLiteral("missing"));
  ASSERT_TRUE(makeLink(missing, destinationGame));

  const auto result = PrefixSymlinkTransaction::apply(
      m_prefix, {{m_gameA, QStringLiteral("Game A")}});
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(QFileInfo(destinationGame).isSymLink());
  EXPECT_FALSE(QFileInfo(missing).exists());
}

TEST_F(PrefixSymlinkTransactionTest,
       PreservesExistingMyDocumentsCompatibilityLink) {
  const QString sourceGame =
      source(m_gameA, QStringLiteral("Documents/My Games/Game"));
  const QString outside =
      QDir(m_temp.path()).filePath(QStringLiteral("compat-outside"));
  const QString myDocuments = destination(QStringLiteral("My Documents"));
  ASSERT_TRUE(QDir().mkpath(sourceGame));
  ASSERT_TRUE(QDir().mkpath(outside));
  ASSERT_TRUE(makeLink(outside, myDocuments));

  const auto result = PrefixSymlinkTransaction::apply(
      m_prefix, {{m_gameA, QStringLiteral("Game A")}});
  ASSERT_TRUE(result.success) << result.error.toStdString();
  EXPECT_TRUE(sameFile(myDocuments, outside));
  EXPECT_TRUE(sameFile(destination(QStringLiteral("Documents/My Games/Game")),
                       sourceGame));
}

TEST_F(PrefixSymlinkTransactionTest,
       RejectsAmbiguousMyDocumentsCompatibilityFamily) {
  const QString first = destination(QStringLiteral("My Documents"));
  const QString second = destination(QStringLiteral("MY DOCUMENTS"));
  const QString targetA =
      QDir(m_temp.path()).filePath(QStringLiteral("compat-a"));
  const QString targetB =
      QDir(m_temp.path()).filePath(QStringLiteral("compat-b"));
  ASSERT_TRUE(QDir().mkpath(targetA));
  ASSERT_TRUE(QDir().mkpath(targetB));
  ASSERT_TRUE(makeLink(targetA, first));
  ASSERT_TRUE(makeLink(targetB, second));

  const auto result = PrefixSymlinkTransaction::apply(m_prefix, {});
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.error.contains(QStringLiteral("ambiguous")));
  EXPECT_TRUE(sameFile(first, targetA));
  EXPECT_TRUE(sameFile(second, targetB));
}

TEST_F(PrefixSymlinkTransactionTest, RejectsSymlinkedDestinationParent) {
  const QString sourceGame =
      source(m_gameA, QStringLiteral("Documents/My Games/Game"));
  ASSERT_TRUE(QDir().mkpath(sourceGame));

  const QString user = destination(QString{});
  const QString outside =
      QDir(m_temp.path()).filePath(QStringLiteral("outside"));
  ASSERT_TRUE(QDir().mkpath(user));
  ASSERT_TRUE(QDir().mkpath(outside));
  ASSERT_TRUE(
      makeLink(outside, QDir(user).filePath(QStringLiteral("Documents"))));

  const auto result = PrefixSymlinkTransaction::apply(
      m_prefix, {{m_gameA, QStringLiteral("Game A")}});
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.error.contains(QStringLiteral("without following links")))
      << result.error.toStdString();
  EXPECT_FALSE(QFileInfo::exists(
      QDir(outside).filePath(QStringLiteral("My Games/Game"))));
}

TEST_F(PrefixSymlinkTransactionTest, FirstRankedCandidateWinsCollisions) {
  const QString first =
      source(m_gameA, QStringLiteral("Documents/My Games/Shared"));
  const QString second =
      source(m_gameB, QStringLiteral("Documents/My Games/Shared"));
  ASSERT_TRUE(QDir().mkpath(first));
  ASSERT_TRUE(QDir().mkpath(second));

  const auto result = PrefixSymlinkTransaction::apply(
      m_prefix, {{m_gameA, QStringLiteral("First")},
                 {m_gameB, QStringLiteral("Second")}});
  ASSERT_TRUE(result.success) << result.error.toStdString();
  EXPECT_TRUE(sameFile(destination(QStringLiteral("Documents/My Games/Shared")),
                       first));
}

TEST_F(PrefixSymlinkTransactionTest, SourceUserFallbackStillTargetsSteamUser) {
  const QString sourceGame = QDir(m_gameA).filePath(
      QStringLiteral("drive_c/users/alice/Documents/My Games/Game"));
  ASSERT_TRUE(QDir().mkpath(sourceGame));
  ASSERT_TRUE(QDir().mkpath(
      QDir(m_gameA).filePath(QStringLiteral("drive_c/users/Default User"))));

  const auto result = PrefixSymlinkTransaction::apply(
      m_prefix, {{m_gameA, QStringLiteral("Game A")}});
  ASSERT_TRUE(result.success) << result.error.toStdString();
  EXPECT_TRUE(sameFile(destination(QStringLiteral("Documents/My Games/Game")),
                       sourceGame));
  EXPECT_FALSE(QFileInfo::exists(QDir(m_prefix).filePath(
      QStringLiteral("drive_c/users/alice/Documents"))));
}

TEST_F(PrefixSymlinkTransactionTest, ExistingManagedUserIsRetained) {
  const QString alternatePrefix =
      QDir(m_temp.path()).filePath(QStringLiteral("alternate-managed"));
  ASSERT_TRUE(QDir().mkpath(
      QDir(alternatePrefix).filePath(QStringLiteral("drive_c/users/alice"))));
  const QString defaultTarget =
      QDir(m_temp.path()).filePath(QStringLiteral("default-profile"));
  ASSERT_TRUE(QDir().mkpath(defaultTarget));
  ASSERT_TRUE(makeLink(
      defaultTarget,
      QDir(alternatePrefix).filePath(QStringLiteral("drive_c/users/Default"))));
  const QString sourceGame =
      source(m_gameA, QStringLiteral("Documents/My Games/Game"));
  ASSERT_TRUE(QDir().mkpath(sourceGame));

  const auto result = PrefixSymlinkTransaction::apply(
      alternatePrefix, {{m_gameA, QStringLiteral("Game A")}});
  ASSERT_TRUE(result.success) << result.error.toStdString();
  const QString target =
      QDir(alternatePrefix)
          .filePath(
              QStringLiteral("drive_c/users/alice/Documents/My Games/Game"));
  EXPECT_TRUE(sameFile(target, sourceGame));
  EXPECT_FALSE(QFileInfo::exists(
      QDir(alternatePrefix)
          .filePath(QStringLiteral("drive_c/users/steamuser"))));
}

TEST_F(PrefixSymlinkTransactionTest, AmbiguousManagedUsersFailWithoutMutation) {
  const QString ambiguous =
      QDir(m_temp.path()).filePath(QStringLiteral("ambiguous"));
  ASSERT_TRUE(QDir().mkpath(
      QDir(ambiguous).filePath(QStringLiteral("drive_c/users/alice"))));
  ASSERT_TRUE(QDir().mkpath(
      QDir(ambiguous).filePath(QStringLiteral("drive_c/users/bob"))));

  const auto result = PrefixSymlinkTransaction::apply(ambiguous, {});
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.error.contains(QStringLiteral("ambiguous")));
  EXPECT_FALSE(QFileInfo::exists(QDir(ambiguous).filePath(
      QStringLiteral("drive_c/users/alice/Documents"))));
}

TEST_F(PrefixSymlinkTransactionTest, NonPrefixRootFailsWithoutMutation) {
  const QString ordinary =
      QDir(m_temp.path()).filePath(QStringLiteral("ordinary-directory"));
  ASSERT_TRUE(QDir().mkpath(ordinary));
  const auto result = PrefixSymlinkTransaction::apply(ordinary, {});
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(
      QFileInfo::exists(QDir(ordinary).filePath(QStringLiteral("drive_c"))));
}

TEST_F(PrefixSymlinkTransactionTest, IgnoresTheManagedPrefixAsASource) {
  const QString existing =
      destination(QStringLiteral("Documents/My Games/Managed"));
  ASSERT_TRUE(QDir().mkpath(existing));

  const auto result = PrefixSymlinkTransaction::apply(
      m_prefix, {{m_prefix, QStringLiteral("Managed")}});
  ASSERT_TRUE(result.success) << result.error.toStdString();
  EXPECT_FALSE(QFileInfo(existing).isSymLink());
}

TEST_F(PrefixSymlinkTransactionTest, TempCreationRejectsSymlinkedParent) {
  const QString user = destination(QString{});
  const QString outside =
      QDir(m_temp.path()).filePath(QStringLiteral("outside"));
  ASSERT_TRUE(QDir().mkpath(user));
  ASSERT_TRUE(QDir().mkpath(outside));
  ASSERT_TRUE(
      makeLink(outside, QDir(user).filePath(QStringLiteral("AppData"))));

  QString error;
  EXPECT_FALSE(PrefixSymlinkTransaction::ensureTempDirectory(m_prefix, error));
  EXPECT_TRUE(error.contains(QStringLiteral("without following links")));
  EXPECT_FALSE(
      QFileInfo::exists(QDir(outside).filePath(QStringLiteral("Local/Temp"))));
}

TEST_F(PrefixSymlinkTransactionTest, TempCreationAdoptsCaseVariantParents) {
  const QString lowerLocal = destination(QStringLiteral("appdata/local"));
  ASSERT_TRUE(QDir().mkpath(lowerLocal));
  QString error;
  ASSERT_TRUE(PrefixSymlinkTransaction::ensureTempDirectory(m_prefix, error))
      << error.toStdString();
  EXPECT_TRUE(
      QFileInfo::exists(QDir(lowerLocal).filePath(QStringLiteral("Temp"))));
  EXPECT_FALSE(QFileInfo::exists(destination(QStringLiteral("AppData"))));
}

TEST_F(PrefixSymlinkTransactionTest, RejectsSymlinkedSourceParent) {
  const QString candidateUser = source(m_gameA, QString{});
  const QString outside =
      QDir(m_temp.path()).filePath(QStringLiteral("source-outside"));
  ASSERT_TRUE(QDir().mkpath(candidateUser));
  ASSERT_TRUE(
      QDir().mkpath(QDir(outside).filePath(QStringLiteral("My Games/Game"))));
  ASSERT_TRUE(makeLink(
      outside, QDir(candidateUser).filePath(QStringLiteral("Documents"))));

  const auto result = PrefixSymlinkTransaction::apply(
      m_prefix, {{m_gameA, QStringLiteral("Game A")}});
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.error.contains(QStringLiteral("without following links")))
      << result.error.toStdString();
  EXPECT_FALSE(QFileInfo::exists(destination(QStringLiteral("Documents"))));
}

TEST_F(PrefixSymlinkTransactionTest, IgnoresExcludedSourceCompatibilityLink) {
  const QString sourceDocuments = source(m_gameA, QStringLiteral("Documents"));
  const QString sourceGame =
      QDir(sourceDocuments).filePath(QStringLiteral("My Games/Game"));
  ASSERT_TRUE(QDir().mkpath(sourceGame));
  ASSERT_TRUE(
      makeLink(sourceDocuments,
               QDir(sourceDocuments).filePath(QStringLiteral("My Documents"))));

  const auto result = PrefixSymlinkTransaction::apply(
      m_prefix, {{m_gameA, QStringLiteral("Game A")}});
  ASSERT_TRUE(result.success) << result.error.toStdString();
  EXPECT_TRUE(sameFile(destination(QStringLiteral("Documents/My Games/Game")),
                       sourceGame));
  EXPECT_FALSE(
      QFileInfo::exists(destination(QStringLiteral("Documents/My Documents"))));
}

TEST_F(PrefixSymlinkTransactionTest, ResolvesCaseVariantSourceStructure) {
  const QString sourceGame =
      source(m_gameA, QStringLiteral("documents/my games/Game"));
  ASSERT_TRUE(QDir().mkpath(sourceGame));

  const auto result = PrefixSymlinkTransaction::apply(
      m_prefix, {{m_gameA, QStringLiteral("Game A")}});
  ASSERT_TRUE(result.success) << result.error.toStdString();
  EXPECT_TRUE(sameFile(destination(QStringLiteral("Documents/My Games/Game")),
                       sourceGame));
}

TEST_F(PrefixSymlinkTransactionTest, RejectsSymlinkedSourceLeaf) {
  const QString sourceBase =
      source(m_gameA, QStringLiteral("Documents/My Games"));
  const QString outside =
      QDir(m_temp.path()).filePath(QStringLiteral("leaf-outside"));
  ASSERT_TRUE(QDir().mkpath(sourceBase));
  ASSERT_TRUE(QDir().mkpath(outside));
  ASSERT_TRUE(
      makeLink(outside, QDir(sourceBase).filePath(QStringLiteral("Game"))));

  const auto result = PrefixSymlinkTransaction::apply(
      m_prefix, {{m_gameA, QStringLiteral("Game A")}});
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.error.contains(QStringLiteral("symlinked source leaf")));
}

TEST_F(PrefixSymlinkTransactionTest, RejectsAmbiguousSourceCaseFamily) {
  ASSERT_TRUE(QDir().mkpath(
      source(m_gameA, QStringLiteral("Documents/My Games/Game"))));
  ASSERT_TRUE(QDir().mkpath(
      source(m_gameA, QStringLiteral("Documents/My Games/game"))));

  const auto result = PrefixSymlinkTransaction::apply(
      m_prefix, {{m_gameA, QStringLiteral("Game A")}});
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.error.contains(QStringLiteral("ambiguous")));
}

TEST_F(PrefixSymlinkTransactionTest, CommitFailureRollsBackCreatedGeneration) {
  ASSERT_TRUE(
      QDir().mkpath(source(m_gameA, QStringLiteral("Documents/My Games/A"))));
  ASSERT_TRUE(
      QDir().mkpath(source(m_gameA, QStringLiteral("Documents/My Games/B"))));

  PrefixSymlinkTransaction::Options options;
  options.failAfterCreations = 1;
  const auto result = PrefixSymlinkTransaction::apply(
      m_prefix, {{m_gameA, QStringLiteral("Game A")}}, options);
  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.created, 0);
  EXPECT_FALSE(QFileInfo::exists(destination(QStringLiteral("Documents"))));

  const auto retry = PrefixSymlinkTransaction::apply(
      m_prefix, {{m_gameA, QStringLiteral("Game A")}});
  ASSERT_TRUE(retry.success) << retry.error.toStdString();
}

TEST_F(PrefixSymlinkTransactionTest, RollbackPreservesReplacedCreatedLink) {
  ASSERT_TRUE(
      QDir().mkpath(source(m_gameA, QStringLiteral("Documents/My Games/A"))));
  ASSERT_TRUE(
      QDir().mkpath(source(m_gameA, QStringLiteral("Documents/My Games/B"))));

  QString replacement;
  PrefixSymlinkTransaction::Options options;
  options.failAfterCreations = 1;
  options.afterCreationForTesting = [&](const QString &path) {
    replacement = path;
    const QString target = QFileInfo(path).symLinkTarget();
    const QByteArray encoded = QFile::encodeName(path);
    ASSERT_EQ(::unlink(encoded.constData()), 0);
    ASSERT_TRUE(makeLink(target, path));
  };
  const auto result = PrefixSymlinkTransaction::apply(
      m_prefix, {{m_gameA, QStringLiteral("Game A")}}, options);
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.error.contains(QStringLiteral("replaced")));
  EXPECT_TRUE(QFileInfo(replacement).isSymLink());
}

TEST_F(PrefixSymlinkTransactionTest,
       RollbackPreservesReplacedCreatedDirectory) {
  ASSERT_TRUE(
      QDir().mkpath(source(m_gameA, QStringLiteral("Documents/My Games/A"))));
  ASSERT_TRUE(
      QDir().mkpath(source(m_gameA, QStringLiteral("Documents/My Games/B"))));

  const QString local = destination(QStringLiteral("AppData/Local"));
  const QString sentinel = QDir(local).filePath(QStringLiteral("sentinel"));
  PrefixSymlinkTransaction::Options options;
  options.failAfterCreations = 1;
  options.afterCreationForTesting = [&](const QString &) {
    ASSERT_TRUE(QDir().rmdir(local));
    ASSERT_TRUE(QDir().mkpath(local));
    QFile file(sentinel);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write("replacement"), 11);
  };
  const auto result = PrefixSymlinkTransaction::apply(
      m_prefix, {{m_gameA, QStringLiteral("Game A")}}, options);
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.error.contains(QStringLiteral("replaced")));
  EXPECT_TRUE(QFileInfo::exists(sentinel));
}

TEST_F(PrefixSymlinkTransactionTest, SourceSwapBeforePublicationFailsClosed) {
  const QString sourceGame =
      source(m_gameA, QStringLiteral("Documents/My Games/Game"));
  const QString outside =
      QDir(m_temp.path()).filePath(QStringLiteral("swapped-source"));
  ASSERT_TRUE(QDir().mkpath(sourceGame));
  ASSERT_TRUE(QDir().mkpath(outside));

  PrefixSymlinkTransaction::Options options;
  options.beforePublicationForTesting = [&] {
    ASSERT_TRUE(QDir().rmdir(sourceGame));
    ASSERT_TRUE(makeLink(outside, sourceGame));
  };
  const auto result = PrefixSymlinkTransaction::apply(
      m_prefix, {{m_gameA, QStringLiteral("Game A")}}, options);
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.error.contains(QStringLiteral("symlinked")) ||
              result.error.contains(QStringLiteral("changed")));
  EXPECT_FALSE(QFileInfo::exists(destination(QStringLiteral("Documents"))));
}

TEST_F(PrefixSymlinkTransactionTest,
       SourceParentSwapDuringPublicationFailsClosed) {
  const QString documents = source(m_gameA, QStringLiteral("Documents"));
  const QString sourceGame =
      QDir(documents).filePath(QStringLiteral("My Games/Game"));
  const QString relocated =
      QDir(m_temp.path()).filePath(QStringLiteral("relocated-documents"));
  ASSERT_TRUE(QDir().mkpath(sourceGame));

  PrefixSymlinkTransaction::Options options;
  options.afterSymlinkPublicationForTesting = [&](const QString &) {
    ASSERT_TRUE(QDir().rename(documents, relocated));
    ASSERT_TRUE(makeLink(relocated, documents));
  };
  const auto result = PrefixSymlinkTransaction::apply(
      m_prefix, {{m_gameA, QStringLiteral("Game A")}}, options);
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.error.contains(QStringLiteral("symlinked")) ||
              result.error.contains(QStringLiteral("changed")));
  EXPECT_FALSE(QFileInfo(destination(QStringLiteral("Documents/My Games/Game")))
                   .exists());
}

TEST_F(PrefixSymlinkTransactionTest,
       ForeignReplacementBeforeRetainIsRejectedAndPreserved) {
  ASSERT_TRUE(QDir().mkpath(
      source(m_gameA, QStringLiteral("Documents/My Games/Game"))));
  const QString foreign =
      QDir(m_temp.path()).filePath(QStringLiteral("foreign-race"));
  ASSERT_TRUE(QDir().mkpath(foreign));
  QString replaced;
  PrefixSymlinkTransaction::Options options;
  options.afterSymlinkCallForTesting = [&](const QString &path) {
    replaced = path;
    ASSERT_EQ(::unlink(QFile::encodeName(path).constData()), 0);
    ASSERT_TRUE(makeLink(foreign, path));
  };
  const auto result = PrefixSymlinkTransaction::apply(
      m_prefix, {{m_gameA, QStringLiteral("Game A")}}, options);
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(QFileInfo(replaced).isSymLink());
  EXPECT_TRUE(sameFile(replaced, foreign));
}

TEST_F(PrefixSymlinkTransactionTest,
       DestinationParentSwapDuringPublicationFailsClosed) {
  ASSERT_TRUE(QDir().mkpath(
      source(m_gameA, QStringLiteral("Documents/My Games/Game"))));
  const QString parent = destination(QStringLiteral("Documents/My Games"));
  const QString detached =
      destination(QStringLiteral("Documents/Detached Games"));
  PrefixSymlinkTransaction::Options options;
  options.afterSymlinkPublicationForTesting = [&](const QString &) {
    ASSERT_TRUE(QDir().rename(parent, detached));
    ASSERT_TRUE(QDir().mkpath(parent));
  };
  const auto result = PrefixSymlinkTransaction::apply(
      m_prefix, {{m_gameA, QStringLiteral("Game A")}}, options);
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.error.contains(QStringLiteral("changed")) ||
              result.error.contains(QStringLiteral("Cleanup was incomplete")));
  EXPECT_FALSE(
      QFileInfo(QDir(parent).filePath(QStringLiteral("Game"))).exists());
  EXPECT_FALSE(
      QFileInfo(QDir(detached).filePath(QStringLiteral("Game"))).exists());
}

TEST_F(PrefixSymlinkTransactionTest,
       CaseSiblingAppearingDuringPublicationFailsClosed) {
  ASSERT_TRUE(QDir().mkpath(
      source(m_gameA, QStringLiteral("Documents/My Games/Game"))));
  const QString foreign =
      QDir(m_temp.path()).filePath(QStringLiteral("case-race-foreign"));
  ASSERT_TRUE(QDir().mkpath(foreign));
  QString exact;
  QString lower;
  PrefixSymlinkTransaction::Options options;
  options.afterSymlinkPublicationForTesting = [&](const QString &path) {
    exact = path;
    lower = QDir(QFileInfo(path).absolutePath())
                .filePath(QFileInfo(path).fileName().toLower());
    ASSERT_TRUE(makeLink(foreign, lower));
  };
  const auto result = PrefixSymlinkTransaction::apply(
      m_prefix, {{m_gameA, QStringLiteral("Game A")}}, options);
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.error.contains(QStringLiteral("case-insensitive")));
  EXPECT_FALSE(QFileInfo(exact).exists());
  EXPECT_TRUE(sameFile(lower, foreign));
}

TEST_F(PrefixSymlinkTransactionTest,
       CompatibilityTargetSwapDuringPublicationFailsClosed) {
  const QString documents = destination(QStringLiteral("Documents"));
  const QString relocated =
      QDir(m_temp.path()).filePath(QStringLiteral("relocated-managed-docs"));
  PrefixSymlinkTransaction::Options options;
  options.afterSymlinkPublicationForTesting = [&](const QString &) {
    ASSERT_TRUE(QDir().rename(documents, relocated));
    ASSERT_TRUE(makeLink(relocated, documents));
  };
  const auto result = PrefixSymlinkTransaction::apply(m_prefix, {}, options);
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.error.contains(QStringLiteral("symlinked")) ||
              result.error.contains(QStringLiteral("changed")));
  EXPECT_FALSE(QFileInfo(destination(QStringLiteral("My Documents"))).exists());
  EXPECT_TRUE(sameFile(documents, relocated));
}

TEST_F(PrefixSymlinkTransactionTest,
       StructuralCaseVariantAppearingBeforePublicationFailsClosed) {
  ASSERT_TRUE(QDir().mkpath(
      source(m_gameA, QStringLiteral("Documents/My Games/Game"))));
  PrefixSymlinkTransaction::Options options;
  options.beforePublicationForTesting = [&] {
    ASSERT_TRUE(
        QDir().mkpath(QDir(m_prefix).filePath(QStringLiteral("Drive_C"))));
  };
  const auto result = PrefixSymlinkTransaction::apply(
      m_prefix, {{m_gameA, QStringLiteral("Game A")}}, options);
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.error.contains(QStringLiteral("ambiguous")));
  EXPECT_FALSE(QFileInfo(destination(QStringLiteral("Documents/My Games/Game")))
                   .exists());
}

TEST_F(PrefixSymlinkTransactionTest, TempCreationRejectsRootReplacement) {
  const QString moved =
      QDir(m_temp.path()).filePath(QStringLiteral("managed-moved"));
  PrefixSymlinkTransaction::Options options;
  options.beforePublicationForTesting = [&] {
    ASSERT_TRUE(QDir().rename(m_prefix, moved));
    ASSERT_TRUE(QDir().mkpath(destination(QString{})));
  };

  QString error;
  EXPECT_FALSE(
      PrefixSymlinkTransaction::ensureTempDirectory(m_prefix, error, options));
  EXPECT_TRUE(error.contains(QStringLiteral("root changed")));
  EXPECT_FALSE(QFileInfo::exists(QDir(moved).filePath(
      QStringLiteral("drive_c/users/steamuser/AppData/Local/Temp"))));
  EXPECT_FALSE(
      QFileInfo::exists(destination(QStringLiteral("AppData/Local/Temp"))));
}

TEST_F(PrefixSymlinkTransactionTest, PrefixLockSerializesConcurrentSetup) {
  const QByteArray path = QFile::encodeName(QDir(m_prefix).filePath(
      QStringLiteral(".fluorine-prefix-symlinks.lock")));
  const int lock = ::open(path.constData(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  ASSERT_GE(lock, 0);
  ASSERT_EQ(::flock(lock, LOCK_EX | LOCK_NB), 0);
  const auto result = PrefixSymlinkTransaction::apply(m_prefix, {});
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.error.contains(QStringLiteral("Another process")));
  EXPECT_EQ(::flock(lock, LOCK_UN), 0);
  EXPECT_EQ(::close(lock), 0);
}

TEST_F(PrefixSymlinkTransactionTest, DescriptorUseIsBoundedPerCreatedLeaf) {
  for (int i = 0; i < 40; ++i) {
    ASSERT_TRUE(QDir().mkpath(
        source(m_gameA, QStringLiteral("Documents/My Games/Game-%1")
                            .arg(i, 2, 10, QChar('0')))));
  }

  const pid_t child = ::fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    struct rlimit limit{};
    if (::getrlimit(RLIMIT_NOFILE, &limit) != 0) {
      ::_exit(2);
    }
    limit.rlim_cur = std::min<rlim_t>(limit.rlim_max, 96);
    if (::setrlimit(RLIMIT_NOFILE, &limit) != 0) {
      ::_exit(3);
    }
    const auto result = PrefixSymlinkTransaction::apply(
        m_prefix, {{m_gameA, QStringLiteral("Game A")}});
    ::_exit(result.success ? 0 : 1);
  }

  int status = 0;
  ASSERT_EQ(::waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

} // namespace
