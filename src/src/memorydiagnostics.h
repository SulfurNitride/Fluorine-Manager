#pragma once

namespace MemoryDiagnostics
{

// Memory snapshots are intentionally opt-in because /proc/self/smaps_rollup
// must walk the process mappings. Enable them with FLUORINE_MEMORY_TRACE=1.
bool enabled() noexcept;
void snapshot(const char* stage) noexcept;

// A VFS session creates and destroys hundreds of thousands of differently
// sized allocations. On glibc, explicitly return the resulting free arena
// pages at that well-defined teardown boundary. Other allocators are a no-op.
bool reclaimAllocatorPages() noexcept;

}
