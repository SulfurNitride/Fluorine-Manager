#include <boost/filesystem.hpp>
#include <boost/type_traits.hpp>
#include <gtest/gtest.h>
#include <injectlib.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>
#include <winapi.h>

using namespace usvfs::shared;
using namespace InjectLib;

#if BOOST_ARCH_X86_64
static const wchar_t INJECT_BIN[] = L"testinject_bin_x64.exe";
#else
static const wchar_t INJECT_BIN[] = L"testinject_bin_x86.exe";
#endif

#if BOOST_ARCH_X86_64
static const wchar_t INJECT_LIB[] = L"testinject_dll_x64.dll";
#else
static const wchar_t INJECT_LIB[] = L"testinject_dll_x86.dll";
#endif

static std::shared_ptr<spdlog::logger> logger()
{
  std::shared_ptr<spdlog::logger> result = spdlog::get("test");
  if (result.get() == nullptr) {
    result = spdlog::stdout_logger_mt("test");
  }
  return result;
}

bool spawn(HANDLE& processHandle, HANDLE& threadHandle)
{
  STARTUPINFO si;
  ::ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);

  PROCESS_INFORMATION pi;
  BOOL success = ::CreateProcess(INJECT_BIN, nullptr, nullptr, nullptr, FALSE,
                                 CREATE_SUSPENDED, nullptr, nullptr, &si, &pi);

  if (!success) {
    throw windows_error("failed to start process");
  }

  processHandle = pi.hProcess;
  threadHandle  = pi.hThread;

  return true;
}

TEST(InjectingTest, InjectionNoInit)
{
  // Verify lib can inject without a init function

  HANDLE process, thread;
  spawn(process, thread);
  EXPECT_NO_THROW(InjectLib::InjectDLL(process, thread, INJECT_LIB));
  ResumeThread(thread);

  DWORD res      = WaitForSingleObject(process, INFINITE);
  DWORD exitCode = NO_ERROR;
  res            = GetExitCodeProcess(process, &exitCode);
  EXPECT_EQ(NOERROR, exitCode);

  CloseHandle(process);
  CloseHandle(thread);
}

TEST(InjectingTest, InjectionSimpleInit)
{
  // Verify lib can inject with a init function with null parameters

  HANDLE process, thread;
  spawn(process, thread);
  EXPECT_NO_THROW(InjectLib::InjectDLL(process, thread, INJECT_LIB, "InitNoParam"));
  ResumeThread(thread);

  DWORD res      = WaitForSingleObject(process, INFINITE);
  DWORD exitCode = NO_ERROR;
  res            = GetExitCodeProcess(process, &exitCode);
  EXPECT_EQ(10001, exitCode);  // used init function exits process with this exit code

  CloseHandle(process);
  CloseHandle(thread);
}

TEST(InjectingTest, InjectionComplexInit)
{
  // Verify lib can inject with a init function with null parameters

  static const WCHAR param[] = L"magic_parameter";
  HANDLE process, thread;
  spawn(process, thread);
  EXPECT_NO_THROW(InjectLib::InjectDLL(process, thread, INJECT_LIB, "InitComplexParam",
                                       reinterpret_cast<LPCVOID>(param),
                                       wcslen(param) * sizeof(WCHAR)));

  ResumeThread(thread);

  DWORD res      = WaitForSingleObject(process, INFINITE);
  DWORD exitCode = NO_ERROR;
  res            = GetExitCodeProcess(process, &exitCode);
  EXPECT_EQ(10002, exitCode);  // used init function exits process with this exit code

  CloseHandle(process);
  CloseHandle(thread);
}

TEST(InjectingTest, InjectionNoQuitInit)
{
  // Verify lib can inject with a init function with null parameters

  HANDLE process, thread;
  spawn(process, thread);
  EXPECT_NO_THROW(InjectLib::InjectDLL(process, thread, INJECT_LIB, "InitNoQuit"));
  ResumeThread(thread);

  DWORD res      = WaitForSingleObject(process, INFINITE);
  DWORD exitCode = NO_ERROR;
  res            = GetExitCodeProcess(process, &exitCode);
  EXPECT_EQ(0, exitCode);  // expect regular exit from process

  CloseHandle(process);
  CloseHandle(thread);
}

TEST(InjectingTest, InjectionSkipInit)
{
  // verify the skip-on-missing mechanism for init function works

  HANDLE process, thread;
  spawn(process, thread);
  EXPECT_NO_THROW(InjectLib::InjectDLL(process, thread, INJECT_LIB, "__InitInvalid",
                                       nullptr, 0, true));
  ResumeThread(thread);

  DWORD res      = WaitForSingleObject(process, INFINITE);
  DWORD exitCode = NO_ERROR;
  res            = GetExitCodeProcess(process, &exitCode);
  EXPECT_EQ(NOERROR, exitCode);

  CloseHandle(process);
  CloseHandle(thread);
}

int main(int argc, char** argv)
{
  auto logger = spdlog::stdout_logger_mt("usvfs");
  logger->set_level(spdlog::level::warn);

  boost::filesystem::path filePath(winapi::wide::getModuleFileName(nullptr));
  SetCurrentDirectoryW(filePath.parent_path().wstring().c_str());

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
