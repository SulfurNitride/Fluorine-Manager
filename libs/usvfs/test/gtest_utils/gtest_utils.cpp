#include "gtest_utils.h"

// this file is shared by both usvfs_global_test and usvfs_global_test_runner
//

#include <filesystem>
#include <format>
#include <fstream>

#include <boost/algorithm/string.hpp>

#include <gtest/gtest.h>

std::string trimmed(std::string content)
{
  boost::algorithm::trim(content);
  return content;
}

// small utility to read files
std::optional<std::string> read_content(const std::filesystem::path& path,
                                        bool trim = true)
{
  std::ifstream ifs(path, std::ios::binary | std::ios::ate);

  if (!ifs) {
    return {};
  }

  const auto count = ifs.tellg();

  std::string buffer(static_cast<std::size_t>(count), '\0');

  ifs.seekg(0, std::ios::beg);
  ifs.read(buffer.data(), count);

  if (trim) {
    boost::algorithm::trim(buffer);
  }

  return buffer;
}

::testing::AssertionResult AssertDirectoryEquals(const std::filesystem::path& expected,
                                                 const std::filesystem::path& actual,
                                                 bool content)
{
  std::vector<std::string> failure_messages;
  std::vector<std::filesystem::path> in_both;

  // iterate left, check on right
  for (const auto& it : std::filesystem::recursive_directory_iterator{expected}) {
    const auto relpath = relative(it.path(), expected);
    if (!exists(actual / relpath)) {
      failure_messages.push_back(
          std::format("{} expected but not found", relpath.string()));
    } else {
      in_both.push_back(relpath);
    }
  }

  // iterate right, check on left
  for (const auto& it : std::filesystem::recursive_directory_iterator{actual}) {
    const auto relpath = relative(it.path(), actual);
    if (!exists(expected / relpath)) {
      failure_messages.push_back(
          std::format("{} found but not expected", relpath.string()));
    }
  }

  // check contents
  if (content) {
    for (const auto& relpath : in_both) {
      const auto expected_path = expected / relpath, actual_path = actual / relpath;

      if (is_directory(expected_path) != is_directory(actual_path)) {
        failure_messages.push_back(
            std::format("{} type mismatch, expected {} but found {}", relpath.string(),
                        is_directory(expected_path) ? "directory" : "file",
                        is_directory(expected_path) ? "file" : "directory"));
        continue;
      }

      if (is_directory(expected_path)) {
        continue;
      }

      if (read_content(expected_path) != read_content(actual_path)) {
        failure_messages.push_back(
            std::format("{} content mismatch", relpath.string()));
      }
    }
  }

  if (failure_messages.empty()) {
    return ::testing::AssertionSuccess();
  }

  return ::testing::AssertionFailure()
         << "\n"
         << boost::algorithm::join(failure_messages, "\n") << "\n";
}

::testing::AssertionResult AssertContentEquals(std::string_view expected,
                                               const std::filesystem::path& path,
                                               bool trim)
{
  const auto content = read_content(path, trim);

  if (!content) {
    return ::testing::AssertionFailure()
           << "failed to open path '" << path.string() << "'";
  }

  if (*content != expected) {
    return ::testing::AssertionFailure()
           << "mismatch content for '" << path.string() << "', expected '" << expected
           << "', found '" << *content << "'";
  }

  return ::testing::AssertionSuccess();
}
