#include "memorydiagnostics.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

namespace MemoryDiagnostics
{
namespace
{
struct ProcMemory
{
  unsigned long long rssKiB          = 0;
  unsigned long long pssKiB          = 0;
  unsigned long long privateDirtyKiB = 0;
  unsigned long long anonymousKiB    = 0;
  unsigned long long swapKiB         = 0;
};

bool envEnabled(const char* name) noexcept
{
  const char* value = std::getenv(name);
  if (value == nullptr) {
    return false;
  }

  std::string normalized(value);
  for (char& c : normalized) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return normalized == "1" || normalized == "true" || normalized == "yes" ||
         normalized == "on";
}

ProcMemory readProcMemory()
{
  ProcMemory result;
  std::ifstream input("/proc/self/smaps_rollup");
  std::string line;
  while (std::getline(input, line)) {
    std::istringstream fields(line);
    std::string key;
    unsigned long long value = 0;
    if (!(fields >> key >> value)) {
      continue;
    }
    if (key == "Rss:") {
      result.rssKiB = value;
    } else if (key == "Pss:") {
      result.pssKiB = value;
    } else if (key == "Private_Dirty:") {
      result.privateDirtyKiB = value;
    } else if (key == "Anonymous:") {
      result.anonymousKiB = value;
    } else if (key == "Swap:") {
      result.swapKiB = value;
    }
  }

  return result;
}
}

bool enabled() noexcept
{
  static const bool value = envEnabled("FLUORINE_MEMORY_TRACE");
  return value;
}

void snapshot(const char* stage) noexcept
{
  if (!enabled()) {
    return;
  }

  try {
    const ProcMemory memory = readProcMemory();

    unsigned long long arenaKiB = 0;
    unsigned long long inUseKiB = 0;
    unsigned long long freeKiB  = 0;
    unsigned long long mmapKiB  = 0;
#if defined(__GLIBC__)
    const struct mallinfo2 allocator = mallinfo2();
    arenaKiB = static_cast<unsigned long long>(allocator.arena) / 1024;
    inUseKiB = static_cast<unsigned long long>(allocator.uordblks) / 1024;
    freeKiB  = static_cast<unsigned long long>(allocator.fordblks) / 1024;
    mmapKiB  = static_cast<unsigned long long>(allocator.hblkhd) / 1024;
#endif

    std::fprintf(
        stderr,
        "[MEM] stage=%s rss_kib=%llu pss_kib=%llu private_dirty_kib=%llu "
        "anonymous_kib=%llu swap_kib=%llu allocator_arena_kib=%llu "
        "allocator_in_use_kib=%llu allocator_free_kib=%llu "
        "allocator_mmap_kib=%llu\n",
        stage != nullptr ? stage : "unknown", memory.rssKiB, memory.pssKiB,
        memory.privateDirtyKiB, memory.anonymousKiB, memory.swapKiB, arenaKiB,
        inUseKiB, freeKiB, mmapKiB);
  } catch (...) {
    std::fprintf(stderr, "[MEM] stage=%s unavailable\n",
                 stage != nullptr ? stage : "unknown");
  }
}

bool reclaimAllocatorPages() noexcept
{
#if defined(__GLIBC__)
  return malloc_trim(0) != 0;
#else
  return false;
#endif
}

}
