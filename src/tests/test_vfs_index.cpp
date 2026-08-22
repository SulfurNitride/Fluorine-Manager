#include "vfs/vfsindex.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <gtest/gtest.h>
#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unistd.h>

namespace
{
namespace fs = std::filesystem;

class TemporaryDirectory
{
public:
  TemporaryDirectory()
  {
    fs::path pattern =
        fs::temp_directory_path() / "vfs-index-XXXXXX";
    std::string writable = pattern.string();
    writable.push_back('\0');
    char* created = ::mkdtemp(writable.data());
    if (created == nullptr) throw std::runtime_error("mkdtemp failed");
    m_path = created;
  }

  ~TemporaryDirectory()
  {
    std::error_code ignored;
    fs::remove_all(m_path, ignored);
  }

  const fs::path& path() const { return m_path; }

private:
  fs::path m_path;
};

VfsDigest digest(unsigned char seed)
{
  VfsDigest value{};
  for (std::size_t i = 0; i < value.size(); ++i) {
    value[i] = static_cast<unsigned char>(seed + i);
  }
  return value;
}

void touch(const fs::path& path, std::string_view contents = "x")
{
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << contents;
}

struct Sample
{
  TemporaryDirectory temporary;
  fs::path base = temporary.path() / "instance";
  fs::path data = temporary.path() / "game" / "Data";
  fs::path mod = temporary.path() / "mods" / "Example";
  fs::path overwrite = temporary.path() / "overwrite";
  fs::path profile = temporary.path() / "profiles" / "Default";
  VfsTree tree;
  std::vector<VfsProviderRoot> providers;
  VfsDigest profile_digest = digest(90);
  std::shared_ptr<VfsArchiveMemberIndex> archive_members =
      std::make_shared<VfsArchiveMemberIndex>(2, 1);

  Sample()
  {
    fs::create_directories(base);
    touch(data / "Textures" / "Stone.dds", "base");
    touch(data / "Skyrim.esm", "base-only");
    touch(mod / "textures" / "stone.dds", "mod");
    touch(overwrite / "Root.esm", "overwrite");
    touch(profile / "plugins.txt", "*Root.esm");
    touch(mod / "Archives" / "Example.bsa", "archive");
    touch(mod / "Téxtures" / "Å.dds", "unicode");

    tree.root.is_directory = true;
    tree.root.insertFile({"Textures", "Stone.DDS"}, "Textures/Stone.dds", 4,
                         std::chrono::system_clock::time_point(
                             std::chrono::seconds(10)),
                         "_base_game", true, 0644, digest(1));
    tree.root.insertFile({"Skyrim.esm"}, "Skyrim.esm", 9,
                         std::chrono::system_clock::time_point(
                             std::chrono::seconds(15)),
                         "_base_game", true, 0444, digest(8));
    // Same normalized key: later provider content wins, while the first
    // established display spelling remains case-preserved.
    tree.root.insertFile({"textures", "stone.dds"},
                         (mod / "textures" / "stone.dds").string(), 3,
                         std::chrono::system_clock::time_point(
                             std::chrono::seconds(20)),
                         "Example", false, 0640, digest(2));
    tree.root.insertFile({"Root.esm"}, (overwrite / "Root.esm").string(), 9,
                         std::chrono::system_clock::time_point(
                             std::chrono::seconds(30)),
                         "Overwrite", false, 0600, digest(3));
    tree.root.insertFile({"plugins.txt"}, (profile / "plugins.txt").string(),
                         9,
                         std::chrono::system_clock::time_point(
                             std::chrono::seconds(40)),
                         "_profile", false, 0644);
    tree.root.insertFile({"Archives", "Example.bsa"},
                         (mod / "Archives" / "Example.bsa").string(), 7,
                         std::chrono::system_clock::time_point(
                             std::chrono::seconds(50)),
                         "Example", false, 0644, digest(4));
    tree.root.insertFile({"Téxtures", "Å.dds"},
                         (mod / "Téxtures" / "Å.dds").string(), 7,
                         std::chrono::system_clock::time_point(
                             std::chrono::seconds(60)),
                         "Example", false, 0644, digest(5));
    // Neither a stale/mod-provided locator nor output artifacts may be
    // included in the database.
    tree.root.insertFile({"SKSE", "Plugins", "VFSIndexer",
                          kVfsIndexLocatorName},
                         (mod / "fake-locator.json").string(), 1, {},
                         "Example", false, 0644, digest(6));
    tree.root.insertFile({"SKSE", "Plugins", "Fluorine",
                          kLegacyVfsIndexLocatorName},
                         (mod / "legacy-locator.json").string(), 1, {},
                         "Example", false, 0644, digest(6));
    tree.root.insertFile({".vfs-indexer", "index", "old.sqlite3"},
                         (data / ".vfs-indexer/index/old.sqlite3").string(), 1,
                         {}, "_base_game", false, 0644, digest(7));
    tree.root.insertFile({".fluorine", "index", "old.sqlite3"},
                         (data / ".fluorine/index/old.sqlite3").string(), 1,
                         {}, "_base_game", false, 0644, digest(7));
    tree.file_count = 11;

    providers = {
        {data.string(), "_base_game", true, 2, digest(21)},
        {mod.string(), "Example", false, 3, digest(22)},
        {overwrite.string(), "Overwrite", false, 1, digest(23)}};
    archive_members->add("textures/archive-only.dds");
    archive_members->add("sound/archive-only.wav");
  }

  VfsIndexPublicationContext context() const
  {
    return {.output_base=base,
            .producer="Fluorine test",
            .instance_name="Test Instance Ω",
            .profile_name="Default 日本語",
            .consumer_path_style=VfsIndexConsumerPathStyle::Wine};
  }

  VfsIndexPublicationResult publish()
  {
    VfsIndexPublisher publisher;
    return publisher.publish(tree, providers, profile_digest, data, context(),
                             archive_members);
  }
};

QJsonObject readJson(const fs::path& path)
{
  QFile file(QString::fromStdString(path.string()));
  EXPECT_TRUE(file.open(QIODevice::ReadOnly));
  return QJsonDocument::fromJson(file.readAll()).object();
}

void writeJson(const fs::path& path, const QJsonObject& json)
{
  QFile file(QString::fromStdString(path.string()));
  ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  ASSERT_NE(file.write(QJsonDocument(json).toJson()), -1);
}

void execDatabase(const fs::path& path, const char* sql)
{
  sqlite3* database = nullptr;
  ASSERT_EQ(sqlite3_open_v2(path.c_str(), &database,
                            SQLITE_OPEN_READWRITE, nullptr),
            SQLITE_OK);
  char* error = nullptr;
  ASSERT_EQ(sqlite3_exec(database, sql, nullptr, nullptr, &error), SQLITE_OK)
      << (error != nullptr ? error : "");
  sqlite3_free(error);
  sqlite3_close(database);
}

int scalarInt(const fs::path& path, const char* sql)
{
  sqlite3* database = nullptr;
  EXPECT_EQ(sqlite3_open_v2(path.c_str(), &database, SQLITE_OPEN_READONLY,
                            nullptr),
            SQLITE_OK);
  sqlite3_stmt* statement = nullptr;
  EXPECT_EQ(sqlite3_prepare_v2(database, sql, -1, &statement, nullptr),
            SQLITE_OK);
  EXPECT_EQ(sqlite3_step(statement), SQLITE_ROW);
  const int value = sqlite3_column_int(statement, 0);
  sqlite3_finalize(statement);
  sqlite3_close(database);
  return value;
}
}  // namespace

TEST(VfsIndex, PublishesAndValidatesCompleteWineGeneration)
{
  Sample sample;
  const auto publication = sample.publish();
  ASSERT_TRUE(publication.success) << publication.error;
  EXPECT_EQ(publication.file_count, 6u);
  EXPECT_TRUE(fs::exists(publication.database_path));
  EXPECT_TRUE(fs::exists(publication.locator_path));
  EXPECT_FALSE(publication.reused_existing);

  const auto reused = sample.publish();
  ASSERT_TRUE(reused.success) << reused.error;
  EXPECT_TRUE(reused.reused_existing);
  EXPECT_EQ(reused.generation, publication.generation);
  EXPECT_EQ(reused.database_path, publication.database_path);

  const QJsonObject json = readJson(publication.locator_path);
  EXPECT_EQ(json.value("format").toString(), QStringLiteral("vfs-index"));
  EXPECT_EQ(json.value("format_version").toInt(), 1);
  EXPECT_EQ(json.value("schema_version").toInt(), 1);
  EXPECT_EQ(json.value("state").toString(), QStringLiteral("complete"));
  EXPECT_EQ(json.value("path_normalization").toString(),
            QStringLiteral("utf8-nfc-casefold-v1"));
  EXPECT_EQ(json.value("host_path_style").toString(),
            QStringLiteral("posix"));
  EXPECT_TRUE(json.value("host_database_path").toString().startsWith(
      QStringLiteral("/")));
  EXPECT_TRUE(json.value("consumer_database_path").toString().startsWith(
      QStringLiteral("Z:\\")));

  const auto validation =
      VfsIndexValidator::validate(publication.locator_path);
  ASSERT_TRUE(validation) << validation.error;
  ASSERT_EQ(validation.index->files.size(), 6u);
  ASSERT_NE(validation.index->archive_members, nullptr);
  EXPECT_EQ(validation.index->archive_members->archiveCount(), 1u);
  EXPECT_EQ(validation.index->archive_members->memberCount(), 2u);
  EXPECT_TRUE(validation.index->archive_members->mightContain(
      "textures/archive-only.dds"));
  EXPECT_FALSE(validation.index->archive_members->mightContain(
      "textures/not-present.dds"));
  EXPECT_EQ(validation.index->locator.generation, publication.generation);
  EXPECT_EQ(validation.index->locator.instance_name, "Test Instance Ω");
  EXPECT_EQ(validation.index->locator.profile_name, "Default 日本語");

  const auto stone = std::find_if(
      validation.index->files.begin(), validation.index->files.end(),
      [](const VfsIndexResolvedFile& file) {
        return file.normalized_path == "textures/stone.dds";
      });
  ASSERT_NE(stone, validation.index->files.end());
  EXPECT_EQ(stone->origin, "Example");
  EXPECT_EQ(stone->host_path,
            (sample.mod / "textures" / "stone.dds").string());
  EXPECT_EQ(stone->consumer_path,
            VfsIndexPublisher::toConsumerPath(
                sample.mod / "textures" / "stone.dds",
                VfsIndexConsumerPathStyle::Wine));
  ASSERT_TRUE(stone->blake3.has_value());
  EXPECT_EQ(*stone->blake3, digest(2));

  const auto backing = std::find_if(
      validation.index->files.begin(), validation.index->files.end(),
      [](const VfsIndexResolvedFile& file) {
        return file.normalized_path == "skyrim.esm";
      });
  ASSERT_NE(backing, validation.index->files.end());
  EXPECT_TRUE(backing->is_backing);
  EXPECT_EQ(backing->host_path,
            (sample.data / "Skyrim.esm").string());
  EXPECT_EQ(backing->consumer_path,
            VfsIndexPublisher::toConsumerPath(
                sample.data / "Skyrim.esm",
                VfsIndexConsumerPathStyle::Wine));

  const auto profile = std::find_if(
      validation.index->files.begin(), validation.index->files.end(),
      [](const VfsIndexResolvedFile& file) {
        return file.normalized_path == "plugins.txt";
      });
  ASSERT_NE(profile, validation.index->files.end());
  EXPECT_FALSE(profile->blake3.has_value());

  const auto unicode = std::find_if(
      validation.index->files.begin(), validation.index->files.end(),
      [](const VfsIndexResolvedFile& file) {
        return file.normalized_path == "téxtures/å.dds";
      });
  EXPECT_NE(unicode, validation.index->files.end());

  EXPECT_EQ(scalarInt(
                publication.database_path,
                "SELECT COUNT(*) FROM resolved WHERE normalized_path LIKE "
                "'%vfs-index%' OR normalized_path LIKE "
                "'.vfs-indexer/index/%' OR normalized_path LIKE "
                "'.fluorine/index/%';"),
            0);
  EXPECT_EQ(scalarInt(publication.database_path,
                      "SELECT COUNT(*) FROM providers WHERE"
                      " (priority=0 AND role='base_game') OR"
                      " (priority=1 AND role='mod') OR"
                      " (priority=2 AND role='overwrite');"),
            3);
  EXPECT_EQ(scalarInt(publication.database_path,
                      "SELECT COUNT(*) FROM archive_membership;"),
            1);
  EXPECT_EQ(scalarInt(publication.database_path, "PRAGMA application_id;"),
            kVfsIndexApplicationId);
  EXPECT_EQ(scalarInt(publication.database_path, "PRAGMA user_version;"),
            kVfsIndexSchemaVersion);
}

TEST(VfsIndex, TranslatesNativeWindowsAndWinePaths)
{
  EXPECT_EQ(VfsIndexPublisher::toConsumerPath(
                fs::path("C:/Games/Skyrim/Data/File.esm"),
                VfsIndexConsumerPathStyle::NativeWindows),
            R"(C:\Games\Skyrim\Data\File.esm)");
  EXPECT_EQ(VfsIndexPublisher::toConsumerPath(
                fs::path("/home/user/Fluorine/index.sqlite3"),
                VfsIndexConsumerPathStyle::Wine),
            R"(Z:\home\user\Fluorine\index.sqlite3)");
  EXPECT_TRUE(VfsIndexValidator::isAbsoluteConsumerPath(
      R"(\\server\share\index.sqlite3)"));
  EXPECT_FALSE(VfsIndexValidator::isAbsoluteConsumerPath(
      R"(relative\index.sqlite3)"));
}

TEST(VfsIndex, RejectsMalformedIncompleteAndUnsupportedLocators)
{
  TemporaryDirectory temporary;
  const fs::path locator = temporary.path() / kVfsIndexLocatorName;

  std::string error;
  EXPECT_FALSE(VfsIndexValidator::parseLocator(locator, error));
  EXPECT_EQ(error, "locator is missing or unreadable");

  touch(locator, "{not json");
  EXPECT_FALSE(VfsIndexValidator::parseLocator(locator, error));
  EXPECT_EQ(error, "locator is malformed JSON");

  Sample sample;
  auto publication = sample.publish();
  ASSERT_TRUE(publication.success) << publication.error;
  QJsonObject json = readJson(publication.locator_path);
  json.insert("state", "building");
  writeJson(publication.locator_path, json);
  EXPECT_FALSE(VfsIndexValidator::validate(publication.locator_path));

  json.insert("state", "complete");
  json.insert("format_version", 2);
  writeJson(publication.locator_path, json);
  EXPECT_FALSE(VfsIndexValidator::validate(publication.locator_path));

  json.insert("format_version", 1);
  json.insert("consumer_database_path", "relative.sqlite3");
  writeJson(publication.locator_path, json);
  EXPECT_FALSE(VfsIndexValidator::validate(publication.locator_path));
}

TEST(VfsIndex, RejectsGenerationDigestCountSchemaAndCompletionMismatch)
{
  {
    Sample sample;
    const auto publication = sample.publish();
    ASSERT_TRUE(publication.success) << publication.error;
    QJsonObject json = readJson(publication.locator_path);
    json.insert("generation", "00000000-0000-4000-8000-000000000000");
    writeJson(publication.locator_path, json);
    const auto validation =
        VfsIndexValidator::validate(publication.locator_path);
    EXPECT_FALSE(validation);
    EXPECT_NE(validation.error.find("generation"), std::string::npos);
  }
  {
    Sample sample;
    const auto publication = sample.publish();
    ASSERT_TRUE(publication.success) << publication.error;
    execDatabase(publication.database_path,
                 "UPDATE snapshot SET expected_file_count=99;");
    const auto validation =
        VfsIndexValidator::validate(publication.locator_path);
    EXPECT_FALSE(validation);
    EXPECT_NE(validation.error.find("count"), std::string::npos);
  }
  {
    Sample sample;
    const auto publication = sample.publish();
    ASSERT_TRUE(publication.success) << publication.error;
    execDatabase(publication.database_path,
                 "UPDATE snapshot SET state='building';");
    const auto validation =
        VfsIndexValidator::validate(publication.locator_path);
    EXPECT_FALSE(validation);
    EXPECT_NE(validation.error.find("complete"), std::string::npos);
  }
  {
    Sample sample;
    const auto publication = sample.publish();
    ASSERT_TRUE(publication.success) << publication.error;
    execDatabase(publication.database_path, "PRAGMA user_version=2;");
    const auto validation =
        VfsIndexValidator::validate(publication.locator_path);
    EXPECT_FALSE(validation);
    EXPECT_NE(validation.error.find("schema"), std::string::npos);
  }
  {
    Sample sample;
    const auto publication = sample.publish();
    ASSERT_TRUE(publication.success) << publication.error;
    execDatabase(publication.database_path,
                 "UPDATE resolved SET size=size+1 WHERE normalized_path="
                 "'root.esm';");
    const auto validation =
        VfsIndexValidator::validate(publication.locator_path);
    EXPECT_FALSE(validation);
    EXPECT_NE(validation.error.find("digest"), std::string::npos);
  }
  {
    Sample sample;
    const auto publication = sample.publish();
    ASSERT_TRUE(publication.success) << publication.error;
    execDatabase(publication.database_path,
                 "UPDATE archive_membership"
                 " SET bits=zeroblob(length(bits));");
    const auto validation =
        VfsIndexValidator::validate(publication.locator_path);
    EXPECT_FALSE(validation);
    EXPECT_NE(validation.error.find("archive membership proof digest"),
              std::string::npos);
  }
}

TEST(VfsIndex, RejectsCorruptDatabase)
{
  Sample sample;
  const auto publication = sample.publish();
  ASSERT_TRUE(publication.success) << publication.error;
  fs::resize_file(publication.database_path, 64);
  const auto validation =
      VfsIndexValidator::validate(publication.locator_path);
  EXPECT_FALSE(validation);
}

TEST(VfsIndex, InterruptedArtifactsDoNotReplaceActiveLocator)
{
  Sample sample;
  const auto active = sample.publish();
  ASSERT_TRUE(active.success) << active.error;

  // Database publication without locator replacement.
  const fs::path orphan =
      active.database_path.parent_path() /
      "vfs-index-00000000-0000-4000-8000-000000000001.sqlite3";
  fs::copy_file(active.database_path, orphan);
  auto validation = VfsIndexValidator::validate(active.locator_path);
  ASSERT_TRUE(validation) << validation.error;
  EXPECT_EQ(validation.index->locator.generation, active.generation);

  // Incomplete temporary database generation before completion is ignored.
  touch(active.database_path.parent_path() /
            ".vfs-index-00000000-0000-4000-8000-000000000002.tmp.sqlite3",
        "partial");
  validation = VfsIndexValidator::validate(active.locator_path);
  ASSERT_TRUE(validation) << validation.error;
  EXPECT_EQ(validation.index->locator.generation, active.generation);

  // A partial locator beside the active locator is ignored.
  touch(active.locator_path.string() + ".tmp-interrupted", "{");
  validation = VfsIndexValidator::validate(active.locator_path);
  ASSERT_TRUE(validation) << validation.error;
  EXPECT_EQ(validation.index->locator.generation, active.generation);
}

TEST(VfsIndex, OmitsIncompleteArchiveMembershipProof)
{
  Sample sample;
  auto incomplete =
      std::make_shared<VfsArchiveMemberIndex>(1, 0, false);
  incomplete->add("textures/unknown-archive.dds");
  VfsIndexPublisher publisher;
  const auto publication = publisher.publish(
      sample.tree, sample.providers, sample.profile_digest, sample.data,
      sample.context(), incomplete);
  ASSERT_TRUE(publication.success) << publication.error;
  EXPECT_EQ(scalarInt(publication.database_path,
                      "SELECT COUNT(*) FROM archive_membership;"),
            0);

  const auto validation =
      VfsIndexValidator::validate(publication.locator_path);
  ASSERT_TRUE(validation) << validation.error;
  EXPECT_EQ(validation.index->archive_members, nullptr);
}

TEST(VfsIndex, PublicationFailureRemovesVirtualLocatorAndFailsOpen)
{
  Sample sample;
  const fs::path notDirectory = sample.temporary.path() / "not-a-directory";
  touch(notDirectory);
  auto context = sample.context();
  context.output_base = notDirectory;

  VfsIndexPublisher publisher;
  const auto result = publisher.publish(
      sample.tree, sample.providers, sample.profile_digest, sample.data,
      context);
  EXPECT_FALSE(result.success);
  EXPECT_EQ(sample.tree.root.resolve(
                {"SKSE", "Plugins", "VFSIndexer", kVfsIndexLocatorName}),
            nullptr);
  EXPECT_EQ(sample.tree.root.resolve(
                {"SKSE", "Plugins", "Fluorine",
                 kLegacyVfsIndexLocatorName}),
            nullptr);
  // Ordinary resolved files remain available to the normal VFS.
  EXPECT_NE(sample.tree.root.resolve({"Root.esm"}), nullptr);
}

TEST(VfsIndex, RetainsOnlyCurrentAndPreviousCompleteGenerations)
{
  Sample sample;
  std::string previous;
  for (int generation = 0; generation < 4; ++generation) {
    sample.profile_digest =
        digest(static_cast<unsigned char>(30 + generation));
    const auto publication = sample.publish();
    ASSERT_TRUE(publication.success) << publication.error;
    previous = publication.generation;
  }

  const fs::path directory = sample.base / ".vfs-indexer" / "index";
  std::size_t databases = 0;
  for (const auto& entry : fs::directory_iterator(directory)) {
    if (entry.path().filename().string().starts_with("vfs-index-") &&
        entry.path().extension() == ".sqlite3") {
      ++databases;
    }
  }
  EXPECT_EQ(databases, 2u);
  const auto validation =
      VfsIndexValidator::validate(sample.base / kVfsIndexLocatorName);
  ASSERT_TRUE(validation) << validation.error;
  EXPECT_EQ(validation.index->locator.generation, previous);
}
