
#include "usvfs_test_base.h"
#include <boost/filesystem.hpp>
#include <boost/type_traits.hpp>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <future>
#include <iostream>
#include <stringcast.h>
#include <thread>
#include <unordered_set>
#include <usvfs.h>
#include <vector>
#include <winapi.h>

using test::throw_testWinFuncFailed;

// usvfs_test_options class:

void usvfs_test_options::fill_defaults(const path& test_name,
                                       const std::wstring& scenario,
                                       const wchar_t* label)
{
  using namespace test;

  std::wstring lbl;
  if (label)
    lbl = label;

#ifdef _WIN64
  set_ops64();
  if (!label)
    lbl = L"x64";
#else
  set_ops32();
  if (!label)
    lbl = L"x86";
#endif

  std::wstring scenario_label = scenario;
  if (!lbl.empty()) {
    scenario_label += L"_";
    scenario_label += lbl;
  }

  if (fixture.empty())
    fixture = path_of_test_fixtures(test_name / scenario);

  if (mapping.empty())
    mapping = fixture / DEFAULT_MAPPING;

  if (temp.empty()) {
    temp = path_of_test_temp(test_name / scenario_label);
  }

  if (mount.empty()) {
    mount        = temp / MOUNT_DIR;
    temp_cleanup = true;
  }

  if (source.empty()) {
    source       = temp / SOURCE_DIR;
    temp_cleanup = true;
  }

  if (output.empty()) {
    output = temp / scenario_label;
    output += ".log";
  }

  if (usvfs_log.empty()) {
    usvfs_log = temp / scenario_label;
    usvfs_log += "_usvfs.log";
  }
}

void usvfs_test_options::set_ops32()
{
  if (opsexe.empty())
    opsexe = test::path_of_test_bin(L"test_file_operations_x86.exe");
}

void usvfs_test_options::set_ops64()
{
  if (opsexe.empty())
    opsexe = test::path_of_test_bin(L"test_file_operations_x64.exe");
}

void usvfs_test_options::add_ops_options(const std::wstring& options)
{
  if (!ops_options.empty())
    ops_options += L" ";
  ops_options += options;
}

// usvfs_connector helper class:

class usvfs_connector
{
public:
  using path = test::path;

  usvfs_connector(const usvfs_test_options& options)
      : m_usvfs_log(test::ScopedFILE::open(options.usvfs_log, L"wt")),
        m_exit_future(m_exit_signal.get_future())
  {
    winapi::ex::wide::createPath(options.usvfs_log.parent_path().c_str());

    std::wcout << "Connecting VFS..." << std::endl;

    std::unique_ptr<usvfsParameters, decltype(&usvfsFreeParameters)> parameters{
        usvfsCreateParameters(), &usvfsFreeParameters};

    usvfsSetInstanceName(parameters.get(), "usvfs_test");
    usvfsSetDebugMode(parameters.get(), false);
    usvfsSetLogLevel(parameters.get(), LogLevel::Debug);
    usvfsSetCrashDumpType(parameters.get(), CrashDumpsType::None);
    usvfsSetCrashDumpPath(parameters.get(), "");

    usvfsInitLogging(false);
    usvfsCreateVFS(parameters.get());

    m_log_thread = std::thread(&usvfs_connector::usvfs_logger, this);
  }

  ~usvfs_connector()
  {
    usvfsDisconnectVFS();
    m_exit_signal.set_value();
    m_log_thread.join();
  }

  enum class map_type
  {
    none,  // the mapping_reader uses this value but will never pass it to the
           // usvfs_connector (which ignored such entries)
    dir,
    dircreate,
    file
  };

  struct mapping
  {
    mapping(map_type itype, std::wstring isource, std::wstring idestination)
        : type(itype), source(isource), destination(idestination)
    {}

    map_type type;
    path source;
    path destination;
  };

  using mappings_list = std::vector<mapping>;

  void updateMapping(const mappings_list& mappings, const usvfs_test_options& options,
                     FILE* log)
  {
    using namespace std;
    using namespace usvfs::shared;

    fprintf(log, "Updating VFS mappings:\n");

    usvfsClearVirtualMappings();

    for (const auto& map : mappings) {
      const string& source =
          usvfs_test_base::SOURCE_LABEL +
          test::path_as_relative(options.source, map.source).string();
      const string& destination =
          usvfs_test_base::MOUNT_LABEL +
          test::path_as_relative(options.mount, map.destination).string();
      switch (map.type) {
      case map_type::dir:
        fprintf(log, "  mapdir: %s => %s\n", source.c_str(), destination.c_str());
        usvfsVirtualLinkDirectoryStatic(map.source.c_str(), map.destination.c_str(),
                                        LINKFLAG_RECURSIVE);
        break;

      case map_type::dircreate:
        fprintf(log, "  mapdircreate: %s => %s\n", source.c_str(), destination.c_str());
        usvfsVirtualLinkDirectoryStatic(map.source.c_str(), map.destination.c_str(),
                                        LINKFLAG_CREATETARGET | LINKFLAG_RECURSIVE);
        break;

      case map_type::file:
        fprintf(log, "  mapfile: %s => %s\n", source.c_str(), destination.c_str());
        usvfsVirtualLinkFile(map.source.c_str(), map.destination.c_str(),
                             LINKFLAG_RECURSIVE);
        break;
      }
    }

    fprintf(log, "\n");
  }

  static DWORD spawn(wchar_t* commandline)
  {
    using namespace usvfs::shared;

    STARTUPINFO si{0};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{0};

    if (!usvfsCreateProcessHooked(NULL, commandline, NULL, NULL, FALSE, 0, NULL, NULL,
                                  &si, &pi))
      throw_testWinFuncFailed(
          "CreateProcessHooked",
          string_cast<std::string>(commandline, CodePage::UTF8).c_str());

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit = 99;
    if (!GetExitCodeProcess(pi.hProcess, &exit)) {
      test::WinFuncFailedGenerator failed;
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
      throw failed("GetExitCodeProcess");
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return exit;
  }

  void usvfs_logger()
  {
    fprintf(m_usvfs_log, "usvfs_test usvfs logger started:\n");
    fflush(m_usvfs_log);

    constexpr size_t size = 1024;
    char buf[size + 1]{0};
    int noLogCycles = 0;
    std::chrono::milliseconds wait_for;
    do {
      if (usvfsGetLogMessages(buf, size, false)) {
        fwrite(buf, 1, strlen(buf), m_usvfs_log);
        fwrite("\n", 1, 1, m_usvfs_log);
        fflush(m_usvfs_log);
        noLogCycles = 0;
      } else
        ++noLogCycles;
      if (noLogCycles)
        wait_for = std::chrono::milliseconds(std::min(40, noLogCycles) * 5);
      else
        wait_for = std::chrono::milliseconds(0);
    } while (m_exit_future.wait_for(wait_for) == std::future_status::timeout);

    while (usvfsGetLogMessages(buf, size, false)) {
      fwrite(buf, 1, strlen(buf), m_usvfs_log);
      fwrite("\n", 1, 1, m_usvfs_log);
    }

    fprintf(m_usvfs_log, "usvfs log closed.\n");
    m_usvfs_log.close();
  }

private:
  test::ScopedFILE m_usvfs_log;
  std::thread m_log_thread;
  std::promise<void> m_exit_signal;
  std::shared_future<void> m_exit_future;
};

// mappings_reader

class mappings_reader
{
public:
  using path          = test::path;
  using string        = std::string;
  using wstring       = std::wstring;
  using map_type      = usvfs_connector::map_type;
  using mapping       = usvfs_connector::mapping;
  using mappings_list = usvfs_connector::mappings_list;

  mappings_reader(const path& mount_base, const path& source_base)
      : m_mount_base(mount_base), m_source_base(source_base)
  {}

  mappings_list read(const path& mapfile)
  {
    const auto map = test::ScopedFILE::open(mapfile, L"rt");
    mappings_list mappings;

    char line[1024];
    while (!feof(map)) {
      // read one line:
      if (!fgets(line, _countof(line), map))
        if (feof(map))
          break;
        else
          throw_testWinFuncFailed("fgets", "reading mappings");

      if (empty_line(line))
        continue;

      if (start_nesting(line, "mapdir"))
        m_nesting = map_type::dir;
      else if (start_nesting(line, "mapdircreate"))
        m_nesting = map_type::dircreate;
      else if (start_nesting(line, "mapfile"))
        m_nesting = map_type::file;
      else if (!isspace(*line))  // mapping sources should be indented and we already
                                 // check all the mapping directives
        throw test::FuncFailed("mappings_reader::read", "invalid mappings file line",
                               line);
      else {
        const auto& source_rel = trimmed_wide_string(line);
        mappings.push_back(mapping(m_nesting, m_source_base / source_rel, m_mount));
      }
    }

    return mappings;
  }

  bool start_nesting(const char* line, const char* directive)
  {
    // check if line starts with directive and if so skip it:
    auto dlen  = strlen(directive);
    auto after = line + dlen;
    if (strncmp(directive, line, dlen) == 0 && (!*after || isspace(*after))) {
      m_mount               = m_mount_base;
      const auto& mount_rel = trimmed_wide_string(after);
      if (!mount_rel.empty())
        m_mount /= mount_rel;
      return true;
    } else
      return false;
  }

  static wstring trimmed_wide_string(const char* in)
  {
    while (std::isspace(*in))
      ++in;
    auto end = in;
    end += strlen(end);
    while (end > in && std::isspace(*(end - 1)))
      --end;
    return usvfs::shared::string_cast<wstring>(string(in, end),
                                               usvfs::shared::CodePage::UTF8);
  }

  static bool empty_line(const char* line)
  {
    for (; *line; ++line) {
      if (*line == '#')  // comment, ignore rest of line
        return true;
      else if (!std::isspace(*line))
        return false;
    }
    return true;
  }

private:
  path m_mount_base;
  path m_source_base;
  path m_mount;
  map_type m_nesting = map_type::none;
};

// usvfs_test_base class:

void usvfs_test_base::cleanup_temp()
{
  using namespace test;
  using namespace winapi::ex::wide;

  bool isDir = false;
  if (!m_o.temp_cleanup || !fileExists(m_o.temp.c_str(), &isDir))
    return;

  if (!isDir) {
    if (m_o.force_temp_cleanup)
      delete_file(m_o.temp);
    else
      throw FuncFailed("cleanup_temp", "temp exists but is a file",
                       m_o.temp.string().c_str());
  } else {
    std::vector<wstring> cleanfiles;
    std::vector<wstring> cleandirs;
    std::vector<wstring> otherdirs;
    bool output_file = false;
    for (auto f : quickFindFiles(m_o.temp.c_str(), L"*"))
      if (f.fileName == L"." || f.fileName == L"..")
        continue;
      else if ((f.attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        if (f.fileName == m_o.output.filename())
          output_file = true;
        cleanfiles.push_back(f.fileName);
      } else if (f.fileName == SOURCE_DIR || f.fileName == MOUNT_DIR)
        cleandirs.push_back(f.fileName);
      else
        otherdirs.push_back(f.fileName);
    if (!cleanfiles.empty() || !cleandirs.empty() || !otherdirs.empty()) {
      if (!m_o.force_temp_cleanup && !otherdirs.empty())
        throw FuncFailed("cleanup_temp",
                         "Refusing to clean temp dir with non-mount/source directories "
                         "(clean manually and rerun)",
                         m_o.temp.string().c_str());
      if (!m_o.force_temp_cleanup && cleandirs.empty() && !output_file)
        throw FuncFailed("cleanup_temp",
                         "Refusing to clean temp dir with no directories and no output "
                         "log (clean manually and rerun)",
                         m_o.temp.string().c_str());
      std::wcout << "Cleaning previous temp dir: " << m_o.temp.c_str() << std::endl;
      for (auto f : cleanfiles)
        delete_file(m_o.temp / f);
      for (auto d : cleandirs)
        recursive_delete_files(m_o.temp / d);
      for (auto d : otherdirs)
        recursive_delete_files(m_o.temp / d);
    }
  }
}

void usvfs_test_base::copy_fixture()
{
  using namespace test;
  using namespace winapi::ex::wide;

  path fmount  = m_o.fixture / MOUNT_DIR;
  path fsource = m_o.fixture / SOURCE_DIR;

  bool isDir = false;
  if (!fileExists(fmount.c_str(), &isDir) || !isDir)
    throw FuncFailed("copy_fixture", "fixtures dir does not exist",
                     fmount.string().c_str());
  if (!fileExists(fsource.c_str(), &isDir) || !isDir)
    throw FuncFailed("copy_fixture", "fixtures dir does not exist",
                     fsource.string().c_str());
  if (fileExists(m_o.mount.c_str(), &isDir))
    throw FuncFailed("copy_fixture", "source dir already exists",
                     m_o.mount.string().c_str());
  if (fileExists(m_o.source.c_str(), &isDir))
    throw FuncFailed("copy_fixture", "source dir already exists",
                     m_o.source.string().c_str());

  std::wcout << "Copying fixture: " << m_o.fixture << std::endl;
  recursive_copy_files(fmount, m_o.mount, false);
  recursive_copy_files(fsource, m_o.source, false);
}

bool usvfs_test_base::postmortem_check()
{
  path gold_output = m_o.fixture / m_o.output.filename();

  {
    const auto& log = output();

    path mount_gold = MOUNT_DIR;
    mount_gold += POSTMORTEM_SUFFIX;
    path source_gold = SOURCE_DIR;
    source_gold += POSTMORTEM_SUFFIX;

    bool is_dir = false;
    if (!winapi::ex::wide::fileExists(m_o.mount.c_str(), &is_dir) || !is_dir) {
      fprintf(log, "  ERROR: mount directory does not exist?!\n");
      return false;
    }
    if (!winapi::ex::wide::fileExists(m_o.source.c_str(), &is_dir) || !is_dir) {
      fprintf(log, "  ERROR: source directory does not exist?!\n");
      return false;
    }
    if (!winapi::ex::wide::fileExists((m_o.fixture / mount_gold).c_str(), &is_dir) ||
        !is_dir) {
      fprintf(log, "  ERROR: fixtures golden mount does not exist: %s\n",
              mount_gold.string().c_str());
      return false;
    }
    if (!winapi::ex::wide::fileExists((m_o.fixture / source_gold).c_str(), &is_dir) ||
        !is_dir) {
      fprintf(log, "  ERROR: fixtures golden source does not exist: %s\n",
              mount_gold.string().c_str());
      return false;
    }
    if (!winapi::ex::wide::fileExists(gold_output.c_str(), &is_dir) || is_dir) {
      fprintf(log, "  ERROR: golden scenario output does not exist: %s\n",
              gold_output.filename().string().c_str());
      return false;
    }

    fprintf(log, "postmortem check of %s against golden %s...\n",
            m_o.mount.filename().string().c_str(), mount_gold.string().c_str());
    bool mount_check =
        recursive_compare_dirs(path(), m_o.fixture / mount_gold, m_o.mount, log);

    fprintf(log, "postmortem check of %s against golden %s...\n",
            m_o.source.filename().string().c_str(), source_gold.string().c_str());
    bool source_check =
        recursive_compare_dirs(path(), m_o.fixture / source_gold, m_o.source, log);

    if (mount_check && source_check)
      fprintf(log, "postmortem check successful.\n");
    else {
      fprintf(log, "ERROR: postmortem check failed!\n");
      return false;
    }
  }  // close output before comparing it

  // don't print anything more to the output (except maybe errors),
  // so that the final output can be copied as is to the fixtures (when updating the
  // golden version)

  // block remove 2024/06/07 - one would need to regenerate a gold output for this but
  // I am not sure this is still relevant
  //
  // if (!test::compare_files(gold_output, m_o.output, false)) {
  //  fprintf(output(), "ERROR: output does not match gold output: %s\n",
  //  m_o.output.filename().string().c_str()); return false;
  //}

  return true;
}

bool usvfs_test_base::recursive_compare_dirs(path rel_path, path gold_base,
                                             path result_base, FILE* log)
{
  path result_full = result_base / rel_path;
  path gold_full   = gold_base / rel_path;

  std::unordered_set<std::wstring> gold_dirs;
  std::unordered_set<std::wstring> gold_files;
  for (const auto& f : winapi::ex::wide::quickFindFiles(gold_full.c_str(), L"*")) {
    if (f.fileName == L"." || f.fileName == L"..")
      continue;
    if (f.attributes & FILE_ATTRIBUTE_DIRECTORY)
      gold_dirs.insert(f.fileName);
    else
      gold_files.insert(f.fileName);
  }

  bool all_good = true;

  std::vector<std::wstring> recurse;
  for (const auto& f : winapi::ex::wide::quickFindFiles(result_full.c_str(), L"*")) {
    if (f.fileName == L"." || f.fileName == L"..")
      continue;
    if (f.attributes & FILE_ATTRIBUTE_DIRECTORY) {
      const auto& find = gold_dirs.find(f.fileName);
      if (find != gold_dirs.end()) {
        gold_dirs.erase(find);
        recurse.push_back(f.fileName);
      } else {
        fprintf(log, "  unexpected directory found: %s%s\n", MOUNT_LABEL,
                (rel_path / f.fileName).string().c_str());
        all_good = false;
      }
    } else {
      const auto& find = gold_files.find(f.fileName);
      if (find != gold_files.end()) {
        gold_files.erase(find);
        if (!test::compare_files(gold_full / f.fileName, result_full / f.fileName,
                                 false)) {
          fprintf(log, "  file contents differs: %s%s\n", MOUNT_LABEL,
                  (rel_path / f.fileName).string().c_str());
          all_good = false;
        }
      } else {
        fprintf(log, "  unexpected file found: %s%s\n", MOUNT_LABEL,
                (rel_path / f.fileName).string().c_str());
        all_good = false;
      }
    }
  }

  for (auto d : gold_dirs) {
    fprintf(log, "  expected directory not found: %s%s\n", MOUNT_LABEL,
            (rel_path / d).string().c_str());
    all_good = false;
  }

  for (auto f : gold_files) {
    fprintf(log, "  expected file not found: %s%s\n", MOUNT_LABEL,
            (rel_path / f).string().c_str());
    all_good = false;
  }

  for (auto r : recurse)
    all_good &= recursive_compare_dirs(rel_path / r, gold_base, result_base, log);

  return all_good;
}

test::ScopedFILE usvfs_test_base::output()
{
  auto log       = test::ScopedFILE::open(m_o.output, m_clean_output ? L"wt" : L"at");
  m_clean_output = false;
  return log;
}

void usvfs_test_base::clean_output()
{
  using namespace std;

  errno_t err;
  auto in = test::ScopedFILE::open(m_o.output, L"rt", err);
  if (err == ENOENT) {
    wcerr << L"warning: no " << m_o.output << L" to clean." << endl;
    return;
  } else if (err || !in)
    throw_testWinFuncFailed("_wfopen_s", m_o.output.string().c_str(), err);

  path clean = m_o.output.parent_path() / m_o.output.stem();
  clean += OUTPUT_CLEAN_SUFFIX;
  clean += m_o.output.extension();

  auto out = test::ScopedFILE::open(clean, L"wt");
  wcout << L"Cleaning " << m_o.output << " to " << clean << endl;

  char line[1024];
  while (!feof(in)) {
    // read one line:
    if (!fgets(line, _countof(line), in))
      if (feof(in))
        break;
      else
        throw_testWinFuncFailed("fgets", "reading output");
    if (*line == '#')
      continue;

    // in order for the clean output to compare cleanly between run with different
    // options we clean out things like the platform and the ops log name (which
    // contians the scenario label):

    char* platform = line;
    while (platform) {
      char* platform_x86 = strstr(platform, "x86");
      char* platform_x64 = strstr(platform, "x64");
      if (platform_x86 && platform_x64)
        platform = std::min(platform_x86, platform_x64);
      else if (platform_x86)
        platform = platform_x86;
      else if (platform_x64)
        platform = platform_x64;
      else
        platform = nullptr;
      if (platform) {
        platform[1] = platform[2] = '?';
        platform += 3;
      }
    }

    char* cout_end     = strstr(line, "-cout+ ");
    char* cout_log_end = nullptr;
    if (cout_end) {
      cout_end += strlen("-cout+ ");
      cout_log_end = strchr(cout_end, ' ');
    }
    if (cout_log_end && cout_log_end > cout_end) {
      cout_end[0] = '?';
      if (cout_log_end > cout_end + 1)
        memmove(cout_end + 1, cout_log_end, strlen(cout_log_end) + 1);
    }

    fputs(line, out);
  }
}

int usvfs_test_base::run(const std::wstring& exe_name)
{
  using namespace usvfs::shared;
  using namespace std;

  int res = run_impl(exe_name);
  try {
    clean_output();
  } catch (const exception& e) {
    wcerr << "CERROR: " << string_cast<wstring>(e.what(), CodePage::UTF8).c_str()
          << endl;
  } catch (...) {
    wcerr << "CERROR: unknown exception" << endl;
  }
  if (!res)
    wcout << "scenario " << scenario_name() << " PASSED." << endl;
  else
    wcerr << "scenario " << scenario_name() << " FAILED!" << endl;
  return res;
}

int usvfs_test_base::run_impl(const std::wstring& exe_name)
{
  using namespace usvfs::shared;
  using namespace std;

  try {
    winapi::ex::wide::createPath(m_o.output.parent_path().c_str());

    // we read mappings first only because it is "non-destructive" but might raise an
    // error if mappings invalid
    auto mappings = mappings_reader(m_o.mount, m_o.source).read(m_o.mapping);

    cleanup_temp();
    log_settings(exe_name);
    copy_fixture();

    usvfs_connector usvfs(m_o);
    {
      const auto& log = output();
      usvfs.updateMapping(mappings, m_o, log);

      fprintf(log, "running scenario %s:\n\n", scenario_name());
    }
    auto res = scenario_run();
    {
      const auto& log = output();
      if (res)
        fprintf(log, "\nscenario ended successfully!\n\n");
      else
        fprintf(log, "\nscenario failed miserably.\n");
    }

    if (!res)
      return 7;

    if (!postmortem_check())
      return 8;

    return 0;
  }
#if 1  // just a convient way to not catch exception when debugging
  catch (const exception& e) {
    try {
      wcerr << "ERROR: " << string_cast<wstring>(e.what(), CodePage::UTF8).c_str()
            << endl;
      fprintf(output(), "ERROR: %s\n", e.what());
    } catch (const exception& e) {
      wcerr << "ERROR^2: " << string_cast<wstring>(e.what(), CodePage::UTF8).c_str()
            << endl;
    } catch (...) {
      wcerr << "ERROR^2: unknown exception" << endl;
    }
  } catch (...) {
    try {
      wcerr << "ERROR: unknown exception" << endl;
      fprintf(output(), "ERROR: unknown exception\n");
    } catch (const exception& e) {
      wcerr << "ERROR^2: " << string_cast<wstring>(e.what(), CodePage::UTF8).c_str()
            << endl;
    } catch (...) {
      wcerr << "ERROR^2: unknown exception" << endl;
    }
  }
#else
  catch (bool) {
  }
#endif
  return 9;  // exception
}

void usvfs_test_base::log_settings(const std::wstring& exe_name)
{
  using namespace usvfs::shared;
  fprintf(output(), "%s %s started with %s%s%s\n\n",
          string_cast<std::string>(exe_name).c_str(), scenario_name(),
          m_o.opsexe.filename().string().c_str(), m_o.ops_options.empty() ? "" : " ",
          string_cast<std::string>(m_o.ops_options).c_str());
}

void usvfs_test_base::ops_list(const path& rel_path, bool recursive, bool with_contents,
                               bool should_succeed, const wstring& additional_args)
{
  wstring cmd = recursive ? L"-r -list" : L"-list";
  if (with_contents)
    cmd += L"contents";
  run_ops(should_succeed, cmd, rel_path, additional_args);
}

void usvfs_test_base::ops_read(const path& rel_path, bool should_succeed,
                               const wstring& additional_args)
{
  run_ops(should_succeed, L"-read", rel_path, additional_args);
}

void usvfs_test_base::ops_rewrite(const path& rel_path, const char* contents,
                                  bool should_succeed, const wstring& additional_args)
{
  using namespace usvfs::shared;
  run_ops(should_succeed, L"-rewrite", rel_path, additional_args,
          L"\"" + string_cast<wstring>(contents, CodePage::UTF8) + L"\"");
}

void usvfs_test_base::ops_overwrite(const path& rel_path, const char* contents,
                                    bool recursive, bool should_succeed,
                                    const wstring& additional_args)
{
  using namespace usvfs::shared;
  run_ops(should_succeed, recursive ? L"-r -overwrite" : L"-overwrite", rel_path,
          additional_args,
          L"\"" + string_cast<wstring>(contents, CodePage::UTF8) + L"\"");
}

void usvfs_test_base::ops_touch(const path& rel_path, bool full_write_access,
                                bool should_succeed, const wstring& additional_args)
{
  run_ops(should_succeed, full_write_access ? L"-touchw" : L"-touch", rel_path,
          additional_args);
}

void usvfs_test_base::ops_deleteoverwrite(const path& rel_path, const char* contents,
                                          bool recursive, bool should_succeed,
                                          const wstring& additional_args)
{
  using namespace usvfs::shared;
  run_ops(should_succeed, recursive ? L"-r -deleteoverwrite" : L"-deleteoverwrite",
          rel_path, additional_args,
          L"\"" + string_cast<wstring>(contents, CodePage::UTF8) + L"\"");
}

void usvfs_test_base::ops_delete(const path& rel_path, bool should_succeed,
                                 const wstring& additional_args)
{
  run_ops(should_succeed, L"-delete", rel_path, additional_args);
}

void usvfs_test_base::ops_copy(const path& src_rel_path, const path& dest_rel_path,
                               bool replace, bool should_succeed,
                               const wstring& additional_args)
{
  run_ops(should_succeed, replace ? L"-copyover" : L"-copy", src_rel_path,
          additional_args, wstring(), dest_rel_path);
}

void usvfs_test_base::ops_rename(const path& src_rel_path, const path& dest_rel_path,
                                 bool replace, bool allow_copy, bool should_succeed,
                                 const wstring& additional_args)
{
  wstring command = allow_copy ? L"-move" : L"-rename";
  if (replace)
    command += L"over";
  run_ops(should_succeed, command, src_rel_path, additional_args, wstring(),
          dest_rel_path);
}

void usvfs_test_base::ops_deleterename(const path& src_rel_path,
                                       const path& dest_rel_path, bool allow_copy,
                                       bool should_succeed,
                                       const wstring& additional_args)
{
  wstring command = allow_copy ? L"-deletemove" : L"-deleterename";
  run_ops(should_succeed, command, src_rel_path, additional_args, wstring(),
          dest_rel_path);
}

void usvfs_test_base::run_ops(bool should_succeed, const wstring& preargs,
                              const path& rel_path, const wstring& additional_args,
                              const wstring& postargs, const path& rel_path2)
{
  using namespace usvfs::shared;
  using string  = std::string;
  using wstring = wstring;

  string commandlog   = test::path(m_o.opsexe).filename().string();
  wstring commandline = m_o.opsexe;
  if (commandline.find(' ') != wstring::npos &&
      commandline.find('"') == wstring::npos) {
    commandline = L"\"" + commandline + L"\"";
    commandlog  = "\"" + commandlog + "\"";
  }

  if (!m_o.mount.empty()) {
    commandline += L" -basedir ";
    commandline += m_o.mount;
    commandlog += " -basedir ";
    commandlog += m_o.mount.filename().string();
  }

  if (!m_o.ops_options.empty()) {
    commandline += L" ";
    commandline += m_o.ops_options;
    commandlog += " ";
    commandlog += string_cast<string>(m_o.ops_options, CodePage::UTF8);
  }

  commandline += L" -cout+ ";
  commandline += m_o.output;
  commandlog += " -cout+ ";
  commandlog += m_o.output.filename().string();

  if (!additional_args.empty()) {
    commandline += L" ";
    commandline += additional_args;
    commandlog += " ";
    commandlog += string_cast<string>(additional_args, CodePage::UTF8);
  }

  if (!preargs.empty()) {
    commandline += L" ";
    commandline += preargs;
    commandlog += " ";
    commandlog += string_cast<string>(preargs, CodePage::UTF8);
  }

  if (!rel_path.empty()) {
    commandline += L" ";
    commandline += m_o.mount / rel_path;
    commandlog += " ";
    commandlog += MOUNT_LABEL + rel_path.string();
  }

  if (!rel_path2.empty()) {
    commandline += L" ";
    commandline += m_o.mount / rel_path2;
    commandlog += " ";
    commandlog += MOUNT_LABEL + rel_path2.string();
  }

  if (!postargs.empty()) {
    commandline += L" ";
    commandline += postargs;
    commandlog += " ";
    commandlog += string_cast<string>(postargs, CodePage::UTF8);
  }

  fprintf(output(), "Spawning: %s\n", commandlog.c_str());
  auto res = usvfs_connector::spawn(&commandline[0]);
  fprintf(output(), "\n");

  bool success = res == 0;
  if (success != should_succeed)
    throw test::FuncFailed("run_ops", success ? "succeeded" : "failed",
                           commandlog.c_str(), res);
}

std::string usvfs_test_base::mount_contents(const path& rel_path)
{
  verify_mount_existance(rel_path);
  const auto& contents = test::read_small_file(m_o.mount / rel_path);
  return std::string(contents.data(), contents.size());
}

std::string usvfs_test_base::source_contents(const path& rel_path)
{
  verify_source_existance(rel_path);
  const auto& contents = test::read_small_file(m_o.source / rel_path);
  return std::string(contents.data(), contents.size());
}

void usvfs_test_base::verify_mount_contents(const path& rel_path, const char* contents)
{
  verify_mount_existance(rel_path);
  if (verify_contents(m_o.mount / rel_path, contents))
    throw test::FuncFailed("verify_mount_contents",
                           (MOUNT_LABEL + rel_path.string()).c_str(), contents);
}

void usvfs_test_base::verify_source_contents(const path& rel_path, const char* contents)
{
  verify_source_existance(rel_path);
  if (verify_contents(m_o.source / rel_path, contents))
    throw test::FuncFailed("verify_source_contents",
                           (SOURCE_LABEL + rel_path.string()).c_str(), contents);
}

bool usvfs_test_base::verify_contents(const path& file, const char* contents)
{
  // we allow difference in trailing whitespace (i.e. extra new line):

  size_t sz = strlen(contents);
  while (sz && isspace(contents[sz - 1]))
    --sz;

  const auto& real_contents = test::read_small_file(file);
  size_t real_sz            = real_contents.size();
  while (real_sz && isspace(real_contents[real_sz - 1]))
    --real_sz;

  return sz == real_sz && memcmp(contents, real_contents.data(), sz);
}

void usvfs_test_base::verify_mount_existance(const path& rel_path, bool exists,
                                             bool is_dir)
{
  bool real_is_dir = false;
  bool real_exists =
      winapi::ex::wide::fileExists((m_o.mount / rel_path).c_str(), &real_is_dir);
  if (exists != real_exists)
    throw test::FuncFailed("verify_mount_existance",
                           real_exists ? "path exists" : "path does not exist",
                           (MOUNT_LABEL + rel_path.string()).c_str());
  else if (real_exists && is_dir != real_is_dir)
    throw test::FuncFailed("verify_mount_existance",
                           real_is_dir ? "path is a directory" : "path is a file",
                           (MOUNT_LABEL + rel_path.string()).c_str());
}

void usvfs_test_base::verify_source_existance(const path& rel_path, bool exists,
                                              bool is_dir)
{
  bool real_is_dir = false;
  bool real_exists =
      winapi::ex::wide::fileExists((m_o.source / rel_path).c_str(), &real_is_dir);
  if (exists != real_exists)
    throw test::FuncFailed("verify_source_existance",
                           real_exists ? "path exists" : "path does not exist",
                           (SOURCE_LABEL + rel_path.string()).c_str());
  else if (real_exists && is_dir != real_is_dir)
    throw test::FuncFailed("verify_source_existance",
                           real_is_dir ? "path is a directory" : "path is a file",
                           (SOURCE_LABEL + rel_path.string()).c_str());
}
