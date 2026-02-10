
#include <iostream>
#include <memory>

#include <boost/filesystem.hpp>
#include <boost/type_traits.hpp>
#include <winapi.h>

#include "usvfs_basic_test.h"
#include <stringcast.h>

void print_usage(const std::wstring& exe_name, const std::wstring& test_name)
{
  using namespace std;
  wcerr << "usage: " << exe_name << " [<options>] <scenario>[:<label>]" << endl;
  wcerr << "available options:" << endl;
  wcerr << " -ops32           : force 32bit file opertations process (default is same "
           "bitness)."
        << endl;
  wcerr << " -ops64           : force 64bit file opertations process (default is same "
           "bitness)."
        << endl;
  wcerr << " -opsexe <file>   : full path to file operations executable (overrides "
           "-ops32/64)."
        << endl;
  wcerr << " -opsarg <arg>    : adds argument to the start of all file operations."
        << endl;
  wcerr << " -fixture <dir>   : fixture dir (default is test\\fixtures\\" << test_name
        << "\\<scenario>)." << endl;
  wcerr << " -mapping <file>  : mapping file (default is <fixture "
           "dir>\\vfs_mappings.txt)."
        << endl;
  wcerr << " -temp <dir>      : temp dir (default is test\\temp\\" << test_name
        << "\\<scenario>_<label>)." << endl;
  wcerr << " -mount <dir>     : mount dir (default is <temp dir>\\mount)." << endl;
  wcerr << " -source <dir>    : source dir (default is <temp dir>\\source)." << endl;
  wcerr << " -out <file>      : output file (default is <temp "
           "dir>\\<scenario>_<label>.log)."
        << endl;
  wcerr << " -usvfslog <file> : output file (default is <temp "
           "dir>\\<scenario>_<label>_usvfs.log)."
        << endl;
  wcerr << " -forcetemprecursivedelete : decimate temp dir even if doesn't look like a "
           "temp dir."
        << endl;
  wcerr << endl;
  wcerr << "note: if <label> is ommited the current platform (x86/x64) is used."
        << endl;
  wcerr << "note: mount and source dirs should not exist or be empty directories. if "
           "either of them"
        << endl;
  wcerr << "is not given, the temp dir is first recusrively deleted. to protect "
           "against accidents a"
        << endl;
  wcerr << "heuristic is used to verify the temp dir only has entires which this test "
           "creates."
        << endl;
  wcerr << "-forcetemprecursivedelete can be used to circumvent this heuristic."
        << endl;
}

usvfs_test_base* find_scenario(const std::string& scenario,
                               const usvfs_test_options& options)
{
  if (scenario == usvfs_basic_test::SCENARIO_NAME)
    return new usvfs_basic_test(options);
  else
    return nullptr;
}

bool verify_args_exist(const wchar_t* flag, int params, int index, int count)
{
  if (index + params >= count) {
    std::wcerr << flag << L" requires " << params << L" arguments" << std::endl;
    return false;
  } else
    return true;
}

bool verify_file(const test::path& file)
{
  bool is_dir = false;
  if (!winapi::ex::wide::fileExists(file.c_str(), &is_dir) || is_dir) {
    std::wcerr << L"File does not exist: " << file << std::endl;
    return false;
  } else
    return true;
}

bool verify_dir(const test::path& dir)
{
  bool is_dir = false;
  if (!winapi::ex::wide::fileExists(dir.c_str(), &is_dir) || !is_dir) {
    std::wcerr << L"Directory does not exist: " << dir << std::endl;
    return false;
  } else
    return true;
}

std::wstring get_env_var(const wchar_t* var)
{
  using namespace std;
  size_t size = GetEnvironmentVariableW(var, nullptr, 0);
  if (size) {
    std::wstring value(size, L'\0');
    size = GetEnvironmentVariableW(var, &value[0], static_cast<DWORD>(value.size()));
    if (size && size < value.size()) {
      value.resize(size);
      return value;
    }
  }
  wcerr << L"GetEnvironmentVariableW(" << var << L") failed result=" << size
        << L" error=" << GetLastError() << endl;
  return wstring();
}

bool set_env_var(const wchar_t* var, const std::wstring& value)
{
  if (SetEnvironmentVariableW(var, value.c_str()))
    return true;
  std::wcerr << L"SetEnvironmentVariableW(" << var << L"," << value
             << L") failed error=" << GetLastError() << std::endl;
  return false;
}

int wmain(int argc, wchar_t* argv[])
{
  using namespace std;
  using namespace test;

  const wchar_t* label = nullptr;
  wstring scenario;
  path temp;
  usvfs_test_options options;

  for (int ai = 1; ai < argc; ++ai) {
    if (wcscmp(argv[ai], L"-ops32") == 0)
      options.set_ops32();
    else if (wcscmp(argv[ai], L"-ops64") == 0)
      options.set_ops64();
    else if (wcscmp(argv[ai], L"-opsexe") == 0) {
      if (!verify_args_exist(L"-opsexe", 1, ai, argc))
        return 1;
      options.opsexe = argv[++ai];
      if (!verify_file(options.opsexe))
        return 1;
    } else if (wcscmp(argv[ai], L"-opsarg") == 0) {
      if (!verify_args_exist(L"-opsarg", 1, ai, argc))
        return 1;
      options.add_ops_options(argv[++ai]);
    } else if (wcscmp(argv[ai], L"-fixture") == 0) {
      if (!verify_args_exist(L"-fixture", 1, ai, argc))
        return 1;
      options.fixture = argv[++ai];
      if (!verify_dir(options.fixture))
        return 1;
    } else if (wcscmp(argv[ai], L"-mapping") == 0) {
      if (!verify_args_exist(L"-mapping", 1, ai, argc))
        return 1;
      options.mapping = argv[++ai];
      if (!verify_file(options.mapping))
        return 1;
    } else if (wcscmp(argv[ai], L"-temp") == 0) {
      if (!verify_args_exist(L"-temp", 1, ai, argc))
        return 1;
      options.mount = argv[++ai];
    } else if (wcscmp(argv[ai], L"-mount") == 0) {
      if (!verify_args_exist(L"-mount", 1, ai, argc))
        return 1;
      options.mount = argv[++ai];
    } else if (wcscmp(argv[ai], L"-source") == 0) {
      if (!verify_args_exist(L"-source", 1, ai, argc))
        return 1;
      options.source = argv[++ai];
    } else if (wcscmp(argv[ai], L"-out") == 0) {
      if (!verify_args_exist(L"-out", 1, ai, argc))
        return 1;
      options.output = argv[++ai];
    } else if (wcscmp(argv[ai], L"-usvfslog") == 0) {
      if (!verify_args_exist(L"-usvfslog", 1, ai, argc))
        return 1;
      options.usvfs_log = argv[++ai];
    } else if (wcscmp(argv[ai], L"-forcetemprecursivedelete") == 0)
      options.force_temp_cleanup = true;
    else if (argv[ai][0] == '-') {
      wcerr << L"Unknown option " << argv[ai] << endl;
      return 1;
    } else if (!scenario.empty()) {
      wcerr << L"Multiple scenarios can not be specified: " << scenario.c_str() << L", "
            << argv[ai] << endl;
      return 1;
    } else {
      label = wcschr(argv[ai], ':');
      if (label) {
        scenario = std::wstring(static_cast<const wchar_t*>(argv[ai]), label);
        ++label;  // skip the ':'
      } else
        scenario = argv[ai];

      if (scenario.empty()) {
        wcerr << L"Scenario name can not be empty!" << endl;
        return 1;
      }
    }
  }

  path test_name{"usvfs_test"};

  if (scenario.empty()) {
    print_usage(path(argv[0]).stem(), test_name);
    return 1;
  }

  options.fill_defaults(test_name, scenario, label);

  unique_ptr<usvfs_test_base> test{find_scenario(
      usvfs::shared::string_cast<std::string>(scenario, usvfs::shared::CodePage::UTF8),
      options)};
  if (!test) {
    wcerr << L"ERROR: Unknown scenario specified: " << scenario.c_str() << endl;
    return 2;
  }

  auto dllPath = path_of_usvfs_lib(platform_dependant_executable("usvfs", "dll"));
  ScopedLoadLibrary loadDll(dllPath.c_str());
  if (!loadDll) {
    wcerr << L"ERROR: failed to load usvfs dll: " << dllPath.c_str() << L", "
          << GetLastError() << endl;
    return 3;
  }

  // In order for bin\usvfs_proxy_x??.exe to find the lib\usvfs_x??.dll file we add the
  // lib folder to our path:
  std::wstring env_path = get_env_var(L"Path");
  if (env_path.empty() ||
      !set_env_var(L"Path", dllPath.parent_path().wstring() + L";" + env_path)) {
    wcerr << L"ERROR: failed to add usvfs dll directory to path" << endl;
    return 3;
  }

  return test->run(path(argv[0]).stem());
}
