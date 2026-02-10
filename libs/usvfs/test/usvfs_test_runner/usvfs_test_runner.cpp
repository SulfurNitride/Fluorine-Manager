#include <gtest/gtest.h>

#include <boost/filesystem.hpp>
#include <iostream>
#include <test_helpers.h>
#include <windows_sane.h>

static std::string usvfs_test_command(const char* scenario, const char* platform,
                                      const char* testflag = nullptr,
                                      const char* opsarg   = nullptr)
{
  using namespace test;
  std::string command =
      path_of_test_bin(platform_dependant_executable("usvfs_test", "exe", platform))
          .string();
  if (testflag) {
    command += " -";
    command += testflag;
  }
  if (opsarg) {
    command += " -opsarg -";
    command += opsarg;
  }
  command += " ";
  command += scenario;
  if (testflag || opsarg) {
    command += ":";
    if (testflag) {
      command += testflag;
      command += "_";
    }
    if (opsarg) {
      command += opsarg;
      command += "_";
    }
    command += platform;
  }
  return command;
}

static DWORD spawn(std::string commandline)
{
  STARTUPINFOA si{0};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{0};

  std::cout << "Running: [" << commandline << "]" << std::endl;
  if (!CreateProcessA(NULL, commandline.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si,
                      &pi)) {
    DWORD gle = GetLastError();
    std::cerr << "CreateProcess failed error=" << gle << std::endl;
    return 98;
  }

  WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD exit = 99;
  if (!GetExitCodeProcess(pi.hProcess, &exit)) {
    DWORD gle = GetLastError();
    std::cerr << "GetExitCodeProcess failed error=" << gle << std::endl;
  }

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  return exit;
}

TEST(UsvfsTest, basic_x64)
{
  EXPECT_EQ(0, spawn(usvfs_test_command("basic", "x64")));
}

TEST(UsvfsTest, basic_x86)
{
  EXPECT_EQ(0, spawn(usvfs_test_command("basic", "x86")));
}

TEST(UsvfsTest, basic_ops32_x64)
{
  EXPECT_EQ(0, spawn(usvfs_test_command("basic", "x64", "ops32")));
}

TEST(UsvfsTest, basic_ops64_x86)
{
  EXPECT_EQ(0, spawn(usvfs_test_command("basic", "x86", "ops64")));
}

/*
TEST(UsvfsTest, basic_ntapi_x64)
{
  EXPECT_EQ(0, spawn(usvfs_test_command("basic", "x64", nullptr, "ntapi")));
}

TEST(UsvfsTest, basic_ntapi_x86)
{
  EXPECT_EQ(0, spawn(usvfs_test_command("basic", "x86", nullptr, "ntapi")));
}
*/

int main(int argc, char** argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
