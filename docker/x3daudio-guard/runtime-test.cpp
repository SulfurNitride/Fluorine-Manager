// Runs the synthetic Skyrim and Fallout 4 audio copies as real code, before
// and after the guard patches them. Also simulates a Steam DRM executable whose
// code is only readable after GetSystemTimeAsFileTime is first called.
// Usage (under Wine): audio-guard-runtime-test.exe <log directory>
#define AUDIO_GUARD_TEST
#include "guard.cpp"
#include "synthetic.h"

#include <vector>

namespace {
int failures;
int invalidParameterCalls;
int fakeErrno;

extern "C" int* fakeErrnoFunction() { return &fakeErrno; }
extern "C" void fakeInvalidParameter() { ++invalidParameterCalls; }

void check(bool ok, const char* what)
{
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  std::fflush(stdout);
  if (!ok) ++failures;
}

struct Image {
  std::uint8_t* base;
  synthetic::Layout layout;
  void (*function)(void*, void*);
};

Image load(const synthetic::Builder& code, const synthetic::Layout& layout, bool steamDrm)
{
  std::uint32_t thunk;
  auto bytes = synthetic::buildImage(code, thunk);
  auto* base = static_cast<std::uint8_t*>(
      VirtualAlloc(nullptr, bytes.size(), MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
  std::memcpy(base, bytes.data(), bytes.size());
  auto set = [&](std::uint32_t rva, const void* value) { std::memcpy(base + rva, &value, 8); };
  set(synthetic::kSlotRva, reinterpret_cast<const void*>(&fakeErrnoFunction));
  set(synthetic::kSlotRva + 8, reinterpret_cast<const void*>(&fakeInvalidParameter));
  set(thunk + 6, reinterpret_cast<const void*>(&std::memcpy));
  if (steamDrm) {
    // kernel32.dll!GetSystemTimeAsFileTime import and a ".bind" section.
    auto put32 = [&](std::uint32_t at, std::uint32_t v) { std::memcpy(base + at, &v, 4); };
    put32(synthetic::kImportRva + 20, 0x2400);
    put32(synthetic::kImportRva + 32, 0x2480);
    put32(synthetic::kImportRva + 36, 0x2500);
    std::memcpy(base + 0x2480, "kernel32.dll", 13);
    std::memcpy(base + 0x2440 + 2, "GetSystemTimeAsFileTime", 24);
    const std::uint64_t name = 0x2440;
    std::memcpy(base + 0x2400, &name, 8);
    set(0x2500, reinterpret_cast<const void*>(&GetSystemTimeAsFileTime));
    const std::uint32_t sections = 0x58 + 0xf0;
    std::uint16_t count = 2;
    std::memcpy(base + 0x46, &count, 2);
    std::memcpy(base + sections + 40, ".bind\0\0\0", 8);
    put32(sections + 40 + 8, 0x100);
    put32(sections + 40 + 12, 0x2800);
    put32(sections + 40 + 36, 0x40000040u);
  }
  return {base, layout, reinterpret_cast<void (*)(void*, void*)>(base + synthetic::kCodeRva)};
}

// Runs one copy. Returns the size the game recorded afterwards.
std::uint32_t run(const Image& image, std::uint32_t count, std::uint32_t capacity,
                  std::vector<std::uint8_t>& destination, std::vector<std::uint8_t>& source)
{
  std::vector<std::uint8_t> object(0x100, 0);
  const auto& l = image.layout;
  auto put = [&](std::uint32_t at, auto value) { std::memcpy(&object[at], &value, sizeof(value)); };
  const std::uint32_t dataOffset = 0x2c;
  put(l.tag, std::uint16_t(1));  // PCM
  put(l.dataOffset, dataOffset);
  put(l.chunk, count);
  put(l.read, dataOffset + count + 64);
  put(l.capacity, capacity);
  put(l.destination, destination.data());
  source.assign(dataOffset + count + 64, 0);
  std::memcpy(source.data(), "RIFF\0\0\0\0WAVEfmt ", 16);
  for (std::size_t i = dataOffset; i < source.size(); ++i) source[i] = std::uint8_t(i * 7 + 1);
  std::fill(destination.begin(), destination.end(), 0xcc);
  image.function(object.data(), source.data());
  std::uint32_t recorded;
  std::memcpy(&recorded, &object[l.size], 4);
  return recorded;
}

bool copied(const std::vector<std::uint8_t>& destination, const std::vector<std::uint8_t>& source,
            std::uint32_t bytes)
{
  return std::memcmp(destination.data(), source.data() + 0x2c, bytes) == 0 &&
         destination[bytes] == 0xcc;
}

void exercise(const Image& image, const char* name)
{
  std::printf("-- %s\n", name);
  std::vector<std::uint8_t> destination(0x400), source;

  invalidParameterCalls = 0;
  std::uint32_t recorded = run(image, 0x300, 0x200, destination, source);
  check(invalidParameterCalls == 1 && destination[0] == 0xcc && recorded == 0x300,
        "unpatched: oversized copy reaches _invalid_parameter_noinfo (the crash)");

  audioguard::start(image.base);

  invalidParameterCalls = 0;
  const LONG before = audioguard::events;
  recorded = run(image, 0x100, 0x200, destination, source);
  check(invalidParameterCalls == 0 && recorded == 0x100 && copied(destination, source, 0x100) &&
            audioguard::events == before,
        "patched: a copy that fits is unchanged and not logged");

  recorded = run(image, 0x300, 0x200, destination, source);
  check(invalidParameterCalls == 0, "patched: oversized copy no longer aborts");
  check(copied(destination, source, 0x200), "patched: copies exactly the buffer capacity");
  check(recorded == 0x200, "patched: the game records the clamped size");
  check(audioguard::events == before + 1, "patched: the event is logged");
}
}  // namespace

int main(int argc, char** argv)
{
  if (argc < 2) {
    std::printf("usage: %s <log directory>\n", argv[0]);
    return 2;
  }
  SetEnvironmentVariableA("FLUORINE_AUDIO_LOG_DIR", argv[1]);

  {
    SetEnvironmentVariableA("FLUORINE_AUDIO_GUARD", "0");
    Image image = load(synthetic::skyrimCode(), synthetic::kSkyrim, false);
    const std::uint8_t original = image.base[synthetic::kCodeRva + 0x65];
    audioguard::start(image.base);
    check(image.base[synthetic::kCodeRva + 0x65] == original,
          "FLUORINE_AUDIO_GUARD=0 leaves the code untouched");
    SetEnvironmentVariableA("FLUORINE_AUDIO_GUARD", nullptr);
  }

  exercise(load(synthetic::skyrimCode(), synthetic::kSkyrim, false), synthetic::kSkyrim.name);
  exercise(load(synthetic::fallout4Code(), synthetic::kFallout4, false), synthetic::kFallout4.name);

  {
    std::printf("-- Steam DRM (code readable only after start-up)\n");
    Image image = load(synthetic::skyrimCode(), synthetic::kSkyrim, true);
    std::uint8_t* code = image.base + synthetic::kCodeRva;
    for (int i = 0; i < 0x200; ++i) code[i] ^= 0x5a;  // "encrypted"
    audioguard::start(image.base);
    void** slot = reinterpret_cast<void**>(image.base + 0x2500);
    check(*slot != reinterpret_cast<void*>(&GetSystemTimeAsFileTime),
          "waits for decryption: GetSystemTimeAsFileTime is hooked");
    for (int i = 0; i < 0x200; ++i) code[i] ^= 0x5a;  // the DRM decrypts
    FILETIME time{};
    reinterpret_cast<void(WINAPI*)(LPFILETIME)>(*slot)(&time);
    check(time.dwHighDateTime != 0, "hook passes the call through");
    check(*slot == reinterpret_cast<void*>(&GetSystemTimeAsFileTime),
          "hook removes itself after patching");
    std::vector<std::uint8_t> destination(0x400), source;
    invalidParameterCalls = 0;
    const std::uint32_t recorded = run(image, 0x300, 0x200, destination, source);
    check(invalidParameterCalls == 0 && recorded == 0x200 && copied(destination, source, 0x200),
          "decrypted code is patched and clamps");
  }

  // The log file holds one record per oversized copy, with the details.
  WIN32_FIND_DATAA entry;
  const std::string pattern = std::string(argv[1]) + "\\*-audio-guard.log";
  HANDLE find = FindFirstFileA(pattern.c_str(), &entry);
  std::string text;
  if (find != INVALID_HANDLE_VALUE) {
    const std::string path = std::string(argv[1]) + "\\" + entry.cFileName;
    FILE* file = std::fopen(path.c_str(), "rb");
    char buffer[4096];
    std::size_t n;
    while (file && (n = std::fread(buffer, 1, sizeof(buffer), file)) > 0) text.append(buffer, n);
    if (file) std::fclose(file);
    FindClose(find);
  }
  auto occurrences = [&](const char* token) {
    int count = 0;
    for (auto at = text.find(token); at != std::string::npos; at = text.find(token, at + 1)) ++count;
    return count;
  };
  check(occurrences("event=oversized audio copy clamped") == 3, "log file has all three events");
  check(occurrences("copy_requested=768 buffer_capacity=512 copied=512 dropped=256") == 3,
        "log records requested, capacity and dropped bytes");
  check(text.find("data chunk claims 768 bytes, 832 bytes were read after the header, the buffer "
                  "holds 512") != std::string::npos,
        "log explains the size mismatch");
  check(text.find("format: tag=0x0001") != std::string::npos &&
            text.find("52 49 46 46") != std::string::npos,
        "log records the wave format and the file header bytes");
  std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
  return failures ? 1 : 0;
}
