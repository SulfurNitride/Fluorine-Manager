#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "modinstallationtransaction.h"

namespace {

void writeFile(const QString &path, const QByteArray &bytes) {
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  ASSERT_EQ(file.write(bytes), bytes.size());
  file.close();
}

QByteArray readFile(const QString &path) {
  QFile file(path);
  EXPECT_TRUE(file.open(QIODevice::ReadOnly));
  return file.readAll();
}

QString makeMod(const QTemporaryDir &temporary, const QString &name) {
  const QString path = QDir(temporary.path()).filePath(name);
  EXPECT_TRUE(QDir().mkdir(path));
  return path;
}

} // namespace

TEST(ModInstallationTransaction, AbortedNewInstallPublishesNothing) {
  QTemporaryDir mods;
  ASSERT_TRUE(mods.isValid());
  {
    QString error;
    auto transaction = ModInstallationTransaction::begin(
        mods.path(), QStringLiteral("New Mod"),
        ModInstallationTransaction::Mode::New, error);
    ASSERT_NE(transaction, nullptr) << error.toStdString();
    writeFile(
        QDir(transaction->stagePath()).filePath(QStringLiteral("candidate")),
        "candidate");
  }

  EXPECT_FALSE(
      QFileInfo::exists(QDir(mods.path()).filePath(QStringLiteral("New Mod"))));
  EXPECT_TRUE(QDir(mods.path())
                  .entryList({QStringLiteral(".fluorine-mod-install-*")},
                             QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot)
                  .isEmpty());
}

TEST(ModInstallationTransaction,
     ReplacementPublishesCompleteStageAndRetiresOld) {
#ifndef Q_OS_LINUX
  GTEST_SKIP() << "atomic replacement is intentionally Linux-only";
#endif
  QTemporaryDir mods;
  ASSERT_TRUE(mods.isValid());
  const QString target = makeMod(mods, QStringLiteral("Target"));
  writeFile(QDir(target).filePath(QStringLiteral("old-only")), "old");
  writeFile(QDir(target).filePath(QStringLiteral("meta.ini")), "category=7\n");

  QString error;
  auto transaction = ModInstallationTransaction::begin(
      mods.path(), QStringLiteral("Target"),
      ModInstallationTransaction::Mode::Replace, error);
  ASSERT_NE(transaction, nullptr) << error.toStdString();
  EXPECT_EQ(
      readFile(
          QDir(transaction->stagePath()).filePath(QStringLiteral("meta.ini"))),
      QByteArray("category=7\n"));
  writeFile(QDir(transaction->stagePath()).filePath(QStringLiteral("new-only")),
            "new");

  const auto published = transaction->publish();
  ASSERT_TRUE(published) << published.error.toStdString();
  EXPECT_FALSE(
      QFileInfo::exists(QDir(target).filePath(QStringLiteral("old-only"))));
  EXPECT_EQ(readFile(QDir(target).filePath(QStringLiteral("new-only"))),
            QByteArray("new"));
  EXPECT_EQ(readFile(QDir(target).filePath(QStringLiteral("meta.ini"))),
            QByteArray("category=7\n"));
}

TEST(ModInstallationTransaction, AbortedReplacementPreservesOriginal) {
  QTemporaryDir mods;
  ASSERT_TRUE(mods.isValid());
  const QString target = makeMod(mods, QStringLiteral("Target"));
  writeFile(QDir(target).filePath(QStringLiteral("sentinel")), "original");
  {
    QString error;
    auto transaction = ModInstallationTransaction::begin(
        mods.path(), QStringLiteral("Target"),
        ModInstallationTransaction::Mode::Replace, error);
    ASSERT_NE(transaction, nullptr) << error.toStdString();
    writeFile(
        QDir(transaction->stagePath()).filePath(QStringLiteral("sentinel")),
        "candidate");
  }
  EXPECT_EQ(readFile(QDir(target).filePath(QStringLiteral("sentinel"))),
            QByteArray("original"));
}

TEST(ModInstallationTransaction, MergeStagesExistingTree) {
#ifndef Q_OS_LINUX
  GTEST_SKIP() << "atomic merge publication is intentionally Linux-only";
#endif
  QTemporaryDir mods;
  ASSERT_TRUE(mods.isValid());
  const QString target = makeMod(mods, QStringLiteral("Target"));
  writeFile(QDir(target).filePath(QStringLiteral("old-only")), "old");

  QString error;
  auto transaction = ModInstallationTransaction::begin(
      mods.path(), QStringLiteral("Target"),
      ModInstallationTransaction::Mode::Merge, error);
  ASSERT_NE(transaction, nullptr) << error.toStdString();
  EXPECT_EQ(
      readFile(
          QDir(transaction->stagePath()).filePath(QStringLiteral("old-only"))),
      QByteArray("old"));
  writeFile(QDir(transaction->stagePath()).filePath(QStringLiteral("new-only")),
            "new");
  ASSERT_TRUE(transaction->publish());
  EXPECT_EQ(readFile(QDir(target).filePath(QStringLiteral("old-only"))),
            QByteArray("old"));
  EXPECT_EQ(readFile(QDir(target).filePath(QStringLiteral("new-only"))),
            QByteArray("new"));
}

TEST(ModInstallationTransaction, NewTargetRaceIsNeverOverwritten) {
  QTemporaryDir mods;
  ASSERT_TRUE(mods.isValid());
  QString error;
  auto transaction = ModInstallationTransaction::begin(
      mods.path(), QStringLiteral("Target"),
      ModInstallationTransaction::Mode::New, error);
  ASSERT_NE(transaction, nullptr) << error.toStdString();
  const QString foreign = makeMod(mods, QStringLiteral("Target"));
  writeFile(QDir(foreign).filePath(QStringLiteral("foreign")), "foreign");

  const auto published = transaction->publish();
  EXPECT_EQ(published.status,
            ModInstallationTransaction::PublishStatus::Failure);
  EXPECT_EQ(readFile(QDir(foreign).filePath(QStringLiteral("foreign"))),
            QByteArray("foreign"));
}

TEST(ModInstallationTransaction, ChangedReplacementTargetIsNeverExchanged) {
  QTemporaryDir mods;
  ASSERT_TRUE(mods.isValid());
  const QString target = makeMod(mods, QStringLiteral("Target"));
  writeFile(QDir(target).filePath(QStringLiteral("old")), "old");

  QString error;
  auto transaction = ModInstallationTransaction::begin(
      mods.path(), QStringLiteral("Target"),
      ModInstallationTransaction::Mode::Replace, error);
  ASSERT_NE(transaction, nullptr) << error.toStdString();
  ASSERT_TRUE(
      QDir(mods.path())
          .rename(QStringLiteral("Target"), QStringLiteral("Moved Target")));
  const QString foreign = makeMod(mods, QStringLiteral("Target"));
  writeFile(QDir(foreign).filePath(QStringLiteral("foreign")), "foreign");

  EXPECT_EQ(transaction->publish().status,
            ModInstallationTransaction::PublishStatus::Failure);
  EXPECT_EQ(readFile(QDir(foreign).filePath(QStringLiteral("foreign"))),
            QByteArray("foreign"));
  EXPECT_EQ(
      readFile(QDir(mods.path()).filePath(QStringLiteral("Moved Target/old"))),
      QByteArray("old"));
}

TEST(ModInstallationTransaction, RejectsGenerationChangedAfterConfirmation) {
#ifndef Q_OS_LINUX
  GTEST_SKIP() << "same-path inode generations are available only on Linux";
#endif
  QTemporaryDir mods;
  ASSERT_TRUE(mods.isValid());
  const QString original = makeMod(mods, QStringLiteral("Target"));
  writeFile(QDir(original).filePath(QStringLiteral("old")), "old");

  ModInstallationTransaction::Target inspected;
  QString error;
  ASSERT_TRUE(ModInstallationTransaction::inspectTarget(
      mods.path(), QStringLiteral("Target"), inspected, error));
  ASSERT_FALSE(inspected.generation.isEmpty());
  ASSERT_TRUE(
      QDir(mods.path())
          .rename(QStringLiteral("Target"), QStringLiteral("Moved Target")));
  const QString replacement = makeMod(mods, QStringLiteral("Target"));
  writeFile(QDir(replacement).filePath(QStringLiteral("foreign")), "foreign");

  EXPECT_EQ(ModInstallationTransaction::begin(
                mods.path(), QStringLiteral("Target"),
                ModInstallationTransaction::Mode::Replace, error,
                inspected.generation),
            nullptr);
  EXPECT_EQ(readFile(QDir(replacement).filePath(QStringLiteral("foreign"))),
            QByteArray("foreign"));
}

TEST(ModInstallationTransaction, RejectsCaseAmbiguityAndSymlinkTargets) {
  QTemporaryDir mods;
  QTemporaryDir outside;
  ASSERT_TRUE(mods.isValid());
  ASSERT_TRUE(outside.isValid());
  ASSERT_TRUE(QDir().mkdir(QDir(mods.path()).filePath(QStringLiteral("Case"))));
  ASSERT_TRUE(QDir().mkdir(QDir(mods.path()).filePath(QStringLiteral("case"))));

  ModInstallationTransaction::Target target;
  QString error;
  EXPECT_FALSE(ModInstallationTransaction::inspectTarget(
      mods.path(), QStringLiteral("CASE"), target, error));

#ifdef Q_OS_WIN
  GTEST_SKIP() << "creating symlinks requires optional Windows privileges";
#else
  ASSERT_TRUE(QFile::link(
      outside.path(), QDir(mods.path()).filePath(QStringLiteral("Linked"))));
  EXPECT_EQ(ModInstallationTransaction::begin(
                mods.path(), QStringLiteral("Linked"),
                ModInstallationTransaction::Mode::Replace, error),
            nullptr);
#endif
}

TEST(ModInstallationTransaction, SerializesWritersForOneModsRoot) {
  QTemporaryDir mods;
  ASSERT_TRUE(mods.isValid());
  QString error;
  auto first = ModInstallationTransaction::begin(
      mods.path(), QStringLiteral("One"), ModInstallationTransaction::Mode::New,
      error);
  ASSERT_NE(first, nullptr) << error.toStdString();
  auto second = ModInstallationTransaction::begin(
      mods.path(), QStringLiteral("Two"), ModInstallationTransaction::Mode::New,
      error);
  EXPECT_EQ(second, nullptr);
  first.reset();
  second = ModInstallationTransaction::begin(
      mods.path(), QStringLiteral("Two"), ModInstallationTransaction::Mode::New,
      error);
  EXPECT_NE(second, nullptr) << error.toStdString();
}

TEST(ModInstallationTransaction, RefusesMetadataChangedWhileStaged) {
#ifndef Q_OS_LINUX
  GTEST_SKIP() << "atomic replacement is intentionally Linux-only";
#endif
  QTemporaryDir mods;
  ASSERT_TRUE(mods.isValid());
  const QString target = makeMod(mods, QStringLiteral("Target"));
  const QString metadata = QDir(target).filePath(QStringLiteral("meta.ini"));
  writeFile(metadata, "category=1\n");

  QString error;
  auto transaction = ModInstallationTransaction::begin(
      mods.path(), QStringLiteral("Target"),
      ModInstallationTransaction::Mode::Replace, error);
  ASSERT_NE(transaction, nullptr) << error.toStdString();
  writeFile(metadata, "category=9\n");

  EXPECT_EQ(transaction->publish().status,
            ModInstallationTransaction::PublishStatus::Failure);
  EXPECT_EQ(readFile(metadata), QByteArray("category=9\n"));
}

TEST(ModInstallationTransaction, RefusesMetadataChangedDuringInitialStaging) {
  QTemporaryDir mods;
  ASSERT_TRUE(mods.isValid());
  const QString target = makeMod(mods, QStringLiteral("Target"));
  const QString metadata = QDir(target).filePath(QStringLiteral("meta.ini"));
  writeFile(metadata, "category=1\n");

  QString error;
  auto transaction = ModInstallationTransaction::begin(
      mods.path(), QStringLiteral("Target"),
      ModInstallationTransaction::Mode::Replace, error, {},
      [&] { writeFile(metadata, "category=9\n"); });

  EXPECT_EQ(transaction, nullptr);
  EXPECT_EQ(readFile(metadata), QByteArray("category=9\n"));
}

TEST(ModInstallationTransaction, AcceptsSymlinkedConfiguredModsRoot) {
#ifdef Q_OS_WIN
  GTEST_SKIP() << "creating symlinks requires optional Windows privileges";
#else
  QTemporaryDir parent;
  ASSERT_TRUE(parent.isValid());
  const QString actual = QDir(parent.path()).filePath(QStringLiteral("actual"));
  const QString linked = QDir(parent.path()).filePath(QStringLiteral("linked"));
  ASSERT_TRUE(QDir().mkdir(actual));
  ASSERT_TRUE(QFile::link(actual, linked));

  QString error;
  auto transaction = ModInstallationTransaction::begin(
      linked, QStringLiteral("Target"), ModInstallationTransaction::Mode::New,
      error);
  ASSERT_NE(transaction, nullptr) << error.toStdString();
  ASSERT_TRUE(transaction->publish());
  EXPECT_TRUE(
      QFileInfo::exists(QDir(actual).filePath(QStringLiteral("Target"))));
#endif
}

TEST(ModInstallationTransaction, RejectsDuplicateStagedMetadataCaseFamily) {
  QTemporaryDir mods;
  ASSERT_TRUE(mods.isValid());
  QString error;
  auto transaction = ModInstallationTransaction::begin(
      mods.path(), QStringLiteral("Target"),
      ModInstallationTransaction::Mode::New, error);
  ASSERT_NE(transaction, nullptr) << error.toStdString();
  writeFile(QDir(transaction->stagePath()).filePath(QStringLiteral("meta.ini")),
            "category=1\n");
  writeFile(QDir(transaction->stagePath()).filePath(QStringLiteral("Meta.ini")),
            "category=2\n");
  int familySize = 0;
  for (const QFileInfo &entry :
       QDir(transaction->stagePath())
           .entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries)) {
    if (entry.fileName().compare(QStringLiteral("meta.ini"),
                                 Qt::CaseInsensitive) == 0) {
      ++familySize;
    }
  }
  if (familySize < 2) {
    GTEST_SKIP() << "filesystem does not support distinct case variants";
  }

  QString metadata;
  EXPECT_FALSE(ModInstallationTransaction::prepareStagedMetadata(
      transaction->stagePath(), metadata, error));
  EXPECT_FALSE(error.isEmpty());
}

TEST(ModInstallationTransaction, MergeCanonicalizesMetadataCase) {
#ifndef Q_OS_LINUX
  GTEST_SKIP() << "atomic merge publication is intentionally Linux-only";
#endif
  QTemporaryDir mods;
  ASSERT_TRUE(mods.isValid());
  const QString target = makeMod(mods, QStringLiteral("Target"));
  writeFile(QDir(target).filePath(QStringLiteral("Meta.ini")), "category=7\n");

  QString error;
  auto transaction = ModInstallationTransaction::begin(
      mods.path(), QStringLiteral("Target"),
      ModInstallationTransaction::Mode::Merge, error);
  ASSERT_NE(transaction, nullptr) << error.toStdString();
  EXPECT_TRUE(QFileInfo::exists(
      QDir(transaction->stagePath()).filePath(QStringLiteral("meta.ini"))));
  EXPECT_FALSE(QFileInfo::exists(
      QDir(transaction->stagePath()).filePath(QStringLiteral("Meta.ini"))));
  ASSERT_TRUE(transaction->publish());
  EXPECT_EQ(readFile(QDir(target).filePath(QStringLiteral("meta.ini"))),
            QByteArray("category=7\n"));
  EXPECT_FALSE(
      QFileInfo::exists(QDir(target).filePath(QStringLiteral("Meta.ini"))));
}

TEST(ModInstallationTransaction, StagedFilesRejectSymlinkAncestorsAndLeaves) {
#ifdef Q_OS_WIN
  GTEST_SKIP() << "creating symlinks requires optional Windows privileges";
#else
  QTemporaryDir mods;
  QTemporaryDir outside;
  ASSERT_TRUE(mods.isValid());
  ASSERT_TRUE(outside.isValid());
  QString error;
  auto transaction = ModInstallationTransaction::begin(
      mods.path(), QStringLiteral("Target"),
      ModInstallationTransaction::Mode::New, error);
  ASSERT_NE(transaction, nullptr) << error.toStdString();

  const QString linkedParent =
      QDir(transaction->stagePath()).filePath(QStringLiteral("linked"));
  ASSERT_TRUE(QFile::link(outside.path(), linkedParent));
  QString output;
  EXPECT_FALSE(ModInstallationTransaction::prepareStagedFile(
      transaction->stagePath(), QStringLiteral("linked/output"), true, output,
      error));
  EXPECT_FALSE(QFileInfo::exists(
      QDir(outside.path()).filePath(QStringLiteral("output"))));

  const QString outsideFile =
      QDir(outside.path()).filePath(QStringLiteral("outside.ini"));
  writeFile(outsideFile, "sentinel");
  ASSERT_TRUE(QFile::link(
      outsideFile,
      QDir(transaction->stagePath()).filePath(QStringLiteral("meta.ini"))));
  EXPECT_FALSE(ModInstallationTransaction::prepareStagedFile(
      transaction->stagePath(), QStringLiteral("meta.ini"), false, output,
      error));
  EXPECT_EQ(readFile(outsideFile), QByteArray("sentinel"));
#endif
}

TEST(ModInstallationTransaction, NewInstallDoesNotWidenStagePermissions) {
  QTemporaryDir mods;
  ASSERT_TRUE(mods.isValid());
  QString error;
  auto transaction = ModInstallationTransaction::begin(
      mods.path(), QStringLiteral("Target"),
      ModInstallationTransaction::Mode::New, error);
  ASSERT_NE(transaction, nullptr) << error.toStdString();
  ASSERT_TRUE(transaction->publish());
  const QFile::Permissions permissions =
      QFileInfo(QDir(mods.path()).filePath(QStringLiteral("Target")))
          .permissions();
  EXPECT_TRUE(permissions.testFlag(QFile::ReadOwner));
  EXPECT_TRUE(permissions.testFlag(QFile::WriteOwner));
  EXPECT_TRUE(permissions.testFlag(QFile::ExeOwner));
  EXPECT_FALSE(permissions.testFlag(QFile::ReadGroup));
  EXPECT_FALSE(permissions.testFlag(QFile::ExeGroup));
  EXPECT_FALSE(permissions.testFlag(QFile::ReadOther));
  EXPECT_FALSE(permissions.testFlag(QFile::ExeOther));
}
