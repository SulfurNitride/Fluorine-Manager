#pragma once

#include <string>

#include <test_helpers.h>

class usvfs_test_options
{
public:
  static constexpr auto DEFAULT_MAPPING = L"vfs_mappings.txt";
  static constexpr auto MOUNT_DIR       = L"mount";
  static constexpr auto SOURCE_DIR      = L"source";

  using path = test::path;

  // fills any values not set (or set to an empty value) to their default value
  void fill_defaults(const path& test_name, const std::wstring& scenario,
                     const wchar_t* label);

  void set_ops32();  // sets opsexe iff opsexe is empty
  void set_ops64();  // sets opsexe iff opsexe is empty
  void add_ops_options(const std::wstring& options);

  path opsexe;
  path fixture;
  path mapping;
  path temp;
  path mount;
  path source;
  path output;
  path usvfs_log;
  std::wstring ops_options;
  bool temp_cleanup       = false;
  bool force_temp_cleanup = false;
};

class usvfs_test_base
{
public:
  static constexpr auto MOUNT_DIR           = usvfs_test_options::MOUNT_DIR;
  static constexpr auto SOURCE_DIR          = usvfs_test_options::SOURCE_DIR;
  static constexpr auto MOUNT_LABEL         = "mount:";
  static constexpr auto SOURCE_LABEL        = "source:";
  static constexpr auto OUTPUT_CLEAN_SUFFIX = L"_clean";
  static constexpr auto POSTMORTEM_SUFFIX   = L".postmortem";

  using wstring = std::wstring;
  using path    = test::path;

  // options object should outlive this object.
  usvfs_test_base(const usvfs_test_options& options) : m_o(options) {}
  virtual ~usvfs_test_base() = default;

  int run(const std::wstring& exe_name);

  // function for override:

  virtual const char* scenario_name() = 0;
  virtual bool scenario_run()         = 0;

  // helpers for derived scenarios:

  virtual void ops_list(const path& rel_path, bool recursive, bool with_contents,
                        bool should_succeed            = true,
                        const wstring& additional_args = wstring());
  virtual void ops_read(const path& rel_path, bool should_succeed = true,
                        const wstring& additional_args = wstring());
  virtual void ops_rewrite(const path& rel_path, const char* contents,
                           bool should_succeed            = true,
                           const wstring& additional_args = wstring());
  virtual void ops_overwrite(const path& rel_path, const char* contents, bool recursive,
                             bool should_succeed            = true,
                             const wstring& additional_args = wstring());
  virtual void ops_touch(const path& rel_path, bool full_write_access = false,
                         bool should_succeed            = true,
                         const wstring& additional_args = wstring());
  virtual void ops_deleteoverwrite(const path& rel_path, const char* contents,
                                   bool recursive, bool should_succeed = true,
                                   const wstring& additional_args = wstring());
  virtual void ops_delete(const path& rel_path, bool should_succeed = true,
                          const wstring& additional_args = wstring());
  virtual void ops_copy(const path& src_rel_path, const path& dest_rel_path,
                        bool replace, bool should_succeed = true,
                        const wstring& additional_args = wstring());
  virtual void ops_rename(const path& src_rel_path, const path& dest_rel_path,
                          bool replace, bool allow_copy = false,
                          bool should_succeed            = true,
                          const wstring& additional_args = wstring());
  virtual void ops_deleterename(const path& src_rel_path, const path& dest_rel_path,
                                bool allow_copy = false, bool should_succeed = true,
                                const wstring& additional_args = wstring());

  virtual std::string mount_contents(const path& rel_path);
  virtual void verify_mount_contents(const path& rel_path, const char* contents);
  virtual void verify_mount_existance(const path& rel_path, bool exists = true,
                                      bool is_dir = false);
  virtual std::string source_contents(const path& rel_path);
  virtual void verify_source_contents(const path& rel_path, const char* contents);
  virtual void verify_source_existance(const path& rel_path, bool exists = true,
                                       bool is_dir = false);

private:
  int run_impl(const std::wstring& exe_name);
  void log_settings(const std::wstring& exe_name);
  void cleanup_temp();
  void copy_fixture();
  bool postmortem_check();
  bool recursive_compare_dirs(path rel_path, path gold_base, path result_base,
                              FILE* log);
  void clean_output();

  test::ScopedFILE output();
  void run_ops(bool should_succeed, const wstring& preargs, const path& rel_path,
               const wstring& additional_args, const wstring& postargs = wstring(),
               const path& rel_path2 = path());
  bool verify_contents(const path& file, const char* contents);

  const usvfs_test_options& m_o;
  bool m_clean_output = true;
};
