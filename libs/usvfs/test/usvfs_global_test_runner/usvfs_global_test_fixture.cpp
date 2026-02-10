#include "usvfs_global_test_fixture.h"

#include <filesystem>
#include <format>
#include <fstream>

#include <gtest/gtest.h>

#include <stringcast.h>
#include <test_helpers.h>
#include <usvfs.h>
#include <windows_sane.h>

// find the path to the executable that contains gtest entries
//
std::filesystem::path path_to_usvfs_global_test()
{
  return test::path_of_test_bin(
      test::platform_dependant_executable("usvfs_global_test", "exe"));
}

// path to the fixture for the given test group
//
std::filesystem::path path_to_usvfs_global_test_figures(std::wstring_view group)
{
  return test::path_of_test_fixtures() / "usvfs_global_test" / group;
}

// spawn the an hook version of the given
//
DWORD spawn_usvfs_hooked_process(
    const std::filesystem::path& app, const std::vector<std::wstring>& arguments = {},
    const std::optional<std::filesystem::path>& working_directory = {})
{
  using namespace usvfs::shared;

  std::wstring command      = app;
  std::filesystem::path cwd = working_directory.value_or(app.parent_path());
  std::vector<wchar_t> env;

  if (!arguments.empty()) {
    command += L" " + boost::algorithm::join(arguments, L" ");
  }

  STARTUPINFO si{0};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{0};

#pragma warning(push)
#pragma warning(disable : 6387)

  if (!usvfsCreateProcessHooked(nullptr, command.data(), nullptr, nullptr, FALSE, 0,
                                nullptr, cwd.c_str(), &si, &pi)) {
    test::throw_testWinFuncFailed(
        "CreateProcessHooked",
        string_cast<std::string>(command, CodePage::UTF8).c_str());
  }

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

#pragma warning(pop)

  return exit;
}

bool UsvfsGlobalTest::m_force_usvfs_logs = false;

void UsvfsGlobalTest::ForceUsvfsLogs()
{
  m_force_usvfs_logs = true;
}

class UsvfsGlobalTest::UsvfsGuard
{
public:
  UsvfsGuard(std::string_view instance_name = "usvfs_test", bool logging = false)
      : m_parameters(usvfsCreateParameters(), &usvfsFreeParameters)
  {
    usvfsSetInstanceName(m_parameters.get(), instance_name.data());
    usvfsSetDebugMode(m_parameters.get(), false);
    usvfsSetLogLevel(m_parameters.get(), LogLevel::Debug);
    usvfsSetCrashDumpType(m_parameters.get(), CrashDumpsType::None);
    usvfsSetCrashDumpPath(m_parameters.get(), "");

    usvfsInitLogging(logging);
    usvfsCreateVFS(m_parameters.get());
  }

  ~UsvfsGuard() { usvfsDisconnectVFS(); }

private:
  std::unique_ptr<usvfsParameters, decltype(&usvfsFreeParameters)> m_parameters;
};

UsvfsGlobalTest::UsvfsGlobalTest()
    : m_usvfs{std::make_unique<UsvfsGuard>()},
      m_temporary_folder{test::path_of_test_temp()}
{
  std::string name{testing::UnitTest::GetInstance()->current_test_info()->name()};
  m_group = {name.begin(), name.end()};
}

UsvfsGlobalTest::~UsvfsGlobalTest()
{
  CleanUp();
}

std::filesystem::path UsvfsGlobalTest::ActualFolder() const
{
  return m_temporary_folder;
}

std::filesystem::path UsvfsGlobalTest::ExpectedFolder() const
{
  return path_to_usvfs_global_test_figures(m_group) / "expected";
}

void UsvfsGlobalTest::CleanUp() const
{
  if (exists(m_temporary_folder)) {
    remove_all(m_temporary_folder);
  }
}

void UsvfsGlobalTest::PrepareFileSystem() const
{
  // cleanup in case a previous tests failed to delete its stuff
  CleanUp();

  // copy fixtures
  const auto fixtures = path_to_usvfs_global_test_figures(m_group) / "source";
  if (exists(fixtures)) {
    copy(fixtures, m_temporary_folder, std::filesystem::copy_options::recursive);
  }
}

void UsvfsGlobalTest::SetUpOverwrite(bool force) const
{
  if (force && !exists(m_overwrite_folder)) {
    create_directory(m_overwrite_folder);
  }

  if (exists(m_overwrite_folder)) {
    usvfsVirtualLinkDirectoryStatic(m_overwrite_folder.c_str(), m_data_folder.c_str(),
                                    LINKFLAG_CREATETARGET | LINKFLAG_RECURSIVE);
  }
}

void UsvfsGlobalTest::PrepareMapping() const
{
  // should not be needed, but just to be safe
  usvfsClearVirtualMappings();

  if (!exists(m_data_folder)) {
    throw std::runtime_error{
        std::format("data path missing at {}", m_data_folder.string())};
  }

  if (exists(m_mods_folder)) {
    for (const auto& mod : std::filesystem::directory_iterator(m_mods_folder)) {
      if (!is_directory(mod)) {
        continue;
      }
      usvfsVirtualLinkDirectoryStatic(mod.path().c_str(), m_data_folder.c_str(),
                                      LINKFLAG_RECURSIVE);
    }
  }

  // by default, only create overwrite if present
  SetUpOverwrite(false);
}

int UsvfsGlobalTest::Run() const
{
  PrepareMapping();

  const auto res = spawn_usvfs_hooked_process(
      path_to_usvfs_global_test(), {std::format(L"--gtest_filter={}.*", m_group),
                                    L"--gtest_brief=1", m_data_folder.native()});

  // TODO: try to do this with gtest itself?
  if (m_force_usvfs_logs || res != 0) {
    const auto log_path = test::path_of_test_bin(m_group + L".log");
    std::ofstream os{log_path};
    std::string buffer(1024, '\0');
    std::cout << "process returned " << std::hex << res << ", usvfs logs dumped to "
              << log_path.string() << '\n';
    while (usvfsGetLogMessages(buffer.data(), buffer.size(), false)) {
      os << "  " << buffer.c_str() << "\n";
    }
  }

  return res;
}
