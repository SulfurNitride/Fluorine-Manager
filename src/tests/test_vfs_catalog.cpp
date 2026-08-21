#include "vfs/vfscatalog.h"
#include "vfs/permissionrepair.h"
#include "usvfssnapshot.h"

#include <gtest/gtest.h>
#include <sqlite3.h>

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <sys/stat.h>

namespace fs = std::filesystem;

namespace
{
class TempRoot
{
public:
  TempRoot()
  {
    char path[] = "/tmp/fluorine-catalog-XXXXXX";
    if (const char* result = mkdtemp(path); result != nullptr) m_path = result;
  }
  ~TempRoot()
  {
    std::error_code ec;
    fs::remove_all(m_path, ec);
  }
  const fs::path& path() const { return m_path; }

private:
  fs::path m_path;
};

void writeFile(const fs::path& path, const std::string& contents)
{
  fs::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(stream.is_open());
  stream << contents;
}

std::string winnerOrigin(const VfsTree& tree, const std::string& name)
{
  const VfsNode* node = tree.root.resolve({name});
  return node != nullptr && !node->is_directory ? node->file_info.origin : "";
}
}  // namespace

TEST(VfsCatalog, ReusesHashesAndPreservesOverwritePriority)
{
  TempRoot temp;
  ASSERT_FALSE(temp.path().empty());
  const fs::path data = temp.path() / "Data";
  const fs::path mod = temp.path() / "Mod";
  const fs::path overwrite = temp.path() / "overwrite";
  const fs::path db = temp.path() / "catalog.sqlite";

  writeFile(data / "same.txt", "base");
  writeFile(mod / "same.txt", "mod");
  writeFile(overwrite / "same.txt", "overwrite");

  VfsCatalog catalog(db);
  VfsCatalogProgress first;
  VfsCatalogResult initialResult = catalog.reconcileAndBuild(
      data.string(), {{"Test Mod", mod.string()}}, overwrite.string(), true,
      [&](const VfsCatalogProgress& progress) { first = progress; });
  VfsTree initial = std::move(initialResult.tree);
  EXPECT_EQ(first.files_scanned, 3u);
  EXPECT_EQ(first.files_hashed, 3u);
  EXPECT_EQ(first.fingerprint_misses, 3u);
  EXPECT_EQ(first.fingerprint_uncached, 3u);
  EXPECT_GE(first.hash_workers, 1u);
  EXPECT_LE(
      first.hash_workers,
      static_cast<uint64_t>(
          std::max(1u, std::thread::hardware_concurrency())));
  EXPECT_EQ(winnerOrigin(initial, "same.txt"), "Overwrite");

  VfsCatalogProgress second;
  VfsCatalogResult warmResult = catalog.reconcileAndBuild(
      data.string(), {{"Test Mod", mod.string()}}, overwrite.string(), true,
      [&](const VfsCatalogProgress& progress) { second = progress; });
  VfsTree warm = std::move(warmResult.tree);
  EXPECT_EQ(second.files_scanned, 3u);
  EXPECT_EQ(second.files_hashed, 0u);
  EXPECT_EQ(second.fingerprint_misses, 0u);
  EXPECT_EQ(second.provider_roots_changed, 0u);
  EXPECT_EQ(second.catalog_rows_written, 0u);
  EXPECT_EQ(second.catalog_rows_deleted, 0u);
  EXPECT_EQ(second.catalog_rows_loaded, 3u);
  EXPECT_EQ(second.merkle_roots_reused, 3u);
  EXPECT_EQ(initialResult.profile_root, warmResult.profile_root);
  ASSERT_EQ(initialResult.provider_roots.size(), warmResult.provider_roots.size());
  for (size_t i = 0; i < initialResult.provider_roots.size(); ++i) {
    EXPECT_EQ(initialResult.provider_roots[i].digest,
              warmResult.provider_roots[i].digest);
  }
  EXPECT_EQ(winnerOrigin(warm, "same.txt"), "Overwrite");

  fs::remove(overwrite / "same.txt");
  VfsTree fallback = std::move(catalog.reconcileAndBuild(
      data.string(), {{"Test Mod", mod.string()}}, overwrite.string(), true).tree);
  EXPECT_EQ(winnerOrigin(fallback, "same.txt"), "Test Mod");
}

TEST(VfsCatalog, BuildsResolvedUsvfsSnapshotWithoutBaseOrSkippedFiles)
{
  TempRoot temp;
  const fs::path data = temp.path() / "Data";
  const fs::path modA = temp.path() / "ModA";
  const fs::path modB = temp.path() / "ModB";
  const fs::path overwrite = temp.path() / "overwrite";
  writeFile(data / "base-only.txt", "base");
  writeFile(modA / "meshes/actors/a.nif", "a");
  writeFile(modA / "shared.txt", "lower");
  writeFile(modA / ".git/ignored.txt", "ignored");
  writeFile(modA / "hidden.mohidden", "ignored");
  writeFile(modB / "shared.txt", "higher");
  writeFile(overwrite / "generated/config.ini", "overwrite");

  const auto catalog = VfsCatalog(temp.path() / "catalog.sqlite").reconcileAndBuild(
      data.string(), {{"Mod A", modA.string()}, {"Mod B", modB.string()}},
      overwrite.string(), true);
  const auto snapshot = buildUsvfsResolvedSnapshot(
      catalog.tree, QString::fromStdString(data.string()),
      {QStringLiteral(".mohidden")}, {QStringLiteral(".git")});

  EXPECT_EQ(snapshot.fileCount, 3u);
  EXPECT_EQ(snapshot.directoryCount, 3u);  // meshes, actors, generated
  ASSERT_EQ(snapshot.mappings.size(), 6u);
  EXPECT_TRUE(std::all_of(
      snapshot.mappings.begin(),
      snapshot.mappings.begin() + static_cast<std::ptrdiff_t>(snapshot.directoryCount),
      [](const Mapping& mapping) { return mapping.isDirectory; }));

  auto mappedSource = [&](const fs::path& relative) -> QString {
    const QString destination =
        QString::fromStdString((data / relative).string());
    const auto found = std::find_if(
        snapshot.mappings.begin(), snapshot.mappings.end(),
        [&](const Mapping& mapping) { return mapping.destination == destination; });
    return found == snapshot.mappings.end() ? QString{} : found->source;
  };
  EXPECT_TRUE(mappedSource("base-only.txt").isEmpty());
  EXPECT_TRUE(mappedSource(".git/ignored.txt").isEmpty());
  EXPECT_TRUE(mappedSource("hidden.mohidden").isEmpty());
  EXPECT_EQ(mappedSource("shared.txt"),
            QString::fromStdString((modB / "shared.txt").string()));
  EXPECT_EQ(mappedSource("generated/config.ini"),
            QString::fromStdString((overwrite / "generated/config.ini").string()));
}

TEST(VfsCatalog, DirectUsvfsSnapshotMatchesCatalogResolution)
{
  TempRoot temp;
  const fs::path data = temp.path() / "Data";
  const fs::path modA = temp.path() / "ModA";
  const fs::path modB = temp.path() / "ModB";
  const fs::path overwrite = temp.path() / "overwrite";
  writeFile(data / "base-only.txt", "base");
  writeFile(modA / "meshes/actors/a.nif", "a");
  writeFile(modA / "shared.txt", "lower");
  writeFile(modA / "meta.ini", "metadata");
  writeFile(modA / ".git/ignored.txt", "ignored");
  writeFile(modA / "hidden.mohidden", "ignored");
  writeFile(modB / "shared.txt", "higher");
  writeFile(overwrite / "generated/config.ini", "overwrite");

  const MappingType mappings{
      {QString::fromStdString(modA.string()),
       QString::fromStdString(data.string()), true, false},
      {QString::fromStdString(modB.string()),
       QString::fromStdString(data.string()), true, false},
      {QString::fromStdString(overwrite.string()),
       QString::fromStdString(data.string()), true, true},
  };
  const auto catalog = VfsCatalog(temp.path() / "catalog.sqlite").reconcileAndBuild(
      data.string(), {{"Mod A", modA.string()}, {"Mod B", modB.string()}},
      overwrite.string(), true);
  const auto expected = buildUsvfsResolvedSnapshot(
      catalog.tree, QString::fromStdString(data.string()),
      {QStringLiteral(".mohidden")}, {QStringLiteral(".git")});
  const auto direct = buildUsvfsResolvedSnapshotFromMappings(
      mappings, QString::fromStdString(data.string()),
      {QStringLiteral(".mohidden")}, {QStringLiteral(".git")});

  ASSERT_EQ(direct.directoryCount, expected.directoryCount);
  ASSERT_EQ(direct.fileCount, expected.fileCount);
  ASSERT_EQ(direct.mappings.size(), expected.mappings.size());
  for (std::size_t index = 0; index < direct.mappings.size(); ++index) {
    EXPECT_EQ(direct.mappings[index].source, expected.mappings[index].source);
    EXPECT_EQ(direct.mappings[index].destination,
              expected.mappings[index].destination);
    EXPECT_EQ(direct.mappings[index].isDirectory,
              expected.mappings[index].isDirectory);
  }
}

TEST(VfsCatalog, DirectUsvfsSnapshotHonorsDestinationSubdirectories)
{
  TempRoot temp;
  const fs::path data = temp.path() / "Data";
  const fs::path mod = temp.path() / "Mod";
  writeFile(mod / "plugin.dll", "plugin");

  const auto snapshot = buildUsvfsResolvedSnapshotFromMappings(
      {{QString::fromStdString(mod.string()),
        QString::fromStdString((data / "SKSE/Plugins").string()), true, false}},
      QString::fromStdString(data.string()));

  EXPECT_EQ(snapshot.fileCount, 1u);
  const auto file = std::find_if(
      snapshot.mappings.begin(), snapshot.mappings.end(),
      [](const Mapping& mapping) { return !mapping.isDirectory; });
  ASSERT_NE(file, snapshot.mappings.end());
  EXPECT_EQ(file->source, QString::fromStdString((mod / "plugin.dll").string()));
  EXPECT_EQ(file->destination,
            QString::fromStdString((data / "SKSE/Plugins/plugin.dll").string()));
}

TEST(VfsCatalog, DirectUsvfsSnapshotReadsLiveProviderContents)
{
  TempRoot temp;
  const fs::path data = temp.path() / "Data";
  const fs::path mod = temp.path() / "Mod";
  writeFile(mod / "old.txt", "old");
  const MappingType mappings{{QString::fromStdString(mod.string()),
                              QString::fromStdString(data.string()), true, false}};

  const auto initial = buildUsvfsResolvedSnapshotFromMappings(
      mappings, QString::fromStdString(data.string()));
  EXPECT_EQ(initial.fileCount, 1u);

  fs::remove(mod / "old.txt");
  writeFile(mod / "new.txt", "new");
  const auto refreshed = buildUsvfsResolvedSnapshotFromMappings(
      mappings, QString::fromStdString(data.string()));
  EXPECT_EQ(refreshed.fileCount, 1u);
  EXPECT_TRUE(std::any_of(
      refreshed.mappings.begin(), refreshed.mappings.end(),
      [&](const Mapping& mapping) {
        return !mapping.isDirectory &&
               mapping.source == QString::fromStdString((mod / "new.txt").string());
      }));
  EXPECT_TRUE(std::none_of(
      refreshed.mappings.begin(), refreshed.mappings.end(),
      [&](const Mapping& mapping) {
        return !mapping.isDirectory &&
               mapping.source == QString::fromStdString((mod / "old.txt").string());
      }));
}

TEST(VfsCatalog, ExtractsOnlyUniqueDataProvidersForUsvfsCatalog)
{
  const MappingType mappings{
      {QStringLiteral("/mods/A"), QStringLiteral("/game/Data"), true, false},
      {QStringLiteral("/mods/A"), QStringLiteral("/game/Data/Sub"), true, false},
      {QStringLiteral("/overwrite"), QStringLiteral("/game/Data"), true, true},
      {QStringLiteral("/profile/saves"), QStringLiteral("/docs/Saves"), true, true},
      {QStringLiteral("/profile/plugins.txt"),
       QStringLiteral("/game/Data/plugins.txt"), false, false},
  };
  const auto mods = usvfsCatalogModsFromMappings(
      mappings, QStringLiteral("/game/Data"), QStringLiteral("/overwrite"));
  ASSERT_EQ(mods.size(), 1u);
  EXPECT_EQ(mods.front().second, "/mods/A");
}

TEST(VfsCatalog, MetadataDriftRehashesOnlyChangedFile)
{
  TempRoot temp;
  const fs::path data = temp.path() / "Data";
  const fs::path overwrite = temp.path() / "overwrite";
  writeFile(data / "a.bin", "unchanged content");
  writeFile(data / "b.bin", "other content");
  fs::create_directories(overwrite);

  VfsCatalog catalog(temp.path() / "catalog.sqlite");
  catalog.reconcileAndBuild(data.string(), {}, overwrite.string(), true);

  std::error_code ec;
  const auto oldTime = fs::last_write_time(data / "a.bin", ec);
  ASSERT_FALSE(ec);
  fs::last_write_time(data / "a.bin", oldTime + std::chrono::seconds(1), ec);
  ASSERT_FALSE(ec);

  VfsCatalogProgress progress;
  catalog.reconcileAndBuild(
      data.string(), {}, overwrite.string(), true,
      [&](const VfsCatalogProgress& value) { progress = value; });
  EXPECT_EQ(progress.files_scanned, 2u);
  EXPECT_EQ(progress.files_hashed, 1u);
  EXPECT_EQ(progress.fingerprint_misses, 1u);
  EXPECT_EQ(progress.fingerprint_uncached, 0u);
  EXPECT_EQ(progress.fingerprint_mtime_mismatches, 1u);
  EXPECT_EQ(progress.fingerprint_ctime_mismatches, 1u);
}

TEST(VfsCatalog, CanonicalPathAliasesShareDatabaseAndCachedHashes)
{
  TempRoot temp;
  const fs::path realRoot = temp.path() / "real";
  const fs::path aliasRoot = temp.path() / "alias";
  const fs::path realData = realRoot / "Data";
  const fs::path realOverwrite = realRoot / "overwrite";
  writeFile(realData / "base.txt", "base");
  writeFile(realOverwrite / "generated.ini", "generated");

  std::error_code ec;
  fs::create_directory_symlink(realRoot, aliasRoot, ec);
  ASSERT_FALSE(ec);
  const fs::path aliasData = aliasRoot / "Data";
  const fs::path aliasOverwrite = aliasRoot / "overwrite";

  EXPECT_EQ(VfsCatalog::databasePath(realData.string()),
            VfsCatalog::databasePath(aliasData.string()));

  const fs::path database = temp.path() / "catalog.sqlite";
  VfsCatalog catalog(database);
  VfsCatalogProgress first;
  const auto initial = catalog.reconcileAndBuild(
      realData.string(), {}, realOverwrite.string(), true,
      [&](const VfsCatalogProgress& value) { first = value; });
  EXPECT_EQ(first.files_hashed, 2u);

  VfsCatalogProgress aliased;
  const auto reused = catalog.reconcileAndBuild(
      aliasData.string(), {}, aliasOverwrite.string(), true,
      [&](const VfsCatalogProgress& value) { aliased = value; });
  EXPECT_EQ(aliased.files_scanned, 2u);
  EXPECT_EQ(aliased.files_hashed, 0u);
  EXPECT_EQ(aliased.fingerprint_misses, 0u);
  EXPECT_EQ(aliased.catalog_rows_loaded, 2u);
  EXPECT_EQ(initial.profile_root, reused.profile_root);
  ASSERT_EQ(reused.provider_roots.size(), 2u);
  EXPECT_EQ(reused.provider_roots.front().root_key,
            fs::canonical(realData).string());
  EXPECT_EQ(reused.provider_roots.back().root_key,
            fs::canonical(realOverwrite).string());
  EXPECT_EQ(catalog.loadBaseSnapshot(aliasData.string()).size(), 1u);

  writeFile(aliasOverwrite / "generated.ini", "updated");
  const auto refreshed = catalog.forceRefreshProviderFiles(
      aliasOverwrite.string(), "Overwrite", false, {"generated.ini"});
  EXPECT_EQ(refreshed.provider_root.root_key,
            fs::canonical(realOverwrite).string());
  VfsCatalogProgress afterRefresh;
  const auto refreshedReconcile = catalog.reconcileAndBuild(
      realData.string(), {}, realOverwrite.string(), true,
      [&](const VfsCatalogProgress& value) { afterRefresh = value; });
  EXPECT_EQ(afterRefresh.files_hashed, 0u);
  EXPECT_EQ(refreshedReconcile.provider_roots.back().digest,
            refreshed.provider_root.digest);

  catalog.invalidateProviderFiles(
      aliasOverwrite.string(), {"generated.ini"});
  VfsCatalogProgress afterInvalidation;
  catalog.reconcileAndBuild(
      realData.string(), {}, realOverwrite.string(), true,
      [&](const VfsCatalogProgress& value) { afterInvalidation = value; });
  EXPECT_EQ(afterInvalidation.files_hashed, 1u);
  EXPECT_EQ(afterInvalidation.fingerprint_uncached, 1u);
}

TEST(VfsCatalog, HashesChangedFilesWithAvailableCpuWorkers)
{
  TempRoot temp;
  const fs::path data = temp.path() / "Data";
  const fs::path overwrite = temp.path() / "overwrite";
  fs::create_directories(overwrite);

  constexpr unsigned int fileCount = 16;
  for (unsigned int index = 0; index < fileCount; ++index) {
    writeFile(data / ("changed-" + std::to_string(index) + ".bin"),
              "changed file " + std::to_string(index));
  }

  VfsCatalog catalog(temp.path() / "catalog.sqlite");
  VfsCatalogProgress first;
  catalog.reconcileAndBuild(
      data.string(), {}, overwrite.string(), true,
      [&](const VfsCatalogProgress& progress) { first = progress; });

  const uint64_t expectedWorkers = std::min<uint64_t>(
      fileCount, std::max(1u, std::thread::hardware_concurrency()));
  EXPECT_EQ(first.files_hashed, fileCount);
  EXPECT_EQ(first.hash_workers, expectedWorkers);

  VfsCatalogProgress warm;
  catalog.reconcileAndBuild(
      data.string(), {}, overwrite.string(), true,
      [&](const VfsCatalogProgress& progress) { warm = progress; });
  EXPECT_EQ(warm.files_hashed, 0u);
  EXPECT_EQ(warm.hash_workers, 0u);
}

TEST(VfsCatalog, ForceRefreshHashesControlledWritesAndUpdatesMerkleRoot)
{
  TempRoot temp;
  const fs::path data = temp.path() / "Data";
  const fs::path overwrite = temp.path() / "overwrite";
  const fs::path file = overwrite / "config/settings.ini";
  writeFile(data / "base.txt", "base");
  writeFile(file, "AAAA");

  VfsCatalog catalog(temp.path() / "catalog.sqlite");
  const auto initial = catalog.reconcileAndBuild(
      data.string(), {}, overwrite.string(), true);
  const auto oldRoot = initial.provider_roots.back().digest;
  const auto oldTime = fs::last_write_time(file);

  // Same size and restored mtime models an application that hides the cheap
  // fingerprint signals. Controlled paths must still be content-hashed.
  writeFile(file, "BBBB");
  fs::last_write_time(file, oldTime);
  const auto refreshed = catalog.forceRefreshProviderFiles(
      overwrite.string(), "Overwrite", false, {"config/settings.ini"});

  ASSERT_EQ(refreshed.files.size(), 1u);
  EXPECT_TRUE(refreshed.files.front().exists);
  EXPECT_NE(refreshed.provider_root.digest, oldRoot);
}

TEST(VfsCatalog, DuplicateReviewUsesCatalogHashesAndHighestPriorityMod)
{
  TempRoot temp;
  const fs::path data = temp.path() / "Data";
  const fs::path modA = temp.path() / "ModA";
  const fs::path modB = temp.path() / "ModB";
  const fs::path overwrite = temp.path() / "overwrite";
  writeFile(data / "base.txt", "base");
  writeFile(overwrite / "same.ini", "same");
  writeFile(overwrite / "different.ini", "overwrite");
  writeFile(modA / "same.ini", "same");
  writeFile(modA / "different.ini", "lower priority");
  writeFile(modB / "different.ini", "highest priority");

  const auto result = VfsCatalog(temp.path() / "catalog.sqlite").reconcileAndBuild(
      data.string(), {{"Mod A", modA.string()}, {"Mod B", modB.string()}},
      overwrite.string(), true);
  ASSERT_EQ(result.overwrite_duplicates.size(), 2u);
  const auto same = std::find_if(
      result.overwrite_duplicates.begin(), result.overwrite_duplicates.end(),
      [](const VfsCatalogDuplicate& value) { return value.relative_path == "same.ini"; });
  ASSERT_NE(same, result.overwrite_duplicates.end());
  EXPECT_EQ(same->mod_name, "Mod A");
  EXPECT_EQ(same->state, VfsDuplicateState::Identical);
  const auto different = std::find_if(
      result.overwrite_duplicates.begin(), result.overwrite_duplicates.end(),
      [](const VfsCatalogDuplicate& value) {
        return value.relative_path == "different.ini";
      });
  ASSERT_NE(different, result.overwrite_duplicates.end());
  EXPECT_EQ(different->mod_name, "Mod B");
  EXPECT_EQ(different->state, VfsDuplicateState::Different);
}

TEST(VfsCatalog, ForceRefreshRemovesDeletedRows)
{
  TempRoot temp;
  const fs::path data = temp.path() / "Data";
  const fs::path overwrite = temp.path() / "overwrite";
  writeFile(data / "base.txt", "base");
  writeFile(overwrite / "removed.ini", "temporary");

  VfsCatalog catalog(temp.path() / "catalog.sqlite");
  const auto initial = catalog.reconcileAndBuild(
      data.string(), {}, overwrite.string(), true);
  ASSERT_EQ(initial.provider_roots.back().file_count, 1u);
  fs::remove(overwrite / "removed.ini");

  const auto refreshed = catalog.forceRefreshProviderFiles(
      overwrite.string(), "Overwrite", false, {"removed.ini"});
  ASSERT_EQ(refreshed.files.size(), 1u);
  EXPECT_FALSE(refreshed.files.front().exists);
  EXPECT_EQ(refreshed.provider_root.file_count, 0u);
}

TEST(VfsCatalog, UsesWalJournalMode)
{
  TempRoot temp;
  const fs::path data = temp.path() / "Data";
  const fs::path overwrite = temp.path() / "overwrite";
  writeFile(data / "file.txt", "content");
  fs::create_directories(overwrite);
  const fs::path dbPath = temp.path() / "catalog.sqlite";
  VfsCatalog(dbPath).reconcileAndBuild(data.string(), {}, overwrite.string(), true);

  sqlite3* db = nullptr;
  ASSERT_EQ(sqlite3_open_v2(dbPath.c_str(), &db, SQLITE_OPEN_READONLY, nullptr),
            SQLITE_OK);
  sqlite3_stmt* stmt = nullptr;
  ASSERT_EQ(sqlite3_prepare_v2(db, "PRAGMA journal_mode;", -1, &stmt, nullptr),
            SQLITE_OK);
  ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
  EXPECT_STREQ(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)), "wal");
  sqlite3_finalize(stmt);
  sqlite3_close(db);
}

TEST(VfsCatalog, UpgradesVersionOneCatalogWithoutRehashing)
{
  TempRoot temp;
  const fs::path data = temp.path() / "Data";
  const fs::path overwrite = temp.path() / "overwrite";
  const fs::path dbPath = temp.path() / "catalog.sqlite";
  writeFile(data / "file.txt", "content");
  fs::create_directories(overwrite);

  VfsCatalog(dbPath).reconcileAndBuild(data.string(), {}, overwrite.string(), true);
  sqlite3* db = nullptr;
  ASSERT_EQ(sqlite3_open(dbPath.c_str(), &db), SQLITE_OK);
  ASSERT_EQ(sqlite3_exec(
                db,
                "UPDATE catalog_meta SET value=1 WHERE key='schema_version';"
                "DROP TABLE catalog_roots;",
                nullptr, nullptr, nullptr),
            SQLITE_OK);
  sqlite3_close(db);

  VfsCatalogProgress progress;
  const auto upgraded = VfsCatalog(dbPath).reconcileAndBuild(
      data.string(), {}, overwrite.string(), true,
      [&](const VfsCatalogProgress& value) { progress = value; });
  EXPECT_EQ(progress.files_hashed, 0u);
  EXPECT_EQ(upgraded.provider_roots.size(), 2u);

  ASSERT_EQ(sqlite3_open_v2(dbPath.c_str(), &db, SQLITE_OPEN_READONLY, nullptr),
            SQLITE_OK);
  sqlite3_stmt* stmt = nullptr;
  ASSERT_EQ(sqlite3_prepare_v2(
                db, "SELECT value FROM catalog_meta WHERE key='schema_version';",
                -1, &stmt, nullptr),
            SQLITE_OK);
  ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
  EXPECT_EQ(sqlite3_column_int(stmt, 0), 4);
  sqlite3_finalize(stmt);
  sqlite3_close(db);
}

#ifdef FLUORINE_HAS_BSA_FFI
TEST(VfsCatalog, CatalogsBsaAndBa2MembersAndReusesContentManifests)
{
  TempRoot temp;
  const fs::path data = temp.path() / "Data";
  const fs::path overwrite = temp.path() / "overwrite";
  fs::create_directories(data);
  fs::create_directories(overwrite);

  const fs::path fixtures =
      fs::path(FLUORINE_TEST_SOURCE_DIR) / "libs/libbsarch/examples";
  ASSERT_TRUE(fs::copy_file(fixtures / "test_read.bsa", data / "Example.bsa"));
  ASSERT_TRUE(fs::copy_file(fixtures / "test_read.ba2", data / "Example.ba2"));

  VfsCatalog catalog(temp.path() / "catalog.sqlite");
  VfsCatalogProgress firstProgress;
  const auto first = catalog.reconcileAndBuild(
      data.string(), {}, overwrite.string(), true,
      [&](const VfsCatalogProgress& value) { firstProgress = value; });

  EXPECT_EQ(firstProgress.archives_discovered, 2u);
  EXPECT_EQ(firstProgress.archives_indexed, 2u);
  EXPECT_EQ(firstProgress.archives_reused, 0u);
  EXPECT_EQ(firstProgress.archive_errors, 0u);
  EXPECT_EQ(firstProgress.archive_membership_cache_hits, 0u);
  EXPECT_GT(firstProgress.archive_membership_cache_bytes, 0u);
  EXPECT_GE(firstProgress.archive_workers, 1u);
  EXPECT_LE(firstProgress.archive_workers, 4u);
  ASSERT_NE(first.archive_member_index, nullptr);
  EXPECT_TRUE(first.archive_member_index->complete());
  EXPECT_EQ(first.archive_member_index->archiveCount(), 2u);
  EXPECT_GE(first.archive_member_index->memberCount(), 2u);
  EXPECT_TRUE(first.archive_member_index->mightContain("textures/grass/test.dds"));
  EXPECT_FALSE(first.archive_member_index->mightContain("textures/not-present.dds"));

  VfsCatalogProgress secondProgress;
  const auto second = catalog.reconcileAndBuild(
      data.string(), {}, overwrite.string(), true,
      [&](const VfsCatalogProgress& value) { secondProgress = value; });
  EXPECT_EQ(secondProgress.files_hashed, 0u);
  EXPECT_EQ(secondProgress.archives_discovered, 2u);
  EXPECT_EQ(secondProgress.archives_indexed, 0u);
  EXPECT_EQ(secondProgress.archives_reused, 2u);
  EXPECT_EQ(secondProgress.archive_errors, 0u);
  EXPECT_EQ(secondProgress.archive_workers, 0u);
  EXPECT_EQ(secondProgress.archive_membership_cache_hits, 1u);
  EXPECT_EQ(secondProgress.archive_membership_cache_bytes,
            firstProgress.archive_membership_cache_bytes);
  ASSERT_NE(second.archive_member_index, nullptr);
  EXPECT_TRUE(second.archive_member_index->complete());
  EXPECT_TRUE(second.archive_member_index->mightContain("textures/grass/test.dds"));

  // A different visible archive set must build its own proof rather than
  // accepting the cached absence proof for the original set.
  ASSERT_TRUE(fs::remove(data / "Example.ba2"));
  VfsCatalogProgress reducedProgress;
  const auto reduced = catalog.reconcileAndBuild(
      data.string(), {}, overwrite.string(), true,
      [&](const VfsCatalogProgress& value) { reducedProgress = value; });
  EXPECT_EQ(reducedProgress.archive_membership_cache_hits, 0u);
  ASSERT_NE(reduced.archive_member_index, nullptr);
  EXPECT_EQ(reduced.archive_member_index->archiveCount(), 1u);

  // Returning to the exact original archive content set can reuse its prior
  // compact proof, even though the file itself was recreated.
  ASSERT_TRUE(fs::copy_file(fixtures / "test_read.ba2", data / "Example.ba2"));
  VfsCatalogProgress restoredProgress;
  const auto restored = catalog.reconcileAndBuild(
      data.string(), {}, overwrite.string(), true,
      [&](const VfsCatalogProgress& value) { restoredProgress = value; });
  EXPECT_EQ(restoredProgress.archive_membership_cache_hits, 1u);
  ASSERT_NE(restored.archive_member_index, nullptr);
  EXPECT_EQ(restored.archive_member_index->archiveCount(), 2u);

  // Corruption is rejected and rebuilt from authoritative member rows.
  sqlite3* db = nullptr;
  ASSERT_EQ(sqlite3_open((temp.path() / "catalog.sqlite").c_str(), &db),
            SQLITE_OK);
  ASSERT_EQ(sqlite3_exec(
                db,
                "UPDATE archive_membership_cache SET bits_digest=zeroblob(32);",
                nullptr, nullptr, nullptr),
            SQLITE_OK);
  sqlite3_close(db);
  VfsCatalogProgress repairedProgress;
  const auto repaired = catalog.reconcileAndBuild(
      data.string(), {}, overwrite.string(), true,
      [&](const VfsCatalogProgress& value) { repairedProgress = value; });
  EXPECT_EQ(repairedProgress.archive_membership_cache_hits, 0u);
  EXPECT_TRUE(repaired.archive_member_index->mightContain(
      "textures/grass/test.dds"));
}
#endif

TEST(VfsCatalog, MerkleRootsTrackContentAndPriorityIndependently)
{
  TempRoot temp;
  const fs::path data = temp.path() / "Data";
  const fs::path firstMod = temp.path() / "First";
  const fs::path secondMod = temp.path() / "Second";
  const fs::path overwrite = temp.path() / "overwrite";
  writeFile(data / "base.bin", "base");
  writeFile(firstMod / "one.bin", "one");
  writeFile(secondMod / "two.bin", "two");
  fs::create_directories(overwrite);

  VfsCatalog catalog(temp.path() / "catalog.sqlite");
  auto original = catalog.reconcileAndBuild(
      data.string(), {{"First", firstMod.string()}, {"Second", secondMod.string()}},
      overwrite.string(), true);
  VfsCatalogProgress reorderedProgress;
  auto reordered = catalog.reconcileAndBuild(
      data.string(), {{"Second", secondMod.string()}, {"First", firstMod.string()}},
      overwrite.string(), true,
      [&](const VfsCatalogProgress& value) { reorderedProgress = value; });

  EXPECT_EQ(reorderedProgress.files_hashed, 0u);
  EXPECT_EQ(reorderedProgress.provider_roots_changed, 0u);
  EXPECT_NE(original.profile_root, reordered.profile_root);
  ASSERT_EQ(original.provider_roots.size(), reordered.provider_roots.size());
  for (const auto& before : original.provider_roots) {
    const auto after = std::find_if(
        reordered.provider_roots.begin(), reordered.provider_roots.end(),
        [&](const VfsProviderRoot& root) { return root.root_key == before.root_key; });
    ASSERT_NE(after, reordered.provider_roots.end());
    EXPECT_EQ(before.digest, after->digest);
  }

  writeFile(firstMod / "one.bin", "changed contents");
  VfsCatalogProgress changedProgress;
  auto changed = catalog.reconcileAndBuild(
      data.string(), {{"First", firstMod.string()}, {"Second", secondMod.string()}},
      overwrite.string(), true,
      [&](const VfsCatalogProgress& value) { changedProgress = value; });
  EXPECT_EQ(changedProgress.files_hashed, 1u);
  EXPECT_EQ(changedProgress.provider_roots_changed, 1u);
  EXPECT_NE(original.profile_root, changed.profile_root);
}

TEST(PermissionRepair, IsIdempotentAndDoesNotFollowSymlinks)
{
  TempRoot temp;
  const fs::path game = temp.path() / "game";
  const fs::path directory = game / "subdir";
  const fs::path file = directory / "archive.bin";
  const fs::path outside = temp.path() / "outside.bin";
  writeFile(file, "game data");
  writeFile(outside, "outside");
  ASSERT_EQ(::chmod(game.c_str(), 0500), 0);
  ASSERT_EQ(::chmod(directory.c_str(), 0500), 0);
  ASSERT_EQ(::chmod(file.c_str(), 0400), 0);
  ASSERT_EQ(::chmod(outside.c_str(), 0400), 0);
  std::error_code ec;
  fs::create_symlink(outside, game / "outside-link", ec);
  ASSERT_FALSE(ec);

  const PermissionRepairStats first = repairGameDirectoryPermissions(game);
  EXPECT_EQ(first.repaired, 3u);
  EXPECT_EQ(first.failed, 0u);

  struct stat before {};
  ASSERT_EQ(::lstat(file.c_str(), &before), 0);
  const PermissionRepairStats second = repairGameDirectoryPermissions(game);
  struct stat after {};
  ASSERT_EQ(::lstat(file.c_str(), &after), 0);
  EXPECT_EQ(second.repaired, 0u);
  EXPECT_EQ(second.failed, 0u);
  EXPECT_EQ(before.st_ctim.tv_sec, after.st_ctim.tv_sec);
  EXPECT_EQ(before.st_ctim.tv_nsec, after.st_ctim.tv_nsec);
  EXPECT_EQ(after.st_mode & 0777, 0600);

  struct stat outsideStatus {};
  ASSERT_EQ(::lstat(outside.c_str(), &outsideStatus), 0);
  EXPECT_EQ(outsideStatus.st_mode & 0777, 0400);
}
