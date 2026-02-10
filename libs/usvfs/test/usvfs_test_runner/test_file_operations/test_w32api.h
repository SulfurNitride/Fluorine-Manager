#pragma once

#include "test_filesystem.h"

class TestW32Api : public TestFileSystem
{
public:
  TestW32Api(FILE* output) : TestFileSystem(output) {}

  path real_path(const char* abs_or_rel_path) override;

  FileInfoList list_directory(const path& directory_path) override;

  void create_path(const path& directory_path) override;

  void read_file(const path& file_path) override;

  void write_file(const path& file_path, const void* data, std::size_t size,
                  bool add_new_line, write_mode mode, bool rw_access = false) override;

  void touch_file(const path& file_path, bool full_write_access = false) override;

  void delete_file(const path& file_path) override;

  void copy_file(const path& source_path, const path& destination_path,
                 bool replace_existing) override;

  void rename_file(const path& source_path, const path& destination_path,
                   bool replace_existing, bool allow_copy) override;

  const char* id() override;

private:
  class SafeHandle;
  class SafeFindHandle;
};
