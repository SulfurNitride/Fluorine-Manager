#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
constexpr unsigned int LINKFLAG_CREATETARGET = 0x00000004;
constexpr unsigned int LINKFLAG_RECURSIVE = 0x00000008;
constexpr unsigned int LINKFLAG_DIRECTORY = 0x00000020;
constexpr std::uint32_t kFormatVersion = 2;
constexpr std::size_t kMaxStringBytes = 16 * 1024 * 1024;
constexpr std::uintmax_t kMaxRequestBytes = 512 * 1024 * 1024;
constexpr std::uint32_t kMaxEntries = 2'000'000;
using Clock = std::chrono::steady_clock;

struct Mapping
{
  enum class InstallMode : std::uint8_t
  {
    Normal = 0,
    Shallow = 1,
    AfterSnapshot = 2,
  };

  bool directory = false;
  bool createTarget = false;
  InstallMode mode = InstallMode::Normal;
  std::wstring source;
  std::wstring destination;
};

struct ResolvedMapping
{
  bool directory = false;
  std::wstring source;
  std::wstring destination;
};

struct ForcedLibrary
{
  std::wstring process;
  std::wstring library;
};

struct Request
{
  std::string instance;
  std::wstring target;
  std::wstring cwd;
  std::wstring logPath;
  std::vector<std::wstring> arguments;
  std::vector<Mapping> mappings;
  std::vector<ResolvedMapping> resolvedMappings;
  std::vector<ForcedLibrary> forcedLibraries;
  std::vector<std::wstring> executableBlacklist;
  std::vector<std::wstring> skipFileSuffixes;
  std::vector<std::wstring> skipDirectories;
};

std::wstring fromUtf8(const std::string& value)
{
  if (value.empty()) return {};
  const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                        static_cast<int>(value.size()), nullptr, 0);
  if (count <= 0) throw std::runtime_error("invalid UTF-8 in request");
  std::wstring result(static_cast<std::size_t>(count), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), result.data(), count) != count) {
    throw std::runtime_error("unable to decode UTF-8 request value");
  }
  return result;
}

class Reader
{
public:
  explicit Reader(std::vector<std::uint8_t> bytes) : m_bytes(std::move(bytes)) {}

  std::uint8_t u8()
  {
    require(1);
    return m_bytes[m_offset++];
  }

  std::uint32_t u32()
  {
    require(4);
    const auto* p = m_bytes.data() + m_offset;
    m_offset += 4;
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
  }

  std::string utf8()
  {
    const std::uint32_t size = u32();
    if (size > kMaxStringBytes) throw std::runtime_error("request string too large");
    require(size);
    const char* first = reinterpret_cast<const char*>(m_bytes.data() + m_offset);
    m_offset += size;
    return std::string(first, first + size);
  }

  std::wstring wide() { return fromUtf8(utf8()); }

  std::uint32_t count()
  {
    const auto value = u32();
    if (value > kMaxEntries) throw std::runtime_error("request entry count too large");
    return value;
  }

  bool done() const { return m_offset == m_bytes.size(); }

private:
  void require(std::size_t count)
  {
    if (count > m_bytes.size() - m_offset) {
      throw std::runtime_error("truncated USVFS request");
    }
  }

  std::vector<std::uint8_t> m_bytes;
  std::size_t m_offset = 0;
};

Request readRequest(const std::filesystem::path& path)
{
  std::error_code sizeError;
  const std::uintmax_t requestSize = std::filesystem::file_size(path, sizeError);
  if (sizeError) throw std::runtime_error("unable to stat USVFS request");
  if (requestSize > kMaxRequestBytes) {
    throw std::runtime_error("USVFS request is larger than 512 MiB");
  }

  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("unable to open USVFS request");
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
  input.close();

  static constexpr std::uint8_t magic[] = {'F', 'U', 'S', 'V', 'F', 'S', '1', 0};
  if (bytes.size() < sizeof(magic) ||
      !std::equal(std::begin(magic), std::end(magic), bytes.begin())) {
    throw std::runtime_error("invalid USVFS request magic");
  }
  bytes.erase(bytes.begin(), bytes.begin() + sizeof(magic));
  Reader reader(std::move(bytes));
  const std::uint32_t version = reader.u32();
  if (version != 1 && version != kFormatVersion) {
    throw std::runtime_error("unsupported USVFS request version");
  }

  Request request;
  request.instance = reader.utf8();
  request.target = reader.wide();
  request.cwd = reader.wide();
  request.logPath = reader.wide();

  for (std::uint32_t count = reader.count(); count != 0; --count) {
    request.arguments.push_back(reader.wide());
  }
  for (std::uint32_t count = reader.count(); count != 0; --count) {
    Mapping mapping;
    mapping.directory = reader.u8() != 0;
    mapping.createTarget = reader.u8() != 0;
    if (version >= 2) {
      const auto mode = reader.u8();
      if (mode > static_cast<std::uint8_t>(Mapping::InstallMode::AfterSnapshot)) {
        throw std::runtime_error("invalid USVFS mapping install mode");
      }
      mapping.mode = static_cast<Mapping::InstallMode>(mode);
    }
    mapping.source = reader.wide();
    mapping.destination = reader.wide();
    request.mappings.push_back(std::move(mapping));
  }
  if (version >= 2) {
    for (std::uint32_t count = reader.count(); count != 0; --count) {
      ResolvedMapping mapping;
      mapping.directory = reader.u8() != 0;
      mapping.source = reader.wide();
      mapping.destination = reader.wide();
      request.resolvedMappings.push_back(std::move(mapping));
    }
  }
  for (std::uint32_t count = reader.count(); count != 0; --count) {
    request.forcedLibraries.push_back({reader.wide(), reader.wide()});
  }
  for (std::uint32_t count = reader.count(); count != 0; --count) {
    request.executableBlacklist.push_back(reader.wide());
  }
  for (std::uint32_t count = reader.count(); count != 0; --count) {
    request.skipFileSuffixes.push_back(reader.wide());
  }
  for (std::uint32_t count = reader.count(); count != 0; --count) {
    request.skipDirectories.push_back(reader.wide());
  }
  if (!reader.done()) throw std::runtime_error("unexpected data after USVFS request");
  return request;
}

std::wstring quoteArgument(const std::wstring& value)
{
  if (value.empty()) return L"\"\"";
  if (value.find_first_of(L" \t\n\v\"") == std::wstring::npos) return value;

  std::wstring result = L"\"";
  std::size_t backslashes = 0;
  for (wchar_t ch : value) {
    if (ch == L'\\') {
      ++backslashes;
    } else if (ch == L'\"') {
      result.append(backslashes * 2 + 1, L'\\');
      result.push_back(L'\"');
      backslashes = 0;
    } else {
      result.append(backslashes, L'\\');
      backslashes = 0;
      result.push_back(ch);
    }
  }
  result.append(backslashes * 2, L'\\');
  result.push_back(L'\"');
  return result;
}

std::wstring commandLine(const Request& request)
{
  std::wstring result = quoteArgument(request.target);
  for (const auto& argument : request.arguments) {
    result.push_back(L' ');
    result += quoteArgument(argument);
  }
  return result;
}

std::filesystem::path moduleDirectory()
{
  std::wstring path(32768, L'\0');
  const DWORD size = GetModuleFileNameW(nullptr, path.data(),
                                        static_cast<DWORD>(path.size()));
  if (size == 0 || size == path.size()) {
    throw std::runtime_error("unable to resolve helper directory");
  }
  path.resize(size);
  return std::filesystem::path(path).parent_path();
}

template <class T>
T loadFunction(HMODULE module, const char* name)
{
  auto address = GetProcAddress(module, name);
  if (!address) throw std::runtime_error(std::string("missing USVFS export: ") + name);
  return reinterpret_cast<T>(address);
}

template <class T>
T loadOptionalFunction(HMODULE module, const char* name)
{
  return reinterpret_cast<T>(GetProcAddress(module, name));
}

struct UsvfsApi
{
  struct Parameters;
  struct VirtualMapping
  {
    LPCWSTR source;
    LPCWSTR destination;
    unsigned int flags;
  };
  using CreateParameters = Parameters* (*)();
  using FreeParameters = void (*)(Parameters*);
  using SetInstanceName = void (*)(Parameters*, const char*);
  using SetDebugMode = void (*)(Parameters*, BOOL);
  using SetLogLevel = void (*)(Parameters*, std::uint8_t);
  using InitLogging = void(WINAPI*)(bool);
  using CreateVfs = BOOL(WINAPI*)(const Parameters*);
  using DisconnectVfs = void(WINAPI*)();
  using ClearMappings = void(WINAPI*)();
  using LinkDirectory = BOOL(WINAPI*)(LPCWSTR, LPCWSTR, unsigned int);
  using LinkFile = BOOL(WINAPI*)(LPCWSTR, LPCWSTR, unsigned int);
  using LinkMappings = BOOL(WINAPI*)(const VirtualMapping*, std::size_t);
  using CreateHooked = BOOL(WINAPI*)(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES,
                                      LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID,
                                      LPCWSTR, LPSTARTUPINFOW,
                                      LPPROCESS_INFORMATION);
  using ProcessList = BOOL(WINAPI*)(std::size_t*, LPDWORD);
  using ClearWideList = void(WINAPI*)();
  using AddWideValue = void(WINAPI*)(LPCWSTR);
  using ForceLibrary = void(WINAPI*)(LPCWSTR, LPCWSTR);
  using GetLogMessage = bool(WINAPI*)(LPSTR, std::size_t, bool);
  using VersionString = const char*(WINAPI*)();

  HMODULE module = nullptr;
  bool connected = false;
  CreateParameters createParameters{};
  FreeParameters freeParameters{};
  SetInstanceName setInstanceName{};
  SetDebugMode setDebugMode{};
  SetLogLevel setLogLevel{};
  InitLogging initLogging{};
  CreateVfs createVfs{};
  DisconnectVfs disconnectVfs{};
  ClearMappings clearMappings{};
  LinkDirectory linkDirectory{};
  LinkFile linkFile{};
  LinkMappings linkMappings{};
  CreateHooked createHooked{};
  ProcessList processList{};
  ClearWideList clearExecutableBlacklist{};
  AddWideValue blacklistExecutable{};
  ClearWideList clearSkipFileSuffixes{};
  AddWideValue addSkipFileSuffix{};
  ClearWideList clearSkipDirectories{};
  AddWideValue addSkipDirectory{};
  ClearWideList clearLibraryForceLoads{};
  ForceLibrary forceLoadLibrary{};
  GetLogMessage getLogMessage{};
  VersionString versionString{};

  explicit UsvfsApi(const std::filesystem::path& dll)
  {
    module = LoadLibraryW(dll.c_str());
    if (!module) throw std::runtime_error("unable to load usvfs_x64.dll");
    createParameters = loadFunction<CreateParameters>(module, "usvfsCreateParameters");
    freeParameters = loadFunction<FreeParameters>(module, "usvfsFreeParameters");
    setInstanceName = loadFunction<SetInstanceName>(module, "usvfsSetInstanceName");
    setDebugMode = loadFunction<SetDebugMode>(module, "usvfsSetDebugMode");
    setLogLevel = loadFunction<SetLogLevel>(module, "usvfsSetLogLevel");
    initLogging = loadFunction<InitLogging>(module, "usvfsInitLogging");
    createVfs = loadFunction<CreateVfs>(module, "usvfsCreateVFS");
    disconnectVfs = loadFunction<DisconnectVfs>(module, "usvfsDisconnectVFS");
    clearMappings = loadFunction<ClearMappings>(module, "usvfsClearVirtualMappings");
    linkDirectory = loadFunction<LinkDirectory>(module, "usvfsVirtualLinkDirectoryStatic");
    linkFile = loadFunction<LinkFile>(module, "usvfsVirtualLinkFile");
    linkMappings = loadOptionalFunction<LinkMappings>(module, "usvfsVirtualLinkMappings");
    createHooked = loadFunction<CreateHooked>(module, "usvfsCreateProcessHooked");
    processList = loadFunction<ProcessList>(module, "usvfsGetVFSProcessList");
    clearExecutableBlacklist = loadFunction<ClearWideList>(module, "usvfsClearExecutableBlacklist");
    blacklistExecutable = loadFunction<AddWideValue>(module, "usvfsBlacklistExecutable");
    clearSkipFileSuffixes = loadFunction<ClearWideList>(module, "usvfsClearSkipFileSuffixes");
    addSkipFileSuffix = loadFunction<AddWideValue>(module, "usvfsAddSkipFileSuffix");
    clearSkipDirectories = loadFunction<ClearWideList>(module, "usvfsClearSkipDirectories");
    addSkipDirectory = loadFunction<AddWideValue>(module, "usvfsAddSkipDirectory");
    clearLibraryForceLoads = loadFunction<ClearWideList>(module, "usvfsClearLibraryForceLoads");
    forceLoadLibrary = loadFunction<ForceLibrary>(module, "usvfsForceLoadLibrary");
    getLogMessage = loadFunction<GetLogMessage>(module, "usvfsGetLogMessages");
    versionString = loadFunction<VersionString>(module, "usvfsVersionString");
  }

  ~UsvfsApi()
  {
    if (connected && disconnectVfs) disconnectVfs();
    if (module) FreeLibrary(module);
  }
};

void writeLog(std::ofstream& log, const std::string& message)
{
  std::cerr << message << '\n';
  if (log) {
    log << message << '\n';
    log.flush();
  }
}

// Open a diagnostic log before any work that can fail, so failure messages
// are recorded in the same file as the normal launch output. Creating the
// parent directory throws like it always did; an unopenable stream is simply
// reported by `log` testing false in writeLog().
void openLogFile(std::ofstream& log, const std::filesystem::path& path)
{
  if (path.empty() || log.is_open()) return;
  const std::filesystem::path parent = path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  log.open(path, std::ios::app);
}

long long elapsedMilliseconds(Clock::time_point start,
                              Clock::time_point end = Clock::now())
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
      .count();
}

void writeBenchmark(std::ofstream& log, const std::string& phase,
                    Clock::time_point start, Clock::time_point end,
                    const std::string& details = {})
{
  std::string message = "[benchmark] format=1 phase=" + phase +
                        " elapsed_ms=" +
                        std::to_string(elapsedMilliseconds(start, end));
  if (!details.empty()) message += " " + details;
  writeLog(log, message);
}

void drainLogs(UsvfsApi& api, std::ofstream& log)
{
  std::vector<char> buffer(64 * 1024, '\0');
  while (api.getLogMessage(buffer.data(), buffer.size(), false)) {
    writeLog(log, buffer.data());
    std::fill(buffer.begin(), buffer.end(), '\0');
  }
}

std::vector<DWORD> activeProcesses(UsvfsApi& api)
{
  for (int attempt = 0; attempt < 4; ++attempt) {
    std::size_t count = 0;
    if (!api.processList(&count, nullptr)) {
      throw std::runtime_error("unable to query USVFS process count");
    }
    std::vector<DWORD> pids(count);
    std::size_t capacity = pids.size();
    if (!api.processList(&capacity, pids.empty() ? nullptr : pids.data())) {
      throw std::runtime_error("unable to query USVFS process list");
    }
    if (capacity <= pids.size()) {
      pids.resize(capacity);
      return pids;
    }
  }
  throw std::runtime_error("USVFS process list changed too often");
}
}

int wmain(int argc, wchar_t** argv)
{
  if (argc == 2 && std::wcscmp(argv[1], L"--self-test") == 0) {
    try {
      const auto directory = moduleDirectory();
      SetDllDirectoryW(directory.c_str());
      UsvfsApi api(directory / L"usvfs_x64.dll");
      std::cout << "Fluorine USVFS runtime OK: " << api.versionString() << '\n';
      return 0;
    } catch (const std::exception& error) {
      std::cerr << "Fluorine USVFS self-test: " << error.what() << '\n';
      return 1;
    }
  }

  if (argc != 2 && argc != 3) {
    std::cerr << "usage: fluorine-usvfs-launcher.exe <request-file> "
                 "[log-file]|--self-test\n";
    return 2;
  }

  const auto helperStartedAt = Clock::now();
  const std::filesystem::path requestPath(argv[1]);
  std::ofstream log;
  try {
    // The log path Fluorine passes out-of-band is usable before the request
    // can be read, so request-read failures land in the log instead of only
    // on a console the helper may not have.
    if (argc == 3) openLogFile(log, std::filesystem::path(argv[2]));

    Request request = readRequest(requestPath);
    const auto requestParsedAt = Clock::now();
    DeleteFileW(requestPath.c_str());

    // Fall back to the path embedded in the request when none was supplied.
    openLogFile(log, std::filesystem::path(request.logPath));
    writeBenchmark(log, "request_parse", helperStartedAt, requestParsedAt,
                   "mappings=" + std::to_string(request.mappings.size()));

    const auto dllLoadStartedAt = Clock::now();
    const auto directory = moduleDirectory();
    SetDllDirectoryW(directory.c_str());
    UsvfsApi api(directory / L"usvfs_x64.dll");
    const auto dllLoadedAt = Clock::now();
    writeBenchmark(log, "dll_load", dllLoadStartedAt, dllLoadedAt);
    writeLog(log, std::string("Fluorine USVFS helper using ") + api.versionString());
    api.initLogging(false);

    const auto vfsCreateStartedAt = Clock::now();
    auto* parameters = api.createParameters();
    if (!parameters) throw std::runtime_error("usvfsCreateParameters failed");
    api.setInstanceName(parameters, request.instance.c_str());
    api.setDebugMode(parameters, FALSE);
    // Info keeps correctness diagnostics without allowing per-file debug I/O
    // to perturb the startup time being measured.
    api.setLogLevel(parameters, 1);
    if (!api.createVfs(parameters)) {
      api.freeParameters(parameters);
      throw std::runtime_error("usvfsCreateVFS failed");
    }
    api.connected = true;
    api.freeParameters(parameters);
    const auto vfsCreatedAt = Clock::now();
    writeBenchmark(log, "vfs_create", vfsCreateStartedAt, vfsCreatedAt);

    api.clearExecutableBlacklist();
    for (const auto& value : request.executableBlacklist) api.blacklistExecutable(value.c_str());
    api.clearSkipFileSuffixes();
    for (const auto& value : request.skipFileSuffixes) api.addSkipFileSuffix(value.c_str());
    api.clearSkipDirectories();
    for (const auto& value : request.skipDirectories) api.addSkipDirectory(value.c_str());
    api.clearLibraryForceLoads();
    for (const auto& value : request.forcedLibraries) {
      api.forceLoadLibrary(value.process.c_str(), value.library.c_str());
    }

    const auto mappingStartedAt = Clock::now();
    api.clearMappings();

    auto linkOrdinary = [&](const Mapping& mapping, bool recursiveDirectories) {
      BOOL linked = FALSE;
      if (mapping.directory) {
        unsigned int flags = recursiveDirectories ? LINKFLAG_RECURSIVE : 0;
        if (mapping.createTarget) flags |= LINKFLAG_CREATETARGET;
        linked = api.linkDirectory(mapping.source.c_str(),
                                   mapping.destination.c_str(), flags);
      } else {
        linked = api.linkFile(mapping.source.c_str(), mapping.destination.c_str(), 0);
      }
      if (!linked) {
        throw std::runtime_error("USVFS mapping failed with Windows error " +
                                 std::to_string(GetLastError()));
      }
    };

    bool importedResolvedSnapshot = false;
    bool snapshotFallback = false;
    if (!request.resolvedMappings.empty() && api.linkMappings != nullptr) {
      // Preserve ordered root/write-target mappings without asking Wine to
      // recursively scan Data. Non-Data mappings retain their normal behavior.
      for (const auto& mapping : request.mappings) {
        if (mapping.mode == Mapping::InstallMode::AfterSnapshot) continue;
        linkOrdinary(mapping, mapping.mode != Mapping::InstallMode::Shallow);
      }

      std::vector<UsvfsApi::VirtualMapping> snapshot;
      snapshot.reserve(request.resolvedMappings.size());
      for (const auto& mapping : request.resolvedMappings) {
        snapshot.push_back({mapping.source.c_str(), mapping.destination.c_str(),
                            mapping.directory ? LINKFLAG_DIRECTORY : 0});
      }

      if (api.linkMappings(snapshot.data(), snapshot.size())) {
        importedResolvedSnapshot = true;
        // A resolved directory entry may share a destination with a nested
        // custom write target. Reapply create-target roots after the bulk
        // import so their flag and physical target remain authoritative while
        // retaining the imported children.
        for (const auto& mapping : request.mappings) {
          if (mapping.mode == Mapping::InstallMode::Shallow &&
              mapping.createTarget) {
            linkOrdinary(mapping, false);
          }
        }
        for (const auto& mapping : request.mappings) {
          if (mapping.mode == Mapping::InstallMode::AfterSnapshot) {
            linkOrdinary(mapping, true);
          }
        }
      } else {
        const DWORD snapshotError = GetLastError();
        writeLog(log, "Resolved USVFS snapshot import failed with Windows error " +
                          std::to_string(snapshotError) +
                          "; rebuilding ordinary recursive mappings");
        api.clearMappings();
        snapshotFallback = true;
        for (const auto& mapping : request.mappings) linkOrdinary(mapping, true);
      }
    } else {
      snapshotFallback = !request.resolvedMappings.empty();
      if (snapshotFallback) {
        writeLog(log, "USVFS runtime has no bulk snapshot export; using ordinary "
                      "recursive mappings");
      }
      for (const auto& mapping : request.mappings) linkOrdinary(mapping, true);
    }
    const auto mappingsInstalledAt = Clock::now();
    writeBenchmark(log, "mapping_install", mappingStartedAt,
                   mappingsInstalledAt,
                   "mappings=" + std::to_string(request.mappings.size()) +
                       " snapshot_entries=" +
                       std::to_string(request.resolvedMappings.size()) +
                       " snapshot_imported=" +
                       std::to_string(importedResolvedSnapshot ? 1 : 0) +
                       " snapshot_fallback=" +
                       std::to_string(snapshotFallback ? 1 : 0));
    writeLog(log, "Installed " + std::to_string(request.mappings.size()) +
                      " USVFS mappings" +
                      (importedResolvedSnapshot
                           ? " plus " +
                                 std::to_string(request.resolvedMappings.size()) +
                                 " resolved snapshot entries"
                           : ""));

    const auto targetCreateStartedAt = Clock::now();
    std::wstring command = commandLine(request);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    // The helper is a Windows-GUI-subsystem binary and therefore owns no
    // console. Without this a console-subsystem target would be handed a new
    // console window of its own.
    if (!api.createHooked(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
                          CREATE_BREAKAWAY_FROM_JOB | CREATE_NO_WINDOW, nullptr,
                          request.cwd.empty() ? nullptr : request.cwd.c_str(),
                          &startup, &process)) {
      throw std::runtime_error("usvfsCreateProcessHooked failed with Windows error " +
                               std::to_string(GetLastError()));
    }
    const auto targetCreatedAt = Clock::now();
    writeBenchmark(log, "target_inject", targetCreateStartedAt,
                   targetCreatedAt,
                   "pid=" + std::to_string(process.dwProcessId));
    CloseHandle(process.hThread);
    const auto targetLifetimeStartedAt = Clock::now();
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    const auto targetExitedAt = Clock::now();
    writeBenchmark(log, "target_lifetime", targetLifetimeStartedAt,
                   targetExitedAt, "exit_code=" + std::to_string(exitCode));

    const auto childDrainStartedAt = Clock::now();
    const DWORD self = GetCurrentProcessId();
    // Script-extender loaders can exit while the real game is still between
    // injection and registration in USVFS's shared process list. An immediate
    // empty query here races SkyrimSE/Fallout4 initialization and can tear the
    // controller down before the child finishes attaching. Require both an
    // initial registration grace period and a stable empty interval. This is
    // shutdown/lifetime bookkeeping; it is not part of mapping or injection
    // startup time.
    constexpr auto registrationGrace = std::chrono::seconds(2);
    constexpr auto stableEmptyPeriod = std::chrono::milliseconds(500);
    const auto acceptEmptyAfter = childDrainStartedAt + registrationGrace;
    Clock::time_point emptySince{};
    bool observedChild = false;
    for (;;) {
      drainLogs(api, log);
      const auto pids = activeProcesses(api);
      bool otherProcessActive = false;
      for (DWORD pid : pids) {
        if (pid != self) {
          otherProcessActive = true;
          break;
        }
      }
      const auto now = Clock::now();
      if (otherProcessActive) {
        observedChild = true;
        emptySince = {};
      } else if (now >= acceptEmptyAfter) {
        if (emptySince == Clock::time_point{}) {
          emptySince = now;
        } else if (now - emptySince >= stableEmptyPeriod) {
          break;
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    const auto childrenDrainedAt = Clock::now();
    writeBenchmark(log, "child_drain", childDrainStartedAt,
                   childrenDrainedAt,
                   std::string("observed_child=") +
                       (observedChild ? "1" : "0") +
                       " registration_grace_ms=" +
                       std::to_string(
                           std::chrono::duration_cast<std::chrono::milliseconds>(
                               registrationGrace)
                               .count()) +
                       " stable_empty_ms=" +
                       std::to_string(stableEmptyPeriod.count()));

    drainLogs(api, log);
    api.disconnectVfs();
    api.connected = false;
    writeBenchmark(log, "helper_total", helperStartedAt, Clock::now(),
                   "exit_code=" + std::to_string(exitCode));
    return static_cast<int>(exitCode);
  } catch (const std::exception& error) {
    // Same text as before, but also into the log: this is the only place
    // request-read and runtime failures are reported.
    writeLog(log, std::string("Fluorine USVFS helper: ") + error.what());
    DeleteFileW(requestPath.c_str());
    return 1;
  }
}
