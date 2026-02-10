#pragma once

// this file is shared by both usvfs_global_test and usvfs_global_test_runner
//

#include <filesystem>

#include <gtest/gtest.h>

::testing::AssertionResult AssertDirectoryEquals(const std::filesystem::path& expected,
                                                 const std::filesystem::path& actual,
                                                 bool content = true);

::testing::AssertionResult AssertContentEquals(std::string_view expected,
                                               const std::filesystem::path& path,
                                               bool trim = true);

// macro to assert that the contents of two directories are identical - directories are
// compared recursively and file contents are compared (excluding extra spaces or lines
// in file)
//
#define ASSERT_DIRECTORY_EQ(Expected, Actual)                                          \
  ASSERT_TRUE(AssertDirectoryEquals(Expected, Actual))

// macro to assert that the contents of two files are identical (excluding extra space
// or lines in file)
//
#define ASSERT_CONTENT_EQ(Expected, Path)                                              \
  ASSERT_TRUE(AssertContentEquals(Expected, Path))
