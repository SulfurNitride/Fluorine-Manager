#pragma once

#include "windows_sane.h"

#include <filesystem>
#include <format>
#include <memory>

namespace test
{

class FuncFailed : public std::runtime_error
{
public:
  FuncFailed(const char* func) : std::runtime_error(msg(func)) {}
  FuncFailed(const char* func, unsigned long res)
      : std::runtime_error(msg(func, nullptr, &res))
  {}
  FuncFailed(const char* func, const char* arg1) : std::runtime_error(msg(func, arg1))
  {}
  FuncFailed(const char* func, const char* arg1, unsigned long res)
      : std::runtime_error(msg(func, arg1, &res))
  {}
  FuncFailed(const char* func, const char* what, const char* arg1)
      : std::runtime_error(msg(func, arg1, nullptr, what))
  {}
  FuncFailed(const char* func, const char* what, const char* arg1, unsigned long res)
      : std::runtime_error(msg(func, arg1, &res, what))
  {}

private:
  std::string msg(std::string_view func, const char* arg1 = nullptr,
                  const unsigned long* res = nullptr, const char* what = nullptr);
};

class WinFuncFailed : public std::runtime_error
{
public:
  using runtime_error::runtime_error;
};

class WinFuncFailedGenerator
{
public:
  WinFuncFailedGenerator(DWORD gle = GetLastError()) : m_gle(gle) {}
  WinFuncFailedGenerator(const WinFuncFailedGenerator&) = delete;

  DWORD lastError() const { return m_gle; }

  WinFuncFailed operator()(std::basic_string_view<char> func)
  {
    return WinFuncFailed(std::format("{} failed : lastError={}", func, m_gle));
  }

  WinFuncFailed operator()(std::basic_string_view<char> func, unsigned long res)
  {
    return WinFuncFailed(
        std::format("{} failed : result=({:#x}), lastError={}", func, res, m_gle));
  }

  WinFuncFailed operator()(std::string_view func, std::basic_string_view<char> arg1)
  {
    return WinFuncFailed(
        std::format("{} failed : {}, lastError={}", func, arg1, m_gle));
  }

  WinFuncFailed operator()(std::string_view func, std::basic_string_view<char> arg1,
                           unsigned long res)
  {
    return WinFuncFailed(std::format("{} failed : {}, result=({:#x}), lastError={}",
                                     func, arg1, res, m_gle));
  }

private:
  DWORD m_gle;
};

// trick to guarantee the evalutation of GetLastError() before the evalution of the
// parameters to the WinFuncFailed message generation
template <class... Args>
[[noreturn]] void throw_testWinFuncFailed(std::string_view func, Args&&... args)
{
  ::test::WinFuncFailedGenerator exceptionGenerator;
  throw exceptionGenerator(func, std::forward<Args>(args)...);
}

class ScopedFILE
{
public:
  // try to open the given filepath with the given mode, if it fails, set err to
  // the return code of _wfopen_s and return a nulled scoped file
  //
  static ScopedFILE open(const std::filesystem::path& filepath, std::wstring_view mode,
                         errno_t& err);

  // same as above but throw a WinFuncFailed() exception if opening the file failed
  //
  static ScopedFILE open(const std::filesystem::path& filepath, std::wstring_view mode);

public:
  ScopedFILE(FILE* f = nullptr) : m_f(f, &fclose) {}

  ScopedFILE(ScopedFILE&& other) noexcept = default;
  ~ScopedFILE()                           = default;

  ScopedFILE(const ScopedFILE&) = delete;

  void close() { m_f = nullptr; }

  operator bool() const { return static_cast<bool>(m_f); }
  operator FILE*() const { return m_f.get(); }

private:
  std::unique_ptr<FILE, decltype(&fclose)> m_f;
};

using std::filesystem::path;

// path functions assume they are called by a test executable
// (calculate the requested path relative to the current executable path)

path path_of_test_bin(const path& relative = path());
path path_of_test_temp(const path& relative = path());
path path_of_test_fixtures(const path& relative = path());
path path_of_usvfs_lib(const path& relative = path());

std::string platform_dependant_executable(const char* name, const char* ext = "exe",
                                          const char* platform = nullptr);

// if full_path is a subfolder of base returns only the relative path,
// if full_path and base are the same folder "." is returned,
// otherwise full_path is returned unchanged
path path_as_relative(const path& base, const path& full_path);

std::vector<char> read_small_file(const path& file, bool binary = true);

// true iff the the contents of the two files is exactly the same
bool compare_files(const path& file1, const path& file2, bool binary = true);

// return true iff the given path is an empty (optionally true also if path doesn't
// exist)
bool is_empty_folder(const path& dpath, bool or_doesnt_exist = false);

void delete_file(const path& file);

// Recursively deletes the given path and all the files and directories under it
// Use with care!!!
void recursive_delete_files(const path& dpath);

// Recursively copies all files and directories from srcPath to destPath
void recursive_copy_files(const path& src_path, const path& dest_path, bool overwrite);

class ScopedLoadLibrary
{
public:
  ScopedLoadLibrary(const wchar_t* dll_path);
  ~ScopedLoadLibrary();

  // returns zero if load library failed
  operator HMODULE() const { return m_mod; }

private:
  HMODULE m_mod;
};
};  // namespace test
