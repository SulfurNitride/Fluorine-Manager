#include <gtest/gtest.h>

#include <gtest_utils.h>
#include <test_helpers.h>
#include <usvfs.h>

#include "usvfs_global_test_fixture.h"

TEST_F(UsvfsGlobalTest, BasicTest)
{
  ASSERT_EQ(0, Run());
  ASSERT_DIRECTORY_EQ(ExpectedFolder(), ActualFolder());
}

TEST_F(UsvfsGlobalTest, BoostFilesystemTest)
{
  ASSERT_EQ(0, Run());
  ASSERT_DIRECTORY_EQ(ExpectedFolder(), ActualFolder());
}

TEST_F(UsvfsGlobalTest, RedFileSystemTest)
{
  SetUpOverwrite(true);
  ASSERT_EQ(0, Run());
  ASSERT_DIRECTORY_EQ(ExpectedFolder(), ActualFolder());
}

TEST_F(UsvfsGlobalTest, SkipFilesTest)
{
  SetUpOverwrite(true);

  usvfsAddSkipFileSuffix(L".skip");

  ASSERT_EQ(0, Run());
  ASSERT_DIRECTORY_EQ(ExpectedFolder(), ActualFolder());
}

int main(int argc, char* argv[])
{
  // load the USVFS DLL
  //
  const auto usvfs_dll =
      test::path_of_usvfs_lib(test::platform_dependant_executable("usvfs", "dll"));
  test::ScopedLoadLibrary loadDll(usvfs_dll.c_str());
  if (!loadDll) {
    std::wcerr << L"ERROR: failed to load usvfs dll: " << usvfs_dll.c_str() << L", "
               << GetLastError() << L"\n";
    return 3;
  }

  testing::InitGoogleTest(&argc, argv);

  UsvfsGlobalTest::ForceUsvfsLogs();

  usvfsInitLogging(false);

  return RUN_ALL_TESTS();
}
