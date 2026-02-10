#pragma once

#include <format>

#include "test_helpers.h"
#include "winapi.h"

namespace test
{

std::string FuncFailed::msg(std::string_view func, const char* arg1,
                            const unsigned long* res, const char* what)
{
  std::string buffer;
  buffer.reserve(128);

  std::format_to(std::back_inserter(buffer), "{}() {}", func, what ? what : "failed");
  const char* sep = " : ";
  if (arg1) {
    std::format_to(std::back_inserter(buffer), "{}{}", sep, arg1);
    sep = ", ";
  }
  if (res) {
    std::format_to(std::back_inserter(buffer), "{}result = {} ({:#x})", sep, *res,
                   *res);
  }
  return buffer;
}

ScopedFILE ScopedFILE::open(const std::filesystem::path& filepath,
                            std::wstring_view mode, errno_t& err)
{
  FILE* fp = nullptr;
  err      = _wfopen_s(&fp, filepath.c_str(), mode.data());
  if (err || !fp) {
    return ScopedFILE();
  }
  return ScopedFILE(fp);
}

ScopedFILE ScopedFILE::open(const std::filesystem::path& filepath,
                            std::wstring_view mode)
{
  errno_t err;
  auto file = open(filepath, mode, err);
  if (err || !file) {
    throw_testWinFuncFailed("_wfopen_s", filepath.string().c_str(), err);
  }
  return file;
}

path path_of_test_bin(const path& relative)
{
  path base(winapi::wide::getModuleFileName(nullptr));
  return relative.empty() ? base.parent_path() : base.parent_path() / relative;
}

path path_of_test_temp(const path& relative)
{
  return path_of_test_bin().parent_path() / "temp" / relative;
}

path path_of_test_fixtures(const path& relative)
{
  return path_of_test_bin().parent_path() / "fixtures" / relative;
}

path path_of_usvfs_lib(const path& relative)
{
  return path_of_test_bin().parent_path().parent_path() / "lib" / relative;
}

std::string platform_dependant_executable(const char* name, const char* ext,
                                          const char* platform)
{
  std::string res = name;
  res += "_";
  if (platform)
    res += platform;
  else
#if _WIN64
    res += "x64";
#else
    res += "x86";
#endif
  res += ".";
  res += ext;
  return res;
}

path path_as_relative(const path& base, const path& full_path)
{
  auto rel_begin = full_path.begin();
  auto base_iter = base.begin();
  while (rel_begin != full_path.end() && base_iter != base.end() &&
         *rel_begin == *base_iter) {
    ++rel_begin;
    ++base_iter;
  }

  if (base_iter != base.end())  // full_path is not a sub-folder of base
    return full_path;

  if (rel_begin == full_path.end())  // full_path == base
    return path(L".");

  // full_path is a sub-folder of base so take only relative path
  path result;
  for (; rel_begin != full_path.end(); ++rel_begin)
    result /= *rel_begin;
  return result;
}

std::vector<char> read_small_file(const path& file, bool binary)
{
  using namespace std;

  const auto f = ScopedFILE::open(file, binary ? L"rb" : L"rt");

  if (fseek(f, 0, SEEK_END))
    throw_testWinFuncFailed("fseek", (unsigned long)0);

  long size = ftell(f);
  if (size < 0)
    throw_testWinFuncFailed("ftell", (unsigned long)size);
  if (size > 0x10000000)  // sanity check limit to 256M
    throw test::FuncFailed("read_small_file", "file size too large",
                           (unsigned long)size);

  if (fseek(f, 0, SEEK_SET))
    throw_testWinFuncFailed("fseek", (unsigned long)0);

  std::vector<char> content(static_cast<size_t>(size));
  content.resize(fread(content.data(), sizeof(char), content.size(), f));

  return content;
}

bool compare_files(const path& file1, const path& file2, bool binary)
{
  // TODO: if this is ever used for big file should read files in chunks
  return read_small_file(file1, binary) == read_small_file(file2, binary);
}

bool is_empty_folder(const path& dpath, bool or_doesnt_exist)
{
  bool isDir = false;
  if (!winapi::ex::wide::fileExists(dpath.c_str(), &isDir))
    return or_doesnt_exist;

  if (!isDir)
    return false;

  for (const auto& f : winapi::ex::wide::quickFindFiles(dpath.c_str(), L"*"))
    if (f.fileName != L"." && f.fileName != L"..")
      return false;
  return true;
}

void delete_file(const path& file)
{
  if (!DeleteFileW(file.c_str())) {
    auto err = GetLastError();
    if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND)
      throw_testWinFuncFailed("DeleteFile", file.string());
  }
}

void recursive_delete_files(const path& dpath)
{
  bool isDir = false;
  if (!winapi::ex::wide::fileExists(dpath.c_str(), &isDir))
    return;
  if (!isDir)
    delete_file(dpath);
  else {
    // dpath exists and its a directory:
    std::vector<std::wstring> recurse;
    for (const auto& f : winapi::ex::wide::quickFindFiles(dpath.c_str(), L"*")) {
      if (f.fileName == L"." || f.fileName == L"..")
        continue;
      if (f.attributes & FILE_ATTRIBUTE_DIRECTORY)
        recurse.push_back(f.fileName);
      else
        delete_file(dpath / f.fileName);
    }
    for (auto r : recurse)
      recursive_delete_files(dpath / r);
    if (!RemoveDirectoryW(dpath.c_str()))
      throw_testWinFuncFailed("RemoveDirectory", dpath.string().c_str());
  }
  if (winapi::ex::wide::fileExists(dpath.c_str()))
    throw FuncFailed("delete_directory_tree", dpath.string().c_str());
}

void recursive_copy_files(const path& src_path, const path& dest_path, bool overwrite)
{
  bool srcIsDir = false, destIsDir = false;
  if (!winapi::ex::wide::fileExists(src_path.c_str(), &srcIsDir))
    throw FuncFailed("recursive_copy", "source doesn't exist",
                     src_path.string().c_str());
  if (!winapi::ex::wide::fileExists(dest_path.c_str(), &destIsDir) && srcIsDir) {
    winapi::ex::wide::createPath(dest_path.c_str());
    destIsDir = true;
  }

  if (!srcIsDir)
    if (!destIsDir) {
      if (!CopyFileW(src_path.c_str(), dest_path.c_str(), overwrite))
        throw_testWinFuncFailed(
            "CopyFile", (src_path.string() + " => " + dest_path.string()).c_str());
      return;
    } else
      throw FuncFailed("recursive_copy",
                       "source is a file but destination is a directory",
                       (src_path.string() + ", " + dest_path.string()).c_str());

  if (!destIsDir)
    throw FuncFailed("recursive_copy",
                     "source is a directory but destination is a file",
                     (src_path.string() + ", " + dest_path.string()).c_str());

  // source and destination are both directories:
  std::vector<std::wstring> recurse;
  for (const auto& f : winapi::ex::wide::quickFindFiles(src_path.c_str(), L"*")) {
    if (f.fileName == L"." || f.fileName == L"..")
      continue;
    if (f.attributes & FILE_ATTRIBUTE_DIRECTORY)
      recurse.push_back(f.fileName);
    else if (!CopyFileW((src_path / f.fileName).c_str(),
                        (dest_path / f.fileName).c_str(), overwrite))
      throw_testWinFuncFailed("CopyFile", ((src_path / f.fileName).string() + " => " +
                                           (dest_path / f.fileName).string()));
  }
  for (auto r : recurse)
    recursive_copy_files(src_path / r, dest_path / r, overwrite);
}

ScopedLoadLibrary::ScopedLoadLibrary(const wchar_t* dll_path)
    : m_mod(LoadLibrary(dll_path))
{}
ScopedLoadLibrary::~ScopedLoadLibrary()
{
  if (m_mod)
    FreeLibrary(m_mod);
}

};  // namespace test
