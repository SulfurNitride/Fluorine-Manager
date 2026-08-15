#include "winesavetargetresolver.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

namespace {

struct Layout {
  QTemporaryDir temporary;
  QString prefix;
  QString drive;
  QString user;
  QString myGames;
  QString game;
  QString saves;
  QString profile;
  QString profileSaves;

  Layout() {
    EXPECT_TRUE(temporary.isValid());
    prefix = temporary.filePath("prefix");
    drive = QDir(prefix).filePath("drive_c");
    user = QDir(drive).filePath("users/steamuser");
    myGames = QDir(user).filePath("Documents/My Games");
    game = temporary.filePath("game");
    saves = QDir(game).filePath("Saves");
    profile = temporary.filePath("profiles/Current");
    profileSaves = QDir(profile).filePath("saves");
    EXPECT_TRUE(QDir().mkpath(myGames));
    EXPECT_TRUE(QDir().mkpath(game));
    EXPECT_TRUE(QDir().mkpath(profileSaves));
  }
};

void writeFile(const QString &path, const QByteArray &bytes) {
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write(bytes), bytes.size());
  file.close();
}

} // namespace

TEST(WineSaveTargetResolver, SelectsFixedGameDirectoryFromExactMapping) {
  Layout paths;
  const MappingType mappings{
      {paths.profileSaves, paths.saves, true, true},
  };

  const auto plan = WineSaveTargetResolver::resolve(
      paths.drive, paths.user, paths.myGames, "Morrowind", paths.game,
      paths.saves, paths.profileSaves, true, mappings);

  ASSERT_TRUE(plan) << plan.error.toStdString();
  EXPECT_EQ(plan.kind, WineSaveTargetResolver::Kind::FixedGameDirectory);
  EXPECT_EQ(plan.topologyRoot, QFileInfo(paths.game).canonicalFilePath());
  EXPECT_EQ(plan.livePath, QDir(plan.topologyRoot).filePath("Saves"));
}

TEST(WineSaveTargetResolver, ExactGameTargetWinsInsideWineUserLookingPath) {
  Layout paths;
  const QString nestedGame =
      QDir(paths.drive).filePath("users/steamuser/Games/Morrowind");
  const QString nestedSaves = QDir(nestedGame).filePath("Saves");
  ASSERT_TRUE(QDir().mkpath(nestedGame));
  const MappingType mappings{
      {paths.profileSaves, nestedSaves, true, true},
  };

  const auto plan = WineSaveTargetResolver::resolve(
      paths.drive, paths.user, paths.myGames, "Morrowind", nestedGame,
      nestedSaves, paths.profileSaves, true, mappings);

  ASSERT_TRUE(plan) << plan.error.toStdString();
  EXPECT_EQ(plan.kind, WineSaveTargetResolver::Kind::FixedGameDirectory);
  EXPECT_EQ(plan.topologyRoot, QFileInfo(nestedGame).canonicalFilePath());
  EXPECT_EQ(plan.livePath, QDir(plan.topologyRoot).filePath("Saves"));
}

TEST(WineSaveTargetResolver, CanonicalizesSymlinkedGameWithMissingSaveLeaf) {
  Layout paths;
  const QString alias = paths.temporary.filePath("game-alias");
  ASSERT_TRUE(QFile::link(paths.game, alias));
  const QString aliasSaves = QDir(alias).filePath("Saves");
  const MappingType mappings{
      {paths.profileSaves, aliasSaves, true, true},
  };

  const auto plan = WineSaveTargetResolver::resolve(
      paths.drive, paths.user, paths.myGames, "Morrowind", alias, aliasSaves,
      paths.profileSaves, true, mappings);

  ASSERT_TRUE(plan) << plan.error.toStdString();
  EXPECT_EQ(plan.topologyRoot, QFileInfo(paths.game).canonicalFilePath());
  EXPECT_EQ(plan.livePath, QDir(plan.topologyRoot).filePath("Saves"));
}

TEST(WineSaveTargetResolver, ExistingManagedLinkStillIdentifiesFixedLeafSlot) {
  Layout paths;
  ASSERT_TRUE(QFile::link(paths.profileSaves, paths.saves));
  const MappingType mappings{
      {paths.profileSaves, paths.saves, true, true},
  };

  const auto plan = WineSaveTargetResolver::resolve(
      paths.drive, paths.user, paths.myGames, "Morrowind", paths.game,
      paths.saves, paths.profileSaves, true, mappings);

  ASSERT_TRUE(plan) << plan.error.toStdString();
  EXPECT_EQ(plan.kind, WineSaveTargetResolver::Kind::FixedGameDirectory);
  EXPECT_EQ(plan.livePath,
            QDir(QFileInfo(paths.game).canonicalFilePath()).filePath("Saves"));
  EXPECT_TRUE(WineSaveTargetResolver::isFixedGameDirectorySaveMapping(
      mappings.front(), paths.game, paths.saves, paths.profileSaves));

  EXPECT_TRUE(WineSaveTargetResolver::restoreLegacyBackup(
      paths.game, paths.saves, QDir(paths.game).filePath("_Saves"),
      paths.profileSaves, true));
  EXPECT_FALSE(WineSaveTargetResolver::restoreLegacyBackup(
      paths.game, paths.saves, QDir(paths.game).filePath("_Saves"),
      paths.profileSaves, false));
}

TEST(WineSaveTargetResolver, ForeignLiveLinkIsNeverAcceptedAsManaged) {
  Layout paths;
  const QString foreign = paths.temporary.filePath("foreign-saves");
  ASSERT_TRUE(QDir().mkpath(foreign));
  ASSERT_TRUE(QFile::link(foreign, paths.saves));

  const auto result = WineSaveTargetResolver::restoreLegacyBackup(
      paths.game, paths.saves, QDir(paths.game).filePath("_Saves"),
      paths.profileSaves, true);
  EXPECT_FALSE(result);
  EXPECT_TRUE(QFileInfo(paths.saves).isSymLink());
}

TEST(WineSaveTargetResolver, TranslatesWineUserMapping) {
  Layout paths;
  const QString foreign = paths.temporary.filePath(
      "other-prefix/drive_c/users/deck/Documents/My Games/Skyrim/Saves");
  const MappingType mappings{
      {paths.profileSaves, foreign, true, true},
  };

  const auto plan = WineSaveTargetResolver::resolve(
      paths.drive, paths.user, paths.myGames, "Skyrim", paths.game, paths.saves,
      paths.profileSaves, false, mappings);

  ASSERT_TRUE(plan) << plan.error.toStdString();
  EXPECT_EQ(plan.kind, WineSaveTargetResolver::Kind::PrefixRouted);
  EXPECT_EQ(plan.livePath,
            QDir(paths.user).filePath("Documents/My Games/Skyrim/Saves"));
}

TEST(WineSaveTargetResolver, TranslatesGameSavePathWithoutAcceptedMapping) {
  Layout paths;
  const QString detected = paths.temporary.filePath(
      "detected/drive_c/users/deck/Documents/My Games/Skyrim/Saves");

  const auto plan = WineSaveTargetResolver::resolve(
      paths.drive, paths.user, paths.myGames, "Skyrim", paths.game, detected,
      paths.profileSaves, false, {});

  ASSERT_TRUE(plan) << plan.error.toStdString();
  EXPECT_EQ(plan.kind, WineSaveTargetResolver::Kind::PrefixRouted);
  EXPECT_EQ(plan.livePath,
            QDir(paths.user).filePath("Documents/My Games/Skyrim/Saves"));
}

TEST(WineSaveTargetResolver, RejectsUnsupportedAndConflictingMappedTargets) {
  Layout paths;
  const MappingType unsupported{
      {paths.profileSaves, paths.temporary.filePath("external/Saves"), true,
       true},
  };
  EXPECT_FALSE(WineSaveTargetResolver::resolve(
      paths.drive, paths.user, paths.myGames, "Morrowind", paths.game,
      paths.saves, paths.profileSaves, true, unsupported));

  const MappingType conflicting{
      {paths.profileSaves, paths.saves, true, true},
      {paths.profileSaves,
       paths.temporary.filePath(
           "other/drive_c/users/deck/Documents/My Games/Morrowind/Saves"),
       true, true},
  };
  EXPECT_FALSE(WineSaveTargetResolver::resolve(
      paths.drive, paths.user, paths.myGames, "Morrowind", paths.game,
      paths.saves, paths.profileSaves, true, conflicting));
}

TEST(WineSaveTargetResolver, NonOptedGameDirectoryMappingKeepsLegacyFallback) {
  Layout paths;
  const QString vampireRoot = QDir(paths.game).filePath("vampire");
  const QString vampireSaves = QDir(vampireRoot).filePath("SAVE");
  ASSERT_TRUE(QDir().mkpath(vampireRoot));
  const MappingType mappings{
      {paths.profileSaves, vampireSaves, true, true},
  };

  const auto plan = WineSaveTargetResolver::resolve(
      paths.drive, paths.user, paths.myGames, "Vampire", paths.game,
      vampireSaves, paths.profileSaves, false, mappings);

  ASSERT_TRUE(plan) << plan.error.toStdString();
  EXPECT_EQ(plan.kind, WineSaveTargetResolver::Kind::PrefixRouted);
  EXPECT_EQ(plan.livePath,
            QDir(paths.myGames).filePath(QStringLiteral("Vampire/Saves")));
  EXPECT_TRUE(WineSaveTargetResolver::isFixedGameDirectorySaveMapping(
      mappings.front(), paths.game, vampireSaves, paths.profileSaves));
}

TEST(WineSaveTargetResolver, FiltersOnlyTransactionOwnedDestinations) {
  Layout paths;
  const QString gameAlias = paths.temporary.filePath("game-alias");
  ASSERT_TRUE(QFile::link(paths.game, gameAlias));
  const MappingType mappings{
      {paths.profileSaves, QDir(gameAlias).filePath("Saves"), true, true},
      {paths.temporary.filePath("nested"),
       QDir(gameAlias).filePath("Saves/nested"), true, false},
      {paths.temporary.filePath("lower"), QDir(gameAlias).filePath("saves"),
       true, true},
      {paths.temporary.filePath("lower-nested"),
       QDir(gameAlias).filePath("saves/nested"), true, false},
      {paths.temporary.filePath("Morrowind.ini"),
       QDir(paths.game).filePath("morrowind.ini"), false, false},
      {paths.temporary.filePath("other"),
       QDir(paths.game).filePath("Data Files/x"), false, false},
  };

  const MappingType filtered = WineSaveTargetResolver::filterFixedMappings(
      mappings, paths.saves, paths.game, {"Morrowind.ini"});

  ASSERT_EQ(filtered.size(), 1U);
  EXPECT_EQ(filtered.front().destination,
            QDir(paths.game).filePath("Data Files/x"));

  const MappingType unowned =
      WineSaveTargetResolver::filterFixedMappings(mappings, {}, paths.game, {});
  EXPECT_EQ(unowned.size(), mappings.size());
}

TEST(WineSaveTargetResolver, FindsInterruptedFixedIniGenerations) {
  Layout paths;
  const QString backup =
      QDir(paths.game).filePath("Morrowind.ini.mo2linux_backup");
  const QString retirement =
      QDir(paths.game)
          .filePath("morrowind.INI.fluorine-session-0123456789abcdef01234567");
  writeFile(backup, "global");
  writeFile(retirement, "session");
  writeFile(QDir(paths.game).filePath("Other.ini.mo2linux_backup"), "other");

  const QStringList recovery = WineSaveTargetResolver::legacyIniRecoveryLeaves(
      paths.game, {QStringLiteral("Morrowind.ini")});
  EXPECT_EQ(recovery.size(), 2);
  EXPECT_TRUE(recovery.contains(backup));
  EXPECT_TRUE(recovery.contains(retirement));
}

TEST(WineSaveTargetResolver, IdentifiesFixedSaveMappingForEveryVfsConsumer) {
  Layout paths;
  const Mapping fixed{paths.profileSaves, paths.saves, true, true};
  const Mapping unrelated{paths.profileSaves,
                          QDir(paths.myGames).filePath("Morrowind/Saves"), true,
                          true};

  EXPECT_TRUE(WineSaveTargetResolver::isFixedGameDirectorySaveMapping(
      fixed, paths.game, paths.saves, paths.profileSaves));
  EXPECT_FALSE(WineSaveTargetResolver::isFixedGameDirectorySaveMapping(
      unrelated, paths.game, paths.saves, paths.profileSaves));
}

TEST(WineSaveTargetResolver, RestoresOnlyUnambiguousLegacyBackup) {
  Layout paths;
  const QString legacy = QDir(paths.game).filePath("_Saves");
  ASSERT_TRUE(QDir().mkpath(legacy));
  writeFile(QDir(legacy).filePath("global.ess"), "global");

  const auto restored = WineSaveTargetResolver::restoreLegacyBackup(
      paths.game, paths.saves, legacy);
  ASSERT_TRUE(restored) << restored.error.toStdString();
  EXPECT_FALSE(QFileInfo::exists(legacy));
  QFile file(QDir(paths.saves).filePath("global.ess"));
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  EXPECT_EQ(file.readAll(), QByteArray("global"));
}

TEST(WineSaveTargetResolver, PreservesCoexistingOrUnsafeLegacyState) {
  Layout paths;
  const QString legacy = QDir(paths.game).filePath("_Saves");
  ASSERT_TRUE(QDir().mkpath(legacy));
  ASSERT_TRUE(QDir().mkpath(paths.saves));
  writeFile(QDir(legacy).filePath("backup.ess"), "backup");
  writeFile(QDir(paths.saves).filePath("live.ess"), "live");

  EXPECT_FALSE(WineSaveTargetResolver::restoreLegacyBackup(
      paths.game, paths.saves, legacy));
  EXPECT_TRUE(QFileInfo::exists(QDir(legacy).filePath("backup.ess")));
  EXPECT_TRUE(QFileInfo::exists(QDir(paths.saves).filePath("live.ess")));

  ASSERT_TRUE(QDir(legacy).removeRecursively());
  ASSERT_TRUE(QDir(paths.saves).removeRecursively());
  ASSERT_TRUE(QFile::link(paths.profileSaves, legacy));
  EXPECT_FALSE(WineSaveTargetResolver::restoreLegacyBackup(
      paths.game, paths.saves, legacy));
  EXPECT_TRUE(QFileInfo(legacy).isSymLink());
}

TEST(WineSaveTargetResolver, LegacyRestoreRejectsEveryCaseFamilyAmbiguity) {
  Layout paths;
  const QString legacy = QDir(paths.game).filePath("_Saves");
  const QString lowerLive = QDir(paths.game).filePath("saves");
  ASSERT_TRUE(QDir().mkpath(legacy));
  ASSERT_TRUE(QDir().mkpath(lowerLive));
  writeFile(QDir(legacy).filePath("backup.ess"), "backup");
  writeFile(QDir(lowerLive).filePath("lower.ess"), "lower");

  EXPECT_FALSE(WineSaveTargetResolver::restoreLegacyBackup(
      paths.game, paths.saves, legacy));
  EXPECT_TRUE(QFileInfo::exists(QDir(legacy).filePath("backup.ess")));
  EXPECT_TRUE(QFileInfo::exists(QDir(lowerLive).filePath("lower.ess")));

  ASSERT_TRUE(QDir(lowerLive).removeRecursively());
  const QString lowerBackup = QDir(paths.game).filePath("_saves");
  ASSERT_TRUE(QDir().mkpath(lowerBackup));
  writeFile(QDir(lowerBackup).filePath("other.ess"), "other");
  EXPECT_FALSE(WineSaveTargetResolver::restoreLegacyBackup(
      paths.game, paths.saves, legacy));
  EXPECT_TRUE(QFileInfo::exists(QDir(legacy).filePath("backup.ess")));
  EXPECT_TRUE(QFileInfo::exists(QDir(lowerBackup).filePath("other.ess")));
}

TEST(WineSaveTargetResolver, RejectsUnownedMixedCaseLiveWithoutLegacyBackup) {
  Layout paths;
  const QString mixed = QDir(paths.game).filePath("SaVeS");
  ASSERT_TRUE(QDir().mkpath(mixed));
  writeFile(QDir(mixed).filePath("sentinel.ess"), "mixed");

  const auto result = WineSaveTargetResolver::restoreLegacyBackup(
      paths.game, paths.saves, QDir(paths.game).filePath("_Saves"));
  EXPECT_FALSE(result);
  EXPECT_TRUE(QFileInfo::exists(QDir(mixed).filePath("sentinel.ess")));
}
