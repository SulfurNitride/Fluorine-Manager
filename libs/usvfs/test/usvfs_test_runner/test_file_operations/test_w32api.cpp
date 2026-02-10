
#include "test_w32api.h"
#include <cstdio>
#include <cstring>
#include <test_helpers.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

using test::throw_testWinFuncFailed;

class TestW32Api::SafeHandle
{
public:
  SafeHandle(TestFileSystem* tfs, HANDLE handle = NULL) : m_handle(handle), m_tfs(tfs)
  {}
  SafeHandle(const SafeHandle&) = delete;
  SafeHandle(SafeHandle&& other) : m_handle(other.m_handle), m_tfs(other.m_tfs)
  {
    other.m_handle = nullptr;
  }

  operator HANDLE() { return m_handle; }
  operator PHANDLE() { return &m_handle; }
  uint32_t result_for_print()
  {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(m_handle));
  }

  bool valid() const { return m_handle != INVALID_HANDLE_VALUE; }

  ~SafeHandle()
  {
    if (m_handle != INVALID_HANDLE_VALUE) {
      BOOL res = CloseHandle(m_handle);
      if (m_tfs)
        m_tfs->print_result("CloseHandle", res, true);
      if (!res)
        if (m_tfs)
          m_tfs->print_error("CloseHandle", res, true);
        else
          std::fprintf(stderr, "CloseHandle failed : %d", GetLastError());
      m_handle = NULL;
    }
  }

private:
  HANDLE m_handle;
  TestFileSystem* m_tfs;
};

class TestW32Api::SafeFindHandle
{
public:
  SafeFindHandle(TestFileSystem* tfs, HANDLE handle = NULL)
      : m_handle(handle), m_tfs(tfs)
  {}
  SafeFindHandle(const SafeFindHandle&) = delete;
  SafeFindHandle(SafeFindHandle&& other) : m_handle(other.m_handle), m_tfs(other.m_tfs)
  {
    other.m_handle = nullptr;
  }

  operator HANDLE() { return m_handle; }
  operator PHANDLE() { return &m_handle; }
  uint32_t result_for_print()
  {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(m_handle));
  }

  bool valid() const { return m_handle != INVALID_HANDLE_VALUE; }

  ~SafeFindHandle()
  {
    if (m_handle != INVALID_HANDLE_VALUE) {
      BOOL res = FindClose(m_handle);
      if (m_tfs)
        m_tfs->print_result("CloseHandle", res, true);
      if (!res)
        if (m_tfs)
          m_tfs->print_error("CloseHandle", res, true);
        else
          std::fprintf(stderr, "CloseHandle failed : %d", GetLastError());
      m_handle = NULL;
    }
  }

private:
  HANDLE m_handle;
  TestFileSystem* m_tfs;
};

const char* TestW32Api::id()
{
  return "W32";
}

TestW32Api::path TestW32Api::real_path(const char* abs_or_rel_path)
{
  if (!abs_or_rel_path || !abs_or_rel_path[0])
    return path();

  char buf[1024];
  size_t res = GetFullPathNameA(abs_or_rel_path, _countof(buf), buf, NULL);
  if (!res || res >= _countof(buf))
    throw_testWinFuncFailed("GetFullPathNameA", res);
  return buf;
}

TestFileSystem::FileInfoList TestW32Api::list_directory(const path& directory_path)
{
  print_operation("Querying directory", directory_path);

  WIN32_FIND_DATA fd;
  SafeFindHandle find(this, FindFirstFileW((directory_path / L"*").c_str(), &fd));
  print_result("FindFirstFileW", 0, true, nullptr, true);
  if (!find.valid())
    throw_testWinFuncFailed("FindFirstFileW");

  FileInfoList files;
  while (true) {
    files.push_back(
        FileInformation(fd.cFileName, clean_attributes(fd.dwFileAttributes),
                        fd.nFileSizeHigh * (MAXDWORD + 1) + fd.nFileSizeLow));
    BOOL res = FindNextFileW(find, &fd);
    print_result("FindNextFileW", res, true);
    if (!res)
      break;
  }

  return files;
}

void TestW32Api::create_path(const path& directory_path)
{
  // sanity and guaranteed recursion end:
  if (!directory_path.has_relative_path())
    throw std::runtime_error("Refusing to create non-existing top level path");

  print_operation("Checking directory", directory_path);

  DWORD attr = GetFileAttributesW(directory_path.c_str());
  DWORD err  = GetLastError();
  print_result("GetFileAttributesW", clean_attributes(attr), true);
  if (attr != INVALID_FILE_ATTRIBUTES) {
    if (attr & FILE_ATTRIBUTE_DIRECTORY)
      return;  // if directory already exists all is good
    else
      throw std::runtime_error("path exists but not a directory");
  }
  if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND)
    throw_testWinFuncFailed("GetFileAttributesW");

  if (err != ERROR_FILE_NOT_FOUND)  // ERROR_FILE_NOT_FOUND means parent directory
                                    // already exists
    create_path(directory_path
                    .parent_path());  // otherwise create parent directory (recursively)

  print_operation("Creating directory", directory_path);

  BOOL res = CreateDirectoryW(directory_path.c_str(), NULL);
  print_result("CreateDirectoryW", res, true);
  if (!res)
    throw_testWinFuncFailed("CreateDirectoryW");
}

void TestW32Api::read_file(const path& file_path)
{
  print_operation("Reading file", file_path);

  SafeHandle file(this, CreateFileW(file_path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL));
  print_result("CreateFileW", 0, true, nullptr, true);
  if (!file.valid())
    throw_testWinFuncFailed("CreateFileW");

  uint32_t total         = 0;
  bool ends_with_newline = true;
  bool pending_prefix    = true;
  while (true) {
    char buf[4096];

    DWORD read = 0;
    BOOL res   = ReadFile(file, buf, sizeof(buf), &read, NULL);
    print_result("ReadFile", res, true);
    if (!res)
      throw_testWinFuncFailed("ReadFile");
    if (!read)  // eof
      break;

    total += read;
    char* begin = buf;
    char* end   = begin + read;
    while (begin != end) {
      if (pending_prefix) {
        if (output())
          fwrite(FILE_CONTENTS_PRINT_PREFIX, 1, strlen(FILE_CONTENTS_PRINT_PREFIX),
                 output());
        pending_prefix = false;
      }
      bool skip_newline = false;
      char* print_end = reinterpret_cast<char*>(std::memchr(begin, '\n', end - begin));
      if (print_end) {
        pending_prefix = true;
        if (print_end > begin && *(print_end - 1) == '\r') {
          // convert \r\n => \n:
          *(print_end - 1) = '\n';
          skip_newline     = true;
        } else  // only a '\n' so just print it
          ++print_end;
      } else {
        print_end = end;
        if (print_end > begin && *(print_end - 1) == '\r') {
          // buffer ends with \r so skip it under the hope it will be followed with a \n
          --print_end;
          skip_newline = true;
        }
      }
      if (output())
        fwrite(begin, 1, print_end - begin, output());
      ends_with_newline = print_end > begin && *(print_end - 1) == '\n';
      begin             = print_end;
      if (skip_newline)
        ++begin;
    }
    if (output() && !ends_with_newline) {
      fwrite("\n", 1, 1, output());
      ends_with_newline = true;
    }
  }
  if (output()) {
    fprintf(output(), "# Successfully read %u bytes.\n", total);
  }
}

void TestW32Api::write_file(const path& file_path, const void* data, std::size_t size,
                            bool add_new_line, write_mode mode, bool rw_access)
{
  print_operation(write_operation_name(mode), file_path);

  ACCESS_MASK access = GENERIC_WRITE;
  DWORD disposition  = OPEN_EXISTING;
  switch (mode) {
  case write_mode::truncate:
    disposition = TRUNCATE_EXISTING;
    break;
  case write_mode::create:
    disposition = CREATE_NEW;
    break;
  case write_mode::overwrite:
    disposition = CREATE_ALWAYS;
    break;
  case write_mode::opencreate:
    disposition = OPEN_ALWAYS;
    break;
  case write_mode::append:
    disposition = OPEN_ALWAYS;
    access      = FILE_APPEND_DATA;
    break;
  }
  if (rw_access)
    access |= GENERIC_READ;

  SafeHandle file(this, CreateFile(file_path.c_str(), access, 0, NULL, disposition,
                                   FILE_ATTRIBUTE_NORMAL, NULL));
  print_result("CreateFileW", 0, true, nullptr, true);
  if (!file.valid())
    throw_testWinFuncFailed("CreateFile");

  if (mode == write_mode::manual_truncate) {
    BOOL res = SetEndOfFile(file);
    print_result("SetEndOfFile", res, true);
    if (!res)
      throw_testWinFuncFailed("SetEndOfFile");
  }

  if (mode == write_mode::append) {
    DWORD res = SetFilePointer(file, 0, NULL, FILE_END);
    print_result("SetFilePointer(FILE_END)", res, true);
    if (res == INVALID_SET_FILE_POINTER)
      throw_testWinFuncFailed("SetEndOfFile");
  }

  size_t total = 0;

  if (data) {
    // finally write the data:
    DWORD written = 0;
    BOOL res      = WriteFile(file, data, static_cast<DWORD>(size), &written, NULL);
    print_result("WriteFile", written, true);
    if (!res)
      throw_testWinFuncFailed("WriteFile");
    total += written;

    if (add_new_line) {
      res = WriteFile(file, "\r\n", 2, &written, NULL);
      print_result("WriteFile", written, true, "<new line>");
      if (!res)
        throw_testWinFuncFailed("WriteFile");
      total += written;
    }
  }

  print_write_success(data, size, total);
}

void TestW32Api::touch_file(const path& file_path, bool full_write_access)
{
  print_operation("Touching file", file_path);

  SYSTEMTIME st;
  GetSystemTime(&st);
  FILETIME ft;
  if (!SystemTimeToFileTime(&st, &ft))
    throw_testWinFuncFailed("SystemTimeToFileTime");

  auto share_all = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
  auto access    = full_write_access ? GENERIC_WRITE : FILE_WRITE_ATTRIBUTES;
  SafeHandle file(this, CreateFile(file_path.c_str(), access, share_all, NULL,
                                   OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL));
  print_result("CreateFileW", 0, true, nullptr, true);
  if (!file.valid())
    throw_testWinFuncFailed("CreateFile");

  BOOL res = SetFileTime(file, nullptr, nullptr, &ft);
  print_result("SetFileTime", res, true);
  if (!res)
    throw_testWinFuncFailed("SetFileTime");
}

void TestW32Api::delete_file(const path& file_path)
{
  print_operation("Deleting file", file_path);

  BOOL res = DeleteFileW(file_path.c_str());
  print_result("DeleteFileW", res, true);
  if (!res)
    throw_testWinFuncFailed("DeleteFileW");
}

void TestW32Api::copy_file(const path& source_path, const path& destination_path,
                           bool replace_existing)
{
  print_operation(replace_existing ? "Copy file over" : "Copy file", source_path,
                  destination_path);

  BOOL res =
      CopyFileW(source_path.c_str(), destination_path.c_str(), !replace_existing);
  print_result("CopyFileW", res, true);
  if (!res)
    throw_testWinFuncFailed("CopyFileW");
}

void TestW32Api::rename_file(const path& source_path, const path& destination_path,
                             bool replace_existing, bool allow_copy)
{
  print_operation(rename_operation_name(replace_existing, allow_copy), source_path,
                  destination_path);

  DWORD flags = 0;
  if (replace_existing)
    flags |= MOVEFILE_REPLACE_EXISTING;
  if (allow_copy)
    flags |= MOVEFILE_COPY_ALLOWED;

  BOOL res = MoveFileExW(source_path.c_str(), destination_path.c_str(), flags);
  print_result("MoveFileExW", res, true);
  if (!res)
    throw_testWinFuncFailed("MoveFileExW");
}
