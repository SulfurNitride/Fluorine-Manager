#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include <QTemporaryDir>

#include "archiveoutputpath.h"

namespace {

std::filesystem::path fsPath(const QString &path) {
  return std::filesystem::path(path.toStdString());
}

bool validate(const std::filesystem::path &root, std::wstring_view name,
              bool directory = false) {
  archive_output::ValidatedPath result;
  std::wstring error;
  return archive_output::validate(root, name, directory, result, error);
}

} // namespace

TEST(ArchiveOutputPath, AcceptsContainedPortablePaths) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());

  archive_output::ValidatedPath result;
  std::wstring error;
  ASSERT_TRUE(archive_output::validate(fsPath(temporary.path()),
                                       L"textures\\actors/file.dds", false,
                                       result, error));
  EXPECT_EQ(result.path,
            fsPath(temporary.path()) / "textures" / "actors" / "file.dds");
}

TEST(ArchiveOutputPath, RejectsRootedTraversalAndMalformedPaths) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const auto root = fsPath(temporary.path());

  EXPECT_FALSE(validate(root, L""));
  EXPECT_FALSE(validate(root, L"/outside"));
  EXPECT_FALSE(validate(root, L"\\outside"));
  EXPECT_FALSE(validate(root, L"//server/share"));
  EXPECT_FALSE(validate(root, L"\\\\server\\share"));
  EXPECT_FALSE(validate(root, L"\\\\?\\C:\\outside"));
  EXPECT_FALSE(validate(root, L"C:\\outside"));
  EXPECT_FALSE(validate(root, L"C:/outside"));
  EXPECT_FALSE(validate(root, L"C:outside"));
  EXPECT_FALSE(validate(root, L"../outside"));
  EXPECT_FALSE(validate(root, L"safe/../../outside"));
  EXPECT_FALSE(validate(root, L"safe/./file"));
  EXPECT_FALSE(validate(root, L"safe//file"));
  EXPECT_FALSE(validate(root, L"safe/"));

  const std::wstring embeddedNul{L's', L'a', L'f', L'e', L'\0', L'x'};
  EXPECT_FALSE(validate(root, embeddedNul));
}

TEST(ArchiveOutputPath, RejectsUnsafeOrOverlappingBatchesBeforeMutation) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const auto root = fsPath(temporary.path());
  std::vector<archive_output::ValidatedPath> results;
  std::wstring error;

  EXPECT_FALSE(archive_output::validateAll(root, {L"safe/file", L"../outside"},
                                           false, results, error));
  EXPECT_TRUE(results.empty());
  EXPECT_TRUE(std::filesystem::is_empty(root));

  EXPECT_FALSE(archive_output::validateAll(root, {L"same", L"same"}, false,
                                           results, error));
  EXPECT_FALSE(archive_output::validateAll(root, {L"file", L"file/child"},
                                           false, results, error));
}

TEST(ArchiveOutputPath, RejectsRelativeOrUnsafeExtractionRoots) {
  EXPECT_FALSE(validate("relative", L"file"));

  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const auto root = fsPath(temporary.path());
  const auto real = root / "real";
  const auto link = root / "link";
  ASSERT_TRUE(std::filesystem::create_directory(real));
#ifdef _WIN32
  GTEST_SKIP() << "creating symlinks requires optional Windows privileges";
#else
  ASSERT_NO_THROW(std::filesystem::create_directory_symlink(real, link));
  EXPECT_FALSE(validate(link, L"file"));
#endif
}

TEST(ArchiveOutputPath, RejectsSymlinkedAncestryAndSpecialCollisions) {
  QTemporaryDir temporary;
  QTemporaryDir outsideDirectory;
  ASSERT_TRUE(temporary.isValid());
  ASSERT_TRUE(outsideDirectory.isValid());
  const auto root = fsPath(temporary.path());
  const auto outside = fsPath(outsideDirectory.path());
#ifdef _WIN32
  GTEST_SKIP() << "creating symlinks requires optional Windows privileges";
#else
  ASSERT_NO_THROW(
      std::filesystem::create_directory_symlink(outside, root / "linked"));

  EXPECT_FALSE(validate(root, L"linked/file"));

  std::ofstream(root / "ordinary") << "sentinel";
  EXPECT_FALSE(validate(root, L"ordinary/child"));
  EXPECT_FALSE(validate(root, L"ordinary", true));
  EXPECT_TRUE(validate(root, L"ordinary", false));
#endif
}

#ifdef _WIN32
TEST(ArchiveOutputPath, RejectsWindowsCaseAliasesInOneBatch) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  std::vector<archive_output::ValidatedPath> results;
  std::wstring error;
  EXPECT_FALSE(archive_output::validateAll(
      fsPath(temporary.path()), {L"Foo", L"foo"}, false, results, error));
}
#endif
