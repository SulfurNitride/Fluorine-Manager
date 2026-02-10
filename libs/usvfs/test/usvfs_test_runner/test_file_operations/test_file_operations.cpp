#include <cstdio>
#include <stdexcept>

#include <boost/filesystem.hpp>
#include <boost/type_traits.hpp>
#include <winapi.h>

#include "test_ntapi.h"
#include "test_w32api.h"
#include <test_helpers.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

using usvfs::shared::CodePage;
using usvfs::shared::string_cast;

void print_usage(const char* myname)
{
  using namespace std;
  fprintf(stderr, "usage: %s [<options>] <command> [<command params>] ...\n", myname);
  fprintf(stderr,
          "options and commands are parsed and executed in the order they appear.\n");
  fprintf(stderr, "\nsupported commands:\n");
  fprintf(
      stderr,
      " -list <dir>         : lists the given directory and outputs the results.\n");
  fprintf(stderr, " -listcontents <dir> : lists the given directory, reading all files "
                  "and outputs the results.\n");
  fprintf(stderr,
          " -read <file>        : reads the given file and outputs the results.\n");
  fprintf(stderr, " -overwrite <file> <string> : overwrites the file at the given path "
                  "with the given line (creating directories if in recursive mode).\n");
  fprintf(stderr, " -deleteoverwrite <file> <string> : shorthand for -delete <file> "
                  "-overwrite <file> <string>\n");
  fprintf(stderr,
          " -rewrite <file> <string> : rewrites the file at the given path with the "
          "given line (fails if file doesn't exist; uses read/write access).\n");
  fprintf(stderr,
          " -touch <file>       : updates last write timestamp of specified file.\n");
  fprintf(stderr, " -touchw <file>      : updates last write timestamp using full "
                  "write permissions.\n");
  fprintf(stderr, " -delete <file>      : deletes the given file.\n");
  fprintf(stderr, " -copy <src> <dst>   : copies the given file.\n");
  fprintf(stderr, " -copyover <src> <dst> : copies the given file (replacing existing "
                  "destination).\n");
  fprintf(stderr, " -rename <src> <dst> : renames the given file.\n");
  fprintf(stderr, " -renameover <src> <dst> : renames the given file (replacing "
                  "existing destination).\n");
  fprintf(stderr, " -deleterename <src> <dst> : shorthand for -delete <dst> -rename "
                  "<src> <dst>.\n");
  fprintf(stderr,
          " -move <src> <dst>   : moves the given file (not supported by ntapi).\n");
  fprintf(stderr, " -moveover <src> <dst> : moves the given file (replacing existing "
                  "destination; not supported by ntapi).\n");
  fprintf(
      stderr,
      " -deletemove <src> <dst> : shorthand for -delete <dst> -move <src> <dst>.\n");
  fprintf(stderr, " -debug              : shows a message box and wait for a debugger "
                  "to connect.\n");
  fprintf(stderr, "\nsupported options:\n");
  fprintf(stderr, " -out <file>         : file to log output to (use \"-\" for the "
                  "stdout; otherwise path to output should exist).\n");
  fprintf(stderr, " -out+ <file>        : similar to -out but appends the file instead "
                  "of overwriting it.\n");
  fprintf(stderr, " -cout <file>        : similar to -out but does not log PID and "
                  "other info which may change between runs.\n");
  fprintf(stderr, " -cout+ <file>       : similar to -cout but appends the file "
                  "instead of overwriting it.\n");
  fprintf(stderr, " -r                  : recursively list/create directories.\n");
  fprintf(stderr,
          " -r-                 : don't recursively list/create directories.\n");
  fprintf(stderr, " -basedir <dir>      : any paths under the basedir will outputed in "
                  "a relative manner (default is current directory).\n");
  fprintf(stderr,
          " -w32api             : use regular Win32 API for file access (default).\n");
  fprintf(stderr,
          " -ntapi              : use lower level ntdll functions for file access.\n");
}

class CommandExecuter
{
public:
  CommandExecuter() : m_output(stdout), m_api(&w32api)
  {
    set_basedir(TestFileSystem::current_directory().string().c_str());
  }

  ~CommandExecuter()
  {
    if (m_output && m_output != stdout)
      fclose(m_output);
  }

  FILE* output() const { return m_output; }

  bool file_output() const { return m_output != stdout; }

  // Options:

  void set_output(const char* output_file, bool clean, bool append, const char* cmdline)
  {
    if (m_output && m_output != stdout) {
      fprintf(m_output, "#< Output log closed.\n", output_file);
      fclose(m_output);
    }

    m_cleanoutput = clean;
    m_output      = nullptr;
    errno_t err   = fopen_s(&m_output, output_file, append ? "at" : "wt");
    if (err || !m_output)
      test::throw_testWinFuncFailed("fopen_s", output_file, err);
    else {
      if (m_cleanoutput)
        fprintf(m_output, "#> Output log openned for: %s\n",
                clean_cmdline_heuristic(cmdline).c_str());
      else
        fprintf(m_output, "#> Output log openned for (pid %d): %s\n",
                GetCurrentProcessId(), cmdline);
      w32api.set_output(m_output, m_cleanoutput);
      ntapi.set_output(m_output, m_cleanoutput);
    }
  }

  bool cleanoutput() const { return m_cleanoutput; }

  void set_recursive(bool recursive) { m_recursive = recursive; }

  void set_basedir(const char* basedir)
  {
    w32api.set_basepath(basedir);
    ntapi.set_basepath(basedir);
  }

  void set_ntapi(bool enable)
  {
    if (enable)
      m_api = &ntapi;
    else
      m_api = &w32api;
  }

  // Commands:

  void list(const char* dir, bool read_files)
  {
    if (debug_pending())
      __debugbreak();

    list_impl(m_api->real_path(dir), read_files);
  }

  void read(const char* path)
  {
    if (debug_pending())
      __debugbreak();

    m_api->read_file(m_api->real_path(path));
  }

  void overwrite(const char* path, const char* value)
  {
    if (debug_pending())
      __debugbreak();

    const auto& real = real_path_create_if_necessary(path);

    m_api->write_file(real, value, strlen(value), true,
                      TestFileSystem::write_mode::overwrite);
  }

  void rewrite(const char* path, const char* value)
  {
    if (debug_pending())
      __debugbreak();

    const auto& real = real_path_create_if_necessary(path);

    // Use read/write access when rewriting to "simulate" the harder case where it is
    // not known if the file is going to actually be changed
    m_api->write_file(real, value, strlen(value), false,
                      TestFileSystem::write_mode::manual_truncate, true);
    m_api->write_file(real, "\r\n", 2, false, TestFileSystem::write_mode::append);
  }

  void touch(const char* path, bool full_write_access)
  {
    if (debug_pending())
      __debugbreak();

    const auto& real = real_path_create_if_necessary(path);

    m_api->touch_file(real, full_write_access);
  }

  void deletef(const char* path)
  {
    if (debug_pending())
      __debugbreak();

    m_api->delete_file(m_api->real_path(path));
  }

  void rename(const char* source, const char* destination, bool replace_existing)
  {
    if (debug_pending())
      __debugbreak();

    m_api->copy_file(m_api->real_path(source), m_api->real_path(destination),
                     replace_existing);
  }

  void rename(const char* source, const char* destination, bool replace_existing,
              bool allow_copy)
  {
    if (debug_pending())
      __debugbreak();

    m_api->rename_file(m_api->real_path(source), m_api->real_path(destination),
                       replace_existing, allow_copy);
  }

  void debug() { m_debug_pending = true; }

  bool debug_pending()
  {
    if (!m_debug_pending)
      return false;
    m_debug_pending = false;
    if (!IsDebuggerPresent())
      MessageBoxA(NULL, "Connect a debugger and press OK to trigger a breakpoint",
                  "DEBUG", 0);
    return IsDebuggerPresent();
  }

  // Traversal:

  void list_impl(TestFileSystem::path real, bool read_files)
  {
    std::vector<TestFileSystem::path> recurse;
    {
      auto files = m_api->list_directory(real);
      fprintf(m_output, ">> Listing directory {%s}:\n",
              m_api->relative_path(real).string().c_str());
      for (auto f : files) {
        if (f.is_dir()) {
          fprintf(m_output, "[%s] DIR (attributes 0x%x)\n",
                  string_cast<std::string>(f.name, CodePage::UTF8).c_str(),
                  f.attributes);
          if (m_recursive && f.name != L"." && f.name != L"..")
            recurse.push_back(real / f.name);
        } else {
          fprintf(m_output, "[%s] FILE (attributes 0x%x, %lld bytes)\n",
                  string_cast<std::string>(f.name, CodePage::UTF8).c_str(),
                  f.attributes, f.size);
          if (read_files)
            m_api->read_file(real / f.name);
        }
      }
    }
    for (auto r : recurse)
      list_impl(r, read_files);
  }

private:
  TestFileSystem::path real_path_create_if_necessary(const char* path)
  {
    auto real = m_api->real_path(path);
    if (m_recursive)
      try {
        m_api->create_path(real.parent_path());
      } catch (const std::exception& e) {
        throw std::runtime_error(
            std::format("Failed to create_path [{}] : {}",
                        m_api->relative_path(real.parent_path()).string(), e.what()));
      }
    return std::move(real);
  }

  std::string clean_cmdline_arg(const char* arg_start, const char* arg_end)
  {
    if (arg_start == arg_end)
      return std::string();
    bool quoted          = *arg_start == '\"' && *(arg_end - 1) == '\"';
    const char* last_sep = arg_end;
    while (last_sep != arg_start && *last_sep != '\\')
      --last_sep;
    if (arg_end - arg_start < (quoted ? 5 : 3) || arg_start[0] == '-' ||
        arg_start[quoted ? 2 : 1] != ':' || last_sep == arg_start)
      return std::string(arg_start, arg_end);
    std::string res = quoted ? "\"" : "";
    res.append(last_sep + 1, arg_end);
    return res;
  }

  std::string clean_cmdline_heuristic(const char* cmdline)
  {
    std::string res;
    bool first = true;
    while (*cmdline) {
      const char* end = strchr(cmdline, ' ');
      if (!end)
        end = cmdline + strlen(cmdline);
      if (first)
        first = false;
      else
        res.push_back(' ');
      res += clean_cmdline_arg(cmdline, end);
      cmdline = end;
      while (*cmdline == ' ')
        ++cmdline;
    }
    return res;
  }

  FILE* m_output;
  bool m_cleanoutput   = false;
  bool m_recursive     = false;
  bool m_debug_pending = false;

  TestFileSystem* m_api;
  static TestW32Api w32api;
  static TestNtApi ntapi;
};

// static
TestW32Api CommandExecuter::w32api(stdout);
TestNtApi CommandExecuter::ntapi(stdout);

class abort_invalid_argument : std::exception
{};

bool verify_args_exist(const char* flag, int params, int index, int count)
{
  if (index + params >= count) {
    fprintf(stderr, "ERROR: %s requires %d arguments\n", flag, params);
    throw abort_invalid_argument();
  }
  return true;
}

const char* UntouchedCommandLineArguments()
{
  const char* cmd = GetCommandLineA();
  for (; *cmd && *cmd != ' '; ++cmd) {
    if (*cmd == '"') {
      int escaped = 0;
      for (++cmd; *cmd && (*cmd != '"' || escaped % 2 != 0); ++cmd)
        escaped = *cmd == '\\' ? escaped + 1 : 0;
    }
  }
  while (*cmd == ' ')
    ++cmd;
  return cmd;
}

int main(int argc, char* argv[])
{
  bool found_commands = false;
  CommandExecuter executer;

  TestFileSystem::path exe_path = argv[0];
  std::string exename           = exe_path.filename().string();
  std::string cmdline           = exename + " " + UntouchedCommandLineArguments();
  fprintf(stdout, "#> process %d started with commandline: %s\n", GetCurrentProcessId(),
          cmdline.c_str());

  for (int ai = 1; ai < argc; ++ai) {
    try {
      SetLastError(0);

      // options:
      if (strcmp(argv[ai], "-out") == 0 && verify_args_exist("-out", 1, ai, argc) ||
          strcmp(argv[ai], "-out+") == 0 && verify_args_exist("-out+", 1, ai, argc) ||
          strcmp(argv[ai], "-cout") == 0 && verify_args_exist("-cout", 1, ai, argc) ||
          strcmp(argv[ai], "-cout+") == 0 && verify_args_exist("-cout+", 1, ai, argc)) {
        bool clean  = argv[ai][1] == 'c';
        bool append = argv[ai][clean ? 5 : 4] == '+';
        executer.set_output(argv[++ai], clean, append, cmdline.c_str());
      } else if (strcmp(argv[ai], "-r") == 0)
        executer.set_recursive(true);
      else if (strcmp(argv[ai], "-r-") == 0)
        executer.set_recursive(false);
      else if (strcmp(argv[ai], "-basedir") == 0 &&
               verify_args_exist("-basedir", 1, ai, argc)) {
        executer.set_basedir(argv[++ai]);
      } else if (strcmp(argv[ai], "-w32api") == 0)
        executer.set_ntapi(false);
      else if (strcmp(argv[ai], "-ntapi") == 0)
        executer.set_ntapi(true);
      // commands:
      else if ((strcmp(argv[ai], "-list") == 0 ||
                strcmp(argv[ai], "-listcontents") == 0) &&
               verify_args_exist("-list", 1, ai, argc)) {
        bool contents = strcmp(argv[ai], "-listcontents") == 0;
        executer.list(argv[++ai], contents);
        found_commands = true;
      } else if (strcmp(argv[ai], "-read") == 0 &&
                 verify_args_exist("-read", 1, ai, argc)) {
        executer.read(argv[++ai]);
        found_commands = true;
      } else if (strcmp(argv[ai], "-overwrite") == 0 &&
                     verify_args_exist("-overwrite", 2, ai, argc) ||
                 strcmp(argv[ai], "-deleteoverwrite") == 0 &&
                     verify_args_exist("-deleteoverwrite", 2, ai, argc)) {
        if (argv[ai][1] == 'd')
          executer.deletef(argv[ai + 1]);
        executer.overwrite(argv[ai + 1], argv[ai + 2]);
        ++ ++ai;
        found_commands = true;
      } else if (strcmp(argv[ai], "-rewrite") == 0 &&
                 verify_args_exist("-rewrite", 2, ai, argc)) {
        executer.rewrite(argv[ai + 1], argv[ai + 2]);
        ++ ++ai;
        found_commands = true;
      } else if (strcmp(argv[ai], "-touch") == 0 &&
                     verify_args_exist("-touch", 1, ai, argc) ||
                 strcmp(argv[ai], "-touchw") == 0 &&
                     verify_args_exist("-touchw", 1, ai, argc)) {
        bool write_access = argv[ai][6] == 'w';
        executer.touch(argv[++ai], write_access);
        found_commands = true;
      } else if (strcmp(argv[ai], "-delete") == 0 &&
                 verify_args_exist("-delete", 1, ai, argc)) {
        executer.deletef(argv[++ai]);
        found_commands = true;
      } else if (strcmp(argv[ai], "-copy") == 0 &&
                     verify_args_exist("-copy", 2, ai, argc) ||
                 strcmp(argv[ai], "-copyover") == 0 &&
                     verify_args_exist("-copyover", 2, ai, argc)) {
        bool over = argv[ai][5] == 'o';
        executer.rename(argv[ai + 1], argv[ai + 2], over);
        ++ ++ai;
        found_commands = true;
      } else if (strcmp(argv[ai], "-rename") == 0 &&
                     verify_args_exist("-rename", 2, ai, argc) ||
                 strcmp(argv[ai], "-renameover") == 0 &&
                     verify_args_exist("-renameover", 2, ai, argc) ||
                 strcmp(argv[ai], "-deleterename") == 0 &&
                     verify_args_exist("-deleterename", 2, ai, argc) ||
                 strcmp(argv[ai], "-move") == 0 &&
                     verify_args_exist("-move", 2, ai, argc) ||
                 strcmp(argv[ai], "-moveover") == 0 &&
                     verify_args_exist("-moveover", 2, ai, argc) ||
                 strcmp(argv[ai], "-deletemove") == 0 &&
                     verify_args_exist("-deletemove", 2, ai, argc)) {
        bool del  = argv[ai][1] == 'd';
        bool move = argv[ai][del ? 7 : 1] == 'm';
        bool over = !del && argv[ai][move ? 5 : 7] == 'o';
        if (del)
          executer.deletef(argv[ai + 2]);
        executer.rename(argv[ai + 1], argv[ai + 2], over, move);
        ++ ++ai;
        found_commands = true;
      } else if (strcmp(argv[ai], "-debug") == 0) {
        executer.debug();
      } else {
        if (executer.file_output())
          fprintf(executer.output(), "ERROR: invalid argument {%s}\n", argv[ai]);
        fprintf(stderr, "ERROR: invalid argument {%s}\n", argv[ai]);
        return 1;
      }
    } catch (const abort_invalid_argument&) {
      return 1;
    }
#if 1  // just a convient way to not catch exception when debugging
    catch (const std::exception& e) {
      if (executer.file_output())
        fprintf(executer.output(), "ERROR: %hs\n", e.what());
      fprintf(stderr, "ERROR: %hs\n", e.what());
      return 1;
    } catch (...) {
      if (executer.file_output())
        fprintf(executer.output(), "ERROR: unknown exception");
      fprintf(stderr, "ERROR: unknown exception\n");
      return 1;
    }
#endif
  }

  if (!found_commands) {
    print_usage(exename.c_str());
    return 2;
  }

  if (executer.file_output())
    if (executer.cleanoutput())
      fprintf(executer.output(), "#< %s ended properly.\n", exename.c_str());
    else
      fprintf(executer.output(), "#< %s ended properly in process %d.\n",
              exename.c_str(), GetCurrentProcessId());
  fprintf(stdout, "#< %s ended properly in process %d.\n", exename.c_str(),
          GetCurrentProcessId());

  return 0;
}
