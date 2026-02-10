#include <gtest/gtest.h>
#include <hooklib.h>
#include <iostream>
#include <ttrampolinepool.h>
#include <utility.h>
#include <windows_sane.h>
// #include <boost/thread.hpp>
#include <boost/filesystem.hpp>
#include <exceptionex.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>
#include <winapi.h>

namespace fs = boost::filesystem;

#include <stringutils.h>

using namespace std;
using namespace HookLib;

class TempFile
{
public:
  TempFile(const wchar_t* relative) : wpath(winapi::wide::getModuleFileName(nullptr))
  {
    size_t path_end = wpath.rfind(L'\\');
    if (path_end != std::wstring::npos) {
      wpath.erase(path_end + 1);
      wpath += L"..\\temp\\";
      wpath += relative;
    } else
      wpath = relative;
    path =
        usvfs::shared::string_cast<std::string>(wpath, usvfs::shared::CodePage::UTF8);
  }

  const char* c_str() const { return path.c_str(); }
  const wchar_t* w_str() const { return wpath.c_str(); }

private:
  std::string path;
  std::wstring wpath;
};

static const HANDLE MARKERHANDLE = reinterpret_cast<HANDLE>(0x1CC0FFEE);
static const TempFile VALID_FILENAME{L"VALID_FILENAME"};
static const TempFile INVALID_FILENAME{L"\\<>/"};

#include "test_hooks.cpp"

static bool stubCalled = false;

void __cdecl CreateFileStub(LPVOID)
{
  stubCalled = true;
}

class HookingTest : public testing::Test
{
public:
  void SetUp()
  {
    /*    typedef sinks::synchronous_sink<sinks::text_ostream_backend> text_sink;
        boost::shared_ptr<text_sink> sink = boost::make_shared<text_sink>();

        // Add a stream to write log to
        sink->locked_backend()->add_stream(boost::make_shared<std::ofstream>("c:\\temp\\testing_out.log"));

        // Register the sink in the logging core
        logging::core::get()->add_sink(sink);

        sink->set_filter(expr::attr<LogLevel>("Severity") >= LogLevel::Debug);*/
  }

  void TearDown() {}

private:
};

TEST(GetProcAddressTest, ReturnsValidResults)
{
  HMODULE mh = GetModuleHandleA("KernelBase.dll");
  EXPECT_NE(nullptr, mh);
  EXPECT_EQ(GetProcAddress(mh, "CreateFileA"), MyGetProcAddress(mh, "CreateFileA"));
}

TEST_F(HookingTest, CanHook)
{
  HMODULE k32Mod  = GetModuleHandleA("kernel32.dll");
  HOOKHANDLE hook = InstallHook(k32Mod, "CreateFileW", THCreateFileW_1);
  if (hook == INVALID_HOOK) {
    k32Mod = GetModuleHandleA("kernelbase.dll");
    hook   = InstallHook(k32Mod, "CreateFileW", THCreateFileW_1);
  }
  EXPECT_NE(INVALID_HOOK, hook);
  RemoveHook(hook);
}

TEST_F(HookingTest, CanStub)
{
  HMODULE k32Mod  = GetModuleHandleA("kernel32.dll");
  HOOKHANDLE hook = InstallStub(k32Mod, "CreateFileW", CreateFileStub);
  if (hook == INVALID_HOOK) {
    k32Mod = GetModuleHandleA("kernelbase.dll");
    hook   = InstallStub(k32Mod, "CreateFileW", CreateFileStub);
  }
  EXPECT_NE(INVALID_HOOK, hook);
  RemoveHook(hook);
}

TEST_F(HookingTest, RemoveHook)
{
  // test that we can remove a hook
  HMODULE k32Mod  = GetModuleHandleA("kernel32.dll");
  HOOKHANDLE hook = InstallHook(k32Mod, "CreateFileA", THCreateFileA_1);
  if (hook == INVALID_HOOK) {
    k32Mod = GetModuleHandleA("kernelbase.dll");
    hook   = InstallHook(k32Mod, "CreateFileA", THCreateFileA_1);
  }

  EXPECT_NE(INVALID_HOOK, hook);
  RemoveHook(hook);
  HANDLE test = CreateFileA(INVALID_FILENAME.c_str(), GENERIC_READ, 0, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  EXPECT_EQ(INVALID_HANDLE_VALUE, test);
}

TEST_F(HookingTest, CreateFileStubTest)
{
  stubCalled = false;
  // test if our stub works
  HMODULE k32Mod  = GetModuleHandleA("kernel32.dll");
  HOOKHANDLE hook = InstallStub(k32Mod, "CreateFileA", CreateFileStub);
  if (hook == INVALID_HOOK) {
    k32Mod = GetModuleHandleA("kernelbase.dll");
    hook   = InstallStub(k32Mod, "CreateFileA", CreateFileStub);
  }
  EXPECT_NE(INVALID_HOOK, hook);
  HANDLE test = CreateFileA(INVALID_FILENAME.c_str(), GENERIC_READ, 0, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  RemoveHook(hook);
  EXPECT_EQ(true, stubCalled);
  EXPECT_EQ(INVALID_HANDLE_VALUE, test);
}

TEST_F(HookingTest, CreateFileHook)
{
  // test if our hook works
  HMODULE k32Mod  = GetModuleHandleA("kernel32.dll");
  HOOKHANDLE hook = InstallHook(k32Mod, "CreateFileA", THCreateFileA_1);
  if (hook == INVALID_HOOK) {
    k32Mod = GetModuleHandleA("kernelbase.dll");
    hook   = InstallHook(k32Mod, "CreateFileA", THCreateFileA_1);
  }
  HANDLE test = CreateFileA(INVALID_FILENAME.c_str(), GENERIC_READ, 0, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  RemoveHook(hook);
  EXPECT_EQ(MARKERHANDLE, test);
}

TEST_F(HookingTest, CreateFileWHook)
{
  // test if our hook works
  HMODULE k32Mod  = GetModuleHandleA("kernel32.dll");
  HOOKHANDLE hook = InstallHook(k32Mod, "CreateFileW", THCreateFileW_1);
  if (hook == INVALID_HOOK) {
    k32Mod = GetModuleHandleA("kernelbase.dll");
    hook   = InstallHook(k32Mod, "CreateFileW", THCreateFileW_1);
  }
  HANDLE test = CreateFileW(INVALID_FILENAME.w_str(), 0x42, 0x43,
                            (LPSECURITY_ATTRIBUTES)0x44, 0x45, 0x46, (HANDLE)0x47);
  RemoveHook(hook);
  EXPECT_EQ(MARKERHANDLE, test);
}

TEST_F(HookingTest, CreateFileHookRecursion)
{
  // test that the trampoline works, so we can call the original function from
  // within the hook
  HMODULE k32Mod  = GetModuleHandleA("kernel32.dll");
  HOOKHANDLE hook = InstallHook(k32Mod, "CreateFileA", THCreateFileA_1);
  if (hook == INVALID_HOOK) {
    k32Mod = GetModuleHandleA("kernelbase.dll");
    hook   = InstallHook(k32Mod, "CreateFileA", THCreateFileA_1);
  }
  HANDLE test = CreateFileA(VALID_FILENAME.c_str(), GENERIC_READ, 0, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  RemoveHook(hook);
  EXPECT_NE(MARKERHANDLE, test);
}

TEST_F(HookingTest, Threading)
{
  // test that multiple threads can concurrently call a hooked function without
  // incorrect results.
  // TODO: this test doesn't reliably find thread-unsafeties
  // NOTE: the hooklib currently does not claim that hook installation or removal
  //   is thread-safe, only the hooked functions shouldn't become less thread-safe by
  //   being hooked!

  static const int NUM_THREADS = 100;
  static const int NUM_TRIES   = 1000;

  HMODULE k32Mod  = GetModuleHandleA("kernel32.dll");
  HOOKHANDLE hook = InstallHook(k32Mod, "CreateFileA", THCreateFileA_1);
  if (hook == INVALID_HOOK) {
    k32Mod = GetModuleHandleA("kernelbase.dll");
    hook   = InstallHook(k32Mod, "CreateFileA", THCreateFileA_1);
  }
  std::thread threads[NUM_THREADS];
  for (int i = 0; i < NUM_THREADS; ++i) {
    threads[i] = std::thread([i] {
      for (int count = 0; count < NUM_TRIES; ++count) {
        HANDLE test = CreateFileA(INVALID_FILENAME.c_str(), GENERIC_READ, 0, nullptr,
                                  OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        EXPECT_EQ(MARKERHANDLE, test);
      }
    });
  }

  for (int i = 0; i < NUM_THREADS; ++i) {
    threads[i].join();
  }

  RemoveHook(hook);
}

int main(int argc, char** argv)
{
  auto logger = spdlog::stdout_logger_mt("usvfs");
  logger->set_level(spdlog::level::warn);
  TrampolinePool::initialize();
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
