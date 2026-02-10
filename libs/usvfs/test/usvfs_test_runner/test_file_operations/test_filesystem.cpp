
#include "test_filesystem.h"
#include <test_helpers.h>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

using test::throw_testWinFuncFailed;

bool TestFileSystem::FileInformation::is_dir() const
{
  return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool TestFileSystem::FileInformation::is_file() const
{
  return (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

TestFileSystem::TestFileSystem(FILE* output) : m_output(output) {}

TestFileSystem::path TestFileSystem::current_directory()
{
  DWORD res = GetCurrentDirectoryW(0, NULL);
  if (!res)
    throw_testWinFuncFailed("GetCurrentDirectory", res);
  std::wstring buf(res + 1, '\0');
  res = GetCurrentDirectoryW(buf.length(), &buf[0]);
  if (!res || res >= buf.length())
    throw_testWinFuncFailed("GetCurrentDirectory", res);
  buf.resize(res);
  return buf;
}

TestFileSystem::path TestFileSystem::relative_path(path full_path)
{
  return test::path_as_relative(m_basepath, full_path);
}

// static
const char* TestFileSystem::write_operation_name(write_mode mode)
{
  switch (mode) {
  case write_mode::manual_truncate:
    return "Writing file (by open & truncate)";
  case write_mode::truncate:
    return "Truncating file";
  case write_mode::create:
    return "Creating file";
  case write_mode::overwrite:
    return "Overwriting file";
  case write_mode::opencreate:
    return "Opening/Creating file";
  case write_mode::append:
    return "Appending file";
  }
  return "Unknown write operation?!";
}

// static
const char* TestFileSystem::rename_operation_name(bool replace_existing,
                                                  bool allow_copy)
{
  if (allow_copy)
    return replace_existing ? "Moving file over" : "Moving file";
  else
    return replace_existing ? "Renaming file over" : "Renaming file";
}

void TestFileSystem::print_operation(const char* operation, const path& target)
{
  if (m_output)
    fprintf(m_output, "# (%s) %s {%s}\n", id(), operation,
            relative_path(target).string().c_str());
}

void TestFileSystem::print_operation(const char* operation, const path& source,
                                     const path& target)
{
  if (m_output)
    fprintf(m_output, "# (%s) %s {%s} {%s}\n", id(), operation,
            relative_path(source).string().c_str(),
            relative_path(target).string().c_str());
}

static inline void print_op_with_result(FILE* output, const char* prefix,
                                        const char* operation, const uint32_t* result,
                                        DWORD* last_error, const char* opt_arg)
{
  if (output) {
    fprintf(output, "%s%s", prefix, operation);
    if (opt_arg)
      fprintf(output, " %s", opt_arg);
    if (result)
      fprintf(output, " returned %u (0x%x)", *result, *result);
    if (last_error)
      fprintf(output, " last error %u (0x%x)", *last_error, *last_error);
    fprintf(output, "\n");
  }
}

void TestFileSystem::print_result(const char* operation, uint32_t result,
                                  bool with_last_error, const char* opt_arg,
                                  bool hide_result)
{
  if (m_output) {
    DWORD last_error   = GetLastError();
    std::string prefix = "# (";
    prefix += id();
    prefix += ")   ";
    print_op_with_result(m_output, prefix.c_str(), operation,
                         hide_result ? nullptr : &result,
                         with_last_error ? &last_error : nullptr, opt_arg);
    SetLastError(last_error);
  }
}

void TestFileSystem::print_error(const char* operation, uint32_t result,
                                 bool with_last_error, const char* opt_arg)
{
  DWORD last_error = with_last_error ? GetLastError() : 0;
  print_op_with_result(stderr, "ERROR: ", operation, &result,
                       with_last_error ? &last_error : nullptr, opt_arg);
  if (m_output && m_output != stdout)
    print_op_with_result(m_output, "ERROR: ", operation, &result,
                         with_last_error ? &last_error : nullptr, opt_arg);
}

void TestFileSystem::print_write_success(const void* data, std::size_t size,
                                         std::size_t written)
{
  if (m_output) {
    fprintf(m_output, "# Successfully written %u bytes ",
            static_cast<unsigned>(written));
    // heuristics to print nicer one liners:
    if (size == 1 && reinterpret_cast<const char*>(data)[0] == '\n' ||
        size == 2 && reinterpret_cast<const char*>(data)[0] == '\r' &&
            reinterpret_cast<const char*>(data)[1] == '\n')
      fprintf(m_output, "<newline>");
    else {
      fprintf(m_output, "{");
      if (size && reinterpret_cast<const char*>(data)[size - 1] == '\n')
        --size;
      if (data)
        fwrite(data, 1, size, m_output);
      else if (size)
        fwrite("NULL?!", 1, 6, m_output);
      fprintf(m_output, "}");
    }
    fprintf(output(), "\n");
  }
}

uint32_t TestFileSystem::clean_attributes(uint32_t attr)
{
  if (m_cleanoutput)
    return attr & ~(FILE_ATTRIBUTE_NOT_CONTENT_INDEXED);
  else
    return attr;
}
