#include "vfs/stagingpromotion.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <blake3.h>
#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <sys/stat.h>

namespace fs = std::filesystem;

namespace
{
class TempRoot
{
public:
  TempRoot()
  {
    char path[] = "/tmp/fluorine-promotion-XXXXXX";
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

std::string readFile(const fs::path& path)
{
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

VfsDigest digestOf(const std::string& contents)
{
  blake3_hasher hasher;
  blake3_hasher_init(&hasher);
  blake3_hasher_update(&hasher, contents.data(), contents.size());
  VfsDigest digest{};
  blake3_hasher_finalize(&hasher, digest.data(), digest.size());
  return digest;
}

QString hexDigest(const VfsDigest& digest)
{
  static constexpr char digits[] = "0123456789abcdef";
  std::string value(digest.size() * 2, '0');
  for (size_t i = 0; i < digest.size(); ++i) {
    value[i * 2] = digits[digest[i] >> 4];
    value[i * 2 + 1] = digits[digest[i] & 0xf];
  }
  return QString::fromStdString(value);
}

void writeJournal(const fs::path& staging, const fs::path& destination,
                  const std::string& relative, const std::string& staged,
                  const std::optional<std::string>& previous)
{
  QJsonObject file;
  file.insert("path", QString::fromStdString(relative));
  file.insert("digest", hexDigest(digestOf(staged)));
  file.insert("size", static_cast<qint64>(staged.size()));
  file.insert("mode", 0644);
  file.insert("previousDigest", previous
                                    ? QJsonValue(hexDigest(digestOf(*previous)))
                                    : QJsonValue(QJsonValue::Null));
  QJsonObject root;
  root.insert("version", 1);
  root.insert("destination", QString::fromStdString(fs::weakly_canonical(destination).string()));
  root.insert("files", QJsonArray{file});
  std::ofstream stream(staging / StagingPromotion::JournalName,
                       std::ios::binary | std::ios::trunc);
  const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
  stream.write(bytes.constData(), bytes.size());
}
}  // namespace

TEST(StagingPromotion, PromotesReplacementAndVerifiesDestination)
{
  TempRoot temp;
  const fs::path staging = temp.path() / "VFS_staging";
  const fs::path destination = temp.path() / "overwrite";
  writeFile(staging / "config/settings.ini", "new setting\n");
  writeFile(destination / "config/settings.ini", "old setting\n");

  const auto result = StagingPromotion::promote(staging, destination);
  ASSERT_EQ(result.status, StagingPromotionStatus::Promoted);
  ASSERT_EQ(result.files.size(), 1u);
  EXPECT_EQ(result.files.front().relative_path, "config/settings.ini");
  EXPECT_EQ(result.files.front().digest, digestOf("new setting\n"));
  EXPECT_EQ(readFile(destination / "config/settings.ini"), "new setting\n");
  EXPECT_FALSE(fs::exists(staging));
}

TEST(StagingPromotion, ReplaysAValidInterruptedJournal)
{
  TempRoot temp;
  const fs::path staging = temp.path() / "VFS_staging";
  const fs::path destination = temp.path() / "custom-output";
  writeFile(staging / "nested/file.ini", "staged");
  writeFile(destination / "nested/file.ini", "previous");
  writeJournal(staging, destination, "nested/file.ini", "staged", "previous");

  const auto result = StagingPromotion::recover(staging, destination);
  EXPECT_EQ(result.status, StagingPromotionStatus::Recovered);
  EXPECT_EQ(readFile(destination / "nested/file.ini"), "staged");
  EXPECT_FALSE(fs::exists(staging));
}

TEST(StagingPromotion, FinishesCleanupWhenDestinationWasAlreadyInstalled)
{
  TempRoot temp;
  const fs::path staging = temp.path() / "VFS_staging";
  const fs::path destination = temp.path() / "overwrite";
  writeFile(staging / "config/file.ini", "installed");
  writeFile(destination / "config/file.ini", "installed");
  writeJournal(staging, destination, "config/file.ini", "installed", "previous");

  const auto result = StagingPromotion::recover(staging, destination);
  EXPECT_EQ(result.status, StagingPromotionStatus::Recovered);
  ASSERT_EQ(result.files.size(), 1u);
  EXPECT_EQ(result.files.front().digest, digestOf("installed"));
  EXPECT_FALSE(fs::exists(staging));
}

TEST(StagingPromotion, RecoversUnjournaledStagingWhenDestinationIsAbsent)
{
  TempRoot temp;
  const fs::path staging = temp.path() / "VFS_staging";
  const fs::path destination = temp.path() / "overwrite";
  writeFile(staging / "config/orphan.ini", "keep me");

  const auto result = StagingPromotion::recover(staging, destination);
  EXPECT_EQ(result.status, StagingPromotionStatus::Recovered);
  EXPECT_EQ(readFile(destination / "config/orphan.ini"), "keep me");
  EXPECT_FALSE(fs::exists(staging));
}

TEST(StagingPromotion, RecoversArchivedUnjournaledStagingFromOlderVersion)
{
  TempRoot temp;
  const fs::path staging = temp.path() / "VFS_staging";
  const fs::path destination = temp.path() / "overwrite";
  const fs::path recovery = temp.path() / "VFS_recovery/promotion-old";
  writeFile(recovery / "staging/www/save/game.rpgsave", "saved game");
  writeFile(recovery / ".fluorine-unresolved",
            "Fluorine found unjournaled files in VFS_staging. They were preserved and launch was blocked.\n");
  writeFile(recovery / "README.txt", "recovery note\n");

  const auto result = StagingPromotion::recover(staging, destination);
  EXPECT_EQ(result.status, StagingPromotionStatus::Recovered);
  EXPECT_EQ(readFile(destination / "www/save/game.rpgsave"), "saved game");
  EXPECT_FALSE(fs::exists(recovery));
}

TEST(StagingPromotion, KeepsArchivedUnjournaledStagingWhenDestinationDiffers)
{
  TempRoot temp;
  const fs::path staging = temp.path() / "VFS_staging";
  const fs::path destination = temp.path() / "overwrite";
  const fs::path recovery = temp.path() / "VFS_recovery/promotion-old";
  writeFile(recovery / "staging/config/settings.ini", "staged version");
  writeFile(recovery / ".fluorine-unresolved",
            "Fluorine found unjournaled files in VFS_staging. They were preserved and launch was blocked.\n");
  writeFile(destination / "config/settings.ini", "different destination");

  const auto result = StagingPromotion::recover(staging, destination);
  EXPECT_EQ(result.status, StagingPromotionStatus::Blocked);
  EXPECT_EQ(result.recovery_path, recovery);
  EXPECT_EQ(readFile(recovery / "staging/config/settings.ini"), "staged version");
  EXPECT_EQ(readFile(destination / "config/settings.ini"),
            "different destination");
  EXPECT_TRUE(fs::exists(recovery / ".fluorine-unresolved"));
}

TEST(StagingPromotion, BlocksUnjournaledStagingWhenDestinationDiffers)
{
  TempRoot temp;
  const fs::path staging = temp.path() / "VFS_staging";
  const fs::path destination = temp.path() / "overwrite";
  writeFile(staging / "config/orphan.ini", "staged version");
  writeFile(destination / "config/orphan.ini", "newer destination");

  const auto result = StagingPromotion::recover(staging, destination);
  EXPECT_EQ(result.status, StagingPromotionStatus::Blocked);
  EXPECT_EQ(readFile(result.recovery_path / "staging/config/orphan.ini"),
            "staged version");
  EXPECT_EQ(readFile(destination / "config/orphan.ini"), "newer destination");
  EXPECT_FALSE(fs::exists(staging));
  const auto nextLaunch = StagingPromotion::recover(staging, destination);
  EXPECT_EQ(nextLaunch.status, StagingPromotionStatus::Blocked);
}

TEST(StagingPromotion, PreservesBothSidesOnReplayConflict)
{
  TempRoot temp;
  const fs::path staging = temp.path() / "VFS_staging";
  const fs::path destination = temp.path() / "overwrite";
  writeFile(staging / "config/conflict.ini", "staged");
  writeFile(destination / "config/conflict.ini", "unexpected");
  writeJournal(staging, destination, "config/conflict.ini", "staged", "previous");

  const auto result = StagingPromotion::recover(staging, destination);
  EXPECT_EQ(result.status, StagingPromotionStatus::Blocked);
  EXPECT_EQ(readFile(result.recovery_path / "staging/config/conflict.ini"), "staged");
  EXPECT_EQ(readFile(result.recovery_path / "destination/config/conflict.ini"),
            "unexpected");
  EXPECT_EQ(readFile(destination / "config/conflict.ini"), "unexpected");
}
