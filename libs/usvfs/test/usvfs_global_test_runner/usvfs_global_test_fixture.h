#pragma once

#include <filesystem>
#include <memory>

#include <gtest/gtest.h>

class UsvfsGlobalTest : public testing::Test
{
public:
  // enable log mode - this will generate USVFS log file for all tests regardless of
  // success or failure (default is to only generate for failure)
  //
  static void ForceUsvfsLogs();

public:
  UsvfsGlobalTest();

  void SetUp() override { PrepareFileSystem(); }

  void TearDown() override { CleanUp(); }

  ~UsvfsGlobalTest();

  // setup the overwrite folder
  //
  // if force is true, force creation of the overwrite folder even if not present, this
  // is useful when the overwrite is initially empty and thus cannot be committed to git
  // but need to contain files after the run
  //
  void SetUpOverwrite(bool force = true) const;

  // run the test, return the exit code of the google test process
  //
  int Run() const;

  // return the path to the folder containing the expected results
  //
  std::filesystem::path ActualFolder() const;

  // return the path to the folder containing the expected results
  //
  std::filesystem::path ExpectedFolder() const;

private:
  class UsvfsGuard;

  // always generate usvfs logs
  static bool m_force_usvfs_logs;

  // prepare the filesystem by copying files and folders from the relevant fixtures
  // folder to the temporary folder
  //
  // after this operations, the temporary folder will contain
  // - a data folder
  // - [optional] a mods folder containing a set of folders that should be mounted
  // - [optional] an overwrite folder that should be mounted as overwrite
  //
  void PrepareFileSystem() const;

  // prepare mapping using the given set of paths
  //
  void PrepareMapping() const;

  // cleanup the temporary path
  //
  void CleanUp() const;

  // usvfs_guard
  std::unique_ptr<UsvfsGuard> m_usvfs;

  // name of GTest group (first argument of the TEST macro) to run
  std::wstring m_group;

  // path to the folder containing temporary files
  std::filesystem::path m_temporary_folder;

  // path to the subfolder inside the temporary folder
  std::filesystem::path m_data_folder      = m_temporary_folder / "data";
  std::filesystem::path m_mods_folder      = m_temporary_folder / "mods";
  std::filesystem::path m_overwrite_folder = m_temporary_folder / "overwrite";
};
