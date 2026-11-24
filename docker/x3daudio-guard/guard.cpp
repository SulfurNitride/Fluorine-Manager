// X3DAudio1_7.dll wrapper that guards the audio data copy in Creation Engine
// games. All X3DAudio exports forward to Microsoft's DLL (x3daudio1_7_ms.dll).
//
// On load it locates the inlined memcpy_s that copies a sound's data into its
// buffer (see scanner.h) and replaces the 5-byte bounds check with a jump to a
// trampoline. When the data fits, the trampoline continues to the original
// copy unchanged. When it does not fit, where the game would abort through
// _invalid_parameter_noinfo, it records the event, copies only what fits, and
// makes the game record that clamped size.
//
// Executables wrapped by Steam DRM are still encrypted while imports load, so
// the search is retried from the game's first GetSystemTimeAsFileTime call,
// which happens right after decryption and before any game thread starts.
//
// Messages go to the process's stderr (Fluorine's game output) and every
// problem or catch is also written to Fluorine's audio log directory.
// FLUORINE_AUDIO_GUARD=0 disables patching.
#define _WIN32_WINNT 0x0601
#include <windows.h>

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

#include "scanner.h"

namespace audioguard {
namespace {

std::uint8_t* gameImage;
std::size_t gameImageSize;
Site site;
SRWLOCK logLock = SRWLOCK_INIT;
SRWLOCK installLock = SRWLOCK_INIT;
std::wstring logPath;
bool logUnavailable;
volatile LONG events;
constexpr LONG kDetailedEvents = 200;

std::string narrow(const std::wstring& text)
{
  if (text.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0,
                                       nullptr, nullptr);
  std::string result(size > 0 ? size : 0, '\0');
  if (size > 0)
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), &result[0], size, nullptr,
                        nullptr);
  return result;
}

std::wstring modulePath(HMODULE module)
{
  wchar_t path[MAX_PATH * 4];
  const DWORD length = GetModuleFileNameW(module, path, MAX_PATH * 4);
  return length && length < MAX_PATH * 4 ? std::wstring(path, length) : std::wstring(L"unknown");
}

std::wstring baseName(const std::wstring& path)
{
  const auto slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::string format(const char* fmt, ...)
{
  char buffer[2048];
  va_list args;
  va_start(args, fmt);
  const int n = std::vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  return std::string(buffer, n < 0 ? 0 : std::min<int>(n, sizeof(buffer) - 1));
}

bool readable(const void* address, std::size_t size)
{
  auto cursor = reinterpret_cast<std::uintptr_t>(address);
  if (!cursor || cursor + size < cursor) return false;
  const auto end = cursor + size;
  while (cursor < end) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<void*>(cursor), &info, sizeof(info)) ||
        info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        !(info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                          PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
      return false;
    const auto next = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
    if (next <= cursor) return false;
    cursor = next;
  }
  return true;
}

template <class T>
bool field(const void* object, std::int32_t offset, T& value)
{
  if (offset < 0 || !readable(static_cast<const std::uint8_t*>(object) + offset, sizeof(T)))
    return false;
  std::memcpy(&value, static_cast<const std::uint8_t*>(object) + offset, sizeof(T));
  return true;
}

// ---------------------------------------------------------------- logging --

void writeConsole(const std::string& text)
{
  OutputDebugStringA(text.c_str());
  const HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
  if (err && err != INVALID_HANDLE_VALUE) {
    DWORD written;
    WriteFile(err, text.data(), DWORD(text.size()), &written, nullptr);
  }
}

bool makeDirectories(const std::wstring& path)
{
  std::size_t start = 3;
  if (path.rfind(L"\\\\", 0) == 0) {
    start = path.find(L'\\', 2);
    if (start == std::wstring::npos) return false;
    start = path.find(L'\\', start + 1);
    if (start == std::wstring::npos) return false;
    ++start;
  }
  for (auto i = start; i < path.size(); ++i)
    if (path[i] == L'\\') CreateDirectoryW(path.substr(0, i).c_str(), nullptr);
  CreateDirectoryW(path.c_str(), nullptr);
  const DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring directoryFrom(const wchar_t* variable, const wchar_t* suffix)
{
  wchar_t value[4096];
  const DWORD length = GetEnvironmentVariableW(variable, value, 4096);
  if (!length || length >= 4096) return {};
  std::wstring result(value);
  if (result.rfind(L"\\??\\unix\\", 0) == 0) result = L"Z:" + result.substr(8);
  else if (result.rfind(L"\\??\\", 0) == 0) result.erase(0, 4);
  if (result[0] == L'/') result = L"Z:" + result;
  if (!(result.size() > 2 && result[1] == L':' && (result[2] == L'\\' || result[2] == L'/')) &&
      result.rfind(L"\\\\", 0) != 0)
    return {};
  result += suffix;
  for (auto& c : result)
    if (c == L'/') c = L'\\';
  return result;
}

HANDLE openLog()
{
  if (!logPath.empty()) {
    const HANDLE file = CreateFileW(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) return file;
    logPath.clear();
  }
  if (logUnavailable) return INVALID_HANDLE_VALUE;
  const wchar_t* variables[] = {L"FLUORINE_AUDIO_LOG_DIR", L"WINE_HOST_XDG_DATA_HOME",
                                L"XDG_DATA_HOME", L"WINE_HOST_HOME", L"HOME", L"LOCALAPPDATA",
                                L"TEMP"};
  const wchar_t* suffixes[] = {L"", L"/fluorine/logs/audio", L"/fluorine/logs/audio",
                               L"/.local/share/fluorine/logs/audio",
                               L"/.local/share/fluorine/logs/audio", L"/Fluorine/logs/audio",
                               L"/Fluorine/logs/audio"};
  SYSTEMTIME time;
  GetSystemTime(&time);
  wchar_t name[200];
  std::swprintf(name, 200, L"\\%ls-%04u%02u%02uT%02u%02u%02uZ-pid%lu-audio-guard.log",
                baseName(modulePath(nullptr)).c_str(), time.wYear, time.wMonth, time.wDay,
                time.wHour, time.wMinute, time.wSecond, GetCurrentProcessId());
  for (unsigned i = 0; i < sizeof(variables) / sizeof(*variables); ++i) {
    std::wstring path = directoryFrom(variables[i], suffixes[i]);
    if (path.empty() || !makeDirectories(path)) continue;
    path += name;
    const HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
      logPath = path;
      return file;
    }
  }
  logUnavailable = true;
  return INVALID_HANDLE_VALUE;
}

// One line on the console; with `record`, the full text in the log file too.
void report(const std::string& line, const std::string& record = {})
{
  const DWORD saved = GetLastError();
  AcquireSRWLockExclusive(&logLock);
  writeConsole("[Fluorine audio guard] " + line + "\n");
  if (!record.empty()) {
    SYSTEMTIME time;
    GetSystemTime(&time);
    std::string text = format("=== Fluorine audio guard %04u-%02u-%02uT%02u:%02u:%02u.%03uZ "
                              "pid=%lu tid=%lu ===\n",
                              time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
                              time.wSecond, time.wMilliseconds, GetCurrentProcessId(),
                              GetCurrentThreadId());
    text += record;
    text += "=== end ===\n\n";
    const HANDLE file = openLog();
    if (file != INVALID_HANDLE_VALUE) {
      std::size_t offset = 0;
      DWORD written;
      while (offset < text.size() &&
             WriteFile(file, text.data() + offset, DWORD(text.size() - offset), &written,
                       nullptr) &&
             written)
        offset += written;
      CloseHandle(file);
      if (offset == text.size())
        writeConsole("[Fluorine audio guard] details: " + narrow(logPath) + "\n");
    } else {
      writeConsole("[Fluorine audio guard] could not create a log file; details follow\n" +
                   text);
    }
  }
  ReleaseSRWLockExclusive(&logLock);
  SetLastError(saved);
}

std::string hex(const std::uint8_t* data, std::size_t size)
{
  std::string out;
  for (std::size_t i = 0; i < size; ++i) out += format(i % 16 ? " %02x" : (i ? "\n  %02x" : "  %02x"), data[i]);
  return out + "\n";
}

std::string gameName() { return narrow(baseName(modulePath(nullptr))); }

// Strings and file handles reachable from the source object that look like
// the sound's file. Heuristic: the games do not keep a documented name here.
std::string fileCandidates(const std::uint8_t* object)
{
  std::string found;
  int count = 0;
  auto addString = [&](const char* text, const char* via) {
    std::size_t length = 0;
    while (length < 260 && readable(text + length, 1) && text[length]) {
      const unsigned char c = text[length];
      if (c < 0x20 || c > 0x7e) return;
      ++length;
    }
    if (length < 5 || length >= 260) return;
    std::string value(text, length);
    std::string lower = value;
    for (auto& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
    static const char* extensions[] = {".wav", ".xwm", ".fuz", ".bsa", ".ba2", ".ogg"};
    bool audio = false;
    for (const char* e : extensions)
      audio |= lower.size() > 4 && lower.compare(lower.size() - 4, 4, e) == 0;
    if (!audio || found.find(value) != std::string::npos || count >= 8) return;
    found += format("  %s (%s)\n", value.c_str(), via);
    ++count;
  };
  auto scanPointers = [&](const std::uint8_t* base, std::size_t size, const char* via, int depth,
                          auto&& self) -> void {
    if (!readable(base, size)) return;
    for (std::size_t i = 0; i + 8 <= size; i += 8) {
      std::uintptr_t value;
      std::memcpy(&value, base + i, 8);
      if (value > 0x10000 && value < 0x7fffffffffffull && readable(reinterpret_cast<void*>(value), 8)) {
        addString(reinterpret_cast<const char*>(value), via);
        if (depth > 0) self(reinterpret_cast<const std::uint8_t*>(value), 0x40, via, depth - 1, self);
      } else if (value && value < 0x10000 && value % 4 == 0 &&
                 GetFileType(reinterpret_cast<HANDLE>(value)) == FILE_TYPE_DISK) {
        wchar_t path[1024];
        const DWORD n = GetFinalPathNameByHandleW(reinterpret_cast<HANDLE>(value), path, 1024, 0);
        if (n && n < 1024) addString(narrow(std::wstring(path, n)).c_str(), "open file handle");
      }
    }
  };
  scanPointers(object, 0x100, "source object", 2, scanPointers);
  return found.empty() ? "  none found\n" : found;
}

// ------------------------------------------------------------- the catch --

extern "C" void onOversizedCopy(const std::uint8_t* object, std::uint64_t count,
                                std::uint64_t capacity, const std::uint8_t* source)
{
  const LONG n = InterlockedIncrement(&events);
  if (n > kDetailedEvents) {
    if (n == kDetailedEvents + 1)
      report(format("%ld oversized audio copies clamped; further events are not logged", n - 1));
    return;
  }
  std::uint32_t read = 0, data = 0, chunk = 0;
  const bool haveRead = field(object, site.readOffset, read);
  const bool haveData = field(object, site.dataOffset, data);
  const bool haveChunk = field(object, site.chunkOffset, chunk);
  std::uint16_t tag = 0, channels = 0, align = 0, bits = 0;
  std::uint32_t rate = 0;
  const bool haveFormat = field(object, 0x20, tag) && field(object, 0x22, channels) &&
                          field(object, 0x24, rate) && field(object, 0x2c, align) &&
                          field(object, 0x2e, bits);

  auto value = [](bool known, std::uint32_t v) {
    return known ? std::to_string(v) : std::string("unknown");
  };
  std::string record = format(
      "game=%s\n"
      "event=oversized audio copy clamped (the game would have aborted in memcpy_s)\n"
      "site=%s+0x%x copy=+0x%x source_register=%s count_register=%s\n"
      "copy_requested=%llu buffer_capacity=%llu copied=%llu dropped=%llu\n",
      narrow(modulePath(nullptr)).c_str(), gameName().c_str(), site.checkRva, site.copyRva,
      registerName64(site.baseReg), registerName32(site.countReg),
      static_cast<unsigned long long>(count), static_cast<unsigned long long>(capacity),
      static_cast<unsigned long long>(capacity), static_cast<unsigned long long>(count - capacity));
  record += format("source_object=%p bytes_read=%s (+0x%x) data_offset=%s (+0x%x) "
                   "data_chunk_size=%s (+0x%x) capacity_field=+0x%x size_field=+0x%x\n",
                   static_cast<const void*>(object), value(haveRead, read).c_str(),
                   site.readOffset, value(haveData, data).c_str(), site.dataOffset,
                   value(haveChunk, chunk).c_str(), site.chunkOffset, site.capacityOffset,
                   site.sizeOffset);
  if (haveFormat)
    record += format("format: tag=0x%04x channels=%u sample_rate=%u block_align=%u bits=%u\n",
                     tag, channels, rate, align, bits);
  if (haveRead && haveData && haveChunk)
    record += format("analysis: data chunk claims %u bytes, %u bytes were read after the "
                     "header, the buffer holds %llu\n",
                     chunk, read > data ? read - data : 0,
                     static_cast<unsigned long long>(capacity));
  if (haveData && data <= 4096 && readable(source - data, data + 16))
    record += "file header and first data bytes:\n" + hex(source - data, std::min<std::uint32_t>(data + 16, 128));
  else if (readable(source, 16))
    record += "first data bytes:\n" + hex(source, 16);
  record += "file candidates (heuristic):\n" + fileCandidates(object);
  record += "result: the sound plays truncated instead of crashing the game\n";

  report(format("clamped oversized audio copy #%ld in %s: %llu bytes requested, buffer holds %llu",
                n, gameName().c_str(), static_cast<unsigned long long>(count),
                static_cast<unsigned long long>(capacity)),
         record);
}

// ----------------------------------------------------------------- patch --

void* allocateNear(std::uint8_t* target, std::size_t size)
{
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  const std::uintptr_t granularity = info.dwAllocationGranularity;
  const std::uintptr_t origin = reinterpret_cast<std::uintptr_t>(target) & ~(granularity - 1);
  for (std::uintptr_t delta = granularity; delta < 0x7ff00000; delta += granularity) {
    for (const std::uintptr_t candidate : {origin - delta, origin + delta}) {
      if (candidate < 0x10000) continue;
      void* memory = VirtualAlloc(reinterpret_cast<void*>(candidate), size,
                                  MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
      if (memory) return memory;
    }
  }
  return nullptr;
}

std::string describe(const Site& s)
{
  return format("site=%s+0x%x source_register=%s count_register=%s capacity=+0x%x "
                "destination=+0x%x read=+0x%x chunk=+0x%x data=+0x%x size=+0x%x\n",
                gameName().c_str(), s.checkRva, registerName64(s.baseReg),
                registerName32(s.countReg), s.capacityOffset, s.destinationOffset,
                s.readOffset, s.chunkOffset, s.dataOffset, s.sizeOffset);
}

bool patch(const Site& found)
{
  std::uint8_t* check = gameImage + found.checkRva;
  const std::uint64_t copy = reinterpret_cast<std::uint64_t>(gameImage + found.copyRva);
  auto* code = static_cast<std::uint8_t*>(allocateNear(check, 128));
  if (!code) {
    report("no memory within 2 GB of the game code; nothing patched",
           "game=" + narrow(modulePath(nullptr)) + "\nerror=trampoline allocation failed\n" +
               describe(found));
    return false;
  }
  std::size_t n = 0;
  auto emit = [&](std::initializer_list<std::uint8_t> bytes) {
    for (auto b : bytes) code[n++] = b;
  };
  auto emit64 = [&](std::uint64_t v) {
    std::memcpy(code + n, &v, 8);
    n += 8;
  };
  emit({0x49, 0x3b, 0xc0, 0x72, 0x0e});                    // cmp rax,r8 ; jb oversized
  emit({0xff, 0x25, 0, 0, 0, 0});                          // jmp [copy]
  emit64(copy);
  emit({0x51, 0x52, 0x41, 0x50, 0x50});                    // push rcx, rdx, r8, rax
  emit({0x48, 0x83, 0xec, 0x20});                          // sub rsp,20h
  emit({0x49, 0x89, 0xd1});                                // r9 = source
  emit({0x4c, 0x89, 0xc2});                                // rdx = count
  emit({0x49, 0x89, 0xc0});                                // r8 = capacity
  emit({0x48, 0x89, std::uint8_t(0xc1 | (found.baseReg << 3))});  // rcx = object
  emit({0x48, 0xb8});                                      // mov rax, onOversizedCopy
  emit64(reinterpret_cast<std::uint64_t>(&onOversizedCopy));
  emit({0xff, 0xd0});                                      // call rax
  emit({0x48, 0x83, 0xc4, 0x20});                          // add rsp,20h
  emit({0x58, 0x41, 0x58, 0x5a, 0x59});                    // pop rax, r8, rdx, rcx
  emit({0x49, 0x89, 0xc0});                                // r8 = capacity
  emit({0x89, std::uint8_t(0xc0 | found.countReg)});       // count = capacity
  emit({0xff, 0x25, 0, 0, 0, 0});                          // jmp [copy]
  emit64(copy);

  DWORD protection;
  if (!VirtualProtect(code, 128, PAGE_EXECUTE_READ, &protection)) {
    VirtualFree(code, 0, MEM_RELEASE);
    report("could not make the trampoline executable; nothing patched",
           "game=" + narrow(modulePath(nullptr)) + "\nerror=VirtualProtect failed (" +
               std::to_string(GetLastError()) + ")\n" + describe(found));
    return false;
  }
  FlushInstructionCache(GetCurrentProcess(), code, n);

  std::uint8_t jump[5] = {0xe9};
  const std::int64_t rel = reinterpret_cast<std::int64_t>(code) -
                           reinterpret_cast<std::int64_t>(check + 5);
  const std::int32_t rel32 = static_cast<std::int32_t>(rel);
  std::memcpy(jump + 1, &rel32, 4);
  DWORD old;
  if (rel != rel32 || !VirtualProtect(check, 5, PAGE_EXECUTE_READWRITE, &old)) {
    VirtualFree(code, 0, MEM_RELEASE);
    report("could not write the game code; nothing patched",
           "game=" + narrow(modulePath(nullptr)) + "\nerror=code page not writable\n" +
               describe(found));
    return false;
  }
  std::memcpy(check, jump, 5);
  VirtualProtect(check, 5, old, &protection);
  FlushInstructionCache(GetCurrentProcess(), check, 5);
  site = found;
  report(format("active in %s (audio copy at +0x%x)", gameName().c_str(), found.checkRva));
  return true;
}

enum class Outcome { Patched, NoSite, Failed };

Outcome install()
{
  AcquireSRWLockExclusive(&installLock);
  const ScanResult result = scan(gameImage, gameImageSize);
  Outcome outcome = Outcome::NoSite;
  if (result.sites.size() == 1) {
    outcome = patch(result.sites[0]) ? Outcome::Patched : Outcome::Failed;
  } else if (result.sites.size() > 1) {
    std::string record = "game=" + narrow(modulePath(nullptr)) +
                         format("\nerror=%zu candidate audio copies; refusing to guess\n",
                                result.sites.size());
    for (const auto& s : result.sites) record += describe(s);
    report(format("found %zu candidate audio copies in %s; nothing patched",
                  result.sites.size(), gameName().c_str()),
           record);
    outcome = Outcome::Failed;
  }
  ReleaseSRWLockExclusive(&installLock);
  return outcome;
}

// ---------------------------------------------- after DRM decryption ----

using SystemTime = void(WINAPI*)(LPFILETIME);
SystemTime originalSystemTime;
void** systemTimeSlot;
volatile LONG deferredAttempts;
volatile LONG deferredDone;

void restoreSlot()
{
  DWORD old, unused;
  if (systemTimeSlot && VirtualProtect(systemTimeSlot, sizeof(void*), PAGE_READWRITE, &old)) {
    InterlockedExchangePointer(systemTimeSlot, reinterpret_cast<void*>(originalSystemTime));
    VirtualProtect(systemTimeSlot, sizeof(void*), old, &unused);
  }
}

void WINAPI deferredSystemTime(LPFILETIME time)
{
  originalSystemTime(time);
  if (deferredDone) return;
  const LONG attempt = InterlockedIncrement(&deferredAttempts);
  const Outcome outcome = install();
  if (outcome == Outcome::NoSite && attempt < 8) return;
  if (InterlockedExchange(&deferredDone, 1)) return;
  restoreSlot();
  if (outcome == Outcome::NoSite)
    report(format("no audio copy found in %s after it started; nothing patched",
                  gameName().c_str()));
}

// The image's import slot for kernel32!GetSystemTimeAsFileTime.
void** importSlot(const char* function)
{
  auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(gameImage);
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(gameImage + dos->e_lfanew);
  const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (!directory.VirtualAddress) return nullptr;
  for (auto* d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(gameImage + directory.VirtualAddress);
       d->Name; ++d) {
    if (!d->OriginalFirstThunk || _stricmp(reinterpret_cast<char*>(gameImage + d->Name), "kernel32.dll"))
      continue;
    auto* names = reinterpret_cast<IMAGE_THUNK_DATA64*>(gameImage + d->OriginalFirstThunk);
    auto* slots = reinterpret_cast<void**>(gameImage + d->FirstThunk);
    for (std::size_t i = 0; names[i].u1.AddressOfData; ++i) {
      if (IMAGE_SNAP_BY_ORDINAL64(names[i].u1.Ordinal)) continue;
      auto* byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(gameImage + names[i].u1.AddressOfData);
      if (!std::strcmp(reinterpret_cast<char*>(byName->Name), function)) return slots + i;
    }
  }
  return nullptr;
}

bool armDeferred()
{
  systemTimeSlot = importSlot("GetSystemTimeAsFileTime");
  if (!systemTimeSlot) return false;
  DWORD old, unused;
  if (!VirtualProtect(systemTimeSlot, sizeof(void*), PAGE_READWRITE, &old)) return false;
  originalSystemTime = reinterpret_cast<SystemTime>(*systemTimeSlot);
  InterlockedExchangePointer(systemTimeSlot, reinterpret_cast<void*>(&deferredSystemTime));
  VirtualProtect(systemTimeSlot, sizeof(void*), old, &unused);
  return true;
}

}  // namespace

// Entry point for the loader and the tests.
void start(std::uint8_t* image)
{
  wchar_t setting[8];
  const DWORD length = GetEnvironmentVariableW(L"FLUORINE_AUDIO_GUARD", setting, 8);
  if (length == 1 && setting[0] == L'0') {
    report("disabled by FLUORINE_AUDIO_GUARD=0");
    return;
  }
  gameImage = image;
  auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE ||
      nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64)
    return;
  gameImageSize = nt->OptionalHeader.SizeOfImage;
  const ScanResult result = scan(gameImage, gameImageSize);
  if (!result.hasImports) return;  // not a program with this copy
  if (!result.sites.empty()) {
    install();
    return;
  }
  // Steam DRM (.bind) decrypts the code after imports load: look again once
  // the game's own code runs.
  bool steamDrm = false;
  auto* section = IMAGE_FIRST_SECTION(nt);
  for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    steamDrm |= std::memcmp(section[i].Name, ".bind", 5) == 0;
  if (!steamDrm || !armDeferred())
    report(format("no audio copy found in %s; nothing patched", gameName().c_str()));
}

}  // namespace audioguard

#ifndef AUDIO_GUARD_TEST
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(instance);
    audioguard::start(reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr)));
  }
  return TRUE;
}
#endif
