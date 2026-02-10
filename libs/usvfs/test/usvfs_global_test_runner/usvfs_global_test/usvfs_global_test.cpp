#include <gtest/gtest.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <filesystem>
#include <fstream>

#include <boost/filesystem.hpp>

#include <gmock/gmock-matchers.h>
#include <gtest_utils.h>

// anonymous class that allow tests to access parameters (currently only where the
// virtualized data folder is)
//
class
{
  bool initialize(int argc, char* argv[])
  {
    // extract the data path
    if (argc != 2) {
      std::cerr << "missing data path, aborting\n";
      return false;
    }
    m_data = std::filesystem::path{argv[1]};

    if (!exists(m_data)) {
      std::cerr << "data path '" << m_data.string() << "'does not exist\n";
      return false;
    }

    return true;
  }

  friend int main(int argc, char* argv[]);
  std::filesystem::path m_data;

public:
  const auto& data() const { return m_data; }
} parameters;

// simple guard for handle
class HandleGuard
{
  HANDLE m_handle = INVALID_HANDLE_VALUE;

public:
  HandleGuard() = default;
  HandleGuard(HANDLE handle) : m_handle{handle} {}

  ~HandleGuard() { close(); }

  operator HANDLE() { return m_handle; }

  void close()
  {
    if (m_handle != INVALID_HANDLE_VALUE) {
      ::CloseHandle(m_handle);
      m_handle = INVALID_HANDLE_VALUE;
    }
  }
};

// simple function to write content to a specified path
void write_content(const std::filesystem::path& path, const std::string_view content)
{
  std::ofstream ofs{path};
  ofs << content;
}

TEST(BasicTest, SimpleTest)
{
  const auto data = parameters.data();

  ASSERT_TRUE(exists(data));
  ASSERT_TRUE(exists(data / "docs"));
  ASSERT_TRUE(exists(data / "empty"));
  ASSERT_TRUE(exists(data / "docs" / "doc.txt"));
  ASSERT_TRUE(exists(data / "readme.txt"));

  // should remove mods/mod1/info.txt
  ASSERT_TRUE(exists(data / "info.txt"));
  remove(data / "info.txt");
  ASSERT_FALSE(exists(data / "info.txt"));

  {
    // retrieve the path of data using GetFinalPathNameByHandleW() to
    // compare later on
    std::filesystem::path dataPathFromHandle;
    {
      HandleGuard hdl = CreateFileW(data.c_str(), GENERIC_READ, 0, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
      ASSERT_NE(INVALID_HANDLE_VALUE, (HANDLE)hdl);

      WCHAR filepath[1024];
      const auto length = GetFinalPathNameByHandleW(
          hdl, filepath, sizeof(filepath) / sizeof(WCHAR), FILE_NAME_NORMALIZED);
      ASSERT_NE(0, length);

      dataPathFromHandle = std::filesystem::path(std::wstring(filepath, length));
    }

    const auto doc_txt = data / "docs" / "doc.txt";
    HandleGuard hdl    = CreateFileW(doc_txt.c_str(), GENERIC_READ, 0, nullptr,
                                     OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    ASSERT_NE(INVALID_HANDLE_VALUE, (HANDLE)hdl);

    WCHAR filepath[1024];
    const auto length = GetFinalPathNameByHandleW(
        hdl, filepath, sizeof(filepath) / sizeof(WCHAR), FILE_NAME_NORMALIZED);
    ASSERT_NE(0, length);

    ASSERT_EQ(dataPathFromHandle / "docs" / "doc.txt",
              std::filesystem::path(std::wstring(filepath, length)));
  }

  {
    // retrieve the path of data using GetFileInformationByHandleEx() to
    // compare later on
    std::filesystem::path dataPathFromHandle;
    {
      HandleGuard hdl = CreateFileW(data.c_str(), GENERIC_READ, 0, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
      ASSERT_NE(INVALID_HANDLE_VALUE, (HANDLE)hdl);

      char buffer[4096];
      ASSERT_TRUE(
          GetFileInformationByHandleEx(hdl, FileNameInfo, buffer, sizeof(buffer)));
      auto* info = reinterpret_cast<FILE_NAME_INFO*>(buffer);

      dataPathFromHandle = std::filesystem::path(
          std::wstring_view(info->FileName, info->FileNameLength / 2));
    }

    const auto info_txt = data / "readme.txt";
    HandleGuard hdl     = CreateFileW(info_txt.c_str(), GENERIC_READ, 0, nullptr,
                                      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    ASSERT_NE(INVALID_HANDLE_VALUE, (HANDLE)hdl);

    char buffer[4096];
    ASSERT_TRUE(
        GetFileInformationByHandleEx(hdl, FileNameInfo, buffer, sizeof(buffer)));

    auto* info = reinterpret_cast<FILE_NAME_INFO*>(buffer);

    ASSERT_EQ(dataPathFromHandle / "readme.txt",
              std::filesystem::path(
                  std::wstring_view(info->FileName, info->FileNameLength / 2)));
  }
}

TEST(BoostFilesystemTest, BoostFilesystemTest)
{
  using namespace boost::filesystem;

  const path data{parameters.data().native()};

  std::vector<std::string> contents;
  for (recursive_directory_iterator it{data}; it != recursive_directory_iterator{};
       ++it) {
    contents.push_back(relative(it->path(), data).string());
  }
  ASSERT_THAT(contents, ::testing::UnorderedElementsAre(
                            ".gitkeep", "docs", "docs\\doc.txt", "docs\\subdocs",
                            "docs\\subdocs\\.gitkeep"));
}

// see https://github.com/ModOrganizer2/modorganizer/issues/2039 for context
//
TEST(RedFileSystemTest, RedFileSystemTest)
{
  const auto data = parameters.data();

  const auto storages_path   = data / "r6" / "storages";
  const auto hudpainter_path = storages_path / "HUDPainter";

  ASSERT_TRUE(exists(hudpainter_path / "DEFAULT.json"));

  // TEST.json does not exist, so will be created in overwrite - this mainly check that
  // weakly_canonical returns the path under data/ and not the actual path under
  // overwrite/
  //
  // this relies on the hook for NtQueryInformationFile
  //
  ASSERT_FALSE(exists(hudpainter_path / "TEST.json"));
  ASSERT_EQ(hudpainter_path / "TEST.json",
            weakly_canonical(hudpainter_path / "TEST.json"));
  write_content(hudpainter_path / "TEST.json", "{}");
}

TEST(SkipFilesTest, SkipFilesTest)
{
  const auto data = parameters.data();

  // file in mod1 should have been skipped
  ASSERT_FALSE(exists(data / "readme.skip"));

  // docs/doc.skip should come from data, not mods1/docs/doc.skip
  ASSERT_CONTENT_EQ("doc.skip in data/docs", data / "docs" / "doc.skip");

  // write to readme.skip should create a file in overwrite
  write_content(data / "readme.skip", "readme.skip in overwrite");
}

int main(int argc, char* argv[])
{
  testing::InitGoogleTest(&argc, argv);

  // initialize parameters from remaining arguments
  if (!parameters.initialize(argc, argv)) {
    return -1;
  }

  return RUN_ALL_TESTS();
}
