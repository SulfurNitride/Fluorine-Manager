#ifndef VFS_PERMISSIONREPAIR_H
#define VFS_PERMISSIONREPAIR_H

#include <cstdint>
#include <filesystem>

struct PermissionRepairStats
{
  uint64_t inspected = 0;
  uint64_t repaired = 0;
  uint64_t skipped = 0;
  uint64_t failed = 0;
  int traversal_error = 0;
};

enum class PermissionRepairOutcome
{
  NoChanges,
  RepairsApplied,
  Failed,
};

PermissionRepairOutcome permissionRepairOutcome(
    const PermissionRepairStats& stats) noexcept;

// Best-effort restoration of the owner permissions required by Wine/Proton.
// Existing group/other and executable bits are preserved. Symbolic links are
// never followed.
PermissionRepairStats repairGameDirectoryPermissions(
    const std::filesystem::path& game_directory);

#endif
