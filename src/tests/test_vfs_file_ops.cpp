#include "vfs/inodetable.h"
#include "vfs/overwritemanager.h"
#include "vfs/vfstree.h"

#include <fcntl.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace
{

class TempRoot
{
public:
  TempRoot()
  {
    char tmpl[] = "/tmp/fluorine-vfs-ops-XXXXXX";
    const char* d = mkdtemp(tmpl);
    if (d != nullptr) {
      m_path = d;
    }
  }

  ~TempRoot()
  {
    if (!m_path.empty()) {
      std::error_code ec;
      fs::remove_all(m_path, ec);
    }
  }

  const fs::path& path() const { return m_path; }

private:
  fs::path m_path;
};

void writeText(const fs::path& path, const std::string& text)
{
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(out.is_open());
  out << text;
}

std::string readText(const fs::path& path)
{
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

}  // namespace

TEST(VfsFileOps, CopyOnWriteFromFdCanCopyBackingSourceToDifferentDestination)
{
  TempRoot tmp;
  ASSERT_FALSE(tmp.path().empty());

  const fs::path backing = tmp.path() / "Data";
  const fs::path staging = tmp.path() / "staging";
  const fs::path overwrite = tmp.path() / "overwrite";
  writeText(backing / "Plugin.esp.save_2026_05_27", "saved plugin");
  writeText(backing / "Plugin.esp", "old plugin");

  const int dirFd = open(backing.c_str(), O_RDONLY | O_DIRECTORY);
  ASSERT_GE(dirFd, 0);

  OverwriteManager overwriteManager(staging.string(), overwrite.string());
  const std::string staged = overwriteManager.copyOnWriteFromFd(
      dirFd, "Plugin.esp.save_2026_05_27", "Plugin.esp");
  close(dirFd);

  EXPECT_EQ(fs::path(staged), staging / "Plugin.esp");
  EXPECT_EQ(readText(staging / "Plugin.esp"), "saved plugin");
}

TEST(VfsFileOps, CreateFileReturnsWritableStagingHandle)
{
  TempRoot tmp;
  ASSERT_FALSE(tmp.path().empty());

  const fs::path staging = tmp.path() / "staging";
  const fs::path overwrite = tmp.path() / "overwrite";

  OverwriteManager overwriteManager(staging.string(), overwrite.string());
  std::string realPath;
  const int fd = overwriteManager.createFile("Data/ShaderCache/Lighting/test.pso",
                                             0600, &realPath);
  ASSERT_GE(fd, 0);

  const char payload[] = "shader";
  ASSERT_EQ(write(fd, payload, sizeof(payload) - 1), ssize_t(sizeof(payload) - 1));
  close(fd);

  EXPECT_EQ(fs::path(realPath), staging / "Data/ShaderCache/Lighting/test.pso");
  EXPECT_EQ(readText(realPath), "shader");
}

TEST(VfsFileOps, CreateShaderCacheFileReusesExistingCaseVariant)
{
  TempRoot tmp;
  ASSERT_FALSE(tmp.path().empty());

  const fs::path staging = tmp.path() / "staging";
  const fs::path overwrite = tmp.path() / "overwrite";
  fs::create_directories(staging / "Data" / "ShaderCache" / "Lighting");

  OverwriteManager overwriteManager(staging.string(), overwrite.string());
  std::error_code ec;
  std::string realPath;
  const int fd = overwriteManager.createFile("data/shadercache/lighting/test.pso",
                                             0600, &realPath, &ec);
  ASSERT_GE(fd, 0);
  ASSERT_FALSE(ec);
  close(fd);

  EXPECT_EQ(fs::path(realPath),
            staging / "Data" / "ShaderCache" / "Lighting" / "test.pso");
}

TEST(VfsFileOps, CreateFileReusesExistingCaseVariant)
{
  TempRoot tmp;
  ASSERT_FALSE(tmp.path().empty());

  const fs::path staging = tmp.path() / "staging";
  const fs::path overwrite = tmp.path() / "overwrite";

  OverwriteManager overwriteManager(staging.string(), overwrite.string());
  std::error_code ec;
  ASSERT_TRUE(overwriteManager.createDirectory("TempProbe", &ec));
  ASSERT_FALSE(ec);

  std::string firstPath;
  int fd = overwriteManager.createFile("TempProbe/.wb_case_test", 0600,
                                       &firstPath, &ec);
  ASSERT_GE(fd, 0);
  ASSERT_FALSE(ec);
  const char first[] = "first";
  ASSERT_EQ(write(fd, first, sizeof(first) - 1), ssize_t(sizeof(first) - 1));
  close(fd);

  std::string secondPath;
  fd = overwriteManager.createFile("tempprobe/.Wb_CaSe_TeSt", 0600,
                                   &secondPath, &ec);
  ASSERT_GE(fd, 0);
  ASSERT_FALSE(ec);
  const char second[] = "second";
  ASSERT_EQ(write(fd, second, sizeof(second) - 1), ssize_t(sizeof(second) - 1));
  close(fd);

  EXPECT_EQ(fs::path(secondPath), fs::path(firstPath));
  EXPECT_EQ(fs::path(firstPath), staging / "TempProbe/.wb_case_test");
  EXPECT_EQ(readText(firstPath), "second");

  size_t childCount = 0;
  for (const auto& entry : fs::directory_iterator(staging / "TempProbe")) {
    (void)entry;
    ++childCount;
  }
  EXPECT_EQ(childCount, 1u);
}

TEST(VfsFileOps, CreateDirectoryReusesExistingCaseVariant)
{
  TempRoot tmp;
  ASSERT_FALSE(tmp.path().empty());

  const fs::path staging = tmp.path() / "staging";
  const fs::path overwrite = tmp.path() / "overwrite";

  OverwriteManager overwriteManager(staging.string(), overwrite.string());
  std::error_code ec;
  ASSERT_TRUE(overwriteManager.createDirectory("TempProbe", &ec));
  ASSERT_FALSE(ec);
  ASSERT_TRUE(overwriteManager.createDirectory("tempprobe", &ec));
  ASSERT_FALSE(ec);

  EXPECT_TRUE(fs::is_directory(staging / "TempProbe"));
  EXPECT_FALSE(fs::exists(staging / "tempprobe"));
}

TEST(VfsFileOps, CreateDirectoryReportsParentFileAsNotDirectory)
{
  TempRoot tmp;
  ASSERT_FALSE(tmp.path().empty());

  const fs::path staging = tmp.path() / "staging";
  const fs::path overwrite = tmp.path() / "overwrite";

  OverwriteManager overwriteManager(staging.string(), overwrite.string());
  std::error_code ec;
  std::string realPath;
  const int fd = overwriteManager.createFile("TempProbe", 0600, &realPath, &ec);
  ASSERT_GE(fd, 0);
  close(fd);
  ASSERT_FALSE(ec);

  EXPECT_FALSE(overwriteManager.createDirectory("tempprobe/child", &ec));
  EXPECT_EQ(ec, std::make_error_code(std::errc::not_a_directory));
}

TEST(VfsFileOps, InodeRenameMovesDescendants)
{
  InodeTable table;
  const uint64_t parent = table.getOrCreate("meshes/actors");
  const uint64_t child = table.getOrCreate("meshes/actors/character/body.nif");

  table.rename("meshes/actors", "meshes/actors_backup");

  EXPECT_EQ(table.get("meshes/actors"), 0u);
  EXPECT_EQ(table.get("meshes/actors_backup"), parent);
  EXPECT_EQ(table.get("meshes/actors_backup/character/body.nif"), child);
  EXPECT_EQ(table.getPath(child), "meshes/actors_backup/character/body.nif");
}

TEST(VfsFileOps, InodeRenameReplacesExistingDestination)
{
  InodeTable table;
  const uint64_t source = table.getOrCreate("config/settings.tmp");
  const uint64_t replaced = table.getOrCreate("config/settings.ini");
  ASSERT_NE(source, replaced);

  table.rename("config/settings.tmp", "config/settings.ini");

  EXPECT_EQ(table.get("config/settings.tmp"), 0u);
  EXPECT_EQ(table.get("config/settings.ini"), source);
  EXPECT_EQ(table.getPath(source), "config/settings.ini");
  EXPECT_TRUE(table.getPath(replaced).empty());
}

TEST(VfsLookupKey, FoldsAsciiAndPathSeparators)
{
  EXPECT_EQ(normalizeForLookup("Meshes\\ARMOR\\Outfit.NIF"), "meshes/armor/outfit.nif");
  EXPECT_EQ(normalizeForLookup(""), "");
}

TEST(VfsLookupKey, FoldsNonAsciiLettersLikeWine)
{
  // Latin-1, Latin Extended-A, Greek, Cyrillic: the names must collide so a
  // case-insensitive mount never needs Wine's directory scan to find them.
  EXPECT_EQ(normalizeForLookup("CAF\xC3\x89.nif"), normalizeForLookup("caf\xC3\xA9.NIF"));
  EXPECT_EQ(normalizeForLookup("\xC5\x81\xC3\x93" "D\xC5\xB9"),
            normalizeForLookup("\xC5\x82\xC3\xB3" "d\xC5\xBA"));
  EXPECT_EQ(normalizeForLookup("\xCE\x91\xCE\x92\xCE\x93"),
            normalizeForLookup("\xCE\xB1\xCE\xB2\xCE\xB3"));
  EXPECT_EQ(normalizeForLookup("\xD0\x9F\xD0\xA0\xD0\x98\xD0\x92\xD0\x95\xD0\xA2"),
            normalizeForLookup("\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82"));
  EXPECT_NE(normalizeForLookup("\xC3\xA9"), normalizeForLookup("e"));
}

TEST(VfsLookupKey, LeavesUncasedAndMalformedBytesAlone)
{
  const std::string cjk = "\xE6\x97\xA5\xE6\x9C\xAC.nif";  // 日本
  EXPECT_EQ(normalizeForLookup(cjk), cjk);
  const std::string malformed = "a\xC3(\xFF\xE2\x82";  // bad continuation, lone 0xFF, truncated
  EXPECT_EQ(normalizeForLookup(malformed), malformed);
  const std::string overlong = "\xC0\xAF";
  EXPECT_EQ(normalizeForLookup(overlong), overlong);
}
