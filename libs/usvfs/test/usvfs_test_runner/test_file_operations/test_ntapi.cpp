
#include "test_ntapi.h"
#include <cstdio>
#include <cstring>
#include <test_helpers.h>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Winternl.h>
#include <stdio.h>

#include "test_ntdll_declarations.h"

class TestNtApi::SafeHandle
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

  bool valid() const { return m_handle != NULL; }

  ~SafeHandle()
  {
    if (m_handle) {
      NTSTATUS status = NtClose(m_handle);
      if (m_tfs)
        m_tfs->print_result("NtClose", status);
      if (!NT_SUCCESS(status))
        if (m_tfs)
          m_tfs->print_error("NtClose", status);
        else
          std::fprintf(stderr, "NtClose failed : 0x%lx", status);
      m_handle = NULL;
    }
  }

private:
  HANDLE m_handle;
  TestFileSystem* m_tfs;
};

const char* TestNtApi::id()
{
  return "Nt";
}

TestNtApi::path TestNtApi::real_path(const char* abs_or_rel_path)
{
  if (!abs_or_rel_path || !abs_or_rel_path[0])
    return path();

  static constexpr char nt_path_prefix[]      = "\\??\\";
  static constexpr wchar_t nt_path_prefix_w[] = L"\\??\\";

  bool path_dos = strncmp(abs_or_rel_path, nt_path_prefix, strlen(nt_path_prefix)) == 0;
  bool path_has_drive = abs_or_rel_path[1] == ':';
  bool path_unc       = abs_or_rel_path[0] == '\\' && abs_or_rel_path[1] == '\\';
  bool path_absolute  = path_has_drive || abs_or_rel_path[0] == '\\';

  path result;
  if (!path_dos) {
    if (!path_unc)
      result.assign(nt_path_prefix_w);
    if (!path_absolute)
      result /= current_directory();
    else if (!path_has_drive && !path_unc)
      // if "absolute" path but without a drive letter (i.e. \windows)
      // the take the drive from the current directory: (i.e. "C:")
      result /= current_directory().root_name();
  }

  int result_size = 0;
  for (auto r : result)
    ++result_size;

  // now append abs_or_rel_path, handling ".." and "." properly:
  path arp{abs_or_rel_path};
  int base_size = path_unc ? 3 : 4;
  for (auto p : arp) {
    if (p == "..") {
      if (result_size > base_size) {  // refuse to remove top level element (i.e.
                                      // \??\C:\ which is 4 elements)
        result.remove_filename();
        --result_size;
      }
    } else if (!p.empty() && p != ".") {
      result /= p;
      ++result_size;
    }
  }

  return result;
}

TestNtApi::SafeHandle TestNtApi::open_directory(const path& directory_path, bool create,
                                                bool allow_non_existence, long* pstatus)
{
  print_operation(create ? "Creating directory" : "Openning directory", directory_path);

  UNICODE_STRING unicode_path;
  RtlInitUnicodeString(&unicode_path, directory_path.c_str());

  OBJECT_ATTRIBUTES attributes;
  InitializeObjectAttributes(&attributes, &unicode_path, OBJ_CASE_INSENSITIVE, NULL,
                             NULL);

  SafeHandle dir(this);
  IO_STATUS_BLOCK iosb;
  NTSTATUS status = NtCreateFile(
      dir, FILE_LIST_DIRECTORY | FILE_TRAVERSE | SYNCHRONIZE, &attributes, &iosb, NULL,
      FILE_ATTRIBUTE_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      create ? FILE_OPEN_IF : FILE_OPEN,
      FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);

  print_result("NtCreateFile", status);

  if (pstatus)
    *pstatus = status;
  if ((status == STATUS_OBJECT_NAME_NOT_FOUND ||
       status == STATUS_OBJECT_PATH_NOT_FOUND) &&
      allow_non_existence)
    return NULL;
  if (!NT_SUCCESS(status))
    throw test::FuncFailed("NtCreateFile", status);
  if (!NT_SUCCESS(iosb.Status))
    throw test::FuncFailed("NtCreateFile", "bad iosb.Status", iosb.Status);

  return dir;
}

TestFileSystem::FileInfoList TestNtApi::list_directory(const path& directory_path)
{
  SafeHandle dir = open_directory(directory_path, false);

  print_operation("Querying directory", directory_path);

  FileInfoList files;
  while (true) {
    char buf[4096]{0};
    IO_STATUS_BLOCK iosb;

    NTSTATUS status =
        NtQueryDirectoryFile(dir, NULL, NULL, NULL, &iosb, buf, sizeof(buf),
                             MyFileBothDirectoryInformation, FALSE, NULL, FALSE);
    print_result("NtQueryDirectoryFile", status);

    if (status == STATUS_NO_MORE_FILES)
      break;
    if (!NT_SUCCESS(status))
      throw test::FuncFailed("NtQueryDirectoryFile", status);
    if (!NT_SUCCESS(iosb.Status))
      throw test::FuncFailed("NtQueryDirectoryFile", "bad iosb.Status", iosb.Status);
    if (iosb.Information == 0)  // This shouldn't happend unless the filename (not full
                                // path) is larger then sizeof(buf)
      throw test::FuncFailed("NtQueryDirectoryFile", "buffer too small",
                             iosb.Information);

    PFILE_BOTH_DIR_INFORMATION info = reinterpret_cast<PFILE_BOTH_DIR_INFORMATION>(buf);
    while (true) {
      files.push_back(FileInformation(
          std::wstring(info->FileName,
                       info->FileNameLength / sizeof(info->FileName[0])),
          clean_attributes(info->FileAttributes), info->EndOfFile.QuadPart));
      if (info->NextEntryOffset)
        info = reinterpret_cast<PFILE_BOTH_DIR_INFORMATION>(
            reinterpret_cast<char*>(info) + info->NextEntryOffset);
      else
        break;
    }
  }

  return files;
}

void TestNtApi::create_path(const path& directory_path)
{
  // sanity and guaranteed recursion end:
  if (!directory_path.has_relative_path())
    throw std::runtime_error("Refusing to create non-existing top level path");

  // if directory already exists all is good
  NTSTATUS status;
  if (open_directory(directory_path, false, true, &status).valid())
    return;

  if (status != STATUS_OBJECT_NAME_NOT_FOUND)  // STATUS_OBJECT_NAME_NOT_FOUND means
                                               // parent directory already exists
    create_path(directory_path
                    .parent_path());  // otherwise create parent directory (recursively)

  open_directory(directory_path, true);
}

void TestNtApi::read_file(const path& file_path)
{
  print_operation("Reading file", file_path);

  UNICODE_STRING unicode_path;
  RtlInitUnicodeString(&unicode_path, file_path.c_str());

  OBJECT_ATTRIBUTES attributes;
  InitializeObjectAttributes(&attributes, &unicode_path, OBJ_CASE_INSENSITIVE, NULL,
                             NULL);

  SafeHandle file(this);
  IO_STATUS_BLOCK iosb;
  NTSTATUS status = NtOpenFile(file, GENERIC_READ | SYNCHRONIZE, &attributes, &iosb,
                               FILE_SHARE_READ, FILE_SYNCHRONOUS_IO_NONALERT);
  print_result("NtOpenFile", status);

  if (!NT_SUCCESS(status))
    throw test::FuncFailed("NtOpenFile", status);
  if (!NT_SUCCESS(iosb.Status))
    throw test::FuncFailed("NtOpenFile", "bad iosb.Status", iosb.Status);

  uint32_t total         = 0;
  bool ends_with_newline = true;
  bool pending_prefix    = true;
  while (true) {
    char buf[4096];

    memset(&iosb, 0, sizeof(iosb));
    status = NtReadFile(file, NULL, NULL, NULL, &iosb, buf, sizeof(buf), NULL, NULL);
    print_result("NtReadFile", status);
    if (status == STATUS_END_OF_FILE)
      break;
    if (!NT_SUCCESS(status))
      throw test::FuncFailed("NtReadFile", status);

    total += iosb.Information;
    char* begin = buf;
    char* end   = begin + iosb.Information;
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

void TestNtApi::write_file(const path& file_path, const void* data, std::size_t size,
                           bool add_new_line, write_mode mode, bool rw_access)
{
  print_operation(write_operation_name(mode), file_path);

  UNICODE_STRING unicode_path;
  RtlInitUnicodeString(&unicode_path, file_path.c_str());

  OBJECT_ATTRIBUTES attributes;
  InitializeObjectAttributes(&attributes, &unicode_path, OBJ_CASE_INSENSITIVE, NULL,
                             NULL);

  ACCESS_MASK access = GENERIC_WRITE | SYNCHRONIZE;
  ULONG disposition  = FILE_OPEN;
  switch (mode) {
  case write_mode::truncate:
    disposition = FILE_OVERWRITE;
    break;
  case write_mode::create:
    disposition = FILE_CREATE;
    break;
  case write_mode::overwrite:
    disposition = FILE_SUPERSEDE;
    break;
  case write_mode::opencreate:
    disposition = FILE_OPEN_IF;
    break;
  case write_mode::append:
    disposition = FILE_OPEN_IF;
    access      = FILE_APPEND_DATA | SYNCHRONIZE;
    break;
  }
  if (rw_access)
    access |= GENERIC_READ;

  SafeHandle file(this);
  IO_STATUS_BLOCK iosb;
  NTSTATUS status =
      NtCreateFile(file, access, &attributes, &iosb, NULL, FILE_ATTRIBUTE_NORMAL, 0,
                   disposition, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
  print_result("NtCreateFile", status);

  if (!NT_SUCCESS(status))
    throw test::FuncFailed("NtCreateFile", status);
  if (!NT_SUCCESS(iosb.Status))
    throw test::FuncFailed("NtCreateFile", "bad iosb.Status", iosb.Status);

  if (mode == write_mode::manual_truncate) {
    FILE_END_OF_FILE_INFORMATION eofinfo{0};
    status = NtSetInformationFile(file, &iosb, &eofinfo, sizeof(eofinfo),
                                  MyFileEndOfFileInformation);
    print_result("NtSetInformationFile", status, false, "EOF");

    if (!NT_SUCCESS(status))
      throw test::FuncFailed("NtSetInformationFile", status);
    if (!NT_SUCCESS(iosb.Status))
      throw test::FuncFailed("NtSetInformationFile", "bad iosb.Status", iosb.Status);
  }

  // finally write the data:
  size_t total = 0;

  if (data) {
    status = NtWriteFile(file, NULL, NULL, NULL, &iosb, const_cast<void*>(data),
                         static_cast<ULONG>(size), NULL, NULL);
    print_result("NtWriteFile", status);
    if (!NT_SUCCESS(status))
      throw test::FuncFailed("NtWriteFile", status);
    if (!NT_SUCCESS(iosb.Status))
      throw test::FuncFailed("NtWriteFile", "bad iosb.Status", iosb.Status);
    total += iosb.Information;

    if (add_new_line) {
      char buffer[] = "\r\n";
      status        = NtWriteFile(file, NULL, NULL, NULL, &iosb, buffer, 2, NULL, NULL);
      print_result("NtWriteFile", status);
      if (!NT_SUCCESS(status))
        throw test::FuncFailed("NtWriteFile", status);
      if (!NT_SUCCESS(iosb.Status))
        throw test::FuncFailed("NtWriteFile", "bad iosb.Status", iosb.Status);
      total += iosb.Information;
    }
  }

  print_write_success(data, size, total);
}

void TestNtApi::touch_file(const path& file_path, bool full_write_access)
{
  print_operation("Touching file", file_path);

  SYSTEMTIME st;
  GetSystemTime(&st);
  FILETIME ft;
  if (!SystemTimeToFileTime(&st, &ft))
    test::throw_testWinFuncFailed("SystemTimeToFileTime");

  UNICODE_STRING unicode_path;
  RtlInitUnicodeString(&unicode_path, file_path.c_str());

  OBJECT_ATTRIBUTES attributes;
  InitializeObjectAttributes(&attributes, &unicode_path, OBJ_CASE_INSENSITIVE, NULL,
                             NULL);

  SafeHandle file(this);
  IO_STATUS_BLOCK iosb;
  auto share_all = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
  auto access =
      (full_write_access ? GENERIC_WRITE : FILE_WRITE_ATTRIBUTES) | SYNCHRONIZE;
  NTSTATUS status =
      NtCreateFile(file, access, &attributes, &iosb, NULL, FILE_ATTRIBUTE_NORMAL,
                   share_all, FILE_OPEN_IF, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
  print_result("NtCreateFile", status);

  if (!NT_SUCCESS(status))
    throw test::FuncFailed("NtCreateFile", status);
  if (!NT_SUCCESS(iosb.Status))
    throw test::FuncFailed("NtCreateFile", "bad iosb.Status", iosb.Status);

  FILE_BASIC_INFORMATION basicinfo{0};
  basicinfo.LastWriteTime.LowPart  = ft.dwLowDateTime;
  basicinfo.LastWriteTime.HighPart = ft.dwHighDateTime;
  status = NtSetInformationFile(file, &iosb, &basicinfo, sizeof(basicinfo),
                                MyFileBasicInformation);
  print_result("NtSetInformationFile", status, false, "Basic");

  if (!NT_SUCCESS(status))
    throw test::FuncFailed("NtSetInformationFile", status);
  if (!NT_SUCCESS(iosb.Status))
    throw test::FuncFailed("NtSetInformationFile", "bad iosb.Status", iosb.Status);
}

void TestNtApi::delete_file(const path& file_path)
{
  print_operation("Deleting file", file_path);

  UNICODE_STRING unicode_path;
  RtlInitUnicodeString(&unicode_path, file_path.c_str());

  OBJECT_ATTRIBUTES attributes;
  InitializeObjectAttributes(&attributes, &unicode_path, OBJ_CASE_INSENSITIVE, NULL,
                             NULL);

  NTSTATUS status = NtDeleteFile(&attributes);
  print_result("NtCreateFile", status);

  if (!NT_SUCCESS(status))
    throw test::FuncFailed("NtDeleteFile", status);
}

void TestNtApi::copy_file(const path& source_path, const path& destination_path,
                          bool replace_existing)
{
  throw test::FuncFailed("copy_file", "ntapi does not support file copy");
}

void TestNtApi::rename_file(const path& source_path, const path& destination_path,
                            bool replace_existing, bool allow_copy)
{
  if (allow_copy)
    throw test::FuncFailed("rename_file", "ntapi does not support file move");

  print_operation(rename_operation_name(replace_existing, allow_copy), source_path,
                  destination_path);

  UNICODE_STRING unicode_path;
  RtlInitUnicodeString(&unicode_path, source_path.c_str());

  OBJECT_ATTRIBUTES attributes;
  InitializeObjectAttributes(&attributes, &unicode_path, OBJ_CASE_INSENSITIVE, NULL,
                             NULL);

  SafeHandle file(this);
  IO_STATUS_BLOCK iosb;
  NTSTATUS status = NtCreateFile(file, FILE_READ_ATTRIBUTES | DELETE | SYNCHRONIZE,
                                 &attributes, &iosb, NULL, FILE_ATTRIBUTE_NORMAL,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
  print_result("NtCreateFile", status);

  if (!NT_SUCCESS(status))
    throw test::FuncFailed("NtCreateFile", status);
  if (!NT_SUCCESS(iosb.Status))
    throw test::FuncFailed("NtCreateFile", "bad iosb.Status", iosb.Status);

  bool dest_full_path = source_path.parent_path() != destination_path.parent_path();
  std::wstring dest   = dest_full_path ? destination_path : destination_path.filename();
  std::vector<char> buf(sizeof(FILE_RENAME_INFORMATION) +
                        sizeof(wchar_t) * dest.length());
  FILE_RENAME_INFORMATION* rename =
      reinterpret_cast<FILE_RENAME_INFORMATION*>(buf.data());
  rename->ReplaceIfExists = replace_existing ? TRUE : FALSE;
  rename->FileNameLength  = sizeof(wchar_t) * dest.length();
  memcpy(&rename->FileName[0], dest.data(), sizeof(wchar_t) * dest.length());

  status =
      NtSetInformationFile(file, &iosb, rename, buf.size(), MyFileRenameInformation);
  print_result("NtSetInformationFile", status, false,
               dest_full_path ? "rename full path" : "rename filename");

  if (!NT_SUCCESS(status))
    throw test::FuncFailed("NtSetInformationFile", status);
  if (!NT_SUCCESS(iosb.Status))
    throw test::FuncFailed("NtSetInformationFile", "bad iosb.Status", iosb.Status);
}
